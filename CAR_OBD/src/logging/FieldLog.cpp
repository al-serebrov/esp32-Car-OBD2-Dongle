#include "FieldLog.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <inttypes.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/task.h"

namespace { constexpr size_t kSector = 4096; }
FieldLog& FieldLog::instance() { static FieldLog log; return log; }

bool FieldLog::begin() {
    partition_ = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, static_cast<esp_partition_subtype_t>(0x40), "fieldlog");
    if (!partition_) return false;
    mutex_ = xSemaphoreCreateMutex();
    queue_ = xQueueCreate(24, sizeof(Pending));
    flushed_ = xSemaphoreCreateBinary();
    if (!mutex_ || !queue_ || !flushed_ || !journal_.begin()) return false;
    return xTaskCreate(task, "flash_log", 3072, this, 1, nullptr) == pdPASS;
}

void FieldLog::write(const char* format, ...) {
    if (!queue_) return;
    Pending pending = {};
    pending.milliseconds = static_cast<uint32_t>(esp_timer_get_time() / 1000);
    va_list args;
    va_start(args, format);
    const int needed = std::vsnprintf(pending.text, sizeof(pending.text), format, args);
    va_end(args);
    if (needed >= static_cast<int>(sizeof(pending.text))) {
        std::memcpy(pending.text + sizeof(pending.text) - 6, "[...]", 6);
    }
    if (xQueueSend(queue_, &pending, 0) != pdTRUE) ++dropped_;
}

void FieldLog::bytes(const char* direction, const uint8_t* data, size_t length) {
    // Preserve raw bytes with escaped CR/LF/NUL/non-ASCII; split long chunks.
    char encoded[180] = {};
    size_t used = 0;
    for (size_t i = 0; i < length; ++i) {
        const uint8_t c = data[i];
        if (used + 5 >= sizeof(encoded)) { encoded[used] = 0; write("%s %s", direction, encoded); used = 0; }
        if (c == '\r' || c == '\n' || c == '\\') {
            encoded[used++] = '\\'; encoded[used++] = c == '\r' ? 'r' : c == '\n' ? 'n' : '\\';
        } else if (c >= 32 && c <= 126) encoded[used++] = static_cast<char>(c);
        else { std::snprintf(encoded + used, 5, "\\x%02X", c); used += 4; }
    }
    encoded[used] = 0;
    if (used) write("%s %s", direction, encoded);
}

size_t FieldLog::size() const { return partition_->size; }
bool FieldLog::read(size_t offset, void* buffer, size_t length) {
    return esp_partition_read(partition_, offset, buffer, length) == ESP_OK;
}
bool FieldLog::write(size_t offset, const void* buffer, size_t length) {
    return esp_partition_write(partition_, offset, buffer, length) == ESP_OK;
}
bool FieldLog::erase(size_t offset, size_t length) {
    return esp_partition_erase_range(partition_, offset, length) == ESP_OK;
}

void FieldLog::task(void* context) {
    auto* self = static_cast<FieldLog*>(context);
    Pending item;
    for (;;) {
        if (xQueueReceive(self->queue_, &item, portMAX_DELAY) != pdTRUE) continue;
        if (!item.text[0]) { xSemaphoreGive(self->flushed_); continue; }
        xSemaphoreTake(self->mutex_, portMAX_DELAY);
        const bool ok = self->journal_.append(item.text, item.milliseconds);
        if (!self->exporting_) std::printf("LOG %s\n", item.text);
        xSemaphoreGive(self->mutex_);
        if (!ok) ESP_LOGE("fieldlog", "flash write failed; record not committed");
        const auto dropped = self->dropped_.exchange(0);
        if (dropped) {
            Pending overflow = {};
            overflow.milliseconds = static_cast<uint32_t>(esp_timer_get_time() / 1000);
            std::snprintf(overflow.text, sizeof(overflow.text), "LOG_QUEUE_OVERFLOW dropped=%u", dropped);
            // Preserve the original count if the queue is still full; do not
            // replace a large loss count with a lost warning about that loss.
            if (xQueueSend(self->queue_, &overflow, 0) != pdTRUE) self->dropped_.fetch_add(dropped);
        }
    }
}

void FieldLog::dump() {
    if (!partition_) { std::puts("LOG_ERROR no fieldlog partition"); return; }
    exporting_ = true;
    Pending barrier = {};
    if (xQueueSend(queue_, &barrier, pdMS_TO_TICKS(3000)) != pdTRUE ||
        xSemaphoreTake(flushed_, pdMS_TO_TICKS(3000)) != pdTRUE) {
        exporting_ = false; std::puts("LOG_ERROR flush timeout"); return;
    }
    xSemaphoreTake(mutex_, portMAX_DELAY);
    // Freeze appends for a coherent, oldest-to-newest snapshot. During a long
    // export new events may fill the bounded queue; a drop count is logged later.
    std::printf("LOG_BEGIN version=1 boot=%" PRIu32 " newest=%" PRIu32 " capacity=%u\n", journal_.boot(), journal_.sequence(), static_cast<unsigned>(journal_.capacity()));
    fieldlog::Record record;
    for (size_t n = 0; n < journal_.capacity(); ++n) {
        if (journal_.readInOrder(n, record))
            std::printf("%" PRIu32 " boot=%" PRIu32 " ms=%" PRIu32 " %s\n", record.sequence, record.boot, record.milliseconds, record.text);
        if (n % (kSector / sizeof(record)) == 0) vTaskDelay(1);
    }
    std::puts("LOG_END");
    std::fflush(stdout);
    xSemaphoreGive(mutex_);
    exporting_ = false;
}
