#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
STORE="${ROOT_DIR}/STORE"
mkdir -p "${STORE}"
move_path() {
    local rel="$1" src="${ROOT_DIR}/$1" dst="${STORE}/$1"
    [[ -e "$src" ]] || return 0
    mkdir -p "$(dirname "$dst")"
    if [[ -e "$dst" ]]; then
        printf 'STORE already contains %s; leaving source in place.\n' "$rel" >&2
        return 0
    fi
    mv "$src" "$dst"
    printf 'Moved %s -> STORE/%s\n' "$rel" "$rel"
}

# Normalize an older mixed-case local archive directory to the canonical STORE.
if [[ -d "${ROOT_DIR}/Store" && "${ROOT_DIR}/Store" != "${STORE}" ]]; then
    shopt -s dotglob nullglob
    for f in "${ROOT_DIR}/Store"/*; do
        name="$(basename "$f")"
        if [[ ! -e "${STORE}/${name}" ]]; then
            mv "$f" "${STORE}/${name}"
        fi
    done
    rmdir "${ROOT_DIR}/Store" 2>/dev/null || true
fi

# Archive accidental desktop/file-manager copies rather than redistribute them.
for rel in ".gitignore (copy)" "CHANGELOG (copy).md" "CMakeLists (copy).txt" "README (copy).md" "LICENSES (copy)"; do
    move_path "$rel"
done

# 0.5.78 briefly left a lowercase duplicate of the canonical Bedstead notices.
if [[ -d "${ROOT_DIR}/LICENSES/bedstead" && -d "${ROOT_DIR}/LICENSES/Bedstead" ]]; then
    move_path LICENSES/bedstead
fi

# Generated build/output trees are not source-distribution material.
move_path build
move_path build-asan
move_path out
# The Bedstead notices moved to LICENSES/Bedstead in 0.5.78. Preserve any
# pre-0.5.78 copy locally rather than leaving a duplicate distribution tree.
if [[ -d "${ROOT_DIR}/third_party/bedstead" && -f "${ROOT_DIR}/LICENSES/Bedstead/CC0-1.0.txt" ]]; then
    move_path third_party/bedstead
    rmdir "${ROOT_DIR}/third_party" 2>/dev/null || true
fi

# Historical Mode 7 font downloads are not used by the current build and
# have no established redistribution licence in this project.
move_path Mode7fonts
move_path Mode7fonts.zip
# Retain installers/ as the package output directory, but archive old outputs.
mkdir -p "${STORE}/installers"
shopt -s nullglob
for f in "${ROOT_DIR}"/installers/*.AppImage "${ROOT_DIR}"/installers/*.deb "${ROOT_DIR}"/installers/installer-build.log; do
    name="$(basename "$f")"
    if [[ ! -e "${STORE}/installers/${name}" ]]; then
        mv "$f" "${STORE}/installers/${name}"
        printf 'Moved installers/%s -> STORE/installers/%s\n' "$name" "$name"
    fi
done
printf 'Project storage organization complete. STORE is intentionally local/non-distribution material.\n'
