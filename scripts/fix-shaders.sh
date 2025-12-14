#!/bin/bash
# Fix broken shader files extracted from pk3
# These have issues in the original RealRTCW/RTCW assets

MAIN_DIR="$1"

if [ -z "$MAIN_DIR" ]; then
    echo "Usage: $0 <main_dir>"
    exit 1
fi

echo "Fixing broken shaders..."

# common.shader: line has corrupted "360azz    }" instead of "360" + newline + "}"
if [ -f "$MAIN_DIR/scripts/common.shader" ]; then
    # Remove CRLF
    sed -i 's/\r$//' "$MAIN_DIR/scripts/common.shader"
    # Fix the corrupted line - replace "360azz.*}" with "360" and add closing brace on next line
    sed -i '/360azz/{s/360azz.*/360/; a\    }
}' "$MAIN_DIR/scripts/common.shader"
fi

# models_mapobjects_et.shader: closing brace is commented out
if [ -f "$MAIN_DIR/scripts/models_mapobjects_et.shader" ]; then
    # Remove CRLF
    sed -i 's/\r$//' "$MAIN_DIR/scripts/models_mapobjects_et.shader"
    # Uncomment the closing brace (pattern: //		} at start of line)
    sed -i 's|^//		}$|		}|' "$MAIN_DIR/scripts/models_mapobjects_et.shader"
fi

echo "Shader fixes applied."
