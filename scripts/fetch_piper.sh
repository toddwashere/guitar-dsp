#!/usr/bin/env bash
# Downloads the Piper TTS binary and the en_US-amy-medium voice model
# into assets/piper/. Idempotent — skips files that already exist.
#
# Usage: ./scripts/fetch_piper.sh
#
# Pin the voice model's HuggingFace revision for rehearsal reproducibility:
#
#     PIPER_VOICE_REF=<commit-sha-or-tag> ./scripts/fetch_piper.sh
#
# The default is "main" which is what most users want for first-time fetch.
# For reproducible conference rehearsals, look up the current commit of
# https://huggingface.co/rhasspy/piper-voices and pin it via the env var.
#
# Requires: curl, tar. Optionally: shasum for verification.

set -euo pipefail

cd "$(dirname "$0")/.."

PIPER_DIR="assets/piper"
PIPER_BIN="$PIPER_DIR/piper"
VOICE_ONNX="$PIPER_DIR/voices/en_US-amy-medium.onnx"
VOICE_JSON="$PIPER_DIR/voices/en_US-amy-medium.onnx.json"

# Pick the right Piper release for this platform.
case "$(uname -s)/$(uname -m)" in
    Darwin/arm64)
        PIPER_URL="https://github.com/rhasspy/piper/releases/download/2023.11.14-2/piper_macos_aarch64.tar.gz"
        ;;
    Darwin/x86_64)
        PIPER_URL="https://github.com/rhasspy/piper/releases/download/2023.11.14-2/piper_macos_x64.tar.gz"
        ;;
    *)
        echo "Unsupported platform: $(uname -s)/$(uname -m)" >&2
        exit 1
        ;;
esac

VOICE_REF="${PIPER_VOICE_REF:-main}"
VOICE_BASE="https://huggingface.co/rhasspy/piper-voices/resolve/${VOICE_REF}/en/en_US/amy/medium"

echo "Voice ref: ${VOICE_REF} (override with PIPER_VOICE_REF=<sha>)"

mkdir -p "$PIPER_DIR/voices"

if [ ! -x "$PIPER_BIN" ]; then
    echo "Fetching Piper binary..."
    tmp=$(mktemp -d)
    curl -L --fail -o "$tmp/piper.tar.gz" "$PIPER_URL"
    tar -xzf "$tmp/piper.tar.gz" -C "$tmp"
    # The release tarball contains a "piper/" directory with the binary
    # and shared libraries. Copy everything into assets/piper/.
    cp -R "$tmp/piper/." "$PIPER_DIR/"
    chmod +x "$PIPER_BIN"
    rm -rf "$tmp"
    echo "  Installed: $PIPER_BIN"
else
    echo "Piper binary already present: $PIPER_BIN"
fi

if [ ! -f "$VOICE_ONNX" ]; then
    echo "Fetching en_US-amy-medium voice..."
    curl -L --fail -o "$VOICE_ONNX"  "$VOICE_BASE/en_US-amy-medium.onnx"
    curl -L --fail -o "$VOICE_JSON" "$VOICE_BASE/en_US-amy-medium.onnx.json"
    echo "  Installed: $VOICE_ONNX"
else
    echo "Voice already present: $VOICE_ONNX"
fi

echo "Done. The next build will copy these into the .app bundle."
