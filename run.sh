#!/bin/bash
# Run RealRTCW from the build directory

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build/release-linux-x86_64"

EXECUTABLE="iowolfsp.x86_64"

if [ ! -f "$BUILD_DIR/$EXECUTABLE" ]; then
    echo "Error: $EXECUTABLE not found in $BUILD_DIR"
    echo "Please build the project first with: make"
    exit 1
fi

cd "$SCRIPT_DIR"

# Add build directory to library path for llama.cpp shared libraries
export LD_LIBRARY_PATH="$BUILD_DIR:$LD_LIBRARY_PATH"

exec "$BUILD_DIR/$EXECUTABLE" "$@"
