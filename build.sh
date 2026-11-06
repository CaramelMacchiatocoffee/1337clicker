#!/bin/sh
# Run inside the ubuntu proot, from this folder.
x86_64-w64-mingw32-g++-win32 -std=c++17 -O2 -municode -mwindows \
  -static-libgcc -static-libstdc++ \
  1337clicker.cpp -o 1337clicker-rebuilt.exe \
  -lgdi32 -luser32 -lwinmm
