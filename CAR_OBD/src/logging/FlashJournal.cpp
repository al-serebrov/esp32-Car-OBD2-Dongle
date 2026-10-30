#include "FlashJournal.h"

#include <cstring>

namespace fieldlog {
namespace { constexpr size_t kSector = 4096; }

bool FlashJournal::begin() {
    if (storage_.size() < kSector * 2 || storage_.size() % kSector) return false;
    next_ = 0; sequence_ = 0; boot_ = 1;
    Record record;
    bool found = false;
    for (size_t offset = 0; offset < storage_.size(); offset += sizeof(record)) {
        if (!storage_.read(offset, &record, sizeof(record))) return false;
        if (valid(record) && (!found || static_cast<int32_t>(record.sequence - sequence_) > 0)) {
            found = true; sequence_ = record.sequence; boot_ = record.boot + 1;
            next_ = (offset + sizeof(record)) % storage_.size();
        }
    }
    return true;
}

bool FlashJournal::append(const char* text, uint32_t milliseconds) {
    Record previous;
    if (!storage_.read(next_, &previous, sizeof(previous))) return false;
    const auto* raw = reinterpret_cast<const uint8_t*>(&previous);
    bool blank = true;
    for (size_t i = 0; i < sizeof(previous); ++i) if (raw[i] != 0xff) { blank = false; break; }
    // Skip a torn/nonblank slot rather than rewriting programmed NOR bits.
    if (!blank && next_ % kSector) next_ = ((next_ / kSector + 1) * kSector) % storage_.size();
    if (next_ % kSector == 0 && !storage_.erase(next_, kSector)) return false;
    Record record = {};
    record.magic = kMagic; record.sequence = ++sequence_; record.boot = boot_;
    record.milliseconds = milliseconds;
    std::strncpy(record.text, text, sizeof(record.text) - 1);
    record.checksum = checksum(record);
    // Payload+CRC first, commit marker last. No valid marker until complete.
    if (!storage_.write(next_ + 4, reinterpret_cast<const uint8_t*>(&record) + 4, sizeof(record) - 4) ||
        !storage_.write(next_, &record.magic, 4)) return false;
    next_ = (next_ + sizeof(record)) % storage_.size();
    return true;
}

bool FlashJournal::readInOrder(size_t index, Record& record) {
    if (index >= capacity()) return false;
    const size_t offset = (next_ + index * sizeof(record)) % storage_.size();
    return storage_.read(offset, &record, sizeof(record)) && valid(record);
}
}  // namespace fieldlog
