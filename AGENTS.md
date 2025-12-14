# RealRTCW Development Guide

## Project Overview

RealRTCW is a modernized fork of Return to Castle Wolfenstein single-player, built with Zig build system. It includes enhanced graphics, AI improvements with LLM integration, and various gameplay enhancements.

## Build System

The project uses Zig as the build system, compiling C/C++ code.

### Build Commands

```bash
# Standard build (release)
zig build

# Debug build with full symbols
zig build -Doptimize=Debug

# Initial setup: extract game assets from Steam (run once)
zig build copy-assets

# Lock assets to prevent accidental overwrites
zig build lock

# Unlock assets (required before copy-assets if locked)
zig build unlock

# Deploy binaries only (preserves your asset modifications)
zig build deploy

# Deploy and run the game
zig build run

# Install to Steam directory
zig build install-steam
```

### Build Options

```bash
zig build -Dclient=true       # Build client (default: true)
zig build -Dserver=true       # Build dedicated server (default: false)
zig build -Drenderer-opengl1=true  # Build OpenGL 1.x renderer
zig build -Drenderer-rend2=true    # Build OpenGL 2+ renderer (rend2)
zig build -Dopenal=true       # Enable OpenAL audio
zig build -Dfreetype=true     # Enable FreeType fonts
```

## Directory Structure

```
RealRTCW/
├── build.zig                 # Zig build configuration
├── code/
│   ├── client/               # Client-side code
│   ├── server/               # Server code
│   ├── game/                 # Game logic (qagame module)
│   ├── cgame/                # Client game module
│   ├── ui/                   # UI module
│   ├── qcommon/              # Common code (filesystem, etc.)
│   ├── renderer/             # OpenGL 1.x renderer
│   ├── rend2/                # OpenGL 2+ renderer with GLSL
│   ├── botlib/               # Bot/AI library
│   └── llama.cpp/            # LLM integration
├── main/                     # Local game assets and configs
├── build/release-linux-x86_64/  # Build output with extracted assets
│   ├── iowolfsp.x86_64       # Main executable
│   ├── renderer_sp_*.so      # Renderer libraries
│   └── main/                 # Extracted game assets (12,000+ files)
└── zig-out/                  # Zig build artifacts
```

## Development Workflow

### Initial Setup (once)

```bash
# 1. Build the project
zig build

# 2. Extract game assets from Steam installations
zig build copy-assets

# 3. Lock assets to protect your modifications
zig build lock

# 4. Run the game to verify
zig build run
```

### Code-Edit-Test Cycle

```bash
# 1. Make code changes

# 2. Build and run (deploys binaries, preserves assets)
zig build run

# Or just deploy without running:
zig build deploy
```

### Modifying Game Assets

Game assets are extracted as loose files in `build/release-linux-x86_64/main/`. You can edit them directly:

- **Scripts**: `main/scripts/*.shader` - Shader definitions
- **AI Scripts**: `main/maps/*.script` - Level AI scripts
- **Configs**: `main/*.cfg` - Game configuration

Changes take effect on map reload or game restart.

## Debugging

### GDB Debugging (Batch Mode)

```bash
# Build with debug symbols
zig build -Doptimize=Debug
zig build copy-assets

# Run with GDB to get backtrace on crash
cd build/release-linux-x86_64
gdb -batch -ex "run +set fs_basepath . +set fs_homepath ." \
    -ex "bt full" ./iowolfsp.x86_64

# With breakpoint
gdb -batch \
    -ex "break G_InitGame" \
    -ex "run +set fs_basepath ." \
    -ex "bt" \
    -ex "continue" \
    ./iowolfsp.x86_64

# Analyze core dump
gdb -batch -ex "bt full" ./iowolfsp.x86_64 core
```

### GDB Command File

Create `/tmp/debug.gdb`:
```gdb
set pagination off
set logging on
break G_Error
run +set fs_basepath . +set developer 1
bt full
quit
```

Run: `gdb -x /tmp/debug.gdb ./iowolfsp.x86_64`

### In-Game Debug Commands

```
developer 1          # Enable developer messages
fs_debug 1           # Show file loading info
cg_debuganim 1       # Debug animations
bot_debug 1          # Debug AI/bots
devmap <mapname>     # Load map in dev mode
map_restart          # Restart current map
vid_restart          # Restart video/renderer
```

### Key Source Files for Debugging

- `code/qcommon/files.c` - Filesystem, asset loading
- `code/game/g_main.c` - Game initialization
- `code/game/ai_cast*.c` - AI system
- `code/cgame/cg_main.c` - Client game init
- `code/server/sv_init.c` - Server initialization

## Game Modules

The game uses dynamically loaded modules (.so files):

| Module | Source | Purpose |
|--------|--------|---------|
| `cgame.sp.x86_64.so` | `code/cgame/` | Client-side game logic |
| `qagame.sp.x86_64.so` | `code/game/` | Server-side game logic, AI |
| `ui.sp.x86_64.so` | `code/ui/` | User interface |
| `renderer_sp_opengl1_x86_64.so` | `code/renderer/` | OpenGL 1.x renderer |
| `renderer_sp_rend2_x86_64.so` | `code/rend2/` | OpenGL 2+ GLSL renderer |

## Loose File Mode (Development)

The game can run **without pk3 files** using extracted loose files. When `scripts/common.shader` exists as a loose file, the game skips pk3 validation checks and runs in development mode.

Benefits:
- Edit game scripts/shaders directly without repacking
- Faster iteration during development
- No need to maintain pk3 files for testing

The console shows: `Running with loose files (no pak0.pk3 required)`

## Asset Sources

Assets are extracted from Steam installations:

- **RTCW Base**: `~/.var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/common/Return to Castle Wolfenstein/Main/`
  - `pak0.pk3`, `sp_pak1-4.pk3`

- **RealRTCW**: `~/.var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/common/RealRTCW/main/`
  - `z_realrtcw_*.pk3` (textures, models, sounds, maps, scripts)

The `copy-assets` build step extracts these pk3 files to loose files, with later pk3s overriding earlier ones.

## Common Issues

### Missing Assets
```bash
# Re-extract all assets
zig build copy-assets
```

### Library Loading Errors
```bash
# Check dependencies
ldd build/release-linux-x86_64/iowolfsp.x86_64

# Run with library path
LD_LIBRARY_PATH=build/release-linux-x86_64 ./build/release-linux-x86_64/iowolfsp.x86_64
```

### Game Won't Start
```bash
# Run with explicit paths
cd build/release-linux-x86_64
./iowolfsp.x86_64 +set fs_basepath "$(pwd)" +set fs_homepath "$(pwd)"
```

### Shader/Script Errors
Check console output for specific file errors. Scripts are in `build/release-linux-x86_64/main/scripts/`.

## LLM Integration

The project includes llama.cpp for AI enhancements:

- Source: `code/llama.cpp/`
- Libraries: `libllama.so`, `libggml*.so`
- Models: `main/models/`

Build llama.cpp separately if needed:
```bash
cd code/llama.cpp
mkdir build && cd build
cmake ..
make -j$(nproc)
```
