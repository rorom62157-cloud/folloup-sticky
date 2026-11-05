// SPDX-License-Identifier: GPL-3.0-only
// Host-only test; not part of device firmware.
#include "audio_vendor/wav_metadata.hpp"
#include <cassert>
#include <unistd.h>
using namespace compagnon;
using namespace compagnon::device::audio_detail;
int main() {
  assert(safeAudioPath("/sdcard/compagnon-audio/rec-0123456789abcdef0123456789abcdef.wav"));
  assert(!safeAudioPath("/sdcard/compagnon-audio/../rec-0123456789abcdef0123456789abcdef.wav"));
  assert(!safeAudioPath("/sdcard/compagnon-audio/rec-0123456789abcdef0123456789abcdef.wav.part"));
  assert(!safeAudioPath("/sdcard/compagnon-audio/rec-0123456789abcdef0123456789abcdeg.wav"));
  char dir[]="/tmp/compagnon-metadata-XXXXXX"; assert(mkdtemp(dir));
  std::string p=std::string(dir)+"/test.wav";
  WavWriter w; assert(w.begin(p)==WavResult::Ok);
  unsigned char pcm[3200]{};assert(w.append(pcm,sizeof(pcm))==WavResult::Ok);assert(w.finish()==WavResult::Ok);
  uint32_t bytes=0;FILE* f=fopen(p.c_str(),"rb");assert(f&&wavInfo(f,bytes)&&bytes==3200);fclose(f);
  assert(truncate(p.c_str(),3243)==0);f=fopen(p.c_str(),"rb");assert(f&&!wavInfo(f,bytes));fclose(f);
  assert(truncate(p.c_str(),3244)==0);
  f=fopen(p.c_str(),"rb+");assert(f);fseek(f,24,SEEK_SET);fputc(0,f);fclose(f);
  f=fopen(p.c_str(),"rb");assert(f&&!wavInfo(f,bytes));fclose(f);unlink(p.c_str());rmdir(dir);
}
