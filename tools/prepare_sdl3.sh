#!/usr/bin/env bash
# Select and unpack the newest bundled SDL3 source release.
#
# Source archives are expected to be named exactly SDL3-X.Y.Z.zip.  Put new
# releases directly in ./sdl3; ./sdl3/downloads remains supported for older
# project trees.  Platform/runtime archives such as SDL3-X.Y.Z-win32-x64.zip
# and SDL3-devel-X.Y.Z-mingw.tar.gz are deliberately ignored.
set -euo pipefail

ROOT_DIR="${1:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
SDL_ROOT="${ROOT_DIR}/sdl3"

[[ -d "${SDL_ROOT}" ]] || {
    echo "ERROR: SDL3 directory not found: ${SDL_ROOT}" >&2
    exit 1
}

command -v unzip >/dev/null 2>&1 || {
    echo "ERROR: 'unzip' is required to unpack the bundled SDL3 source archive." >&2
    exit 1
}

latest_archive=""
latest_version=""

consider_archive() {
    local archive="$1"
    local base version newest
    base="$(basename "${archive}")"
    if [[ ! "${base}" =~ ^SDL3-([0-9]+(\.[0-9]+)+)\.zip$ ]]; then
        return 0
    fi
    version="${BASH_REMATCH[1]}"

    if [[ -z "${latest_version}" ]]; then
        latest_version="${version}"
        latest_archive="${archive}"
        return 0
    fi

    newest="$(printf '%s\n%s\n' "${latest_version}" "${version}" | sort -V | tail -n 1)"
    if [[ "${newest}" == "${version}" ]]; then
        # Equal versions found in both places: prefer the archive directly in
        # ./sdl3, which is the documented drop-in location.
        if [[ "${version}" != "${latest_version}" || "$(dirname "${archive}")" == "${SDL_ROOT}" ]]; then
            latest_version="${version}"
            latest_archive="${archive}"
        fi
    fi
}

# Scan the legacy location first, then the preferred drop-in directory so an
# equal-version archive in ./sdl3 wins.
if [[ -d "${SDL_ROOT}/downloads" ]]; then
    while IFS= read -r -d '' archive; do
        consider_archive "${archive}"
    done < <(find "${SDL_ROOT}/downloads" -maxdepth 1 -type f -name 'SDL3-*.zip' -print0)
fi
while IFS= read -r -d '' archive; do
    consider_archive "${archive}"
done < <(find "${SDL_ROOT}" -maxdepth 1 -type f -name 'SDL3-*.zip' -print0)

if [[ -z "${latest_archive}" ]]; then
    cat >&2 <<MSG
ERROR: no SDL3 source archive was found.

Drop an SDL3 source release into:
  ${SDL_ROOT}/SDL3-X.Y.Z.zip

Only source archives named exactly SDL3-X.Y.Z.zip are considered.
MSG
    exit 1
fi

# Validate the archive before removing any existing extracted source tree.
if ! unzip -tq "${latest_archive}" >/dev/null; then
    echo "ERROR: SDL3 archive failed its ZIP integrity check: ${latest_archive}" >&2
    exit 1
fi


# Reject path traversal, absolute paths, backslashes and unexpected top-level
# content before extraction.  The archive must contain only SDL3-X.Y.Z/...
expected_prefix="SDL3-${latest_version}/"
while IFS= read -r member; do
    [[ -n "${member}" ]] || continue
    if [[ "${member}" == /* || "${member}" == *\\* || "${member}" == ../* || "${member}" == */../* || "${member}" != "${expected_prefix}"* ]]; then
        echo "ERROR: unsafe/unexpected path in SDL3 archive: ${member}" >&2
        exit 1
    fi
done < <(unzip -Z1 "${latest_archive}")

# Optional trusted checksum: put a sibling SDL3-X.Y.Z.zip.sha256 file beside
# the archive.  If present it is mandatory and is checked before extraction.
checksum_file="${latest_archive}.sha256"
if [[ -f "${checksum_file}" ]]; then
    command -v sha256sum >/dev/null 2>&1 || { echo "ERROR: sha256sum is required to verify ${checksum_file}" >&2; exit 1; }
    expected="$(awk 'NF {print $1; exit}' "${checksum_file}")"
    [[ "${expected}" =~ ^[0-9A-Fa-f]{64}$ ]] || { echo "ERROR: invalid SDL3 SHA-256 file: ${checksum_file}" >&2; exit 1; }
    actual="$(sha256sum "${latest_archive}" | awk '{print $1}')"
    [[ "${actual,,}" == "${expected,,}" ]] || { echo "ERROR: SDL3 SHA-256 mismatch for ${latest_archive}" >&2; exit 1; }
    echo "Verified SDL3 SHA-256." >&2
else
    echo "WARNING: no SDL3 SHA-256 sidecar found (${checksum_file}); trusting this local source archive." >&2
fi

# Remove only version-shaped SDL3 source directories.  Never touch archives,
# downloads/, platform packages, or unrelated files under sdl3/.
while IFS= read -r -d '' dir; do
    base="$(basename "${dir}")"
    if [[ "${base}" =~ ^SDL3-[0-9]+(\.[0-9]+)+$ ]]; then
        rm -rf -- "${dir}"
    fi
done < <(find "${SDL_ROOT}" -maxdepth 1 -mindepth 1 -type d -name 'SDL3-*' -print0)

# Also remove an old extracted source tree in the historical downloads/
# location, while leaving all downloaded archives intact.
if [[ -d "${SDL_ROOT}/downloads" ]]; then
    while IFS= read -r -d '' dir; do
        base="$(basename "${dir}")"
        if [[ "${base}" =~ ^SDL3-[0-9]+(\.[0-9]+)+$ ]]; then
            rm -rf -- "${dir}"
        fi
    done < <(find "${SDL_ROOT}/downloads" -maxdepth 1 -mindepth 1 -type d -name 'SDL3-*' -print0)
fi

printf 'Preparing SDL3 %s from:\n  %s\n' "${latest_version}" "${latest_archive}" >&2
unzip -q "${latest_archive}" -d "${SDL_ROOT}"

SDL_DIR="${SDL_ROOT}/SDL3-${latest_version}"
if [[ ! -f "${SDL_DIR}/CMakeLists.txt" ]]; then
    echo "ERROR: archive did not produce the expected SDL3 source tree:" >&2
    echo "  ${SDL_DIR}/CMakeLists.txt" >&2
    exit 1
fi

# stdout is intentionally just the path so callers can use command substitution.
printf '%s\n' "${SDL_DIR}"
