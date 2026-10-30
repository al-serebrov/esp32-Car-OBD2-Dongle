#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <vector>
#include "display/GaugeRenderer.h"

namespace {
constexpr unsigned kPixels = config::kWidth * config::kHeight;
constexpr std::array<int,4> kDigitHeights{120,104,88,76};
constexpr std::array<int,4> kDigitTops{50,42,38,34};
constexpr std::array<int,4> kDigitBottoms{170,146,126,110};
bool digitInk(uint16_t pixel, bool day) {
    // Only the numerals use neutral text/background alpha; arc/unit/icons are coloured.
    for (unsigned alpha = 1; alpha <= 15; ++alpha) {
        const auto light = day ? 15-alpha : alpha;
        const auto r = (31*light+7)/15, g = (63*light+7)/15;
        if (pixel == ((r<<11)|(g<<5)|r)) return true;
    }
    return false;
}
}
int main() {
    for (bool day : {false,true}) {
        std::array<uint16_t,kPixels+2> pixels{};
        pixels.front()=pixels.back()=0xa55a;
        GaugeRenderer gauge(pixels.data()+1);
        gauge.begin(); gauge.setPage(day,0); gauge.showSpeed(111); gauge.setLive(true);
        std::vector<WarningIcon> order;
        const auto verify = [&] {
            std::array<uint16_t,kPixels> clean{};
            GaugeRenderer fresh(clean.data());
            fresh.setPage(day,1); // Build the fixture while hidden; paint icons once.
            for (const auto icon:order) fresh.setWarning(icon,true);
            fresh.begin(); fresh.setPage(day,0); fresh.showSpeed(111); fresh.setLive(true);
            assert(std::equal(clean.begin(),clean.end(),pixels.begin()+1));
            assert(pixels.front()==0xa55a && pixels.back()==0xa55a);
            const auto rows=(order.size()+1)/2;
            int minY=240,maxY=-1,minX=240,maxX=-1;
            for (int y=30;y<kDigitBottoms[rows];++y) for (int x=20;x<220;++x) {
                if (!digitInk(gauge.row(y)[x],day)) continue;
                minY=std::min(minY,y);maxY=std::max(maxY,y);
                minX=std::min(minX,x);maxX=std::max(maxX,x);
            }
            // Glyph 1 has a small top/bottom bearing within its nominal font height.
            assert(minY>=kDigitTops[rows] && minY<=kDigitTops[rows]+2);
            assert(maxY-minY+1>=kDigitHeights[rows]-3 && maxY-minY+1<=kDigitHeights[rows]);
            assert(std::abs(minX+maxX-239)<=1); // Leading 1 remains visually centred at every size.
            gauge.takeDirty();gauge.showSpeed(111);
            assert(gauge.takeDirty().empty());
        };
        verify();
        // Traverse every row boundary in both directions, preserving arrival order.
        for (unsigned i=0;i<5;++i) {auto icon=static_cast<WarningIcon>(i);order.push_back(icon);gauge.setWarning(icon,true);verify();}
        for (unsigned i=0;i<5;++i) {auto icon=static_cast<WarningIcon>(i);order.erase(order.begin());gauge.setWarning(icon,false);verify();}
        // Exercise all active subsets; redraws must remove both old icons and old numerals.
        for (unsigned mask=0;mask<32;++mask) {
            for (unsigned i=0;i<5;++i) {
                const auto icon=static_cast<WarningIcon>(i);
                const auto found=std::find(order.begin(),order.end(),icon);
                const bool active=mask&(1u<<i);
                if (active && found==order.end()) order.push_back(icon);
                if (!active && found!=order.end()) order.erase(found);
                gauge.setWarning(icon,active);
            }
            verify();
            // Digit-width transitions must preserve all warnings and the unit.
            const auto stableFrame=pixels;
            for (const auto speed:{9,11,99,100,110,199,255,111}) {
                gauge.showSpeed(speed);
                const auto rows=(order.size()+1)/2;
                for (int y=kDigitBottoms[rows];y<228;++y) for (int x=20;x<220;++x) {
                    const int dx=x-120,dy=y-120;
                    if (dx*dx+dy*dy<103*103 || (x>=62 && x<178 && y>=140 && y<226))
                        assert(gauge.row(y)[x]==stableFrame[1+y*config::kWidth+x]);
                }
            }
            verify();
            if (mask!=0 && mask!=1 && mask!=3 && mask!=7 && mask!=31) continue;
            // Changes while hidden take effect when returning to the speed page.
            gauge.setPage(day,3);gauge.takeDirty();
            gauge.setWarning(WarningIcon::Battery,false);
            order.erase(std::remove(order.begin(),order.end(),WarningIcon::Battery),order.end());
            assert(gauge.takeDirty().empty());
            gauge.setPage(day,0);gauge.showSpeed(111);gauge.setLive(true);verify();
        }
        // The optional dimming overlay survives layout changes and clears completely.
        gauge.showDimmingTestPercent(50);
        gauge.setDemoWarnings(true);gauge.setDemoWarnings(false);
        gauge.clearDimmingTest();gauge.showSpeed(111);
        order.clear();verify();
    }
    std::puts("PASS: 120/104/88/76px adaptive digits, centred three-digit ink, all warning subsets, grow/shrink redraws, hidden pages, overlay");
}
