#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <vector>
#include "display/GaugeRenderer.h"

namespace {
constexpr unsigned kPixels = config::kWidth * config::kHeight;
constexpr int kDigitHeight=120, kDigitTop=50, kDigitBottom=170;
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
            int minY=240,maxY=-1,minX=240,maxX=-1;
            for (int y=30;y<kDigitBottom;++y) for (int x=48;x<192;++x) {
                if (!digitInk(gauge.row(y)[x],day)) continue;
                minY=std::min(minY,y);maxY=std::max(maxY,y);
                minX=std::min(minX,x);maxX=std::max(maxX,x);
            }
            // Glyph 1 has a small top/bottom bearing within its nominal font height.
            assert(minY>=kDigitTop && minY<=kDigitTop+2);
            assert(maxY-minY+1>=kDigitHeight-3 && maxY-minY+1<=kDigitHeight);
            assert(std::abs(minX+maxX-239)<=1); // Leading 1 remains visually centred with every warning count.
            gauge.takeDirty();gauge.showSpeed(111);
            assert(gauge.takeDirty().empty());
        };
        verify();
        // Fill and clear side slots in activation order without changing digit size.
        for (unsigned i=0;i<5;++i) {auto icon=static_cast<WarningIcon>(i);order.push_back(icon);gauge.setWarning(icon,true);verify();}
        for (unsigned i=0;i<5;++i) {auto icon=static_cast<WarningIcon>(i);order.erase(order.begin());gauge.setWarning(icon,false);verify();}
        // Exercise all active subsets; redraws must remove old icons without moving numerals.
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
                for (int y=80;y<156;++y) for (int x=22;x<218;++x) {
                    if(x>=46 && x<194)continue;
                    const int dx=x-120,dy=y-120;
                    if(dx*dx+dy*dy<103*103)
                        assert(gauge.row(y)[x]==stableFrame[1+y*config::kWidth+x]);
                }
                std::array<uint16_t,kPixels> expected{};
                GaugeRenderer reference(expected.data());reference.setPage(day,1);
                for(auto icon:order)reference.setWarning(icon,true);
                reference.begin();reference.setPage(day,0);reference.showSpeed(speed);reference.setLive(true);
                assert(std::equal(expected.begin(),expected.end(),pixels.begin()+1));
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
    std::puts("PASS: fixed 120px digits, centred three-digit ink, all warning subsets, side-column redraws, hidden pages, overlay");
}
