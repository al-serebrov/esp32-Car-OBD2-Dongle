#include "GaugeRenderer.h"
#include "SpeedFont.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>

namespace {
constexpr uint16_t rgb(int r, int g, int b) {
    return static_cast<uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}
struct Palette { uint16_t background, track, accent, text, muted, demo; };
constexpr Palette kNight{rgb(0, 0, 0), rgb(30, 42, 52), rgb(44, 211, 194),
                         rgb(255, 255, 255), rgb(128, 150, 163), rgb(239, 181, 84)};
constexpr Palette kDay{rgb(255, 255, 255), rgb(210, 215, 220), rgb(0, 115, 105),
                       rgb(0, 0, 0), rgb(70, 80, 90), rgb(135, 80, 0)};
const Palette& palette(bool day) { return day ? kDay : kNight; }
constexpr int kSegments = 80;
constexpr int kArcInnerRadius = 103;
struct SpeedLayout { int digitTop, unitTop, iconTop, iconWidth, iconHeight, iconPitch; };
constexpr SpeedLayout kSpeedLayouts[] = {
    {50, 182, 184, 44, 36, 38}, // No warnings: full 120px digits.
    {42, 156, 184, 44, 36, 38}, // One row: 104px digits.
    {38, 134, 152, 44, 36, 38}, // Two rows: 88px digits.
    {34, 120, 140, 32, 26, 28}, // Three rows: 76px digits.
};
unsigned warningRows(unsigned count) { return (count + 1) / 2; }
constexpr float kPi = 3.14159265359f;

// Compact vector strokes; sampled at 2x2 for smooth warning-icon edges.
struct Stroke { float x0, y0, x1, y1; };
constexpr Stroke kEngineStrokes[] = {
    {7, 9, 12, 9}, {12, 9, 12, 6}, {12, 6, 26, 6},
    {26, 6, 32, 12}, {32, 12, 36, 12}, {36, 12, 36, 9},
    {36, 9, 40, 9}, {40, 9, 40, 27}, {40, 27, 36, 27},
    {36, 27, 36, 24}, {36, 24, 32, 24}, {32, 24, 27, 30},
    {27, 30, 12, 30}, {12, 30, 7, 25}, {7, 25, 7, 9},
    {17, 2, 25, 2}, {21, 2, 21, 6},
    {2, 12, 2, 24}, {2, 18, 7, 18},
};
constexpr Stroke kParkingP[] = {
    {18, 25, 18, 12}, {18, 12, 24, 12}, {24, 12, 27, 15},
    {27, 15, 27, 17}, {27, 17, 24, 20}, {24, 20, 18, 20},
};
constexpr Stroke kTemperatureStrokes[] = {
    {14, 4, 14, 19}, {14, 4, 20, 4}, {20, 4, 20, 19}, {17, 10, 17, 23},
    {25, 8, 30, 8}, {25, 14, 30, 14}, {25, 20, 30, 20},
    {3, 33, 8, 31}, {8, 31, 13, 33}, {13, 33, 18, 31},
    {18, 31, 23, 33}, {23, 33, 28, 31}, {28, 31, 33, 33},
    {33, 33, 38, 31}, {38, 31, 41, 32},
};
constexpr Stroke kSeatbeltStrokes[] = {
    {14, 14, 17, 11}, {17, 11, 26, 11}, {26, 11, 29, 17},
    {29, 17, 28, 25}, {28, 25, 15, 25}, {15, 25, 14, 14},
    {15, 25, 17, 33}, {28, 25, 26, 33},
    {14, 12, 28, 26}, {15, 22, 27, 22},
};
constexpr Stroke kBatteryStrokes[] = {
    {3, 10, 41, 10}, {41, 10, 41, 30}, {41, 30, 3, 30}, {3, 30, 3, 10},
    {8, 10, 8, 6}, {8, 6, 15, 6}, {15, 6, 15, 10},
    {29, 10, 29, 6}, {29, 6, 36, 6}, {36, 6, 36, 10},
    {9, 20, 17, 20}, {27, 20, 35, 20}, {31, 16, 31, 24},
};
template <size_t N>
bool onStroke(float x, float y, const Stroke (&strokes)[N]) {
    for (const auto& stroke : strokes) {
        const float dx = stroke.x1 - stroke.x0, dy = stroke.y1 - stroke.y0;
        const float t = std::max(0.0f, std::min(1.0f,
            ((x - stroke.x0) * dx + (y - stroke.y0) * dy) / (dx * dx + dy * dy)));
        const float ex = x - stroke.x0 - t * dx, ey = y - stroke.y0 - t * dy;
        if (ex * ex + ey * ey <= 1.25f * 1.25f) return true;
    }
    return false;
}
bool onWarningIcon(WarningIcon icon, float x, float y) {
    switch (icon) {
        case WarningIcon::CheckEngine:
            return onStroke(x, y, kEngineStrokes);
        case WarningIcon::ParkingBrake: {
            const float dx = x - 22, dy = y - 18;
            const float radius = std::sqrt(dx * dx + dy * dy);
            return std::abs(radius - 12) <= 1.25f ||
                   (std::abs(radius - 18) <= 1.25f && std::abs(dx) >= 13) ||
                   onStroke(x, y, kParkingP);
        }
        case WarningIcon::Overheat: {
            const float dx = x - 17, dy = y - 23;
            const float radius = std::sqrt(dx * dx + dy * dy);
            return std::abs(radius - 5) <= 1.25f || radius <= 3 ||
                   onStroke(x, y, kTemperatureStrokes);
        }
        case WarningIcon::Seatbelt: {
            const float dx = x - 22, dy = y - 5;
            return dx * dx + dy * dy <= 16 || onStroke(x, y, kSeatbeltStrokes);
        }
        case WarningIcon::Battery: return onStroke(x, y, kBatteryStrokes);
        case WarningIcon::Count: return false;
    }
    return false;
}
uint16_t blend(uint16_t foreground, uint16_t background, unsigned alpha, unsigned maximum) {
    const unsigned inverse = maximum - alpha;
    const unsigned red = (((foreground >> 11) & 31) * alpha + ((background >> 11) & 31) * inverse + maximum / 2) / maximum;
    const unsigned green = (((foreground >> 5) & 63) * alpha + ((background >> 5) & 63) * inverse + maximum / 2) / maximum;
    const unsigned blue = ((foreground & 31) * alpha + (background & 31) * inverse + maximum / 2) / maximum;
    return static_cast<uint16_t>((red << 11) | (green << 5) | blue);
}

// Original 5x7 bitmap alphabet, rows stored as five low bits.
struct Glyph { char ch; uint8_t rows[7]; };
constexpr Glyph kGlyphs[] = {
    {'0',{14,17,19,21,25,17,14}}, {'1',{4,12,4,4,4,4,14}},
    {'2',{14,17,1,2,4,8,31}}, {'3',{30,1,1,14,1,1,30}},
    {'4',{2,6,10,18,31,2,2}}, {'5',{31,16,16,30,1,1,30}},
    {'6',{14,16,16,30,17,17,14}}, {'7',{31,1,2,4,8,8,8}},
    {'8',{14,17,17,14,17,17,14}}, {'9',{14,17,17,15,1,1,14}},
    {'A',{14,17,17,31,17,17,17}}, {'D',{30,17,17,17,17,17,30}},
    {'E',{31,16,16,30,16,16,31}}, {'H',{17,17,17,31,17,17,17}},
    {'K',{17,18,20,24,20,18,17}}, {'M',{17,27,21,21,17,17,17}},
    {'I',{14,4,4,4,4,4,14}}, {'L',{16,16,16,16,16,16,31}},
    {'V',{17,17,17,17,17,10,4}},
    {'O',{14,17,17,17,17,17,14}}, {'P',{30,17,17,30,16,16,16}},
    {'S',{15,16,16,14,1,1,30}}, {'T',{31,4,4,4,4,4,4}},
    {'B',{30,17,17,30,17,17,30}}, {'C',{14,17,16,16,16,17,14}},
    {'G',{14,17,16,23,17,17,15}}, {'N',{17,25,25,21,19,19,17}},
    {'U',{17,17,17,17,17,17,14}}, {'.',{0,0,0,0,0,12,12}},
    {'F',{31,16,16,30,16,16,16}}, {'R',{30,17,17,30,20,18,17}},
    {'W',{17,17,17,21,21,27,17}}, {'X',{17,17,10,4,10,17,17}},
    {'Y',{17,17,10,4,4,4,4}}, {'Q',{14,17,17,17,21,18,13}},
    {'J',{7,2,2,2,18,18,12}}, {':',{0,4,4,0,4,4,0}},
    {'?',{14,17,1,2,4,0,4}},
    {'%',{25,26,2,4,8,11,19}},
    {'-',{0,0,0,31,0,0,0}},
    {'/',{1,2,2,4,8,8,16}}, {' ',{0,0,0,0,0,0,0}},
};
const uint8_t* glyph(char ch) {
    for (const auto& item : kGlyphs) if (item.ch == ch) return item.rows;
    return kGlyphs[sizeof(kGlyphs) / sizeof(kGlyphs[0]) - 1].rows;
}
void point(float degrees, int radius, int& x, int& y) {
    const float angle = degrees * kPi / 180.0f;
    x = 120 + static_cast<int>(std::lround(std::cos(angle) * radius));
    y = 120 + static_cast<int>(std::lround(std::sin(angle) * radius));
}
}  // namespace

void GaugeRenderer::pixel(int x, int y, uint16_t color) {
    if (x < 0 || y < 0 || x >= config::kWidth || y >= config::kHeight) return;
    auto& target = const_cast<uint16_t*>(row(y))[x];
    if (target == color) return;
    target = color;
    dirty_.x0 = std::min(dirty_.x0, x);
    dirty_.y0 = std::min(dirty_.y0, y);
    dirty_.x1 = std::max(dirty_.x1, x + 1);
    dirty_.y1 = std::max(dirty_.y1, y + 1);
}

void GaugeRenderer::rectangle(int x, int y, int w, int h, uint16_t color) {
    for (int row = y; row < y + h; ++row)
        for (int col = x; col < x + w; ++col) pixel(col, row, color);
}

void GaugeRenderer::line(int x0, int y0, int x1, int y1, uint16_t color) {
    const int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    const int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    for (;;) {
        pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        const int twice = error * 2;
        if (twice >= dy) { error += dy; x0 += sx; }
        if (twice <= dx) { error += dx; y0 += sy; }
    }
}

void GaugeRenderer::text(const char* value, int cx, int y, int scale, uint16_t color) {
    const int length = static_cast<int>(std::strlen(value));
    int x = cx - (length * 6 - 1) * scale / 2;
    for (int i = 0; i < length; ++i, x += 6 * scale) {
        const auto* rows = glyph(value[i]);
        for (int row = 0; row < 7; ++row)
            for (int col = 0; col < 5; ++col)
                if (rows[row] & (1 << (4 - col)))
                    rectangle(x + col * scale, y + row * scale, scale, scale, color);
    }
}

void GaugeRenderer::digit(int value, int x, int y, int profile) {
    // Pre-rendered Roboto Condensed Bold digits, four-bit alpha in flash.
    // A small RGB565 palette gives smooth edges without heap allocation.
    uint16_t colors[16];
    const unsigned background = palette(day_).background;
    const unsigned foreground = palette(day_).text;
    for (unsigned alpha = 0; alpha < 16; ++alpha) {
        colors[alpha] = blend(foreground, background, alpha, 15);
    }
    const auto& font = speedfont::kFonts[profile < 0 ? warningRows(warningCount_) : static_cast<unsigned>(profile)];
    for (int row = 0; row < font.height; ++row) {
        for (int col = 0; col < font.width; ++col) {
            const int offset = row * font.width + col;
            const uint8_t packed = font.alpha[value * font.width * font.height / 2 + offset / 2];
            const unsigned alpha = offset % 2 ? packed & 15 : packed >> 4;
            const int dx = x + col - 120, dy = y + row - 120;
            if (alpha && dx * dx + dy * dy < kArcInnerRadius * kArcInnerRadius)
                pixel(x + col, y + row, colors[alpha]);
        }
    }
}

void GaugeRenderer::arcSegment(int index, uint16_t color) {
    // Bottom-left -> top -> bottom-right, leaving a 90 degree bottom gap.
    const float start = 135.0f + index * 270.0f / kSegments;
    const float end = start + 0.86f * 270.0f / kSegments;
    int minX = 240, minY = 240, maxX = 0, maxY = 0;
    // Bound this small annular sector, including any cardinal extremum.
    const float samples[] = {start, end, 180.0f, 270.0f, 360.0f};
    for (const float angle : samples) {
        if (angle < start || angle > end) continue;
        for (const int radius : {kArcInnerRadius, 112}) {
            int x, y;
            point(angle, radius, x, y);
            minX = std::min(minX, x); maxX = std::max(maxX, x);
            minY = std::min(minY, y); maxY = std::max(maxY, y);
        }
    }
    for (int y = minY - 1; y <= maxY + 1; ++y) {
        for (int x = minX - 1; x <= maxX + 1; ++x) {
            const int dx = x - 120, dy = y - 120;
            const int radiusSquared = dx * dx + dy * dy;
            if (radiusSquared < kArcInnerRadius * kArcInnerRadius || radiusSquared > 112 * 112) continue;
            float angle = std::atan2(static_cast<float>(dy), static_cast<float>(dx)) * 180.0f / kPi;
            if (angle < 135.0f) angle += 360.0f;
            // Each pixel belongs to at most one segment: no color trails when
            // an adjacent segment is switched off on the downward sweep.
            if (angle >= start && angle < end) pixel(x, y, color);
        }
    }
}

void GaugeRenderer::clear() {
    for (int y = 0; y < config::kHeight; ++y)
        std::fill(const_cast<uint16_t*>(row(y)), const_cast<uint16_t*>(row(y)) + config::kWidth, palette(day_).background);
    dirty_ = {0, 0, config::kWidth, config::kHeight};
}

void GaugeRenderer::splash() {
    clear();
    for (int i = 0; i < kSegments; ++i) arcSegment(i, palette(day_).track);
    text("DASH", 120, 91, 3, palette(day_).text);
    statusIndicator();
}

void GaugeRenderer::begin() {
    page_ = 0;
    clear();
    lastSpeed_ = -1;
    lastSegments_ = 0;
    live_ = false;
    lastDimmingTestPercent_ = -1;
    for (int i = 0; i < kSegments; ++i) arcSegment(i, palette(day_).track);
    text("KM/H", 120, kSpeedLayouts[warningRows(warningCount_)].unitTop, 2, palette(day_).muted);
    warningIcons();
    statusIndicator();
    showSpeed(0);
}

void GaugeRenderer::showSpeed(int kmh) {
    kmh = std::max(0, std::min(kmh, 255));
    if (page_ != 0 || kmh == lastSpeed_) return;
    const int count = std::min(kmh, config::kSpeedScaleKmh) * kSegments / config::kSpeedScaleKmh;
    for (int i = std::min(count, lastSegments_); i < std::max(count, lastSegments_); ++i)
        arcSegment(i, i < count ? palette(day_).accent : palette(day_).track);
    lastSegments_ = count;
    const auto rows = warningRows(warningCount_);
    const auto& font = speedfont::kFonts[rows];
    const auto& layout = kSpeedLayouts[rows];
    const int numberWidth = 3 * font.width;
    const int numberLeft = (config::kWidth - numberWidth) / 2;
    // Clear in RAM only. The panel receives completed pixels, never a blank frame.
    // Clear only the dial interior. The large bounding box reaches the arc
    // near its corners; a rectangular erase would remove those arc pixels.
    for (int y = layout.digitTop; y < layout.digitTop + font.height; ++y) {
        for (int x = numberLeft; x < numberLeft + numberWidth; ++x) {
            const int dx = x - 120, dy = y - 120;
            if (dx * dx + dy * dy < kArcInnerRadius * kArcInnerRadius)
                pixel(x, y, palette(day_).background);
        }
    }
    char number[8];
    std::snprintf(number, sizeof(number), "%d", kmh);
    const int length = static_cast<int>(std::strlen(number));
    // Center visible ink using the leading glyph's bearing. All glyphs share
    // a right edge, so the last two cells never reflow throughout 100..199.
    const int leadingInset = font.inkLeft[number[0] - '0'];
    const int visibleWidth = length * font.width - leadingInset;
    const int left = (config::kWidth - visibleWidth) / 2 - leadingInset;
    for (int i = 0; i < length; ++i) digit(number[i] - '0', left + i * font.width, layout.digitTop);
    lastSpeed_ = kmh;
}

void GaugeRenderer::setLive(bool live) {
    if (live == live_) return;
    live_ = live;
    if (page_ <= 1) statusIndicator();
}

void GaugeRenderer::warningIcons() {
    const uint16_t amber = day_ ? rgb(160, 90, 0) : rgb(255, 185, 35);
    const uint16_t red = day_ ? rgb(195, 15, 30) : rgb(255, 60, 65);
    const auto& layout = kSpeedLayouts[warningRows(warningCount_)];
    const int width = layout.iconWidth, height = layout.iconHeight;
    for (unsigned index = 0; index < warningCount_; ++index) {
        const auto icon = warningOrder_[index];
        const int left = (width == 32 ? 68 : 62) + static_cast<int>(index % 2) * 72;
        const int top = layout.iconTop + static_cast<int>(index / 2) * layout.iconPitch;
        const uint16_t color = icon == WarningIcon::CheckEngine ? amber : red;
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                unsigned coverage = 0;
                for (const float sy : {0.25f, 0.75f}) {
                    for (const float sx : {0.25f, 0.75f}) {
                        const float px = (x + sx) * 44 / width, py = (y + sy) * 36 / height;
                        if (onWarningIcon(icon, px, py)) ++coverage;
                    }
                }
                if (coverage) pixel(left + x, top + y,
                    blend(color, palette(day_).background, coverage, 4));
            }
        }
    }
}

void GaugeRenderer::redrawWarnings(unsigned previousCount) {
    if (page_ != 0) return;
    const auto rows = warningRows(warningCount_);
    if (rows != warningRows(previousCount)) {
        // Remove all previous digit/unit positions without touching the arc.
        // Only completed pixels are flushed, so resizing never exposes a blank frame.
        for (int y = 17; y < 224; ++y) for (int x = 17; x < 224; ++x) {
            const int dx = x - 120, dy = y - 120;
            if (dx * dx + dy * dy < kArcInnerRadius * kArcInnerRadius)
                pixel(x, y, palette(day_).background);
        }
        rectangle(62, 140, 116, 86, palette(day_).background);
        text("KM/H", 120, kSpeedLayouts[rows].unitTop, 2, palette(day_).muted);
        const int speed = std::max(lastSpeed_, 0);
        lastSpeed_ = -1;
        showSpeed(speed);
        if (lastDimmingTestPercent_ >= 0) {
            const auto percent = static_cast<unsigned>(lastDimmingTestPercent_);
            lastDimmingTestPercent_ = -1;
            showDimmingTestPercent(percent);
        }
    } else if (rows) {
        const int top = kSpeedLayouts[rows].iconTop;
        rectangle(62, top, 116, 226 - top, palette(day_).background);
    }
    warningIcons();
}

void GaugeRenderer::setDemoWarnings(bool enabled) {
    if (enabled == demoWarnings_) return;
    const auto previousCount = warningCount_;
    demoWarnings_ = enabled;
    warningCount_ = 0;
    if (enabled) {
        for (unsigned i = 0; i < static_cast<unsigned>(WarningIcon::Count); ++i)
            warningOrder_[warningCount_++] = static_cast<WarningIcon>(i);
    }
    redrawWarnings(previousCount);
}

void GaugeRenderer::setWarning(WarningIcon icon, bool active) {
    if (static_cast<unsigned>(icon) >= static_cast<unsigned>(WarningIcon::Count)) return;
    unsigned index = 0;
    const auto previousCount = warningCount_;
    while (index < warningCount_ && warningOrder_[index] != icon) ++index;
    if (active) {
        if (index < warningCount_) return; // Repeated readings do not reorder icons.
        warningOrder_[warningCount_++] = icon;
    } else {
        if (index == warningCount_) return;
        for (unsigned i = index + 1; i < warningCount_; ++i) warningOrder_[i - 1] = warningOrder_[i];
        --warningCount_;
    }
    // State may change while the engine-data page is visible. Its next speed
    // page render will use the same order without drawing icons over the tiles.
    redrawWarnings(previousCount);
}

void GaugeRenderer::statusIndicator() {
    const uint16_t fill = live_ ? rgb(0, 195, 70) : rgb(255, 200, 0);
    const uint16_t border = live_ ? rgb(0, 105, 35) : rgb(150, 110, 0);
    // Ten-pixel disc, centered between pixels for an even 10x10 footprint.
    for (int y = 0; y < 10; ++y) {
        for (int x = 0; x < 10; ++x) {
            const int dx = 2 * x - 9, dy = 2 * y - 9;
            const int distance = dx * dx + dy * dy;
            if (distance <= 100) pixel(115 + x, 228 + y, distance <= 64 ? fill : border);
        }
    }
}

void GaugeRenderer::setView(bool day, bool details) { setPage(day, details ? 2 : 0); }

void GaugeRenderer::setPage(bool day, unsigned page) {
    page = std::min(page, 3u);
    if (day_ == day && page_ == page) return;
    day_ = day;
    if (page == 1) beginDriving();
    else if (page == 2) beginDetails();
    else if (page == 0) begin();
    else {
        page_ = 3;
        clear();
        for (auto& line : lastDiagnosticLines_) line[0] = 0;
        text("DIAGNOSTICS", 120, 30, 1, palette(day_).muted);
    }
}

void GaugeRenderer::showDiagnostics(const DiagnosticStatus& status) {
    if (page_ != 3) return;
    char lines[8][32] = {};
    std::snprintf(lines[0], sizeof(lines[0]), "%s", !status.connected ? "WAIT FOR CAR" :
                  !status.tested ? "WAIT FOR SPEED" : "RECORDING");
    std::snprintf(lines[1], sizeof(lines[1]), "OK %u  FAIL %u", status.succeeded, status.failed);
    std::snprintf(lines[2], sizeof(lines[2]), "%s", status.complete ? "SWEEP DONE / SAMPLING" : "DISCOVERING DATA");
    std::snprintf(lines[3], sizeof(lines[3]), "TIME %u:%02u", static_cast<unsigned>(status.seconds/60), static_cast<unsigned>(status.seconds%60));
    std::snprintf(lines[4], sizeof(lines[4]), "TRIP %u.%03u KM", static_cast<unsigned>(status.tripMeters/1000), static_cast<unsigned>(status.tripMeters%1000));
    std::snprintf(lines[5], sizeof(lines[5]), "DEFERRED %u", status.deferred);
    std::snprintf(lines[6], sizeof(lines[6]), "%.28s", status.item[0] ? status.item : "WAITING FOR CONNECTION");
    std::snprintf(lines[7], sizeof(lines[7]), "%.24s", status.outcome[0] ? status.outcome : "LOGS SAVED ON DEVICE");
    if (status.candidateFocus) {
        if (status.gearValid) std::snprintf(lines[2], sizeof(lines[2]), "GEAR? %d / CMD? %d", status.gear, status.commandedGear);
        else std::snprintf(lines[2], sizeof(lines[2]), "GEAR? -- / CMD? --");
        if (status.atfValid) std::snprintf(lines[3], sizeof(lines[3]), "ATF? %d C", status.atfC);
        else std::snprintf(lines[3], sizeof(lines[3]), "ATF? -- C");
        if (status.oilValid) {
            const int value = std::abs(status.oilQuarterC);
            std::snprintf(lines[4], sizeof(lines[4]), "OIL? %s%d.%02d C", status.oilQuarterC < 0 ? "-" : "", value/4, value%4*25);
        } else std::snprintf(lines[4], sizeof(lines[4]), "OIL? -- C");
        std::snprintf(lines[5], sizeof(lines[5]), "%u:%02u / SAMPLES %u", static_cast<unsigned>(status.seconds/60),
                      static_cast<unsigned>(status.seconds%60), static_cast<unsigned>(status.samples));
    }
    constexpr int rows[] = {55, 79, 106, 126, 145, 164, 184, 205};
    for (unsigned i = 0; i < 8; ++i) {
        for (auto& ch : lines[i]) {
            if (ch == '_') ch = ' ';
            else ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        }
        if (!std::strcmp(lastDiagnosticLines_[i], lines[i])) continue;
        std::strcpy(lastDiagnosticLines_[i], lines[i]);
        rectangle(18, rows[i], 204, 14, palette(day_).background);
        text(lines[i], 120, rows[i], i < 2 ? 2 : 1, i == 0 ? palette(day_).accent : palette(day_).text);
    }
}

void GaugeRenderer::beginDetails() {
    page_ = 2;
    clear();
    for (auto& label : lastDetailText_) label[0] = 0;
    line(120, 39, 120, 211, palette(day_).track);
    line(16, 120, 224, 120, palette(day_).track);
    text("ENGINE DATA", 120, 22, 1, palette(day_).muted);
    text("VOLTAGE", 67, 55, 1, palette(day_).muted);
    text("COOLANT", 173, 55, 1, palette(day_).muted);
    text("V", 67, 104, 1, palette(day_).muted);
    text("C", 173, 104, 1, palette(day_).muted);
    text("LAMBDA 1", 67, 146, 1, palette(day_).muted);
    text("LAMBDA 2", 173, 146, 1, palette(day_).muted);
    text("V / B1S1", 67, 194, 1, palette(day_).muted);
    text("V / B1S2", 173, 194, 1, palette(day_).muted);
}

void GaugeRenderer::showDetails(const DetailReadings& values, uint64_t now) {
    if (page_ != 2) return;
    for (unsigned i = 0; i < 4; ++i) {
        char label[16];
        const auto& reading = values.values[i];
        const int staleMs = config::kEngineMonitorEnabled && i < 2 ?
            (i == 0 ? config::kVoltageStaleMs : config::kCoolantStaleMs) : config::kDetailStaleMs;
        if (!isFresh(reading, now, staleMs)) std::snprintf(label, sizeof(label), "--");
        else if (i == 0) {
            const int tenths = (reading.value + 50) / 100;
            std::snprintf(label, sizeof(label), "%d.%d", tenths / 10, tenths % 10);
        } else if (i == 1) std::snprintf(label, sizeof(label), "%d", reading.value);
        else std::snprintf(label, sizeof(label), "%d.%03d", reading.value / 1000, reading.value % 1000);
        if (!std::strcmp(label, lastDetailText_[i])) continue;
        std::strcpy(lastDetailText_[i], label);
        const int cx = i % 2 ? 173 : 67;
        const int y = i < 2 ? 79 : 165;
        rectangle(cx - 45, y, 90, 14, palette(day_).background);
        text(label, cx, y, 2, palette(day_).text);
    }
}

void GaugeRenderer::showDimmingTestPercent(unsigned percent) {
    if (page_ != 0) return;
    percent = std::min(percent, 100u);
    if (lastDimmingTestPercent_ == static_cast<int>(percent)) return;
    rectangle(94, 22, 52, 10, palette(day_).background);
    char label[16];
    std::snprintf(label, sizeof(label), "%s %u%%", config::kImageDimmingEnabled ? "IMG" : "DIM", percent);
    text(label, 120, 23, 1, palette(day_).demo);
    lastDimmingTestPercent_ = static_cast<int>(percent);
}

void GaugeRenderer::clearDimmingTest() {
    if (lastDimmingTestPercent_ < 0) return;
    if (page_ == 0) rectangle(94, 22, 52, 10, palette(day_).background);
    lastDimmingTestPercent_ = -1;
}

DirtyRect GaugeRenderer::takeDirty() {
    const auto result = dirty_;
    dirty_ = {};
    return result;
}
