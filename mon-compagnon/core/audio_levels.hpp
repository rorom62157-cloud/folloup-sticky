#pragma once
#include <algorithm>
#include <cmath>
namespace compagnon {
// ES8311 DAC0x32: 0xBF=0dB, each register step=0.5dB. A UI percentage is
// linear amplitude, never a raw register percentage. Zero uses codec mute.
inline int audioVolumeRegister(int percent){
    if(percent<0||percent>100)return -1;
    if(percent==0)return 0;
    return std::clamp(static_cast<int>(std::lround(191.0+40.0*std::log10(percent/100.0))),0,191);
}
}
