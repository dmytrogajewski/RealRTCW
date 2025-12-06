#!/bin/bash
# Setup script for TTS integration - downloads Piper and Thorsten model

set -e

TTS_DIR="main/tts"
CACHE_DIR="${TTS_DIR}/cache"
THORSTEN_REPO="Thorsten-Voice/Piper"

echo "Setting up TTS system for RealRTCW..."
echo "======================================"

# Create directories
mkdir -p "${TTS_DIR}"
mkdir -p "${CACHE_DIR}"

# Detect architecture
ARCH=$(uname -m)
if [ "$ARCH" = "x86_64" ]; then
    PIPER_ARCH="amd64"
elif [ "$ARCH" = "aarch64" ] || [ "$ARCH" = "arm64" ]; then
    PIPER_ARCH="arm64"
else
    echo "Warning: Unsupported architecture $ARCH, trying x86_64"
    PIPER_ARCH="amd64"
fi

# Download Piper binary
PIPER_BINARY="${TTS_DIR}/piper"
if [ ! -f "${PIPER_BINARY}" ] || [ ! -x "${PIPER_BINARY}" ]; then
    echo "Downloading Piper binary..."
    
    # Get latest release download URL
    if command -v python3 >/dev/null 2>&1; then
        # Match amd64 to x86_64 in filenames
        ARCH_PATTERN="${PIPER_ARCH}"
        if [ "${PIPER_ARCH}" = "amd64" ]; then
            ARCH_PATTERN="x86_64"
        fi
        PIPER_URL=$(curl -sL "https://api.github.com/repos/rhasspy/piper/releases/latest" | \
            python3 -c "import sys, json; data=json.load(sys.stdin); arch='${ARCH_PATTERN}'; assets=[a for a in data.get('assets',[]) if 'linux' in a['name'].lower() and arch in a['name'].lower() and 'tar.gz' in a['name']]; print(assets[0]['browser_download_url'] if assets else '')" 2>/dev/null)
    elif command -v jq >/dev/null 2>&1; then
        PIPER_URL=$(curl -sL "https://api.github.com/repos/rhasspy/piper/releases/latest" | \
            jq -r ".assets[] | select(.name | test(\"linux.*${PIPER_ARCH}.*tar.gz\"; \"i\")) | .browser_download_url" | head -1)
    else
        # Fallback: try to extract URL with grep (less reliable)
        PIPER_URL=$(curl -sL "https://api.github.com/repos/rhasspy/piper/releases/latest" | \
            grep -i "browser_download_url.*linux.*${PIPER_ARCH}.*tar.gz" | head -1 | sed 's/.*"browser_download_url": "\([^"]*\)".*/\1/')
    fi
    
    if [ -z "${PIPER_URL}" ] || [ "${PIPER_URL}" = "null" ] || [ "${PIPER_URL}" = "" ]; then
        echo "Could not determine Piper download URL automatically."
        echo "Please download Piper manually:"
        echo "  1. Visit: https://github.com/rhasspy/piper/releases/latest"
        echo "  2. Download the Linux ${PIPER_ARCH} tar.gz file"
        echo "  3. Extract 'piper' binary to: ${TTS_DIR}/"
        echo "  4. Make it executable: chmod +x ${PIPER_BINARY}"
        exit 1
    fi
    
    echo "Downloading from: ${PIPER_URL}"
    TEMP_TAR="/tmp/piper_${PIPER_ARCH}.tar.gz"
    
    if command -v wget >/dev/null 2>&1; then
        wget -q --show-progress "${PIPER_URL}" -O "${TEMP_TAR}" || {
            echo "Error: Failed to download Piper"
            exit 1
        }
    elif command -v curl >/dev/null 2>&1; then
        curl -L -s --progress-bar "${PIPER_URL}" -o "${TEMP_TAR}" || {
            echo "Error: Failed to download Piper"
            exit 1
        }
    else
        echo "Error: Neither wget nor curl found"
        exit 1
    fi
    
    # Extract all files from archive (piper needs its shared libraries)
    echo "Extracting Piper..."
    TEMP_DIR="/tmp/piper_extract_$$"
    mkdir -p "${TEMP_DIR}"
    tar -xzf "${TEMP_TAR}" -C "${TEMP_DIR}" 2>/dev/null || {
        echo "Error: Failed to extract archive"
        rm -rf "${TEMP_DIR}" "${TEMP_TAR}"
        exit 1
    }
    
    # Find the piper executable (could be in a subdirectory)
    FOUND_PIPER=$(find "${TEMP_DIR}" -name "piper" -type f -executable | head -1)
    if [ -z "${FOUND_PIPER}" ]; then
        # Try finding it as a regular file
        FOUND_PIPER=$(find "${TEMP_DIR}" -name "piper" -type f | head -1)
    fi
    
    if [ -z "${FOUND_PIPER}" ]; then
        echo "Error: Could not find piper binary in archive"
        echo "Archive contents:"
        find "${TEMP_DIR}" -type f | head -10
        rm -rf "${TEMP_DIR}" "${TEMP_TAR}"
        exit 1
    fi
    
    # Get the directory containing piper
    PIPER_BIN_DIR=$(dirname "${FOUND_PIPER}")
    
    # Copy all files from that directory (binary + libraries)
    # But don't copy subdirectories that might conflict
    for file in "${PIPER_BIN_DIR}"/*; do
        if [ -f "${file}" ]; then
            cp "${file}" "${TTS_DIR}/" 2>/dev/null
        elif [ -d "${file}" ]; then
            # Copy directories (like espeak-ng-data)
            cp -r "${file}" "${TTS_DIR}/" 2>/dev/null
        fi
    done
    
    # Ensure piper is executable
    if [ -f "${PIPER_BINARY}" ]; then
        chmod +x "${PIPER_BINARY}"
    else
        echo "Warning: piper binary not found at expected location after copy"
    fi
    
    echo "Piper extracted successfully"
    rm -rf "${TEMP_DIR}" "${TEMP_TAR}"
else
    echo "Piper binary already exists, skipping download"
fi

# Download Thorsten emotional German model
MODEL_NAME="de_DE-thorsten_emotional-medium"
MODEL_ONNX="${TTS_DIR}/${MODEL_NAME}.onnx"
MODEL_JSON="${TTS_DIR}/${MODEL_NAME}.onnx.json"

if [ ! -f "${MODEL_ONNX}" ]; then
    echo "Downloading Thorsten emotional German model..."
    echo "This may take a while (model is ~50MB)..."
    
    # HuggingFace direct download URLs
    HF_BASE="https://huggingface.co/${THORSTEN_REPO}/resolve/main"
    MODEL_ONNX_URL="${HF_BASE}/${MODEL_NAME}.onnx"
    MODEL_JSON_URL="${HF_BASE}/${MODEL_NAME}.onnx.json"
    
    if command -v wget >/dev/null 2>&1; then
        wget -q --show-progress "${MODEL_ONNX_URL}" -O "${MODEL_ONNX}" || {
            echo "Error: Failed to download model file"
            echo "Please download manually from: https://huggingface.co/${THORSTEN_REPO}"
            exit 1
        }
        wget -q "${MODEL_JSON_URL}" -O "${MODEL_JSON}" 2>/dev/null || {
            echo "Warning: Could not download JSON config (may still work)"
        }
    elif command -v curl >/dev/null 2>&1; then
        curl -L -s --progress-bar "${MODEL_ONNX_URL}" -o "${MODEL_ONNX}" || {
            echo "Error: Failed to download model file"
            echo "Please download manually from: https://huggingface.co/${THORSTEN_REPO}"
            exit 1
        }
        curl -L -s "${MODEL_JSON_URL}" -o "${MODEL_JSON}" 2>/dev/null || {
            echo "Warning: Could not download JSON config (may still work)"
        }
    else
        echo "Error: Neither wget nor curl found"
        exit 1
    fi
    
    echo "Model downloaded successfully"
else
    echo "Model already exists, skipping download"
fi

# Verify files
echo ""
echo "Verifying installation..."
if [ -f "${PIPER_BINARY}" ] && [ -x "${PIPER_BINARY}" ]; then
    echo "✓ Piper binary: OK"
    # Test piper
    "${PIPER_BINARY}" --help >/dev/null 2>&1 && echo "  (Piper is working)" || echo "  (Piper binary found)"
else
    echo "✗ Piper binary: MISSING"
    exit 1
fi

if [ -f "${MODEL_ONNX}" ]; then
    MODEL_SIZE=$(du -h "${MODEL_ONNX}" | cut -f1)
    echo "✓ Model file: OK (${MODEL_SIZE})"
else
    echo "✗ Model file: MISSING"
    exit 1
fi

if [ -f "${MODEL_JSON}" ]; then
    echo "✓ Model config: OK"
else
    echo "⚠ Model config: MISSING (may still work)"
fi

echo ""
echo "TTS setup complete!"
echo "Files are in: ${TTS_DIR}/"
echo ""
echo "To enable TTS in-game, set: g_tts_enable 1"
