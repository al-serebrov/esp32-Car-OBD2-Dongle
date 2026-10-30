#include "GaugeRenderer.h"
#include "DrivingAssets.h"
#include "SpeedFont.h"
#include "obd/TransmissionPoll.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
namespace {
using namespace drivingassets;
uint16_t rgb(int r,int g,int b){return uint16_t(((r>>3)<<11)|((g>>2)<<5)|(b>>3));}
uint16_t mix(uint16_t fg,uint16_t bg,unsigned alpha,unsigned max) {
    const auto r=(((fg>>11)&31)*alpha+((bg>>11)&31)*(max-alpha)+max/2)/max;
    const auto g=(((fg>>5)&63)*alpha+((bg>>5)&63)*(max-alpha)+max/2)/max;
    const auto b=((fg&31)*alpha+(bg&31)*(max-alpha)+max/2)/max;
    return uint16_t((r<<11)|(g<<5)|b);
}
unsigned alphaAt(const Mask& mask,int x,int y) {
    if(x<0 || y<0 || x>=mask.w || y>=mask.h)return 0;
    const auto i=y*mask.w+x;const auto data=mask.data[i/2];return i%2?data&15:data>>4;
}
const Glyph* find(const Font& f,char ch){for(unsigned i=0;i<f.count;++i)if(f.glyphs[i].ch==ch)return f.glyphs+i;return nullptr;}
}
void GaugeRenderer::restoreDriving(int x,int y,int w,int h) {
    const auto* base=day_?kDayBase:kNightBase;
    for(int yy=y;yy<y+h;++yy)for(int xx=x;xx<x+w;++xx)
        if(xx>=0 && yy>=0 && xx<240 && yy<240)pixel(xx,yy,base[yy*240+xx]);
}
void GaugeRenderer::beginDriving() {
    page_=1;live_=false;restoreDriving(0,0,240,240);
    for(auto& label:drivingLabels_)label[0]=0;
    drivingSegments_[0]=drivingSegments_[1]=-1;
    dirty_={0,0,240,240};statusIndicator();
}
void GaugeRenderer::drawMask(const Mask& mask,int x,int y,uint16_t color) {
    const auto bg=day_?uint16_t(0xffff):uint16_t(0);
    uint16_t colors[16];for(unsigned alpha=0;alpha<16;++alpha)colors[alpha]=mix(color,bg,alpha,15);
    for(int yy=0;yy<mask.h;++yy)for(int xx=0;xx<mask.w;++xx) {
        const auto alpha=alphaAt(mask,xx,yy);
        if(alpha)pixel(x+xx,y+yy,colors[alpha]);
    }
}
int GaugeRenderer::smoothText(const Font& f,const char* value,int cx,int y,uint16_t color) {
    int advance=0;
    for(const char* p=value;*p;++p)if(auto g=find(f,*p))advance+=g->advance;
    const int width=(advance+128)/256;int cursor=cx*256-advance/2;
    for(const char* p=value;*p;++p)if(auto g=find(f,*p)){drawMask(g->mask,(cursor+128)/256,y,color);cursor+=g->advance;}
    return width;
}
void GaugeRenderer::drivingArc(bool right,int segments) {
    segments=std::max(0,std::min(segments,30));
    const int side=right?1:0,previous=drivingSegments_[side];
    if(previous==segments)return;
    const auto track=day_?rgb(210,215,220):rgb(30,42,52);
    const auto active=right?(day_?rgb(0,90,190):rgb(77,161,255)):(day_?rgb(0,115,105):rgb(44,211,194));
    for(int n=0;n<30;++n) {
        const auto& mask=kArcs[side*30+n];
        drawMask(mask,mask.x,mask.y,n<segments?active:track);
    }
    drivingSegments_[side]=segments;
}
void GaugeRenderer::showDriving(int kmh,bool live,const DrivingReadings& v,uint64_t now) {
    if(page_!=1)return;
    setLive(live);
    const auto fg=day_?uint16_t(0):uint16_t(0xffff),muted=day_?rgb(70,80,90):rgb(128,150,163);
    const auto loadColor=day_?rgb(0,115,105):rgb(44,211,194),throttleColor=day_?rgb(0,90,190):rgb(77,161,255);
    char labels[7][16]={};
    std::snprintf(labels[0],16,"%d",std::max(0,std::min(kmh,255)));
    const bool load=isFresh(v.load,now,5000),throttle=isFresh(v.throttle,now,5000);
    if(load)std::snprintf(labels[1],16,"%d%%",v.load.value);else std::strcpy(labels[1],"--");
    if(throttle)std::snprintf(labels[2],16,"%d%%",v.throttle.value);else std::strcpy(labels[2],"--");
    auto gear=v.transmission;gear.valid=gear.valid && now>=gear.receivedMs && now-gear.receivedMs<5000;
    char gearText[4];obd::transmissionLabel(gear,gearText);std::strcpy(labels[3],gearText);
    if(isFresh(v.rpm,now,5000))std::snprintf(labels[4],16,"%d",v.rpm.value);else std::strcpy(labels[4],"--");
    if(isFresh(v.coolant,now,config::kCoolantStaleMs))std::snprintf(labels[5],16,"%d",v.coolant.value);else std::strcpy(labels[5],"--");
    if(isFresh(v.voltage,now,config::kVoltageStaleMs)) {
        const int tenths=(v.voltage.value+50)/100;
        std::snprintf(labels[6],16,"%d.%d",tenths/10,tenths%10);
    }
    else std::strcpy(labels[6],"--");
    drivingArc(false,load?(std::max(0,std::min(v.load.value,100))*30+99)/100:0);
    drivingArc(true,throttle?(std::max(0,std::min(v.throttle.value,100))*30+99)/100:0);
    for(unsigned i=0;i<7;++i) {
        if(!std::strcmp(labels[i],drivingLabels_[i]))continue;
        std::strcpy(drivingLabels_[i],labels[i]);
        switch(i) {
            case 0: {
                restoreDriving(63,62,114,79);const auto& font=speedfont::kFonts[3];
                const int count=std::strlen(labels[0]),inset=font.inkLeft[labels[0][0]-'0'];
                const int left=(240-(count*font.width-inset))/2-inset;
                for(int n=0;n<count;++n)digit(labels[0][n]-'0',left+n*font.width,63,3);
                break;
            }
            case 1:restoreDriving(20,98,49,20);smoothText(kFont19,labels[i],44,100,loadColor);break;
            case 2:restoreDriving(172,98,48,20);smoothText(kFont19,labels[i],196,100,throttleColor);break;
            case 3: {
                restoreDriving(84,159,72,37);const Mask* mask=&kGearUnknown;
                if(!std::strcmp(labels[i],"P"))mask=&kGearP;
                else if(!std::strcmp(labels[i],"R"))mask=&kGearR;
                else if(!std::strcmp(labels[i],"N"))mask=&kGearN;
                else if(labels[i][0]=='D') {
                    const Mask* gears[]={&kGearD,&kGearD1,&kGearD2,&kGearD3,&kGearD4,&kGearD5,&kGearD6};
                    mask=gears[labels[i][1]?labels[i][1]-'0':0];
                }
                drawMask(*mask,mask->x,mask->y,fg);break;
            }
            case 4: {
                restoreDriving(69,200,118,22);const int width=smoothText(kFont24,labels[i],120,202,fg);
                drawMask(kRpmUnit,120+width/2+4,219-kRpmUnit.h,muted);break;
            }
            case 5: {
                restoreDriving(20,178,60,21);const int width=smoothText(kFont20,labels[i],45,179,fg);
                drawMask(kC,45+width/2+3,184,muted);break;
            }
            case 6: {
                restoreDriving(158,178,66,21);const int width=smoothText(kFont20,labels[i],190,179,fg);
                drawMask(kV,190+width/2+1,184,muted);break;
            }
        }
    }
}
void GaugeRenderer::updateMilOverlay(bool active,uint64_t now) {
    if(active && !milOverlay_)milStartMs_=now;
    unsigned alpha=0;
    if(active) {
        const auto elapsed=now>=milStartMs_?(now-milStartMs_)%2000:0;
        alpha=unsigned(std::lround(575-175*std::cos(elapsed*6.283185307179586/2000)));
    }
    if(active==milOverlay_ && alpha==milAlpha_)return;
    milOverlay_=active;milAlpha_=alpha;
    for(unsigned i=0;i<16;++i)milWeights_[i]=static_cast<uint16_t>((i*alpha*256+7500)/15000);
    const auto& m=kMilOutline;
    dirty_.x0=std::min(dirty_.x0,m.x);dirty_.y0=std::min(dirty_.y0,m.y);
    dirty_.x1=std::max(dirty_.x1,m.x+m.w);dirty_.y1=std::max(dirty_.y1,m.y+m.h);
}
uint16_t GaugeRenderer::displayPixel(int x,int y) const {
    const auto base=row(y)[x];if(!milOverlay_)return base;
    const auto& m=kMilOutline;const auto alpha=alphaAt(m,x-m.x,y-m.y);
    if(!alpha)return base;
    const auto color=day_?rgb(160,90,0):rgb(255,185,35);
    const unsigned weight=milWeights_[alpha],inverse=256-weight;
    const unsigned r=(((color>>11)&31)*weight+((base>>11)&31)*inverse+128)>>8;
    const unsigned g=(((color>>5)&63)*weight+((base>>5)&63)*inverse+128)>>8;
    const unsigned b=((color&31)*weight+(base&31)*inverse+128)>>8;
    return uint16_t((r<<11)|(g<<5)|b);
}
