"""Embed exact public client assets into a generated compile-stage header."""
import pathlib
import sys

source, output = map(pathlib.Path, sys.argv[1:])
files = [("index.html", "text/html; charset=utf-8"),
         ("app.js", "application/javascript; charset=utf-8"),
         ("style.css", "text/css; charset=utf-8")]
parts = ["#pragma once\n#include <cstddef>\n"]
for i, (name, _) in enumerate(files):
    data = (source / name).read_bytes()
    parts.append(f"static const char web_asset_{i}[] = {{\n")
    for offset in range(0, len(data), 24):
        parts.append(",".join(str(b) for b in data[offset:offset + 24]) + ",\n")
    parts.append("0};\n")
parts.append("struct WebAsset { const char* path; const char* type; const char* data; std::size_t size; };\n")
parts.append("static const WebAsset web_assets[] = {\n")
for i, (name, mime) in enumerate(files):
    parts.append(f'{{"/{name}","{mime}",web_asset_{i},sizeof(web_asset_{i})-1}},\n')
parts.append("};\n")
output.write_text("".join(parts))
