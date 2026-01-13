#!/usr/bin/env python3
"""Generate C++ header from PNG file for embedding in Godot."""

import sys

def png_to_header(png_path, header_path, array_name):
    with open(png_path, 'rb') as f:
        data = f.read()
    
    with open(header_path, 'w') as f:
        f.write("// THIS FILE IS GENERATED. DO NOT EDIT!\n")
        f.write("// DORO Logo image data\n")
        f.write("#pragma once\n\n")
        f.write(f"inline constexpr unsigned int {array_name}_size = {len(data)};\n")
        f.write(f"inline constexpr unsigned char {array_name}[] = {{\n\t")
        
        for i, byte in enumerate(data):
            f.write(f"0x{byte:02x}")
            if i < len(data) - 1:
                f.write(", ")
                if (i + 1) % 16 == 0:
                    f.write("\n\t")
        
        f.write("\n};\n")
    
    print(f"Generated {header_path} ({len(data)} bytes)")

if __name__ == "__main__":
    png_to_header(
        "editor/resources/doro/doro_logo.png",
        "editor/project_manager/doro_logo_data.gen.h",
        "doro_logo_png_data"
    )
