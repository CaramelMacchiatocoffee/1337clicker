# 1337clicker

C++ source for `1337clicker.exe`, a left clicker for Lunar Client on Minecraft 1.7/1.8, rebuilt from the exe.

从 `1337clicker.exe` 还原的 C++ 源码，Lunar 1.7/1.8 连点器。

The original exe is not in this repo. It is an unpacked x64 MinGW build that kept its symbols, so function and global names come from the exe. Names of inlined helpers are new.

Original sha256: `46b4055beda0c47cf105d64ea0006638935e87a67e96eece53e9bd787869d346`

## Use

- Hold the real left mouse button in Lunar.
- F6 arms the clicker.
- Hold F8 for 500 ms to hide the window.
- Default 20 CPS, range 1 to 50. Left shift with inventory mode forces 40.

## Build

Run from this folder with MinGW-w64:

```sh
sh build-posix.sh
```

`build-posix.sh` uses the posix compiler and gives the same clock and sleep calls as the original. `build.sh` uses the win32 compiler, which does not.

## Status

Compared against the original through disassembly and Ghidra output. The only difference found in the logic is that the original reads `g_captureHotkey` twice in `PaintInto`, where this source reads it once.

The rebuilt exe opens and draws correctly under Wine. Clicking in Lunar is untested.
