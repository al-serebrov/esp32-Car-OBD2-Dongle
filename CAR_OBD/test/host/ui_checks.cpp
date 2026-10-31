#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <vector>
#include "input/ButtonGestures.h"
#include "display/GaugeRenderer.h"
#include "display/ImageBrightness.h"
#include "telemetry/EngineWarnings.h"

void showPreviewIcons(GaugeRenderer& gauge) {
    for (auto icon : {WarningIcon::CheckEngine, WarningIcon::ParkingBrake, WarningIcon::Overheat, WarningIcon::Seatbelt})
        gauge.setWarning(icon, true);
}

struct ButtonTest {
    ButtonGestures button;
    uint64_t now = 0;
    std::vector<ButtonEvent> events;
    ButtonTest(bool pressed = false) { button.reset(pressed, 0); }
    void sample(bool pressed, unsigned duration) {
        for (unsigned i = 0; i < duration; i += 10) {
            now += 10;
            const auto event = button.update(pressed, now);
            if (event != ButtonEvent::None) events.push_back(event);
        }
    }
};

int main() {
    assert(dimRgb565(0xffff, 100) == 0xffff);
    assert(dimRgb565(0xffff, 0) == 0);
    assert(dimRgb565(0, 50) == 0);
    assert(dimRgb565(0xffff, 50) == 0x8410);
    assert(dimRgb565(0xf800, 50) == 0x8000);
    assert(dimRgb565(0x07e0, 50) == 0x0400);
    assert(dimRgb565(0x001f, 50) == 0x0010);
    ButtonTest single;
    single.sample(true, 80); single.sample(false, 320);
    assert(single.events == std::vector<ButtonEvent>{ButtonEvent::Single});
    ButtonTest doubleClick;
    doubleClick.sample(true, 80); doubleClick.sample(false, 100);
    doubleClick.sample(true, 80); doubleClick.sample(false, 500);
    assert(doubleClick.events == std::vector<ButtonEvent>{ButtonEvent::Double});
    ButtonTest hold;
    hold.sample(true, 1000); assert(hold.events.empty());
    hold.sample(true, 2200); hold.sample(false, 500);
    assert(hold.events == std::vector<ButtonEvent>{ButtonEvent::Hold});
    ButtonTest bounce;
    bounce.sample(true, 10); bounce.sample(false, 10);
    bounce.sample(true, 10); bounce.sample(false, 400);
    assert(bounce.events.empty());
    ButtonTest bootHeld(true);
    bootHeld.sample(true, 2500); bootHeld.sample(false, 400);
    assert(bootHeld.events.empty());
    bootHeld.sample(true, 80); bootHeld.sample(false, 400);
    assert(bootHeld.events == std::vector<ButtonEvent>{ButtonEvent::Single});
    ButtonTest secondHold;
    secondHold.sample(true, 80); secondHold.sample(false, 100);
    secondHold.sample(true, 1500); secondHold.sample(false, 500);
    assert(secondHold.events == std::vector<ButtonEvent>{ButtonEvent::Hold});
    ButtonTest separate;
    separate.sample(true, 80); separate.sample(false, 320);
    separate.sample(true, 80); separate.sample(false, 400);
    assert((separate.events == std::vector<ButtonEvent>{ButtonEvent::Single, ButtonEvent::Single}));
    // A second press inside the window can finish debouncing after the deadline.
    ButtonTest boundary;
    boundary.sample(true, 80); boundary.sample(false, 290);
    boundary.sample(true, 80); boundary.sample(false, 400);
    assert(boundary.events == std::vector<ButtonEvent>{ButtonEvent::Double});

    constexpr unsigned count = config::kWidth * config::kHeight;
    std::array<uint16_t, count + 2> pixels{};
    std::array<uint16_t, count> reference{};
    pixels.front() = pixels.back() = 0xa55a;
    GaugeRenderer ui(pixels.data() + 1), fresh(reference.data());
    showPreviewIcons(ui); showPreviewIcons(fresh); // Explicit fixtures; production boots with no sample icons.
    ui.begin(); ui.showSpeed(87); ui.setLive(true);
    for (bool day : {true, false, true}) {
        ui.setView(day, false); ui.showSpeed(87); ui.setLive(true);
        fresh.begin(); fresh.setView(day, false); fresh.showSpeed(87); fresh.setLive(true);
        assert(std::equal(reference.begin(), reference.end(), pixels.begin() + 1));
        assert(ui.row(120)[120] == (day ? 0xffff : 0x0000)); // Gap between the digits.
        // The fixed prototype lights must remain intact through every speed
        // width/arc transition, including when LIVE changes and in both themes.
        for (unsigned slot = 0; slot < 4; ++slot) {
            unsigned coloredPixels = 0;
            const int left = slot % 2 ? 194 : 22;
            const int top = 80 + static_cast<int>(slot / 2) * 28;
            for (int y = top; y < top + 20; ++y) {
                for (int x = left; x < left + 24; ++x) {
                    const auto color = ui.row(y)[x];
                    const unsigned r = (color >> 11) & 31, g = (color >> 5) & 63, b = color & 31;
                    if (slot == 0 ? (r > 10 && g > 10 && b < 8) : (r > 15 && g < 20 && b < 12))
                        ++coloredPixels;
                }
            }
            assert(coloredPixels > 25);
        }
        const auto warningFrame = pixels;
        for (int speed = 0; speed <= 255; ++speed) {
            ui.showSpeed(speed); ui.setLive(speed % 2);
            for (int y = 80; y < 156; ++y) for (int x = 22; x < 218; ++x) {
                if (x >= 46 && x < 194) continue;
                const int dx=x-120,dy=y-120;
                if(dx*dx+dy*dy<103*103)
                    assert(ui.row(y)[x]==warningFrame[1+y*config::kWidth+x]);
            }
        }
        ui.showSpeed(87); ui.setLive(true);
    }
    DetailReadings readings;
    readings.values[0] = {14200, 100, true};
    readings.values[1] = {-40, 100, true};
    readings.values[2] = {0, 100, true};
    readings.values[3] = {1275, 100, true};
    ui.setView(true, true); ui.showDetails(readings, 100);
    auto populated = pixels;
    ui.takeDirty(); ui.showDetails(readings, 101);
    assert(ui.takeDirty().empty());
    ui.showDetails(readings, 100 + config::kCoolantStaleMs);
    assert(pixels != populated); // Expired/zero values must not share the placeholder.
    fresh.setView(true, true); fresh.showDetails({}, 100);
    assert(std::equal(reference.begin(), reference.end(), pixels.begin() + 1));
    for (bool day : {false, true, false}) {
        ui.setView(day, true); ui.showDetails(readings, 100);
        fresh.setView(day, true); fresh.showDetails(readings, 100);
        assert(std::equal(reference.begin(), reference.end(), pixels.begin() + 1));
        ui.setView(day, false); ui.showSpeed(87); ui.setLive(true);
        fresh.begin(); fresh.setView(day, false); fresh.showSpeed(87); fresh.setLive(true);
        assert(std::equal(reference.begin(), reference.end(), pixels.begin() + 1));
    }
    const auto normalGauge = pixels;
    ui.showDimmingTestPercent(50);
    assert(pixels != normalGauge);
    ui.takeDirty(); ui.showDimmingTestPercent(50);
    assert(ui.takeDirty().empty());
    ui.clearDimmingTest();
    assert(pixels == normalGauge);  // Ending the test restores the uncluttered dial.
    // Compare relocated icons against their original pixel masks, exercising
    // arrival order, gap closure, reactivation and updates on a hidden page.
    for (bool day : {false, true}) {
        std::array<uint16_t, count> warningPixels{};
        GaugeRenderer warnings(warningPixels.data());
        showPreviewIcons(warnings);
        warnings.begin(); warnings.setView(day, false); warnings.showSpeed(111);
        const auto canonical = warningPixels;
        const auto verifyGrid = [&](std::initializer_list<WarningIcon> expected) {
            unsigned slot = 0;
            for (const auto icon : expected) {
                const int x0 = slot % 2 ? 194 : 22, y0 = 80 + static_cast<int>(slot / 2)*28;
                const unsigned source = static_cast<unsigned>(icon);
                const int sourceX = source % 2 ? 194 : 22, sourceY = 80 + static_cast<int>(source / 2)*28;
                for (int y = 0; y < 20; ++y)
                    for (int x = 0; x < 24; ++x) {
                        const int dx=x0+x-120,dy=y0+y-120;
                        const int sx=sourceX+x-120,sy=sourceY+y-120;
                        // The curved dial edge outside the icon is location-dependent.
                        if(dx*dx+dy*dy<103*103 && sx*sx+sy*sy<103*103)
                            assert(warnings.row(y0 + y)[x0 + x] == canonical[(sourceY+y)*config::kWidth+sourceX+x]);
                    }
                ++slot;
            }
            std::array<uint16_t, count> freshPixels{};
            GaugeRenderer referenceGauge(freshPixels.data());
            for (const auto icon : expected) referenceGauge.setWarning(icon, true);
            referenceGauge.begin(); referenceGauge.setView(day, false); referenceGauge.showSpeed(111);
            assert(warningPixels == freshPixels); // Includes fixed-size digits and stale icon clearing.
        };
        for (auto icon : {WarningIcon::CheckEngine, WarningIcon::ParkingBrake, WarningIcon::Overheat, WarningIcon::Seatbelt})
            warnings.setWarning(icon, false);
        verifyGrid({});
        warnings.takeDirty();
        warnings.setWarning(WarningIcon::CheckEngine, false);
        warnings.setWarning(WarningIcon::Count, true);
        assert(warnings.takeDirty().empty());
        warnings.setWarning(WarningIcon::Seatbelt, true);
        verifyGrid({WarningIcon::Seatbelt});
        warnings.setWarning(WarningIcon::Overheat, true);
        warnings.setWarning(WarningIcon::ParkingBrake, true);
        warnings.setWarning(WarningIcon::CheckEngine, true);
        verifyGrid({WarningIcon::Seatbelt, WarningIcon::Overheat, WarningIcon::ParkingBrake, WarningIcon::CheckEngine});
        warnings.takeDirty(); warnings.setWarning(WarningIcon::Seatbelt, true);
        assert(warnings.takeDirty().empty());
        warnings.setWarning(WarningIcon::Overheat, false);
        verifyGrid({WarningIcon::Seatbelt, WarningIcon::ParkingBrake, WarningIcon::CheckEngine});
        warnings.setWarning(WarningIcon::Overheat, true);
        verifyGrid({WarningIcon::Seatbelt, WarningIcon::ParkingBrake, WarningIcon::CheckEngine, WarningIcon::Overheat});
        warnings.setWarning(WarningIcon::Seatbelt, false);
        verifyGrid({WarningIcon::ParkingBrake, WarningIcon::CheckEngine, WarningIcon::Overheat});
        warnings.setView(day, true);
        const auto detailsPixels = warningPixels;
        warnings.takeDirty(); warnings.setWarning(WarningIcon::ParkingBrake, false);
        assert(warnings.takeDirty().empty() && warningPixels == detailsPixels);
        warnings.setView(!day, false); warnings.setView(day, false); warnings.showSpeed(111);
        verifyGrid({WarningIcon::CheckEngine, WarningIcon::Overheat});
        warnings.setWarning(WarningIcon::Overheat, false);
        verifyGrid({WarningIcon::CheckEngine});
        warnings.setWarning(WarningIcon::CheckEngine, false);
        verifyGrid({});
    }
    EngineWarnings monitor;
    auto state = monitor.update({}, 100, true);
    assert(state.mil == WarningState::Unknown && !state.showMil && !state.showTemperature);
    EngineReadings engine{{0x02, 100, true}, {104, 100, true}};
    state = monitor.update(engine, 100, true);
    assert(state.mil == WarningState::Clear && !state.showMil); // Stored DTCs alone do not mean MIL commanded on.
    assert(state.temperature == WarningState::Clear && !state.showTemperature);
    engine = {{0x81, 200, true}, {105, 200, true}};
    state = monitor.update(engine, 200, true);
    assert(state.showMil && state.showTemperature);
    engine.coolant = {103, 300, true};
    state = monitor.update(engine, 300, true);
    assert(state.showTemperature); // Hysteresis prevents flicker just below 105.
    engine.coolant = {100, 400, true};
    state = monitor.update(engine, 400, true);
    assert(!state.showTemperature);
    engine = {{0x80, 500, true}, {106, 500, true}};
    monitor.update(engine, 500, true);
    state = monitor.update(engine, 500 + config::kCoolantStaleMs, true);
    assert(state.mil == WarningState::Unknown && state.temperature == WarningState::Unknown);
    assert(state.showMil && state.showTemperature); // Staleness does not manufacture an all-clear.
    engine.mil.valid = engine.coolant.valid = false;
    state = monitor.update(engine, 600, true);
    assert(state.showMil && state.showTemperature);
    engine = {{0x00, 700, true}, {99, 700, true}};
    state = monitor.update(engine, 700, true);
    assert(!state.showMil && !state.showTemperature); // Fresh clear readings release alerts.
    engine = {{0x80, 800, true}, {120, 800, true}};
    monitor.update(engine, 800, true);
    state = monitor.update(engine, 801, false);
    assert(state.mil == WarningState::Unknown && !state.showMil && !state.showTemperature); // No DEMO warning samples.
    state = monitor.update({}, 802, true);
    assert(!state.showMil && !state.showTemperature); // No old-session alert carried into a new unknown session.
    state = monitor.update(engine, 799, true);
    assert(state.mil == WarningState::Unknown && !state.showMil); // Future timestamps are unknown.
    engine = {{}, {}, {12600, 1000, true}};
    state = monitor.update(engine, 1000, true);
    assert(state.voltage == WarningState::Clear && !state.showVoltage); // Exactly 12.6 V is not below the threshold.
    engine.voltage = {12599, 1100, true};
    state = monitor.update(engine, 1100, true);
    assert(state.voltage == WarningState::Active && state.showVoltage);
    engine.voltage = {12700, 1200, true};
    state = monitor.update(engine, 1200, true);
    assert(state.showVoltage); // Recovery hysteresis.
    state = monitor.update(engine, 1200 + config::kVoltageStaleMs, true);
    assert(state.voltage == WarningState::Unknown && state.showVoltage);
    engine.voltage = {12800, 1300, true};
    state = monitor.update(engine, 1300, true);
    assert(state.voltage == WarningState::Clear && !state.showVoltage);
    engine = {{0, 2000, true}, {85, 2000, true}, {14200, 2000, true}};
    state = monitor.update(engine, 22000, true);
    assert(state.mil == WarningState::Clear && state.temperature == WarningState::Clear && state.voltage == WarningState::Clear);
    state = monitor.update(engine, 2000 + config::kMilStaleMs, true);
    assert(state.mil == WarningState::Unknown && state.temperature == WarningState::Clear && state.voltage == WarningState::Clear);
    for (bool day : {false, true}) {
        std::array<uint16_t, count + 2> demoPixels{};
        demoPixels.front() = demoPixels.back() = 0xa55a;
        GaugeRenderer demo(demoPixels.data() + 1);
        demo.begin(); demo.setView(day, false); demo.showSpeed(111);
        const auto empty = demoPixels;
        demo.setDemoWarnings(true);
        const auto fullDemo = demoPixels;
        for (unsigned slot = 0; slot < 5; ++slot) {
            unsigned colored = 0;
            const int left = slot % 2 ? 194 : 22, top = 80 + static_cast<int>(slot / 2) * 28;
            for (int y = top; y < top + 20; ++y)
                for (int x = left; x < left + 24; ++x)
                    if (demo.row(y)[x] != (day ? 0xffff : 0)) ++colored;
            assert(colored > 25); // Includes the fifth, battery icon in the left column.
        }
        demo.takeDirty(); demo.setDemoWarnings(true);
        assert(demo.takeDirty().empty());
        for (int speed : {0, 9, 99, 100, 199, 255, 111}) demo.showSpeed(speed);
        assert(demoPixels == fullDemo);
        demo.setWarning(WarningIcon::Battery, false); // Clear the last side slot.
        demo.setWarning(WarningIcon::Battery, true); // Restore it without trails.
        assert(demoPixels == fullDemo);
        demo.setView(day, true); demo.setDemoWarnings(false);
        demo.setView(day, false); demo.showSpeed(111);
        assert(demoPixels == empty); // No DEMO samples leak into LIVE after a page switch.
        demo.setWarning(WarningIcon::Battery, true);
        assert(demoPixels != empty);
        demo.setWarning(WarningIcon::Battery, false);
        assert(demoPixels == empty);
        demo.setDemoWarnings(true);
        assert(demoPixels == fullDemo);
        assert(demoPixels.front() == 0xa55a && demoPixels.back() == 0xa55a);
    }
    // Discovery page redraws only changed text and survives theme/page changes.
    DiagnosticStatus discovery;
    discovery.enabled = discovery.connected = true;
    discovery.tested = 31; discovery.succeeded = 23; discovery.failed = 8;
    discovery.deferred = 2; discovery.seconds = 123; discovery.tripMeters = 12345;
    std::snprintf(discovery.item, sizeof(discovery.item), "transmission_21A0");
    std::snprintf(discovery.outcome, sizeof(discovery.outcome), "SUCCESS");
    for (bool day : {false, true, false}) {
        ui.setPage(day,3); ui.showDiagnostics(discovery);
        fresh.setPage(day,3); fresh.showDiagnostics(discovery);
        assert(std::equal(reference.begin(), reference.end(), pixels.begin()+1));
        ui.takeDirty(); ui.showDiagnostics(discovery);
        ui.showSpeed(123); ui.setLive(true); ui.showDimmingTestPercent(50);
        assert(ui.takeDirty().empty());
        ui.setWarning(WarningIcon::Battery, true);
        assert(ui.takeDirty().empty());
        auto waiting = discovery; waiting.connected = false; waiting.seconds = 124;
        ui.showDiagnostics(waiting);
        fresh.setPage(!day,3); fresh.setPage(day,3); fresh.showDiagnostics(waiting);
        assert(std::equal(reference.begin(), reference.end(), pixels.begin()+1));
        ui.setPage(day, 0); ui.setPage(day, 1); ui.setPage(day,3); ui.showDiagnostics(discovery);
        fresh.showDiagnostics(discovery);
        assert(std::equal(reference.begin(), reference.end(), pixels.begin()+1));
    }
    discovery.candidateFocus = discovery.gearValid = discovery.atfValid = discovery.oilValid = true;
    discovery.gear = 14; discovery.commandedGear = 0; discovery.atfC = 35; discovery.oilQuarterC = 171;
    discovery.samples = 99;
    for (const bool day : {false, true}) {
        ui.setPage(day,3);ui.showDiagnostics(discovery);
        fresh.setPage(!day,3);fresh.setPage(day,3);fresh.showDiagnostics(discovery);
        assert(std::equal(reference.begin(),reference.end(),pixels.begin()+1));
        ui.takeDirty();ui.showDiagnostics(discovery);assert(ui.takeDirty().empty());
        auto expired=discovery;expired.gearValid=expired.atfValid=expired.oilValid=false;
        ui.showDiagnostics(expired);
        fresh.setPage(!day,3);fresh.setPage(day,3);fresh.showDiagnostics(expired);
        assert(std::equal(reference.begin(),reference.end(),pixels.begin()+1));
        discovery.oilQuarterC=-1;ui.showDiagnostics(discovery); // Preserve sign on -0.25 C.
        fresh.showDiagnostics(discovery);
        assert(std::equal(reference.begin(),reference.end(),pixels.begin()+1));
    }
    assert(pixels.front() == 0xa55a && pixels.back() == 0xa55a);
    assert(isFresh({0, 100, true}, 100));
    assert(!isFresh({0, 100, true}, 99));
    std::puts("PASS: button bounce/single/double/hold/boot, theme/page redraw, missing/stale/zero metrics");
}
