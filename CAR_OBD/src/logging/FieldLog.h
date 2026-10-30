#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include "esp_partition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "FlashJournal.h"

class FieldLog : private fieldlog::Storage {
public:
    bool begin();
    void write(const char* format, ...) __attribute__((format(printf, 2, 3)));
    void bytes(const char* direction, const uint8_t* data, size_t length);
    void dump();  // USB command, read-only. Does not clear previous boots.
    static FieldLog& instance();
private:
    struct Pending { uint32_t milliseconds; char text[236]; };
    static void task(void* context);
    size_t size() const override;
    bool read(size_t offset, void* buffer, size_t length) override;
    bool write(size_t offset, const void* buffer, size_t length) override;
    bool erase(size_t offset, size_t length) override;
    const esp_partition_t* partition_ = nullptr;
    QueueHandle_t queue_ = nullptr;
    SemaphoreHandle_t mutex_ = nullptr;
    fieldlog::FlashJournal journal_{*this};
    SemaphoreHandle_t flushed_ = nullptr;
    std::atomic<unsigned> dropped_{0};
    std::atomic<bool> exporting_{false};
};
