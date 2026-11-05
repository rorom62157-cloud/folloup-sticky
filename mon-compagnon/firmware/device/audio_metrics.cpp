#include "audio_metrics.hpp"
#include "audio_vendor/wav_metadata.hpp"
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace compagnon::device {
bool audioDiagnostics(const std::string& path,std::string& summary){
    if(!audio_detail::safeAudioPath(path)){summary="AUDIO_METRICS FAIL invalid path";return false;}
    FILE* file=fopen(path.c_str(),"rb");uint32_t bytes=0;
    if(!file||!audio_detail::wavInfo(file,bytes)||!bytes||fseek(file,44,SEEK_SET)!=0){if(file)fclose(file);summary="AUDIO_METRICS FAIL invalid WAV";return false;}
    std::array<uint8_t,2048> buffer{};uint64_t square=0,count=0,zero=0,clipped=0;int64_t sum=0;int lo=32767,hi=-32768;
    while(bytes){const auto chunk=std::min<size_t>(bytes,buffer.size());if(fread(buffer.data(),1,chunk,file)!=chunk){fclose(file);summary="AUDIO_METRICS FAIL read";return false;}
        for(size_t i=0;i<chunk;i+=2){int value=static_cast<int16_t>(uint16_t(buffer[i])|(uint16_t(buffer[i+1])<<8));sum+=value;square+=int64_t(value)*value;lo=std::min(lo,value);hi=std::max(hi,value);zero+=value==0;clipped+=value>=32760||value<=-32760;++count;}bytes-=chunk;
    }
    if(fclose(file)!=0){summary="AUDIO_METRICS FAIL close";return false;}
    const double mean=double(sum)/count,rms=std::sqrt(double(square)/count),sd=std::sqrt(std::max(0.0,double(square)/count-mean*mean));
    char out[240];snprintf(out,sizeof out,"AUDIO_METRICS samples=%llu duration_ms=%llu min=%d max=%d rms=%.2f stddev=%.2f zeros=%.4f clipped=%.4f",static_cast<unsigned long long>(count),static_cast<unsigned long long>(count/16),lo,hi,rms,sd,double(zero)/count,double(clipped)/count);summary=out;return true;
}
}
