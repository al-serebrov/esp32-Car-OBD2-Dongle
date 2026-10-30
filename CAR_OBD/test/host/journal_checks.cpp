#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
#include "logging/FlashJournal.h"

class MemoryFlash : public fieldlog::Storage {
public:
    std::array<uint8_t, 8192> bytes;
    int writeBudget = -1;
    int eraseBudget = -1;
    MemoryFlash() { bytes.fill(0xff); }
    size_t size() const override { return bytes.size(); }
    bool read(size_t offset, void* output, size_t length) override {
        assert(offset + length <= size()); std::memcpy(output, bytes.data() + offset, length); return true;
    }
    bool write(size_t offset, const void* input, size_t length) override {
        assert(offset + length <= size());
        const auto* source = static_cast<const uint8_t*>(input);
        for (size_t i = 0; i < length; ++i) {
            if (writeBudget == 0) return false; // Power cut between any two bytes.
            if (writeBudget > 0) --writeBudget;
            assert((bytes[offset + i] & source[i]) == source[i]); // NOR: only 1 -> 0.
            bytes[offset + i] &= source[i];
        }
        return true;
    }
    bool erase(size_t offset, size_t length) override {
        assert(offset % 4096 == 0 && length == 4096 && offset + length <= size());
        for (size_t i = 0; i < length; ++i) {
            if (eraseBudget == 0) return false;
            if (eraseBudget > 0) --eraseBudget;
            bytes[offset + i] = 0xff;
        }
        return true;
    }
};

std::vector<fieldlog::Record> records(fieldlog::FlashJournal& journal) {
    std::vector<fieldlog::Record> result;
    for (size_t i = 0; i < journal.capacity(); ++i) {
        fieldlog::Record record;
        if (journal.readInOrder(i, record)) {
            if (!result.empty()) assert(record.sequence > result.back().sequence);
            result.push_back(record);
        }
    }
    return result;
}

int main() {
    MemoryFlash flash;
    fieldlog::FlashJournal journal(flash);
    assert(journal.begin() && journal.boot() == 1 && records(journal).empty());
    for (unsigned i = 1; i <= 100; ++i) assert(journal.append("speed record", i));
    auto exported = records(journal);
    assert(exported.back().sequence == 100 && exported.size() >= 16 && exported.size() <= 32);
    fieldlog::FlashJournal rebooted(flash);
    assert(rebooted.begin() && rebooted.boot() == 2 && rebooted.sequence() == 100);
    assert(rebooted.append("new boot", 1));
    exported = records(rebooted);
    assert(exported.back().boot == 2 && exported.back().sequence == 101);
    assert(exported.front().boot == 1);

    // Every interruption point across payload, CRC and commit marker. Recovery
    // must preserve previous records and safely append after the torn slot.
    for (int cut = 0; cut < 256; ++cut) {
        MemoryFlash simulated = flash;
        fieldlog::FlashJournal before(simulated);
        assert(before.begin());
        simulated.writeBudget = cut;
        assert(!before.append("interrupted", 50));
        simulated.writeBudget = -1;
        fieldlog::FlashJournal recovered(simulated);
        assert(recovered.begin() && recovered.sequence() == 101);
        assert(recovered.append("recovered", 1));
        const auto rows = records(recovered);
        assert(rows.back().sequence == 102 && !std::strcmp(rows.back().text, "recovered"));
        bool retained = false;
        for (const auto& row : rows) if (row.sequence == 101) retained = true;
        assert(retained);
    }
    // Interrupted sector erases at rollover must not remove the newest record.
    MemoryFlash boundary;
    fieldlog::FlashJournal filled(boundary);
    assert(filled.begin());
    for (int i = 0; i < 32; ++i) assert(filled.append("before rollover", i));
    for (int cut = 0; cut < 4096; cut += 127) {
        MemoryFlash simulated = boundary;
        fieldlog::FlashJournal before(simulated);
        assert(before.begin()); simulated.eraseBudget = cut;
        assert(!before.append("interrupted erase", 0));
        simulated.eraseBudget = -1;
        fieldlog::FlashJournal recovered(simulated);
        assert(recovered.begin() && recovered.sequence() == 32);
        assert(recovered.append("after erase recovery", 1));
        assert(records(recovered).back().sequence == 33);
    }
    auto corrupted = exported.back();
    assert(fieldlog::valid(corrupted));
    corrupted.text[0] ^= 1;
    assert(!fieldlog::valid(corrupted));
    corrupted = exported.back(); corrupted.magic = 0xffffffff;
    assert(!fieldlog::valid(corrupted));
    std::puts("PASS: flash rollover, ordered export, reboot retention, CRC, all 256 interrupted writes, interrupted erases");
}
