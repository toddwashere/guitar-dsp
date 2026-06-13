#!/usr/bin/env bash
# Downloads the Whisper STT model (ggml-base.en.bin, ~150 MB) used by the
# conversational-AI feature. Idempotent — skips if a sane-sized file already
# exists.
#
# Usage: ./scripts/fetch_whisper.sh
#
# Requires: curl.

set -euo pipefail

cd "$(dirname "$0")/.."

DEST="Resources/whisper/ggml-base.en.bin"
URL="https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-base.en.bin"
MIN_BYTES=100000000  # ~95 MB; the real file is ~141 MB

if [ -f "$DEST" ]; then
    size="$(stat -f %z "$DEST" 2>/dev/null || stat -c %s "$DEST" 2>/dev/null || echo 0)"
    if [ "$size" -gt "$MIN_BYTES" ]; then
        echo "Whisper model already present at $DEST (${size} bytes)"
        exit 0
    fi
    echo "Existing $DEST is suspiciously small (${size} bytes) — re-downloading."
fi

mkdir -p "$(dirname "$DEST")"
echo "Fetching whisper model (~150 MB) from $URL"
curl -L --fail --progress-bar -o "$DEST" "$URL"

size="$(stat -f %z "$DEST" 2>/dev/null || stat -c %s "$DEST" 2>/dev/null || echo 0)"
if [ "$size" -lt "$MIN_BYTES" ]; then
    echo "ERROR: downloaded file is too small (${size} bytes). URL may be broken." >&2
    exit 1
fi
echo "Done. $DEST (${size} bytes)"
