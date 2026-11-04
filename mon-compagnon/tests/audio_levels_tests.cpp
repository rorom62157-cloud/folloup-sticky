#include "audio_levels.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
int main(){using compagnon::audioVolumeRegister;assert(audioVolumeRegister(-1)==-1&&audioVolumeRegister(101)==-1);assert(audioVolumeRegister(0)==0);assert(audioVolumeRegister(100)==191);assert(audioVolumeRegister(65)==184);assert(audioVolumeRegister(50)==179);int previous=0;for(int p=1;p<=100;++p){int reg=audioVolumeRegister(p);assert(reg>=previous&&reg<=191);double amplitude=std::pow(10.,(reg-191)/40.);assert(std::abs(amplitude-p/100.)<=0.03);previous=reg;}std::puts("Audio volume amplitude mapping tests passed");}
