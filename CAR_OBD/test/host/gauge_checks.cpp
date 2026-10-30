#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstdint>
#include "display/GaugeRenderer.h"
#include "telemetry/SimulatedSpeed.h"

namespace {
constexpr int kPixelCount = config::kWidth * config::kHeight;
constexpr uint16_t kGuard = 0xa55a;
std::array<uint16_t, kPixelCount + 2> guarded{};
std::array<uint16_t, kPixelCount> reference{};

void checkGuards() {
    assert(guarded.front() == kGuard);
    assert(guarded.back() == kGuard);
}

void compareFresh(GaugeRenderer& gauge, int speed) {
    gauge.showSpeed(speed);
    GaugeRenderer fresh(reference.data());
    fresh.begin();
    fresh.showSpeed(speed);
    assert(std::equal(reference.begin(), reference.end(), guarded.begin() + 1));
    checkGuards();
}
}  // namespace

int main(int argc, char** argv) {
    const int expectedDemo[] = {11, 22, 33, 44, 55, 66, 77, 88, 99, 111};
    for (unsigned i = 0; i < 10; ++i) {
        assert(simulatedSpeedKmh(i * 3000) == expectedDemo[i]);
        assert(simulatedSpeedKmh(i * 3000 + 2999) == expectedDemo[i]);
    }
    assert(simulatedSpeedKmh(30000) == 11);
    assert(simulatedSpeedKmh((UINT64_C(1) << 40) * 30000 + 27000) == 111);

    guarded.front() = guarded.back() = kGuard;
    GaugeRenderer gauge(guarded.data() + 1);
    gauge.splash();
    checkGuards();
    gauge.begin();
    const auto initial = gauge.takeDirty();
    assert(initial.x0 == 0 && initial.y0 == 0);
    assert(initial.x1 == config::kWidth && initial.y1 == config::kHeight);
    gauge.showSpeed(0);
    assert(gauge.takeDirty().empty());

    // Compare all up/down transitions with a freshly drawn frame, detecting
    // stale digit segments, stale arcs, clipping errors and framebuffer writes.
    for (int speed = 0; speed <= config::kSpeedScaleKmh; ++speed) compareFresh(gauge, speed);
    for (int speed = config::kSpeedScaleKmh; speed >= 0; --speed) compareFresh(gauge, speed);
    compareFresh(gauge, -100);
    compareFresh(gauge, 10000);
    gauge.showSpeed(87);
    const auto changed = gauge.takeDirty();
    assert(!changed.empty());
    assert(changed.x0 >= 0 && changed.x1 <= config::kWidth);
    assert(changed.y0 >= 0 && changed.y1 <= config::kHeight);
    gauge.showSpeed(87);
    assert(gauge.takeDirty().empty());
    gauge.setLive(true);
    const auto liveIndicator = gauge.takeDirty();
    assert(!liveIndicator.empty() && liveIndicator.x0 >= 115 && liveIndicator.x1 <= 125);
    assert(liveIndicator.y0 >= 228 && liveIndicator.y1 <= 238);
    gauge.setLive(true);
    assert(gauge.takeDirty().empty());
    gauge.setLive(false);
    assert(!gauge.takeDirty().empty());
    compareFresh(gauge, 87); // Returning to DEMO leaves no LIVE-indicator artifacts.
    compareFresh(gauge, 255); // Numeric speed can exceed the arc's 160 km/h scale.

    // Hardware uses separate row bands because BT fragments the RAM map.
    // Compare that path pixel-for-pixel with a contiguous reference and guard
    // both ends of every independently allocated band.
    constexpr int kBandPixels = config::kWidth * config::kStripRows;
    std::array<std::array<uint16_t, kBandPixels + 2>, config::kHeight / config::kStripRows> bandStorage{};
    std::array<uint16_t*, config::kHeight / config::kStripRows> bands{};
    for (size_t i = 0; i < bands.size(); ++i) {
        bandStorage[i].front() = bandStorage[i].back() = kGuard;
        bands[i] = bandStorage[i].data() + 1;
    }
    GaugeRenderer striped(bands.data()), flat(reference.data());
    striped.begin(); flat.begin();
    for (const int speed : {0, 9, 10, 99, 100, 120, 255, 87, 0}) {
        striped.setLive(speed % 2); flat.setLive(speed % 2);
        striped.showSpeed(speed); flat.showSpeed(speed);
        for (int y = 0; y < config::kHeight; ++y)
            assert(std::equal(flat.row(y), flat.row(y) + config::kWidth, striped.row(y)));
        for (const auto& band : bandStorage) assert(band.front() == kGuard && band.back() == kGuard);
    }

    if (argc == 2) {
        gauge.showSpeed(87);
        auto* file = std::fopen(argv[1], "wb");
        assert(file);
        std::fprintf(file, "P6\n240 240\n255\n");
        for (int i = 1; i <= kPixelCount; ++i) {
            const auto c = guarded[i];
            const uint8_t rgb[] = {static_cast<uint8_t>(((c >> 11) & 31) * 255 / 31),
                                   static_cast<uint8_t>(((c >> 5) & 63) * 255 / 63),
                                   static_cast<uint8_t>((c & 31) * 255 / 31)};
            assert(std::fwrite(rgb, 1, 3, file) == 3);
        }
        std::fclose(file);
    }
    std::puts("PASS: simulation bounds/endpoints, up/down redraw equivalence, framebuffer guards, dirty tracking");
}
