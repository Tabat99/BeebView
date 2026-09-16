#!/usr/bin/env bash
#
# BeebView Linux installer builder
# Builds independent SDL and terminal AppImage/.deb packages.
#
# Usage:
#   ./build_Installers.sh [--yes] [--version X.Y.Z] [--choice 1|2|3|4|5|q] [--keep-build] [--debug-binaries]
#
# Choices:
#   1  SDL graphical frontend — AppImage
#   2  Terminal frontend      — AppImage
#   3  SDL graphical frontend — .deb
#   4  Terminal frontend      — .deb
#   5  All four [default]
#   Q  Quit

set -euo pipefail

SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
INSTALLER_DIR="${SRC_DIR}/installers"
BUILD_DIR="${SRC_DIR}/out/build/linux-installer"
WORK_DIR="${SRC_DIR}/out/installer-work"
LOG="${INSTALLER_DIR}/installer-build.log"
DEBUG_DIR="${INSTALLER_DIR}/debug"
SDL3_DIR="${VIEWBBC_SDL3_SOURCE:-}"
AUTO_SDL3=0
AUTO_YES=0
CHOICE=""
VERSION=""
KEEP_BUILD=0
DEBUG_BINARIES=0

read_project_version() {
    # CMake's project() version is the single authoritative engineering/package
    # version.  Do not derive package versions from CHANGELOG heading style.
    local file="${SRC_DIR}/CMakeLists.txt"
    local value=""
    if [[ -f "${file}" ]]; then
        value="$(sed -nE 's/^[[:space:]]*project\([[:space:]]*BeebView[[:space:]]+VERSION[[:space:]]+([0-9]+\.[0-9]+\.[0-9]+).*$/\1/p' "${file}" | head -1)"
    fi
    [[ -n "${value}" ]] || { echo "[X] Could not read BeebView version from CMakeLists.txt" >&2; return 1; }
    printf '%s\n' "${value}"
}

VERSION="$(read_project_version)"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --version)
            shift
            [[ $# -gt 0 ]] || { echo "[X] --version requires a value" >&2; exit 1; }
            VERSION="$1"
            ;;
        --yes|-y) AUTO_YES=1 ;;
        --keep-build) KEEP_BUILD=1 ;;
        --debug-binaries) DEBUG_BINARIES=1 ;;
        --clean-build) KEEP_BUILD=0 ;; # backward-compatible alias for the new default
        --choice)
            shift
            [[ $# -gt 0 ]] || { echo "[X] --choice requires 1, 2, 3, 4, 5 or q" >&2; exit 1; }
            CHOICE="$1"
            ;;
        --help|-h)
            cat <<HELP
Usage: $0 [--version X.Y.Z] [--yes] [--choice 1|2|3|4|5|q] [--keep-build] [--debug-binaries]

  1  SDL graphical frontend — AppImage
  2  Terminal frontend      — AppImage
  3  SDL graphical frontend — .deb
  4  Terminal frontend      — .deb
  5  All four [default]
  Q  Quit

  --keep-build      Retain out/build/linux-installer after packaging.
  --debug-binaries  Also retain exact package-build binaries in installers/debug/.
  --clean-build     Accepted for compatibility; cleanup is now the default.

Environment:
  VIEWBBC_SDL3_SOURCE=/path/to/SDL3-X.Y.Z   Override automatic SDL3 selection.
  Otherwise the newest ./sdl3/SDL3-X.Y.Z.zip is unpacked automatically.
HELP
            exit 0
            ;;
        *) echo "[X] Unknown argument: $1" >&2; exit 1 ;;
    esac
    shift
done

cat <<MENU

╔══════════════════════════════════════════════╗
║              BeebView Installer              ║
╚══════════════════════════════════════════════╝

  What would you like to build?

    [1]  AppImage SDL       — graphical, portable
    [2]  AppImage Terminal  — ncurses, portable
    [3]  DEB SDL            — graphical, system install
    [4]  DEB Terminal       — ncurses, system install
    [5]  All                — all four packages  [default]
    [Q]  Quit
MENU

if [[ -z "${CHOICE}" ]]; then
    read -rp "  Your choice [1-5,Q] (default=5): " CHOICE
    CHOICE="${CHOICE:-5}"
fi

BUILD_SDL_APPIMAGE=0
BUILD_TUI_APPIMAGE=0
BUILD_SDL_DEB=0
BUILD_TUI_DEB=0
case "${CHOICE}" in
    1) BUILD_SDL_APPIMAGE=1 ;;
    2) BUILD_TUI_APPIMAGE=1 ;;
    3) BUILD_SDL_DEB=1 ;;
    4) BUILD_TUI_DEB=1 ;;
    5) BUILD_SDL_APPIMAGE=1; BUILD_TUI_APPIMAGE=1; BUILD_SDL_DEB=1; BUILD_TUI_DEB=1 ;;
    q|Q) echo "  Bye."; exit 0 ;;
    *) echo "[X] Invalid choice." >&2; exit 1 ;;
esac

NEED_SDL=0
NEED_TUI=0
NEED_APPIMAGE=0
[[ ${BUILD_SDL_APPIMAGE} -eq 1 || ${BUILD_SDL_DEB} -eq 1 ]] && NEED_SDL=1
[[ ${BUILD_TUI_APPIMAGE} -eq 1 || ${BUILD_TUI_DEB} -eq 1 ]] && NEED_TUI=1
[[ ${BUILD_SDL_APPIMAGE} -eq 1 || ${BUILD_TUI_APPIMAGE} -eq 1 ]] && NEED_APPIMAGE=1

mkdir -p "${INSTALLER_DIR}"
: > "${LOG}"
log()  { echo -e "[+] $*" | tee -a "${LOG}"; }
warn() { echo -e "[!] $*" | tee -a "${LOG}" >&2; }
die()  { echo -e "[X] $*" | tee -a "${LOG}" >&2; exit 1; }
trap 'die "FAILED — see: ${LOG}"' ERR

[[ "${VERSION}" =~ ^[0-9][0-9A-Za-z.+:~-]*$ ]] || die "Invalid package version: ${VERSION}"
if [[ ${NEED_SDL} -eq 1 ]]; then
    if [[ -z "${SDL3_DIR}" ]]; then
        SDL3_DIR="$(${SRC_DIR}/tools/prepare_sdl3.sh "${SRC_DIR}")"
        AUTO_SDL3=1
    fi
    [[ -f "${SDL3_DIR}/CMakeLists.txt" ]] || die "SDL3 source tree not found: ${SDL3_DIR}"
fi

DEB_ARCH="$(dpkg --print-architecture 2>/dev/null || true)"
[[ -n "${DEB_ARCH}" ]] || die "dpkg is required to build Debian packages"
MACHINE="$(uname -m)"
case "${MACHINE}" in
    x86_64) APPIMAGE_ARCH="x86_64" ;;
    *) [[ ${NEED_APPIMAGE} -eq 0 ]] || die "AppImage builder currently supports x86_64; detected ${MACHINE}"; APPIMAGE_ARCH="${MACHINE}" ;;
esac

SDL_APPIMAGE_NAME="BeebView-SDL-${VERSION}-${APPIMAGE_ARCH}.AppImage"
TUI_APPIMAGE_NAME="BeebView-Terminal-${VERSION}-${APPIMAGE_ARCH}.AppImage"
SDL_DEB_NAME="beebview-sdl_${VERSION}_${DEB_ARCH}.deb"
TUI_DEB_NAME="beebview-terminal_${VERSION}_${DEB_ARCH}.deb"

# Remove BeebView packages left by older installer runs.  A failed current run
# must not leave an old package looking like newly generated output.
rm -f "${INSTALLER_DIR}"/BeebView-SDL-*.AppImage \
      "${INSTALLER_DIR}"/BeebView-Terminal-*.AppImage \
      "${INSTALLER_DIR}"/beebview-sdl_*.deb \
      "${INSTALLER_DIR}"/beebview-terminal_*.deb

cat <<PLAN

  ┌─────────────────────────────────────────┐
  │  Build plan                             │
  ├─────────────────────────────────────────┤
PLAN
[[ ${BUILD_SDL_APPIMAGE} -eq 1 ]] && printf "  │  SDL AppImage : %-24s│\n" "${SDL_APPIMAGE_NAME}"
[[ ${BUILD_TUI_APPIMAGE} -eq 1 ]] && printf "  │  TUI AppImage : %-24s│\n" "${TUI_APPIMAGE_NAME}"
[[ ${BUILD_SDL_DEB} -eq 1 ]] && printf "  │  SDL .deb     : %-24s│\n" "${SDL_DEB_NAME}"
[[ ${BUILD_TUI_DEB} -eq 1 ]] && printf "  │  TUI .deb     : %-24s│\n" "${TUI_DEB_NAME}"
printf "  │  Version      : %-24s│\n" "${VERSION}"
echo "  └─────────────────────────────────────────┘"
echo

if [[ ${AUTO_YES} -eq 0 ]]; then
    read -rp "  Proceed? [Y/n]: " answer
    case "${answer}" in [nN]*) echo "  Aborted."; exit 0 ;; esac
fi

# Build dependencies. SDL3 remains private and is never installed system-wide.
MISSING_PKGS=()
need_pkg() { dpkg -s "$1" &>/dev/null || MISSING_PKGS+=("$1"); }
need_pkg build-essential
need_pkg cmake
need_pkg ninja-build
need_pkg pkg-config
need_pkg python3
[[ ${NEED_SDL} -eq 1 ]] && need_pkg unzip
need_pkg fakeroot
need_pkg dpkg-dev
need_pkg desktop-file-utils
[[ ${NEED_TUI} -eq 1 ]] && need_pkg libncurses-dev
[[ ${NEED_SDL} -eq 1 ]] && need_pkg patchelf
[[ ${NEED_APPIMAGE} -eq 1 ]] && { need_pkg curl; need_pkg patchelf; }

if [[ ${#MISSING_PKGS[@]} -gt 0 ]]; then
    mapfile -t MISSING_PKGS < <(printf '%s\n' "${MISSING_PKGS[@]}" | sort -u)
    log "Installing missing build dependencies: ${MISSING_PKGS[*]}"
    sudo apt-get update -q 2>&1 | tee -a "${LOG}"
    sudo apt-get install -y "${MISSING_PKGS[@]}" 2>&1 | tee -a "${LOG}"
fi

log "Version : ${VERSION}"
[[ ${NEED_SDL} -eq 1 ]] && log "SDL3    : ${SDL3_DIR}"
log "Building Release binaries..."
rm -rf "${BUILD_DIR}" "${WORK_DIR}"
mkdir -p "${BUILD_DIR}" "${WORK_DIR}"

SDL_OPT=OFF
TUI_OPT=OFF
[[ ${NEED_SDL} -eq 1 ]] && SDL_OPT=ON
[[ ${NEED_TUI} -eq 1 ]] && TUI_OPT=ON

CMAKE_ARGS=(
    -S "${SRC_DIR}" -B "${BUILD_DIR}" -G Ninja
    -DCMAKE_BUILD_TYPE=Release
    -DCMAKE_INSTALL_PREFIX=/usr
    -DVIEWBBC_BUILD_SDL3="${SDL_OPT}"
    -DVIEWBBC_BUILD_NCURSES="${TUI_OPT}"
    -DVIEWBBC_BUILD_TESTS=ON
)
[[ ${NEED_SDL} -eq 1 ]] && CMAKE_ARGS+=( -DVIEWBBC_SDL3_SOURCE="${SDL3_DIR}" )
cmake "${CMAKE_ARGS[@]}" 2>&1 | tee -a "${LOG}"
cmake --build "${BUILD_DIR}" --parallel 2>&1 | tee -a "${LOG}"
ctest --test-dir "${BUILD_DIR}" --output-on-failure 2>&1 | tee -a "${LOG}"

GUI_BIN="${BUILD_DIR}/BeebView"
TUI_BIN="${BUILD_DIR}/BeebView-terminal"
[[ ${NEED_SDL} -eq 0 || -x "${GUI_BIN}" ]] || die "SDL binary not found: ${GUI_BIN}"
[[ ${NEED_TUI} -eq 0 || -x "${TUI_BIN}" ]] || die "terminal binary not found: ${TUI_BIN}"

# Diagnostic copies were useful while validating the package build, but are
# now opt-in so normal installer runs leave only distributable artifacts.
rm -rf "${DEBUG_DIR}"
if [[ ${DEBUG_BINARIES} -eq 1 ]]; then
    mkdir -p "${DEBUG_DIR}"
    if [[ ${NEED_SDL} -eq 1 ]]; then
        cp -p "${GUI_BIN}" "${DEBUG_DIR}/BeebView"
        chmod 755 "${DEBUG_DIR}/BeebView"
    fi
    if [[ ${NEED_TUI} -eq 1 ]]; then
        cp -p "${TUI_BIN}" "${DEBUG_DIR}/BeebView-terminal"
        chmod 755 "${DEBUG_DIR}/BeebView-terminal"
    fi
    ( cd "${DEBUG_DIR}" && sha256sum BeebView* > SHA256SUMS )
    log "Diagnostic build copies retained in installers/debug/ (--debug-binaries)"
fi

SDL_LIB=""
SDL_SONAME="libSDL3.so.0"
if [[ ${NEED_SDL} -eq 1 ]]; then
    SDL_LIB="$(find "${BUILD_DIR}/_deps/sdl3-build" -maxdepth 2 -type f -name 'libSDL3.so.0.*' | head -1 || true)"
    [[ -n "${SDL_LIB}" && -f "${SDL_LIB}" ]] || die "private SDL3 runtime library not found"
fi

ICON_DIR="${WORK_DIR}/icons"
mkdir -p "${ICON_DIR}"
ICON_PNG="${ICON_DIR}/beebview.png"
ICON_SVG="${ICON_DIR}/beebview.svg"
log "Generating Mode 7 BeebView icon..."
python3 "${SRC_DIR}/tools/make_beebview_icon.py" \
    --source "${SRC_DIR}/src/frontend/sdl3/bitmap_font.c" \
    --png "${ICON_PNG}" \
    --svg "${ICON_SVG}" 2>&1 | tee -a "${LOG}"

make_sdl_desktop_file() {
    local target="$1"
    cat > "${target}" <<'DESK'
[Desktop Entry]
Type=Application
Name=BeebView
Comment=Native reimplementation of Acornsoft VIEW for the BBC Micro
Exec=BeebView %f
TryExec=BeebView
Icon=beebview
Terminal=false
Categories=Utility;TextEditor;
MimeType=text/plain;text/x-log;text/x-csrc;text/x-chdr;text/x-c++src;text/x-c++hdr;application/x-shellscript;text/x-python;text/markdown;text/csv;application/json;application/xml;text/xml;text/html;
StartupNotify=true
DESK
}

make_terminal_desktop_file() {
    local target="$1"
    local exec_name="${2:-BeebView-terminal-launcher}"
    cat > "${target}" <<DESK
[Desktop Entry]
Type=Application
Name=BeebView Terminal
Comment=Run the terminal frontend of BeebView
Exec=${exec_name} %F
TryExec=${exec_name}
Icon=beebview-terminal
Terminal=false
Categories=Utility;TextEditor;
StartupNotify=false
DESK
}

write_terminal_launcher() {
    local target="$1"
    local program="$2"
    cat > "${target}" <<LAUNCH
#!/bin/sh
# Start BeebView Terminal in an explicit terminal emulator.  Cinnamon/Mint's
# handling of Desktop Entry Terminal=true can differ from an already-open
# terminal, so menu launches use this controlled path instead.
PROGRAM='${program}'

if command -v x-terminal-emulator >/dev/null 2>&1; then
    exec x-terminal-emulator -e "\${PROGRAM}" "\$@"
elif command -v gnome-terminal >/dev/null 2>&1; then
    exec gnome-terminal -- "\${PROGRAM}" "\$@"
elif command -v mate-terminal >/dev/null 2>&1; then
    exec mate-terminal -x "\${PROGRAM}" "\$@"
elif command -v xfce4-terminal >/dev/null 2>&1; then
    exec xfce4-terminal -x "\${PROGRAM}" "\$@"
elif command -v konsole >/dev/null 2>&1; then
    exec konsole -e "\${PROGRAM}" "\$@"
fi

echo "BeebView Terminal: no supported terminal emulator was found." >&2
exit 1
LAUNCH
    chmod 755 "${target}"
}

install_docs() {
    local root="$1"
    local docdir="$2"
    mkdir -p "${root}${docdir}"
    [[ -f "${SRC_DIR}/README.md" ]] && cp "${SRC_DIR}/README.md" "${root}${docdir}/"
    [[ -f "${SRC_DIR}/CHANGELOG.md" ]] && cp "${SRC_DIR}/CHANGELOG.md" "${root}${docdir}/"
    if [[ -d "${SRC_DIR}/LICENSES" ]]; then
        mkdir -p "${root}${docdir}/LICENSES"
        cp -a "${SRC_DIR}/LICENSES/." "${root}${docdir}/LICENSES/"
    fi
}

write_deb_maintainer_scripts() {
    local root="$1"
    cat > "${root}/DEBIAN/postinst" <<'POST'
#!/bin/sh
set -e
command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database -q /usr/share/applications || true
command -v gtk-update-icon-cache >/dev/null 2>&1 && gtk-update-icon-cache -q -t /usr/share/icons/hicolor || true
POST
    cat > "${root}/DEBIAN/postrm" <<'POST'
#!/bin/sh
set -e
command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database -q /usr/share/applications || true
command -v gtk-update-icon-cache >/dev/null 2>&1 && gtk-update-icon-cache -q -t /usr/share/icons/hicolor || true
POST
    chmod 755 "${root}/DEBIAN/postinst" "${root}/DEBIAN/postrm"
}

# ---------------------------------------------------------------------------
# Debian package — SDL frontend only.
# ---------------------------------------------------------------------------
if [[ ${BUILD_SDL_DEB} -eq 1 ]]; then
    log "--- Building SDL .deb ---"
    PKGROOT="${WORK_DIR}/deb-sdl-root"
    rm -rf "${PKGROOT}"
    mkdir -p \
        "${PKGROOT}/usr/bin" \
        "${PKGROOT}/usr/lib/beebview" \
        "${PKGROOT}/usr/share/applications" \
        "${PKGROOT}/usr/share/icons/hicolor/256x256/apps" \
        "${PKGROOT}/usr/share/icons/hicolor/scalable/apps" \
        "${PKGROOT}/DEBIAN"

    cp "${GUI_BIN}" "${PKGROOT}/usr/bin/BeebView"
    chmod 755 "${PKGROOT}/usr/bin/BeebView"
    cp "${SDL_LIB}" "${PKGROOT}/usr/lib/beebview/$(basename "${SDL_LIB}")"
    ln -s "$(basename "${SDL_LIB}")" "${PKGROOT}/usr/lib/beebview/${SDL_SONAME}"
    patchelf --set-rpath '$ORIGIN/../lib/beebview' "${PKGROOT}/usr/bin/BeebView"

    cp "${ICON_PNG}" "${PKGROOT}/usr/share/icons/hicolor/256x256/apps/beebview.png"
    cp "${ICON_SVG}" "${PKGROOT}/usr/share/icons/hicolor/scalable/apps/beebview.svg"
    make_sdl_desktop_file "${PKGROOT}/usr/share/applications/beebview.desktop"
    install_docs "${PKGROOT}" "/usr/share/doc/beebview-sdl"

    INSTALLED_KB="$(du -sk "${PKGROOT}" | awk '{print $1}')"
    cat > "${PKGROOT}/DEBIAN/control" <<CTRL
Package: beebview-sdl
Version: ${VERSION}
Architecture: ${DEB_ARCH}
Maintainer: BeebView maintainers
Installed-Size: ${INSTALLED_KB}
Depends: libc6
Section: editors
Priority: optional
Description: BeebView SDL graphical frontend
 Native clean-room implementation of Acornsoft VIEW using SDL3.
CTRL
    write_deb_maintainer_scripts "${PKGROOT}"

    SDL_DEB_OUT="${INSTALLER_DIR}/${SDL_DEB_NAME}"
    fakeroot dpkg-deb --build "${PKGROOT}" "${SDL_DEB_OUT}" 2>&1 | tee -a "${LOG}"
    [[ -f "${SDL_DEB_OUT}" ]] || die "SDL .deb not produced"
    log "SDL Debian package ready: installers/$(basename "${SDL_DEB_OUT}")"
fi

# ---------------------------------------------------------------------------
# Debian package — terminal frontend only.
# ---------------------------------------------------------------------------
if [[ ${BUILD_TUI_DEB} -eq 1 ]]; then
    log "--- Building terminal .deb ---"
    PKGROOT="${WORK_DIR}/deb-terminal-root"
    rm -rf "${PKGROOT}"
    mkdir -p \
        "${PKGROOT}/usr/bin" \
        "${PKGROOT}/usr/share/applications" \
        "${PKGROOT}/usr/share/icons/hicolor/256x256/apps" \
        "${PKGROOT}/usr/share/icons/hicolor/scalable/apps" \
        "${PKGROOT}/DEBIAN"

    cp "${TUI_BIN}" "${PKGROOT}/usr/bin/BeebView-terminal"
    chmod 755 "${PKGROOT}/usr/bin/BeebView-terminal"
    write_terminal_launcher "${PKGROOT}/usr/bin/BeebView-terminal-launcher" "/usr/bin/BeebView-terminal"
    cp "${ICON_PNG}" "${PKGROOT}/usr/share/icons/hicolor/256x256/apps/beebview-terminal.png"
    cp "${ICON_SVG}" "${PKGROOT}/usr/share/icons/hicolor/scalable/apps/beebview-terminal.svg"
    make_terminal_desktop_file "${PKGROOT}/usr/share/applications/beebview-terminal.desktop"
    install_docs "${PKGROOT}" "/usr/share/doc/beebview-terminal"

    INSTALLED_KB="$(du -sk "${PKGROOT}" | awk '{print $1}')"
    cat > "${PKGROOT}/DEBIAN/control" <<CTRL
Package: beebview-terminal
Version: ${VERSION}
Architecture: ${DEB_ARCH}
Maintainer: BeebView maintainers
Installed-Size: ${INSTALLED_KB}
Depends: libc6, libncursesw6 | libncurses6, libtinfo6
Section: editors
Priority: optional
Description: BeebView ncurses terminal frontend
 Native clean-room implementation of Acornsoft VIEW for terminal use.
CTRL
    write_deb_maintainer_scripts "${PKGROOT}"

    TUI_DEB_OUT="${INSTALLER_DIR}/${TUI_DEB_NAME}"
    fakeroot dpkg-deb --build "${PKGROOT}" "${TUI_DEB_OUT}" 2>&1 | tee -a "${LOG}"
    [[ -f "${TUI_DEB_OUT}" ]] || die "terminal .deb not produced"
    log "Terminal Debian package ready: installers/$(basename "${TUI_DEB_OUT}")"
fi

prepare_linuxdeploy() {
    local appimage_work="$1"
    local linuxdeploy="${appimage_work}/linuxdeploy-${APPIMAGE_ARCH}.AppImage"
    local extract="${appimage_work}/linuxdeploy-extract"
    mkdir -p "${appimage_work}"
    if [[ ! -f "${linuxdeploy}" ]] || [[ $(stat -c%s "${linuxdeploy}" 2>/dev/null || echo 0) -lt 1048576 ]]; then
        log "Downloading linuxdeploy..."
        curl -fL --retry 3 -o "${linuxdeploy}" \
            "https://github.com/linuxdeploy/linuxdeploy/releases/download/${LINUXDEPLOY_VERSION:-1-alpha-20251107-1}/linuxdeploy-x86_64.AppImage" \
            2>&1 | tee -a "${LOG}"
        chmod +x "${linuxdeploy}"
    fi
    # Optional but recommended: set LINUXDEPLOY_SHA256 (or provide
    # tools/linuxdeploy-x86_64.sha256) from a trusted source.
    local expected_sha="${LINUXDEPLOY_SHA256:-}"
    if [[ -z "${expected_sha}" && -f "${SRC_DIR}/tools/linuxdeploy-x86_64.sha256" ]]; then
        expected_sha="$(awk 'NF {print $1; exit}' "${SRC_DIR}/tools/linuxdeploy-x86_64.sha256")"
    fi
    if [[ -n "${expected_sha}" ]]; then
        [[ "${expected_sha}" =~ ^[0-9A-Fa-f]{64}$ ]] || die "Invalid linuxdeploy SHA-256 value"
        local actual_sha; actual_sha="$(sha256sum "${linuxdeploy}" | awk '{print $1}')"
        [[ "${actual_sha,,}" == "${expected_sha,,}" ]] || die "linuxdeploy SHA-256 verification failed"
        log "Verified linuxdeploy SHA-256."
    else
        log "WARNING: linuxdeploy checksum not configured; using pinned release ${LINUXDEPLOY_VERSION:-1-alpha-20251107-1}."
    fi
    log "Extracting linuxdeploy (avoids requiring FUSE)..."
    (
        cd "${appimage_work}"
        rm -rf squashfs-root "${extract}"
        APPIMAGE_EXTRACT_AND_RUN=1 "${linuxdeploy}" --appimage-extract 2>&1 | tee -a "${LOG}"
        mv squashfs-root "${extract}"
    )
    LINUXDEPLOY_RUN="${extract}/AppRun"
    # Use appimagetool directly for the final image.  linuxdeploy's bundled
    # output-plugin discovery has varied between releases and was the source
    # of silent/no-AppImage builds.  linuxdeploy still prepares the AppDir;
    # appimagetool performs the deterministic final packaging step.
    APPIMAGETOOL_RUN="$(find "${extract}" -type f -name appimagetool -perm -u+x -print -quit)"
    if [[ -z "${APPIMAGETOOL_RUN}" ]]; then
        APPIMAGETOOL_RUN="$(find "${extract}" -type f -name 'appimagetool*' -print -quit)"
    fi
    [[ -n "${APPIMAGETOOL_RUN}" && -f "${APPIMAGETOOL_RUN}" ]] || die "appimagetool not found inside linuxdeploy"
    chmod +x "${APPIMAGETOOL_RUN}"
}

# ---------------------------------------------------------------------------
# AppImage — SDL frontend with private SDL3 runtime bundled inside AppDir.
# ---------------------------------------------------------------------------
if [[ ${BUILD_SDL_APPIMAGE} -eq 1 ]]; then
    log "--- Building SDL AppImage ---"
    APPIMAGE_WORK="${WORK_DIR}/appimage-sdl"
    APPDIR="${APPIMAGE_WORK}/BeebView-SDL.AppDir"
    rm -rf "${APPIMAGE_WORK}"
    mkdir -p \
        "${APPDIR}/usr/bin" \
        "${APPDIR}/usr/lib" \
        "${APPDIR}/usr/share/applications" \
        "${APPDIR}/usr/share/icons/hicolor/256x256/apps" \
        "${APPDIR}/usr/share/icons/hicolor/scalable/apps"

    cp "${GUI_BIN}" "${APPDIR}/usr/bin/BeebView"
    chmod 755 "${APPDIR}/usr/bin/BeebView"
    cp "${SDL_LIB}" "${APPDIR}/usr/lib/$(basename "${SDL_LIB}")"
    ln -s "$(basename "${SDL_LIB}")" "${APPDIR}/usr/lib/${SDL_SONAME}"
    patchelf --set-rpath '$ORIGIN/../lib' "${APPDIR}/usr/bin/BeebView"

    make_sdl_desktop_file "${APPDIR}/usr/share/applications/beebview.desktop"
    cp "${ICON_PNG}" "${APPDIR}/usr/share/icons/hicolor/256x256/apps/beebview.png"
    cp "${ICON_SVG}" "${APPDIR}/usr/share/icons/hicolor/scalable/apps/beebview.svg"
    cp "${ICON_PNG}" "${APPDIR}/beebview.png"
    cp "${ICON_PNG}" "${APPDIR}/.DirIcon"
    cp "${APPDIR}/usr/share/applications/beebview.desktop" "${APPDIR}/beebview.desktop"
    install_docs "${APPDIR}" "/usr/share/doc/beebview-sdl"

    cat > "${APPDIR}/AppRun" <<'RUN'
#!/bin/sh
HERE="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
exec "${HERE}/usr/bin/BeebView" "$@"
RUN
    chmod 755 "${APPDIR}/AppRun"
    desktop-file-validate "${APPDIR}/usr/share/applications/beebview.desktop" || true

    prepare_linuxdeploy "${APPIMAGE_WORK}"
    SDL_APPIMAGE_OUT="${INSTALLER_DIR}/${SDL_APPIMAGE_NAME}"
    rm -f "${SDL_APPIMAGE_OUT}"
    log "Running linuxdeploy for SDL AppImage..."
    APPIMAGE_EXTRACT_AND_RUN=1 \
        "${LINUXDEPLOY_RUN}" \
        --appdir "${APPDIR}" \
        --desktop-file "${APPDIR}/usr/share/applications/beebview.desktop" \
        --icon-file "${ICON_PNG}" \
        2>&1 | tee -a "${LOG}"
    log "Packaging SDL AppDir with appimagetool..."
    ARCH="${APPIMAGE_ARCH}" VERSION="${VERSION}" \
        "${APPIMAGETOOL_RUN}" "${APPDIR}" "${SDL_APPIMAGE_OUT}" \
        2>&1 | tee -a "${LOG}"
    [[ -s "${SDL_APPIMAGE_OUT}" ]] || die "SDL AppImage not produced"
    chmod +x "${SDL_APPIMAGE_OUT}"
    log "SDL AppImage ready: installers/$(basename "${SDL_APPIMAGE_OUT}")"
fi

# ---------------------------------------------------------------------------
# AppImage — terminal frontend. Desktop entry requests a terminal, while the
# program's existing terminal-launch helper still covers direct GUI launches.
# ---------------------------------------------------------------------------
if [[ ${BUILD_TUI_APPIMAGE} -eq 1 ]]; then
    log "--- Building terminal AppImage ---"
    APPIMAGE_WORK="${WORK_DIR}/appimage-terminal"
    APPDIR="${APPIMAGE_WORK}/BeebView-Terminal.AppDir"
    rm -rf "${APPIMAGE_WORK}"
    mkdir -p \
        "${APPDIR}/usr/bin" \
        "${APPDIR}/usr/share/applications" \
        "${APPDIR}/usr/share/icons/hicolor/256x256/apps" \
        "${APPDIR}/usr/share/icons/hicolor/scalable/apps"

    cp "${TUI_BIN}" "${APPDIR}/usr/bin/BeebView-terminal"
    chmod 755 "${APPDIR}/usr/bin/BeebView-terminal"
    cat > "${APPDIR}/usr/share/applications/beebview-terminal.desktop" <<'DESK'
[Desktop Entry]
Type=Application
Name=BeebView Terminal
Comment=Run the terminal frontend of BeebView
Exec=BeebView-terminal %F
TryExec=BeebView-terminal
Icon=beebview-terminal
Terminal=true
Categories=Utility;TextEditor;
StartupNotify=false
DESK
    cp "${ICON_PNG}" "${APPDIR}/usr/share/icons/hicolor/256x256/apps/beebview-terminal.png"
    cp "${ICON_SVG}" "${APPDIR}/usr/share/icons/hicolor/scalable/apps/beebview-terminal.svg"
    cp "${ICON_PNG}" "${APPDIR}/beebview-terminal.png"
    cp "${ICON_PNG}" "${APPDIR}/.DirIcon"
    cp "${APPDIR}/usr/share/applications/beebview-terminal.desktop" "${APPDIR}/beebview-terminal.desktop"
    install_docs "${APPDIR}" "/usr/share/doc/beebview-terminal"

    cat > "${APPDIR}/AppRun" <<'RUN'
#!/bin/sh
HERE="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
exec "${HERE}/usr/bin/BeebView-terminal" "$@"
RUN
    chmod 755 "${APPDIR}/AppRun"
    desktop-file-validate "${APPDIR}/usr/share/applications/beebview-terminal.desktop" || true

    prepare_linuxdeploy "${APPIMAGE_WORK}"
    TUI_APPIMAGE_OUT="${INSTALLER_DIR}/${TUI_APPIMAGE_NAME}"
    rm -f "${TUI_APPIMAGE_OUT}"
    log "Running linuxdeploy for terminal AppImage..."
    APPIMAGE_EXTRACT_AND_RUN=1 \
        "${LINUXDEPLOY_RUN}" \
        --appdir "${APPDIR}" \
        --desktop-file "${APPDIR}/usr/share/applications/beebview-terminal.desktop" \
        --icon-file "${ICON_PNG}" \
        2>&1 | tee -a "${LOG}"
    log "Packaging terminal AppDir with appimagetool..."
    ARCH="${APPIMAGE_ARCH}" VERSION="${VERSION}" \
        "${APPIMAGETOOL_RUN}" "${APPDIR}" "${TUI_APPIMAGE_OUT}" \
        2>&1 | tee -a "${LOG}"
    [[ -s "${TUI_APPIMAGE_OUT}" ]] || die "terminal AppImage not produced"
    chmod +x "${TUI_APPIMAGE_OUT}"
    log "Terminal AppImage ready: installers/$(basename "${TUI_APPIMAGE_OUT}")"
fi

log ""
log "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
log " Build complete"
log "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
[[ ${BUILD_SDL_APPIMAGE} -eq 1 ]] && log " SDL AppImage : installers/${SDL_APPIMAGE_NAME}"
[[ ${BUILD_TUI_APPIMAGE} -eq 1 ]] && log " TUI AppImage : installers/${TUI_APPIMAGE_NAME}"
[[ ${BUILD_SDL_DEB} -eq 1 ]] && log " SDL .deb      : installers/${SDL_DEB_NAME}"
[[ ${BUILD_TUI_DEB} -eq 1 ]] && log " TUI .deb      : installers/${TUI_DEB_NAME}"
log " Icon          : generated from the embedded Mode 7/Bedstead bitmap"
log " Log           : installers/installer-build.log"
log "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

rm -rf "${WORK_DIR}"
if [[ ${KEEP_BUILD} -eq 1 ]]; then
    log "Build binaries : out/build/linux-installer/ (--keep-build)"
else
    rm -rf "${BUILD_DIR}"
    log "Temporary installer build tree cleaned."
fi
if [[ ${DEBUG_BINARIES} -eq 1 ]]; then
    log "Debug copies   : installers/debug/ (--debug-binaries)"
    [[ ${NEED_SDL} -eq 1 ]] && log " Test SDL       : ./installers/debug/BeebView"
    [[ ${NEED_TUI} -eq 1 ]] && log " Test Terminal  : ./installers/debug/BeebView-terminal"
fi

# Remove only the SDL3 source tree that this installer run unpacked
# automatically.  The selected source ZIP is retained for the next build,
# failed builds keep their source tree for diagnosis, and explicit override
# trees are never touched.
if [[ ${AUTO_SDL3} -eq 1 ]]; then
    "${SRC_DIR}/tools/cleanup_sdl3.sh" "${SRC_DIR}" "${SDL3_DIR}"
    log "Temporary extracted SDL3 source tree cleaned."
fi

if command -v xdg-open >/dev/null 2>&1 && [[ -n "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ]]; then
    xdg-open "${INSTALLER_DIR}" >/dev/null 2>&1 || true
fi
