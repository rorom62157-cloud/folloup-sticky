#include "ink.hpp"
#include "ui_font_data.hpp"
#include <algorithm>
#include <cmath>
namespace compagnon::device {
namespace {
uint32_t next(const std::string&s,size_t&i){auto c=static_cast<unsigned char>(s[i++]);if(c<128)return c;unsigned n=c>=0xf0?3:c>=0xe0?2:c>=0xc2?1:0;uint32_t p=c&((1u<<(6-n))-1);if(!n)return '?';while(n--){if(i==s.size()||(static_cast<unsigned char>(s[i])&0xc0)!=0x80)return '?';p=(p<<6)|(static_cast<unsigned char>(s[i++])&63);}return p;}
const fontdata::Glyph& glyph(uint32_t cp,Face f){const auto&a=fontdata::faces[static_cast<unsigned>(f)];
    if(f!=Face::Clock){unsigned lo=0,hi=a.count;while(lo<hi){unsigned m=(lo+hi)/2;if(fontdata::glyphs[a.start+m].cp<cp)lo=m+1;else hi=m;}if(lo<a.count&&fontdata::glyphs[a.start+lo].cp==cp)return fontdata::glyphs[a.start+lo];}
    else for(unsigned i=0;i<a.count;++i)if(fontdata::glyphs[a.start+i].cp==cp)return fontdata::glyphs[a.start+i];
    for(unsigned i=0;i<a.count;++i)if(fontdata::glyphs[a.start+i].cp=='?')return fontdata::glyphs[a.start+i];return fontdata::glyphs[a.start];}

}
int Ink::lineHeight(Face face)const{return fontdata::faces[static_cast<unsigned>(face)].line;}
int Ink::measure(const std::string&s,Face face)const{int width=0,line=0;for(size_t i=0;i<s.size();){auto cp=next(s,i);if(cp=='\n'){width=std::max(width,line);line=0;}else line+=glyph(cp,face).advance;}return std::max(width,line);}
void Ink::text(int x,int y,const std::string&s,Face face,bool white){int left=x;const auto&f=fontdata::faces[static_cast<unsigned>(face)];for(size_t i=0;i<s.size();){auto cp=next(s,i);if(cp=='\n'){x=left;y+=f.line;continue;}const auto&g=glyph(cp,face);for(unsigned yy=0;yy<g.h;++yy)for(unsigned xx=0;xx<g.w;++xx){const auto b=fontdata::bits[g.offset+yy*((g.w+7)/8)+xx/8];if(b&(0x80>>(xx%8)))r_.bar(x+g.x+xx,y+g.y+yy,1,1,!white);}x+=g.advance;}}
std::vector<std::string> Ink::wrap(const std::string&s,int width,Face face)const{std::vector<std::string> out;std::string line;for(size_t i=0;i<s.size();){auto start=i;auto cp=next(s,i);if(cp=='\r')continue;if(cp=='\n'){out.push_back(line);line.clear();continue;}auto part=s.substr(start,i-start);if(!line.empty()&&measure(line+part,face)>width){auto cut=line.rfind(' ');if(cut!=std::string::npos&&cut){out.push_back(line.substr(0,cut));line.erase(0,cut+1);}else{out.push_back(line);line.clear();}}if(line.empty()&&cp==' ')continue;line+=part;}out.push_back(line);return out;}
std::string Ink::fit(const std::string&s,int width,Face face)const{if(measure(s,face)<=width)return s;std::string out;for(size_t i=0;i<s.size();){auto start=i;next(s,i);auto part=s.substr(start,i-start);if(measure(out+part+"…",face)>width)break;out+=part;}return out+"…";}
void Ink::line(int x1,int y1,int x2,int y2,bool black,int stroke){int dx=std::abs(x2-x1),sx=x1<x2?1:-1,dy=-std::abs(y2-y1),sy=y1<y2?1:-1,e=dx+dy;for(;;){r_.bar(x1-stroke/2,y1-stroke/2,stroke,stroke,black);if(x1==x2&&y1==y2)break;int t=2*e;if(t>=dy){e+=dy;x1+=sx;}if(t<=dx){e+=dx;y1+=sy;}}}
void Ink::circle(int x,int y,int rad,bool black,int stroke){for(int yy=-rad;yy<=rad;++yy)for(int xx=-rad;xx<=rad;++xx){int d=xx*xx+yy*yy;if(d<=rad*rad&&d>=(rad-stroke)*(rad-stroke))r_.bar(x+xx,y+yy,1,1,black);}}
void Ink::box(int x,int y,int w,int h,int rad,bool fill,bool black){rad=std::max(0,std::min(rad,std::min(w,h)/2));for(int yy=0;yy<h;++yy)for(int xx=0;xx<w;++xx){int cx=std::max(rad-xx,std::max(0,xx-(w-rad-1))),cy=std::max(rad-yy,std::max(0,yy-(h-rad-1)));bool inside=cx*cx+cy*cy<=rad*rad;if(inside&&(fill||xx<2||yy<2||xx>=w-2||yy>=h-2||cx||cy)) {if(fill||cx*cx+cy*cy>=(rad-2)*(rad-2)||xx<2||yy<2||xx>=w-2||yy>=h-2)r_.bar(x+xx,y+yy,1,1,black);}}}
void Ink::icon(Icon type,int x,int y,int size,bool white){const bool b=!white;auto L=[&](int a,int c,int d,int e){line(x+a*size/32,y+c*size/32,x+d*size/32,y+e*size/32,b,2);};auto B=[&](int a,int c,int w,int h,int rad=2){box(x+a*size/32,y+c*size/32,w*size/32,h*size/32,rad,false,b);};switch(type){
case Icon::Today:case Icon::Calendar:B(3,5,26,25);L(3,12,29,12);L(10,2,10,8);L(22,2,22,8);L(10,18,14,18);L(19,18,23,18);L(10,24,14,24);break;
case Icon::Grid:for(int j=0;j<2;++j)for(int i=0;i<2;++i)B(3+i*15,3+j*15,11,11);break;
case Icon::Task:B(3,5,26,24);L(9,18,14,23);L(14,23,24,12);break;
case Icon::Folder:L(2,9,12,9);L(12,9,16,13);L(16,13,30,13);L(30,13,27,28);L(27,28,3,28);L(3,28,2,9);break;
case Icon::Note:B(6,2,21,28);L(11,11,22,11);L(11,17,22,17);L(11,23,18,23);break;
case Icon::Pin:L(10,4,24,4);L(12,4,12,14);L(22,4,22,14);L(12,14,7,20);L(7,20,27,20);L(27,20,22,14);L(17,20,17,30);break;
case Icon::Wifi:for(int r:{6,12,18})for(int a=210;a<=330;++a){double t=a*3.14159265/180;int xx=x+size/2+std::cos(t)*r*size/32,yy=y+24*size/32+std::sin(t)*r*size/32;r_.bar(xx,yy,2,2,b);}circle(x+size/2,y+24*size/32,2,b,2);break;
case Icon::Gear:circle(x+size/2,y+size/2,size/3,b,2);circle(x+size/2,y+size/2,size/8,b,2);for(int a=0;a<360;a+=45){double t=a*3.14159265/180;L(16+int(10*std::cos(t)),16+int(10*std::sin(t)),16+int(15*std::cos(t)),16+int(15*std::sin(t)));}break;
case Icon::Audio:case Icon::Mic:B(11,2,10,18,4);L(6,12,6,19);L(6,19,11,24);L(11,24,21,24);L(21,24,26,19);L(26,19,26,12);L(16,24,16,30);L(10,30,22,30);break;
case Icon::Play:L(9,4,27,16);L(27,16,9,28);L(9,28,9,4);break;
case Icon::Stop:B(7,7,18,18);break;
case Icon::Battery:B(2,9,25,15);L(29,13,29,20);break;
case Icon::Back:L(20,5,9,16);L(9,16,20,27);break;
case Icon::Plus:L(16,5,16,27);L(5,16,27,16);break;
case Icon::Check:L(5,17,13,25);L(13,25,28,7);break;
case Icon::Lock:B(5,14,23,16);L(10,14,10,7);L(10,7,15,3);L(15,3,21,7);L(21,7,21,14);break;
case Icon::Moon:{int r=size/2-3;for(int yy=-r;yy<=r;++yy)for(int xx=-r;xx<=r;++xx)if(xx*xx+yy*yy<=r*r&&(xx-r/2)*(xx-r/2)+(yy+r/5)*(yy+r/5)>(r-1)*(r-1))r_.bar(x+size/2+xx,y+size/2+yy,1,1,b);break;}
case Icon::Sun:circle(x+size/2,y+size/2,size/4,b,2);for(int a=0;a<360;a+=45){double t=a*3.14159265/180;L(16+int(12*std::cos(t)),16+int(12*std::sin(t)),16+int(15*std::cos(t)),16+int(15*std::sin(t)));}break;
default:circle(x+size/2,y+size/2,size/2-2,b,2);text(x+size/3,y, "?",Face::Body,white);break;
}}
}
