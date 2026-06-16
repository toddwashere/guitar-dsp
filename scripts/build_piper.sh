#!/usr/bin/env bash
# Builds Piper from source into assets/piper/, INCLUDING the three runtime
# dylibs (libespeak-ng.1.dylib, libpiper_phonemize.1.dylib,
# libonnxruntime.1.14.1.dylib) that the upstream macOS tarball is missing.
#
# Usage: ./scripts/build_piper.sh
#
# Idempotent — re-runs are cheap. Pins Piper to commit ${PIPER_COMMIT}
# for reproducibility.

set -euo pipefail
cd "$(dirname "$0")/.."

PIPER_DIR="assets/piper"
PIPER_REPO="https://github.com/rhasspy/piper.git"
PIPER_COMMIT="${PIPER_COMMIT:-2023.11.14-2}"   # tag of last good release
ORT_VERSION="${ORT_VERSION:-1.14.1}"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

mkdir -p "$PIPER_DIR"

# 1) Clone + checkout pinned commit.
echo "==> cloning piper @ ${PIPER_COMMIT}"
git clone --depth 1 --branch "${PIPER_COMMIT}" "$PIPER_REPO" "$WORK/piper" \
  || git clone "$PIPER_REPO" "$WORK/piper"
( cd "$WORK/piper" && git checkout "$PIPER_COMMIT" )

# 2) Fetch ONNX Runtime release binary (contains libonnxruntime dylib).
ORT_URL="https://github.com/microsoft/onnxruntime/releases/download/v${ORT_VERSION}/onnxruntime-osx-arm64-${ORT_VERSION}.tgz"
echo "==> fetching onnxruntime ${ORT_VERSION}"
curl -L --fail -o "$WORK/ort.tgz" "$ORT_URL"
tar -xzf "$WORK/ort.tgz" -C "$WORK"
ORT_DIR="$WORK/onnxruntime-osx-arm64-${ORT_VERSION}"

# 3) Configure + build Piper.
echo "==> configuring piper"
cmake -S "$WORK/piper" -B "$WORK/build" \
    -DCMAKE_BUILD_TYPE=Release \
    -DONNXRUNTIME_ROOTDIR="$ORT_DIR"
echo "==> building piper"
cmake --build "$WORK/build" -j

# 4) Copy artifacts into assets/piper/.
echo "==> installing into $PIPER_DIR"
cp "$WORK/build/piper"                          "$PIPER_DIR/piper"
cp "$ORT_DIR/lib/libonnxruntime.${ORT_VERSION}.dylib" \
   "$PIPER_DIR/libonnxruntime.1.14.1.dylib"
# Piper's CMake places these in the build tree:
find "$WORK/build" -name "libespeak-ng.1.dylib"        -exec cp {} "$PIPER_DIR/" \;
find "$WORK/build" -name "libpiper_phonemize.1.dylib"  -exec cp {} "$PIPER_DIR/" \;

# 5) Fix rpaths so the binary finds its dylibs relative to itself at runtime.
#    The CMake build bakes temp-dir rpaths into the binary; replace them with
#    @loader_path (= the directory containing the piper binary).
echo "==> fixing rpaths"
# Remove all existing temp-dir rpaths from the binary.
while IFS= read -r rp; do
    install_name_tool -delete_rpath "$rp" "$PIPER_DIR/piper" 2>/dev/null || true
done < <(otool -l "$PIPER_DIR/piper" | awk '/LC_RPATH/{found=1} found && /path /{print $2; found=0}')
# Add a single @loader_path rpath so dylibs sitting next to the binary are found.
install_name_tool -add_rpath "@loader_path" "$PIPER_DIR/piper"

# 6) Verify.
for lib in libespeak-ng.1.dylib libpiper_phonemize.1.dylib libonnxruntime.1.14.1.dylib; do
    if [[ ! -f "$PIPER_DIR/$lib" ]]; then
        echo "ERROR: $lib missing after build — check build logs in $WORK/build" >&2
        exit 1
    fi
done
chmod +x "$PIPER_DIR/piper"
echo "==> piper install complete: $PIPER_DIR"
otool -L "$PIPER_DIR/piper" | head
