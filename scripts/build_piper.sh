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
# Same espeak-ng commit used by piper-phonemize (pinned for reproducibility).
ESPEAK_NG_COMMIT="0f65aa301e0d6bae5e172cc74197d32a6182200f"
ESPEAK_NG_URL="https://github.com/rhasspy/espeak-ng/archive/${ESPEAK_NG_COMMIT}.zip"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

mkdir -p "$PIPER_DIR"

# 1) Clone + checkout pinned commit.
echo "==> cloning piper @ ${PIPER_COMMIT}"
if ! git clone --depth 1 --branch "${PIPER_COMMIT}" "$PIPER_REPO" "$WORK/piper" 2>/dev/null; then
    echo "==> shallow clone of branch/tag '${PIPER_COMMIT}' failed (likely a bare SHA); falling back to full clone"
    git clone "$PIPER_REPO" "$WORK/piper"
fi
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

# 3b) Build the arm64 espeak-ng executable from the same rhasspy/espeak-ng
#     source that piper-phonemize uses for libespeak-ng.  Piper's own CMake
#     build only produces the *library* (BUILD_SHARED_LIBS=ON, no install of
#     the binary), so we build the binary in a sibling directory here.
echo "==> building arm64 espeak-ng binary"
curl -L --fail -o "$WORK/espeak-ng.zip" "$ESPEAK_NG_URL"
unzip -q "$WORK/espeak-ng.zip" -d "$WORK"
ESPEAK_SRC="$WORK/espeak-ng-${ESPEAK_NG_COMMIT}"
ESPEAK_BLD="$WORK/espeak-build"
ESPEAK_INST="$WORK/espeak-inst"
cmake -S "$ESPEAK_SRC" -B "$ESPEAK_BLD" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$ESPEAK_INST" \
    -DBUILD_SHARED_LIBS=OFF \
    -DUSE_ASYNC=OFF \
    -DUSE_MBROLA=ON \
    -DUSE_LIBSONIC=OFF \
    -DUSE_LIBPCAUDIO=OFF \
    -DUSE_KLATT=OFF \
    -DUSE_SPEECHPLAYER=OFF
cmake --build "$ESPEAK_BLD" -j --target espeak-ng-bin
# The executable is in the build tree; find it.
ESPEAK_BIN=$(find "$ESPEAK_BLD" -type f -name "espeak-ng" -perm +111 -print -quit 2>/dev/null)
if [[ -z "$ESPEAK_BIN" ]]; then
    echo "ERROR: espeak-ng binary not found in build tree $ESPEAK_BLD" >&2
    exit 1
fi
# Confirm it is arm64.
ESPEAK_ARCH=$(file "$ESPEAK_BIN" | grep -oE 'arm64|x86_64' | head -1)
if [[ "$ESPEAK_ARCH" != "arm64" ]]; then
    echo "ERROR: built espeak-ng is ${ESPEAK_ARCH}, not arm64: $ESPEAK_BIN" >&2
    exit 1
fi
echo "==> found arm64 espeak-ng at $ESPEAK_BIN"

# 4) Copy artifacts into assets/piper/.
echo "==> installing into $PIPER_DIR"
cp "$WORK/build/piper"                          "$PIPER_DIR/piper"
cp "$ORT_DIR/lib/libonnxruntime.${ORT_VERSION}.dylib" \
   "$PIPER_DIR/libonnxruntime.${ORT_VERSION}.dylib"
# Piper's CMake places these in the build tree:
for lib in libespeak-ng.1.dylib libpiper_phonemize.1.dylib; do
    found=$(find "$WORK/build" -name "$lib" -print -quit)
    if [[ -z "$found" ]]; then
        echo "ERROR: $lib not found anywhere under $WORK/build — Piper's build tree layout may have changed" >&2
        exit 1
    fi
    cp "$found" "$PIPER_DIR/"
done
# Install the arm64 espeak-ng executable built in step 3b.
cp "$ESPEAK_BIN" "$PIPER_DIR/espeak-ng"
chmod +x "$PIPER_DIR/espeak-ng"

# 5) Fix rpaths so the binary finds its dylibs relative to itself at runtime.
#    The CMake build bakes temp-dir rpaths into the binary; replace them with
#    @loader_path (= the directory containing the piper binary).
echo "==> fixing rpaths"
# Remove all existing temp-dir rpaths from the binary.
while IFS= read -r rp; do
    # `|| true` so re-runs are idempotent: if a previous run already deleted
    # this rpath, install_name_tool errors out — that's expected, not a fault.
    install_name_tool -delete_rpath "$rp" "$PIPER_DIR/piper" 2>/dev/null || true
done < <(otool -l "$PIPER_DIR/piper" | awk '/LC_RPATH/{found=1} found && /path /{print $2; found=0}')
# Add a single @loader_path rpath so dylibs sitting next to the binary are found.
install_name_tool -add_rpath "@loader_path" "$PIPER_DIR/piper"

# Apply the same rpath fixup to espeak-ng — it was built against the temp-dir
# install prefix for libespeak-ng.1.dylib and needs @loader_path instead.
while IFS= read -r rp; do
    install_name_tool -delete_rpath "$rp" "$PIPER_DIR/espeak-ng" 2>/dev/null || true
done < <(otool -l "$PIPER_DIR/espeak-ng" | awk '/LC_RPATH/{found=1} found && /path /{print $2; found=0}')
# The binary links with @rpath/libespeak-ng.1.dylib; ensure @loader_path resolves it.
install_name_tool -add_rpath "@loader_path" "$PIPER_DIR/espeak-ng" 2>/dev/null || true

# 6) Verify.
for lib in libespeak-ng.1.dylib libpiper_phonemize.1.dylib "libonnxruntime.${ORT_VERSION}.dylib"; do
    if [[ ! -f "$PIPER_DIR/$lib" ]]; then
        echo "ERROR: $lib missing after build — cmake/build output above should show the failure" >&2
        exit 1
    fi
done
if [[ ! -f "$PIPER_DIR/espeak-ng" ]]; then
    echo "ERROR: espeak-ng binary missing after build" >&2
    exit 1
fi
VERIFY_ARCH=$(file "$PIPER_DIR/espeak-ng" | grep -oE 'arm64|x86_64' | head -1)
if [[ "$VERIFY_ARCH" != "arm64" ]]; then
    echo "ERROR: installed espeak-ng is ${VERIFY_ARCH}, not arm64" >&2
    exit 1
fi
chmod +x "$PIPER_DIR/piper"
echo "==> piper install complete: $PIPER_DIR"
otool -L "$PIPER_DIR/piper" | head
echo "==> espeak-ng (arm64):"
otool -L "$PIPER_DIR/espeak-ng" | head
