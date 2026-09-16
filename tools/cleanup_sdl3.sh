#!/usr/bin/env bash
# Remove an SDL3 source tree that was unpacked automatically for a successful
# BeebView build.  This deliberately refuses to remove arbitrary paths.
set -euo pipefail

ROOT_DIR="${1:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
SDL_DIR="${2:-}"
SDL_ROOT="${ROOT_DIR}/sdl3"

[[ -n "${SDL_DIR}" ]] || {
    echo "ERROR: cleanup_sdl3.sh requires the extracted SDL3 directory." >&2
    exit 1
}

# Canonicalise without requiring the leaf to remain after validation.
root_real="$(cd "${SDL_ROOT}" && pwd -P)"
dir_parent="$(cd "$(dirname "${SDL_DIR}")" && pwd -P)"
dir_base="$(basename "${SDL_DIR}")"
dir_real="${dir_parent}/${dir_base}"

if [[ ! "${dir_base}" =~ ^SDL3-[0-9]+(\.[0-9]+)+$ ]]; then
    echo "ERROR: refusing to remove non-versioned SDL3 path: ${SDL_DIR}" >&2
    exit 1
fi

if [[ "${dir_parent}" != "${root_real}" ]]; then
    echo "ERROR: refusing to remove SDL3 source outside ${SDL_ROOT}: ${SDL_DIR}" >&2
    exit 1
fi

if [[ -d "${dir_real}" ]]; then
    rm -rf -- "${dir_real}"
    printf 'Removed temporary SDL3 source tree:\n  %s\n' "${dir_real}" >&2
fi
