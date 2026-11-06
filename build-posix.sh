#!/bin/sh
# Run inside the ubuntu proot, from this folder.
# The posix compiler gives the same clock and sleep calls as the original exe.
x86_64-w64-mingw32-g++-posix -std=c++17 -O2 -municode -mwindows -static \
  1337clicker.cpp -o 1337clicker-posix.exe \
  -lgdi32 -luser32 -lwinmm
