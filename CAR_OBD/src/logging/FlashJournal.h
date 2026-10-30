#pragma once

#include <cstddef>
#include "LogRecord.h"

namespace fieldlog {
class Storage {
public:
    virtual ~Storage() = default;
    virtual size_t size() const = 0;
    virtual bool read(size_t offset, void* buffer, size_t length) = 0;
    virtual bool write(size_t offset, const void* buffer, size_t length) = 0;
    virtual bool erase(size_t offset, size_t length) = 0;
};

// Flash algorithm independent of ESP-IDF; tested with NOR flash semantics and
// interrupted writes. Storage size must be a multiple of 4096, at least 8192.
class FlashJournal {
public:
    explicit FlashJournal(Storage& storage) : storage_(storage) {}
    bool begin();
    bool append(const char* text, uint32_t milliseconds);
    bool readInOrder(size_t index, Record& record);
    size_t capacity() const { return storage_.size() / sizeof(Record); }
    uint32_t sequence() const { return sequence_; }
    uint32_t boot() const { return boot_; }
private:
    Storage& storage_;
    size_t next_ = 0;
    uint32_t sequence_ = 0;
    uint32_t boot_ = 1;
};
}  // namespace fieldlog
