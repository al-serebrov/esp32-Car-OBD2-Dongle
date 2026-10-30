#pragma once

#include <cstdint>
#include "Config.h"
#include "Telemetry.h"
namespace drivingassets { struct Mask; struct Font; }

struct DirtyRect {
    int x0 = config::kWidth;
    int y0 = config::kHeight;
    int x1 = 0;  // Exclusive.
    int y1 = 0;
    bool empty() const { return x0 >= x1 || y0 >= y1; }
};

enum class WarningIcon : uint8_t { CheckEngine, ParkingBrake, Overheat, Seatbelt, Battery, Count };

// Pure CPU RGB565 renderer; caller supplies contiguous pixels or 16-row bands.
class GaugeRenderer {
public:
    explicit GaugeRenderer(uint16_t* pixels) : pixels_(pixels) {}
    explicit GaugeRenderer(uint16_t* const* bands) : pixels_(nullptr), bands_(bands) {}
    void splash();
    void begin();
    void showSpeed(int kmh);
    void setLive(bool live);
    // Active warnings keep arrival order; clearing one compacts the grid.
    void setWarning(WarningIcon icon, bool active);
    void setDemoWarnings(bool enabled);
    void setView(bool day, bool details);
    void setPage(bool day, unsigned page);
    void showDiagnostics(const DiagnosticStatus& status);
    void showDetails(const DetailReadings& values, uint64_t now);
    void showDriving(int kmh, bool live, const DrivingReadings& values, uint64_t now);
    void updateMilOverlay(bool active, uint64_t now);
    uint16_t displayPixel(int x, int y) const;
    unsigned milOpacityPermille() const { return milAlpha_; }
    void showDimmingTestPercent(unsigned percent);
    void clearDimmingTest();
    DirtyRect takeDirty();
    const uint16_t* row(int y) const {
        return bands_ ? bands_[y / config::kStripRows] + (y % config::kStripRows) * config::kWidth
                      : pixels_ + y * config::kWidth;
    }

private:
    void pixel(int x, int y, uint16_t color);
    void rectangle(int x, int y, int w, int h, uint16_t color);
    void line(int x0, int y0, int x1, int y1, uint16_t color);
    void text(const char* value, int cx, int y, int scale, uint16_t color);
    void digit(int value, int x, int y, int profile = -1);
    void arcSegment(int index, uint16_t color);
    void statusIndicator();
    void warningIcons();
    void redrawWarnings(unsigned previousCount);
    void clear();
    void beginDetails();
    void beginDriving();
    void restoreDriving(int x, int y, int w, int h);
    void drawMask(const drivingassets::Mask& mask, int x, int y, uint16_t color);
    int smoothText(const drivingassets::Font& font, const char* value, int cx, int y, uint16_t color);
    void drivingArc(bool right, int segments);
    uint16_t* pixels_;
    uint16_t* const* bands_ = nullptr;
    DirtyRect dirty_;
    int lastSpeed_ = -1;
    int lastSegments_ = 0;
    bool live_ = false;
    bool day_ = false;
    unsigned page_ = 0;
    char lastDiagnosticLines_[8][32] = {};
    WarningIcon warningOrder_[static_cast<unsigned>(WarningIcon::Count)] = {};
    unsigned warningCount_ = 0;
    bool demoWarnings_ = false;
    char lastDetailText_[4][16] = {};
    int lastDimmingTestPercent_ = -1;
    char drivingLabels_[7][16] = {};
    int drivingSegments_[2] = {-1, -1};
    bool milOverlay_ = false;
    unsigned milAlpha_ = 0;
    uint16_t milWeights_[16] = {};
    uint64_t milStartMs_ = 0;
};
