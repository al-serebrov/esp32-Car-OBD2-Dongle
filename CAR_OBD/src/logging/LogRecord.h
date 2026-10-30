#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace fieldlog {
constexpr uint32_t kMagic = 0x4f424431;  // OBD1; format version marker.
struct Record {
    uint32_t magic;
    uint32_t sequence;
    uint32_t boot;
    uint32_t milliseconds;
    char text[236];
    uint32_t checksum;
};
static_assert(sizeof(Record) == 256, "Flash record layout changed");
inline uint32_t checksum(const Record& record) {
    // CRC32 excluding commit marker and checksum. Also exercised by host tests.
    uint32_t crc = 0xffffffff;
    const auto* bytes = reinterpret_cast<const uint8_t*>(&record);
    for (size_t i = sizeof(record.magic); i < offsetof(Record, checksum); ++i) {
        crc ^= bytes[i];
        for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
    }
    return ~crc;
}
inline bool valid(const Record& record) {
    return record.magic == kMagic && std::memchr(record.text, 0, sizeof(record.text)) &&
           record.checksum == checksum(record);
}
}  // namespace fieldlog
