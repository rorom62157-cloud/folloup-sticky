#pragma once
#include "raster.hpp"
namespace compagnon::device {
enum class Face { Small, Body, Heading, Title, Clock, Large, Display, Hero, HeadingLarge, TitleLarge, Strong, Lead, Reading };
enum class Icon { Today, Grid, Note, Pin, Task, Folder, Audio, Calendar, Wifi, Gear, Help, Back, Plus, Play, Stop, Mic, Battery, Check, Lock, Sun, Moon };
class Ink {
public:
    explicit Ink(Raster& raster):r_(raster){}
    int lineHeight(Face face=Face::Body)const;
    int measure(const std::string& text,Face face=Face::Body)const;
    void text(int x,int y,const std::string& value,Face face=Face::Body,bool white=false);
    std::vector<std::string> wrap(const std::string& text,int width,Face face=Face::Body)const;
    std::string fit(const std::string& text,int width,Face face=Face::Body)const;
    void line(int x1,int y1,int x2,int y2,bool black=true,int stroke=2);
    void box(int x,int y,int w,int h,int radius=10,bool fill=false,bool black=true);
    void circle(int x,int y,int radius,bool black=true,int stroke=2);
    void icon(Icon type,int x,int y,int size=28,bool white=false);
private: Raster&r_;
};
}
