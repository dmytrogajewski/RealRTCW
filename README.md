# RealRTCW

RealRTCW is a community single-player overhaul project for Return to Castle Wolfenstein based on the iortcw and rtcw-sp source code.

Features:
* All iortcw features including proper widescreen support
* Steam integration support using Steamshim by Ryan C. Gordon
* Support for 99% of community made maps
* Expanded .weap files system from Enemy Territory
* Reworked difficulty levels
* Automatic AI attributes system
* Greatly expanded arsenal of weapons
* Overhauled recoil system
* Atmospheric effects support
* Foliage tech support
* Subtitles support
* New inventory items
* Increased engine limits
* Improved controller support
* Extended scripting functionality
* Custom BSPC and BSPCUI included in sdk folder
* A lot of bug fixes and QOL improvements

## Building from Source

### Requirements

- [Zig](https://ziglang.org/download/) (0.13.0 or later)
- SDL2 development libraries
- OpenGL development libraries
- llama.cpp (pre-built in `code/llama.cpp/build/`)

### Linux

Install dependencies (Fedora):
```bash
sudo dnf install SDL2-devel mesa-libGL-devel
```

Install dependencies (Ubuntu/Debian):
```bash
sudo apt install libsdl2-dev libgl1-mesa-dev
```

Build:
```bash
zig build
```

Output files will be in `zig-out/`:
- `bin/iowolfsp` - Main game executable
- `lib/libcgame.sp.x86_64.so` - Client game module
- `lib/libqagame.sp.x86_64.so` - Server game module  
- `lib/libui.sp.x86_64.so` - UI module
- `lib/librenderer_sp_opengl1_x86_64.so` - OpenGL 1.x renderer
- `lib/librenderer_sp_rend2_x86_64.so` - OpenGL 2+ renderer

### Build Options

```bash
# Build with specific options
zig build -Dclient=true -Dserver=false -Dgame-so=true

# Release build
zig build -Doptimize=ReleaseFast

# See all options
zig build --help
```

### TTS Setup (Optional)

To enable text-to-speech for AI dialogue:
```bash
./setup-tts.sh
```

This downloads the Piper TTS engine and German voice model.

### Steam Installation

To build and install to Steam (Flatpak) directory:
```bash
zig build install-steam
```

This builds the game and copies binaries, game assets, and original RTCW files to the Steam RealRTCW directory.

The original id software readme that accompanied the RTCW source release is named README.txt and is contained within the source tree of both MP and SP games.

Available on ModDB:
https://www.moddb.com/mods/realrtcw-realism-mod

And on Steam:
https://store.steampowered.com/app/1379630/RealRTCW/

Arch Linux Repo by M0Rf30:
https://aur.archlinux.org/packages/realrtcw/

Opensuse package:
https://build.opensuse.org/package/show/games/realrtcw
