#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>
#include "display/GaugeRenderer.h"
#include "obd/Elm327.h"
#include "fixtures/hyundai_ride_2026_10_30.h"
using obd::Elm327;
namespace {
constexpr unsigned count=240*240;
Elm327::Result reply(Elm327& e,const char* s,uint64_t now){auto result=Elm327::Result::None;for(;*s;++s){auto r=e.receive(*s,now);if(r!=Elm327::Result::None)result=r;}return result;}
uint64_t init(Elm327& e) {
    e.reset(0);uint64_t now=0;
    for(unsigned i=0;i<7;++i){assert(e.nextCommand(now));reply(e,i==0?"ELM327 v2.3>":i==6?"410000080000>":"OK>",now+10);now+=100;}
    return now;
}
DrivingReadings values(uint64_t now) {return {{2600,now,true},{38,now,true},{24,now,true},{85,now,true},{13800,now,true},{8,4,now,true}};}
}
int main() {
    char label[4];
    for(auto pair:{std::pair<int,const char*>{8,"D4"},{9,"N"},{10,"R"},{11,"P"},{0,"--"},{255,"--"}}) {
        obd::transmissionLabel({pair.first,4,100,true},label);assert(!std::strcmp(label,pair.second));
    }
    obd::transmissionLabel({8,0,100,true},label);assert(!std::strcmp(label,"D"));
    obd::transmissionLabel({8,9,100,true},label);assert(!std::strcmp(label,"D"));
    // Route cancellation, negative/short reply recovery and every restore step.
    const char* captured=nullptr;for(const auto& f:kHyundaiRideFixtures)if(f.item==269){captured=f.response;break;}assert(captured);
    for(unsigned stop=0;stop<6;++stop){
        obd::TransmissionPoll poll;poll.reset(0);poll.enable(true);assert(poll.due(0));poll.start();
        const char* commands[]={"ATDPN\r","ATSH7E1\r","ATCRA7E9\r","21A0\r","ATSH7DF\r","ATCRA\r"};
        for(unsigned i=0;i<6;++i){
            if(i==stop && stop>0 && stop<4){poll.enable(false);break;}
            assert(!std::strcmp(poll.command(),commands[i]));
            auto r=poll.receive(i==0?"A6":i==3?captured:"OK",100+i*20);
            if(i==3)assert(r==obd::TransmissionPoll::Result::Sample);
        }
        while(poll.active()){const auto* c=poll.command();assert(!std::strcmp(c,"ATSH7DF\r")||!std::strcmp(c,"ATCRA\r"));poll.receive("OK",300);}
    }
    for(unsigned fail=0;fail<6;++fail) {
        Elm327 timed;uint64_t clock=init(timed);timed.setDrivingActive(true);
        const auto next=[&](){const char* c=nullptr;for(unsigned spin=0;spin<5000 && !c;++spin,++clock)c=timed.nextCommand(clock);assert(c);return std::string(c);};
        std::string command;
        for(unsigned n=0;n<20;++n) {
            command=next();if(command=="ATDPN\r")break;
            const char* answer=command=="010D\r"?"410D00>":command=="010C\r"?"410C1000>":command=="0104\r"?"410410>":"411110>";
            reply(timed,answer,clock+10);clock+=10;
        }
        assert(command=="ATDPN\r");
        for(unsigned phase=0;phase<fail;++phase) {
            reply(timed,phase==0?"A6>":phase==3?(std::string(captured)+">").c_str():"OK>",clock+10);clock+=10;
            command=next();
        }
        assert(timed.timedOut(clock+5000) && !timed.alive());
    }
    obd::TransmissionPoll shortReply;shortReply.reset(0);shortReply.enable(true);shortReply.start();
    shortReply.receive("A6",10);shortReply.receive("OK",20);shortReply.receive("OK",30);
    assert(shortReply.receive("01E\r0:61A0FFBF0000\r1:00000000000000\r",40)==obd::TransmissionPoll::Result::Error);
    shortReply.receive("OK",50);shortReply.receive("OK",60);
    assert(!shortReply.due(1039) && shortReply.due(1040));
    shortReply.start();shortReply.timeout();shortReply.reset(2000);assert(!shortReply.due(5000));
    // End-to-end scheduler preserves speed/route ownership on driving and exit.
    Elm327 e;uint64_t now=init(e);e.setDrivingActive(true);e.setEngineMonitorEnabled(true);
    std::string pending;uint64_t at=0,lastSpeed=0,maxGap=0;bool routed=false,optional=false;unsigned speeds=0;unsigned metrics[4]={};
    for(;now<120000;now+=10){
        if(now==90000)e.setDrivingActive(false);
        assert(!e.timedOut(now));
        if(!pending.empty() && now>=at){
            const char* response="OK>";
            if(pending=="010D\r")response="410D37>";
            else if(pending=="010C\r")response="410C28A0>";
            else if(pending=="0104\r")response="410461>";
            else if(pending=="0111\r")response="41113D>";
            else if(pending=="0101\r")response="410100000000>";
            else if(pending=="0105\r")response="41057D>";
            else if(pending=="ATRV\r")response="13.8V>";
            else if(pending=="ATDPN\r")response="A6>";
            else if(pending=="21A0\r"){static std::string r;r=std::string(captured)+">";response=r.c_str();}
            if(pending=="ATSH7E1\r")routed=true;
            if(pending=="ATSH7DF\r")routed=false;
            const auto r=reply(e,response,now);assert(e.alive());
            if(r==Elm327::Result::Speed){assert(!routed);if(lastSpeed)maxGap=std::max(maxGap,now-lastSpeed);lastSpeed=now;++speeds;optional=false;}
            if(r==Elm327::Result::Driving || r==Elm327::Result::Monitor){assert(!optional);optional=true;}
            if(r==Elm327::Result::Driving)++metrics[e.drivingMetric()];
            pending.clear();
        }
        if(const auto* c=e.nextCommand(now)){
            assert(pending.empty());pending=c;at=now+60;
            if(pending=="010D\r")assert(!routed);
            assert(e.speedTransaction()==(pending=="010D\r"));
            if(now>91000)assert(pending=="010D\r" || pending=="0101\r" || pending=="0105\r" || pending=="ATRV\r");
        }
    }
    for(auto n:metrics)assert(n>20);assert(maxGap<1500);
    std::printf("DRIVING_SCHEDULER speed_samples=%u RPM=%u load=%u throttle=%u gear=%u max_gap_ms=%llu\n",speeds,metrics[0],metrics[1],metrics[2],metrics[3],static_cast<unsigned long long>(maxGap));
    // Rendering is deterministic across changing widths, missing readings and themes.
    for(bool day:{false,true}) {
        std::array<uint16_t,count+2> pixels{};pixels.front()=pixels.back()=0xa55a;
        GaugeRenderer ui(pixels.data()+1);ui.setPage(day,1);
        auto v=values(100);
        const auto compare=[&](int speed,uint64_t clock){
            std::array<uint16_t,count> expected{};GaugeRenderer fresh(expected.data());fresh.setPage(day,1);fresh.showDriving(speed,true,v,clock);
            ui.showDriving(speed,true,v,clock);assert(std::equal(expected.begin(),expected.end(),pixels.begin()+1));
            assert(pixels.front()==0xa55a && pixels.back()==0xa55a);
            ui.takeDirty();ui.showDriving(speed,true,v,clock);assert(ui.takeDirty().empty());
        };
        for(int n:{0,1,24,38,99,100,24,0}){
            v.load.value=n;v.throttle.value=100-n;compare(n==100?199:n,100);
        }
        for(int selector:{8,9,10,11,255,8})for(int gear:{0,1,4,6,8}){v.transmission.selector=selector;v.transmission.gear=gear;compare(111,100);}
        for(int volts:{9999,10000,13800,12599}){v.voltage.value=volts;compare(88,100);}
        compare(88,70000);v=values(70000);compare(88,70000);
        ui.setPage(day,3);ui.setPage(day,2);ui.setPage(day,0);ui.setPage(day,1);compare(88,70000);
        const auto base=pixels;
        ui.updateMilOverlay(true,1000);assert(ui.milOpacityPermille()==400);
        ui.updateMilOverlay(true,2000);assert(ui.milOpacityPermille()==750);
        for(uint64_t time=2000;time<=5000;time+=50){
            ui.updateMilOverlay(true,time);assert(ui.milOpacityPermille()>=400 && ui.milOpacityPermille()<=750);
            assert(pixels==base); // The pulse never mutates the dashboard's source pixels.
            assert(ui.displayPixel(120,120)==ui.row(120)[120]); // Hollow interior.
        }
        v.rpm.value=3500;ui.showDriving(99,true,v,70000);ui.updateMilOverlay(false,71000);
        for(int y=0;y<240;++y)for(int x=0;x<240;++x)assert(ui.displayPixel(x,y)==ui.row(y)[x]);
    }
    constexpr unsigned bandPixels=240*config::kStripRows;
    std::array<std::array<uint16_t,bandPixels+2>,240/config::kStripRows> storage{};
    std::array<uint16_t*,240/config::kStripRows> bands{};
    for(unsigned i=0;i<bands.size();++i){storage[i].front()=storage[i].back()=0xa55a;bands[i]=storage[i].data()+1;}
    std::array<uint16_t,count> flatPixels{};GaugeRenderer flat(flatPixels.data()),striped(bands.data());
    for(bool day:{false,true}) {
        flat.setPage(day,1);striped.setPage(day,1);auto v=values(100);
        flat.showDriving(199,true,v,100);striped.showDriving(199,true,v,100);
        flat.updateMilOverlay(true,100);striped.updateMilOverlay(true,100);
        flat.updateMilOverlay(true,1100);striped.updateMilOverlay(true,1100);
        for(int y=0;y<240;++y)for(int x=0;x<240;++x)assert(flat.displayPixel(x,y)==striped.displayPixel(x,y));
        for(const auto& band:storage)assert(band.front()==0xa55a && band.back()==0xa55a);
    }
    std::puts("PASS: driver-confirmed P/R/N/D, routed page polling/exit, fresh/missing data, driving redraws, hollow 40-75% MIL layer");
}
