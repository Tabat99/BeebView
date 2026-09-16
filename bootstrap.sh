#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT_DIR"

need_cmd() {
    command -v "$1" >/dev/null 2>&1
}

pkg_installed() {
    dpkg-query -W -f='${Status}' "$1" 2>/dev/null | grep -q '^install ok installed$'
}

missing=()

if ! need_cmd cmake; then
    missing+=(cmake)
fi
if ! need_cmd cc && ! need_cmd gcc && ! need_cmd clang; then
    missing+=(build-essential)
fi
if ! need_cmd pkg-config; then
    missing+=(pkg-config)
fi

# The graphical build uses the bundled SDL3 source release.  unzip is needed
# to refresh that source tree from sdl3/SDL3-X.Y.Z.zip.
if ! need_cmd unzip; then
    missing+=(unzip)
fi

# ncurses is optional, but bootstrap installs it by default so the terminal
# frontend can be built alongside the SDL3 frontend.
if ! pkg-config --exists ncurses 2>/dev/null && ! pkg-config --exists ncursesw 2>/dev/null; then
    missing+=(libncurses-dev)
fi

if ((${#missing[@]} == 0)); then
    echo "BeebView build dependencies are already installed."
    echo "Bundled SDL3 source will be selected automatically by ./build.sh."
    exit 0
fi

if [[ ! -r /etc/os-release ]]; then
    echo "Missing build dependencies: ${missing[*]}" >&2
    echo "Automatic installation currently supports Debian-family systems only." >&2
    exit 1
fi

# shellcheck disable=SC1091
. /etc/os-release
case "${ID:-} ${ID_LIKE:-}" in
    *debian*|*ubuntu*) ;;
    *)
        echo "Missing build dependencies: ${missing[*]}" >&2
        echo "Automatic installation currently supports Debian/Ubuntu/Mint and related systems." >&2
        exit 1
        ;;
esac

# Remove duplicates while preserving order.
unique=()
for pkg in "${missing[@]}"; do
    seen=false
    for existing in "${unique[@]:-}"; do
        if [[ "$existing" == "$pkg" ]]; then
            seen=true
            break
        fi
    done
    if [[ "$seen" == false ]]; then
        unique+=("$pkg")
    fi
done

echo "ViewBBC needs the following packages:"
printf '  %s\n' "${unique[@]}"
echo
read -r -p "Install them now with apt? [Y/n] " answer
answer="${answer:-Y}"
case "$answer" in
    Y|y|YES|Yes|yes)
        sudo apt-get update
        sudo apt-get install -y "${unique[@]}"
        ;;
    *)
        echo "Nothing installed. Re-run ./bootstrap.sh when ready."
        exit 1
        ;;
esac

echo
if [[ -x "${ROOT_DIR}/tools/prepare_sdl3.sh" ]]; then
    echo "Bundled SDL3 source archive support is ready."
fi

echo "Dependencies ready. Next run:"
echo "  ./build.sh"
