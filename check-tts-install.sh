#!/bin/bash
# Check TTS installation in Steam directory

STEAMDIR="${HOME}/.var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/common/RealRTCW"

echo "Checking TTS installation..."
echo "=============================="
echo ""

if [ ! -d "$STEAMDIR" ]; then
    echo "ERROR: Steam directory not found: $STEAMDIR"
    exit 1
fi

echo "Steam directory: $STEAMDIR"
echo ""

# Check TTS directory
TTS_DIR="$STEAMDIR/Main/tts"
if [ ! -d "$TTS_DIR" ]; then
    echo "✗ TTS directory not found: $TTS_DIR"
    echo "  Run: make install-tts-only"
    exit 1
else
    echo "✓ TTS directory exists: $TTS_DIR"
fi

# Check files
MISSING=0

if [ -f "$TTS_DIR/piper" ] && [ -x "$TTS_DIR/piper" ]; then
    echo "✓ Piper binary exists and is executable"
    "$TTS_DIR/piper" --version 2>/dev/null | head -1 || echo "  (version check failed)"
else
    echo "✗ Piper binary missing or not executable: $TTS_DIR/piper"
    MISSING=1
fi

if [ -f "$TTS_DIR/de_DE-thorsten_emotional-medium.onnx" ]; then
    SIZE=$(du -h "$TTS_DIR/de_DE-thorsten_emotional-medium.onnx" | cut -f1)
    echo "✓ Model file exists: $SIZE"
else
    echo "✗ Model file missing: $TTS_DIR/de_DE-thorsten_emotional-medium.onnx"
    MISSING=1
fi

if [ -f "$TTS_DIR/de_DE-thorsten_emotional-medium.onnx.json" ]; then
    echo "✓ Model config exists"
else
    echo "✗ Model config missing: $TTS_DIR/de_DE-thorsten_emotional-medium.onnx.json"
    MISSING=1
fi

# Check libraries
LIB_COUNT=$(find "$TTS_DIR" -name "*.so*" -type f | wc -l)
if [ "$LIB_COUNT" -gt 0 ]; then
    echo "✓ Found $LIB_COUNT shared library file(s)"
else
    echo "⚠ No shared libraries found (piper may not work)"
fi

# Check espeak-ng
if [ -d "$TTS_DIR/espeak-ng-data" ]; then
    echo "✓ espeak-ng-data directory exists"
else
    echo "⚠ espeak-ng-data directory missing (may still work)"
fi

echo ""
if [ "$MISSING" -eq 0 ]; then
    echo "✓ TTS installation looks good!"
    echo ""
    echo "In-game, check console for:"
    echo "  [TTS] Initialized successfully"
    echo ""
    echo "If you don't see that message, check for errors like:"
    echo "  [TTS] Piper binary not found"
    echo "  [TTS] Model file not found"
    exit 0
else
    echo "✗ TTS installation incomplete!"
    echo ""
    echo "To fix, run from source directory:"
    echo "  make install-tts-only"
    exit 1
fi

