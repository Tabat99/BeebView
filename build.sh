#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$ROOT_DIR/build"
OUTPUT_DIR="$ROOT_DIR/out/bin"
AUTO_SDL3=0
if [[ -n "${VIEWBBC_SDL3_SOURCE:-}" ]]; then
    SDL3_DIR="${VIEWBBC_SDL3_SOURCE}"
else
    SDL3_DIR="$(${ROOT_DIR}/tools/prepare_sdl3.sh "${ROOT_DIR}")"
    AUTO_SDL3=1
fi

if [[ ! -f "$SDL3_DIR/CMakeLists.txt" ]]; then
    echo "ERROR: SDL3 source tree not found:"
    echo "  $SDL3_DIR"
    echo
    echo "Drop the latest SDL3 source ZIP into ./sdl3 as SDL3-X.Y.Z.zip,"
    echo "or override automatic selection with:"
    echo "  VIEWBBC_SDL3_SOURCE=/path/to/SDL3-X.Y.Z ./build.sh"
    exit 1
fi

printf 'Project root:\n  %s\n' "$ROOT_DIR"
printf 'SDL3 source:\n  %s\n' "$SDL3_DIR"

echo "Cleaning build directory..."
rm -rf "$BUILD_DIR"

echo "Configuring..."
cmake \
    -S "$ROOT_DIR" \
    -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    '-DCMAKE_BUILD_RPATH=$ORIGIN' \
    -DVIEWBBC_SDL3_SOURCE="$SDL3_DIR" \
    -DVIEWBBC_BUILD_SDL3=ON \
    -DVIEWBBC_BUILD_NCURSES=ON

echo "Building..."
cmake --build "$BUILD_DIR" --parallel

echo "Running tests..."
ctest --test-dir "$BUILD_DIR" --output-on-failure

# Preserve the runnable results outside the disposable CMake tree.  The SDL
# executable is built with an $ORIGIN runtime search path; when SDL3 is shared,
# copy its SONAME beside BeebView so deleting build/ cannot break the program.
rm -rf "$OUTPUT_DIR"
mkdir -p "$OUTPUT_DIR"
if [[ -x "$BUILD_DIR/BeebView" ]]; then
    cp -p "$BUILD_DIR/BeebView" "$OUTPUT_DIR/BeebView"
    SDL3_LIB="$(find "$BUILD_DIR" -type f \( -name 'libSDL3.so.0' -o -name 'libSDL3.so.0.*' \) -print | sort -V | tail -1 || true)"
    if [[ -n "$SDL3_LIB" ]]; then
        cp -p "$SDL3_LIB" "$OUTPUT_DIR/libSDL3.so.0"
    fi
fi
if [[ -x "$BUILD_DIR/BeebView-terminal" ]]; then
    cp -p "$BUILD_DIR/BeebView-terminal" "$OUTPUT_DIR/BeebView-terminal"
fi

# Everything above this point must succeed before temporary trees are removed.
# A failed configure/build/test/staging run therefore leaves build/ and the
# extracted SDL3 source available for diagnosis.
#rm -rf "$BUILD_DIR"
if [[ ${AUTO_SDL3} -eq 1 ]]; then
    "${ROOT_DIR}/tools/cleanup_sdl3.sh" "${ROOT_DIR}" "${SDL3_DIR}"
fi

echo
echo "Build complete."
echo "Temporary build directory cleaned: $BUILD_DIR"
[[ -x "$OUTPUT_DIR/BeebView" ]] && echo "SDL3 application:       $OUTPUT_DIR/BeebView"
[[ -x "$OUTPUT_DIR/BeebView-terminal" ]] && echo "Terminal application:   $OUTPUT_DIR/BeebView-terminal"
echo
if [[ -x "$OUTPUT_DIR/BeebView" ]]; then
    echo "Run graphical version with:"
    echo "  $OUTPUT_DIR/BeebView"
fi
