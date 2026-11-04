#!/usr/bin/env python3
"""Generate Compagnon Sans Bitmap, an OFL bitmap derivative. Pillow required.
Sources: PDF.js standard_fonts/LiberationSans-{Regular,Bold}.ttf (SIL OFL1.1).
Pass --font-root for the directory with these unmodified source fonts.
"""
from pathlib import Path
import argparse
from PIL import Image, ImageFont
p=argparse.ArgumentParser();p.add_argument('--font-root',type=Path,required=True);args=p.parse_args()
chars=sorted(set(range(32,127))|set(range(160,256))|{0x152,0x153,0x2018,0x2019,0x2013,0x2014,0x2026})
rows=[];data=[];fonts=[]
for idx,(name,size,bold) in enumerate([('Small',18,False),('Body',22,False),('Heading',28,True),('Title',36,True),('Clock',64,True),('Large',48,False),('Display',72,False),('Hero',96,True),('HeadingLarge',64,True),('TitleLarge',48,True),('Strong',22,True),('Lead',36,False),('Reading',28,False)]):
 font=ImageFont.truetype(str(args.font_root/('LiberationSans-Bold.ttf' if bold else 'LiberationSans-Regular.ttf')),size)
 start=len(rows)
 for cp in (chars if name!='Clock' else list(map(ord,' 0123456789:-?'))):
  s=chr(cp);bbox=font.getbbox(s,anchor='lt');mask=font.getmask(s,mode='L');w,h=mask.size
  # getmask has the same top-aligned bbox but baseline position is in getbbox('ls').
  bb=font.getbbox(s,anchor='ls'); advance=round(font.getlength(s));offset=len(data); stride=(w+7)//8
  for y in range(h):
   for byte in range(stride):
    v=0
    for bit in range(8):
     x=byte*8+bit
     if x<w and mask[y*w+x]>=128:v|=0x80>>bit
    data.append(v)
  # Text y is font line top. A stable ascent keeps accents and lowercase aligned.
  ascent=font.getmetrics()[0]
  rows.append((cp,advance,w,h,bb[0],ascent+bb[1],offset))
 fonts.append((start,len(rows)-start,size+6))
out=Path(__file__).resolve().parents[1]/'firmware/device/ui_font_data.hpp'
text='// Generated Compagnon Sans Bitmap. Derived from Liberation Sans, SIL OFL1.1.\n// Copyright Google2010/RedHat2012. See font_vendor/OFL-Liberation.txt.\n#pragma once\n#include <cstdint>\nnamespace compagnon::device::fontdata {\nstruct Glyph {uint32_t cp; uint16_t advance,w,h; int16_t x,y; uint32_t offset;};\nstruct Face {uint16_t start,count,line;};\ninline constexpr Face faces[]={'+','.join('{%d,%d,%d}'%v for v in fonts)+'};\ninline constexpr Glyph glyphs[]={\n'+''.join('{'+','.join(map(str,r))+'},\n' for r in rows)+'};\ninline constexpr uint8_t bits[]={\n'
text+='\n'.join(','.join('0x%02x'%b for b in data[i:i+32])+',' for i in range(0,len(data),32))+'\n};\n}\n'
out.write_text(text);print('Generated',len(rows),'glyphs;',len(data),'bitmap bytes')
