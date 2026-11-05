#include "../firmware/device/ink.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <iostream>

using namespace compagnon::device;
namespace {
using Buffer = std::array<std::uint8_t, Raster::bytes + 64>;
void guarded(const Buffer& b) {
    assert(std::all_of(b.begin(), b.begin()+32, [](auto x){return x==0xA5;}));
    assert(std::all_of(b.end()-32, b.end(), [](auto x){return x==0xA5;}));
}
void fonts() {
    Buffer b; b.fill(0xA5); Raster raster(b.data()+32); Ink ink(raster);
    for(auto face : {Face::Small,Face::Body,Face::Heading,Face::Title}) {
        assert(ink.measure("iii",face)<ink.measure("WWW",face));
        assert(ink.measure("bonjour",face)!=ink.measure("BONJOUR",face));
        const std::string accented="été déjà Noël, cœur";
        assert(ink.fit(accented,ink.measure(accented,face),face)==accented);
        auto lines=ink.wrap(accented,ink.measure(accented,face),face);
        assert(lines==std::vector<std::string>{accented}); // Case and UTF-8 survive.
        auto fitted=ink.fit(accented,ink.measure("été…",face),face);
        assert(fitted=="été…"); assert(ink.measure(fitted,face)<=ink.measure("été…",face));
        assert((ink.wrap("éééé",ink.measure("éé",face),face)==std::vector<std::string>{"éé","éé"}));
        assert((ink.wrap("un deux trois",std::max(ink.measure("deux",face),ink.measure("trois",face)),face)==std::vector<std::string>{"un","deux","trois"}));
        assert((ink.wrap("a\n\nb\n",1000,face)==std::vector<std::string>{"a","","b",""}));
        assert((ink.wrap("",100,face)==std::vector<std::string>{""}));
        raster.clear(); ink.text(20,20,"a",face);
        const auto lower=b; raster.clear(); ink.text(20,20,"A",face);assert(b!=lower);
        raster.clear(); ink.text(20,20,"e",face);
        const auto plain=b; raster.clear(); ink.text(20,20,"é",face);assert(b!=plain);
        guarded(b);
    }
    raster.clear();ink.text(20,20,"12:45",Face::Clock);assert(ink.measure("12:45",Face::Clock)>0);
    assert(std::any_of(raster.data(),raster.data()+Raster::bytes,[](auto x){return x!=0xFF;}));guarded(b);
}
void clipsAndIcons() {
    for(auto orientation : {Orientation::Landscape,Orientation::Portrait,Orientation::PortraitInverted}) {
        Buffer b;b.fill(0xA5); Raster raster(b.data()+32,orientation);Ink ink(raster);
        const int w=raster.logicalWidth(),h=raster.logicalHeight();
        for(auto face : {Face::Small,Face::Body,Face::Heading,Face::Title,Face::Clock}) {
            raster.clear();ink.text(-25,-20,"Été à Noël",face);ink.text(w-4,h-4,"Été",face);
            ink.text(w+20,h+20,"hors cadre",face);guarded(b);
        }
        for(auto icon : {Icon::Today,Icon::Grid,Icon::Note,Icon::Pin,Icon::Task,Icon::Folder,Icon::Audio,Icon::Calendar,Icon::Wifi,Icon::Gear,Icon::Help,Icon::Back,Icon::Plus,Icon::Play,Icon::Stop,Icon::Mic,Icon::Battery,Icon::Check,Icon::Lock,Icon::Sun,Icon::Moon}) {
            raster.clear();ink.icon(icon,40,40,32);
            assert(std::any_of(raster.data(),raster.data()+Raster::bytes,[](auto x){return x!=0xFF;}));
            for(auto p : {std::array<int,2>{-16,-16}, {w-4,0},{0,h-4},{w-4,h-4}}) {
                ink.icon(icon,p[0],p[1],32);ink.icon(icon,p[0],p[1],64,true);guarded(b);
            }
        }
        ink.line(-10,-10,w+10,h+10);ink.circle(0,0,32);ink.box(w-10,h-10,40,40,8,true);guarded(b);
        Raster null(nullptr,orientation);Ink empty(null);empty.text(0,0,"é");empty.icon(Icon::Mic,0,0);
    }
}
}
int main(){fonts();clipsAndIcons();std::cout<<"Ink bitmap-font and clipping tests passed\n";}
