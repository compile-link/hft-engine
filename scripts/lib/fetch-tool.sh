# shellcheck shell=bash
# Shared helper for scripts that run a pinned release binary. Source it, then:
#
#   fetch_tool <name> <version> <url> <sha256> [<member>]
#
# Downloads <url> once into ${XDG_CACHE_HOME:-~/.cache}/hft-engine/<name>-<version>,
# verifies its SHA-256, and, if <member> is given, extracts that file from the
# tarball (.tar.gz or .tar.xz); otherwise the download itself is the binary.
# Sets TOOL_BIN to the executable path.
#
# Pinning by checksum, not just by version, means a replaced release asset is
# detected rather than silently run.

fetch_tool() {
    local name="$1" version="$2" url="$3" sha256="$4" member="${5:-}"
    local dir="${XDG_CACHE_HOME:-$HOME/.cache}/hft-engine/${name}-${version}"
    TOOL_BIN="$dir/$name"
    [[ -x "$TOOL_BIN" ]] && return 0

    local tmp
    tmp="$(mktemp -d)"
    # shellcheck disable=SC2064  # expand $tmp now; it is final.
    trap "rm -rf '$tmp'" RETURN

    local asset="$tmp/${url##*/}"
    echo "Downloading ${name} ${version}"
    curl -fsSL --retry 4 --retry-all-errors -o "$asset" "$url"
    if command -v sha256sum >/dev/null 2>&1; then
        echo "${sha256}  ${asset}" | sha256sum -c --quiet -
    else
        echo "${sha256}  ${asset}" | shasum -a 256 -c --quiet -
    fi

    mkdir -p "$dir"
    if [[ -n "$member" ]]; then
        tar -xf "$asset" -C "$tmp" "$member"
        mv "$tmp/$member" "$TOOL_BIN"
    else
        mv "$asset" "$TOOL_BIN"
    fi
    chmod +x "$TOOL_BIN"
}

# Maps uname output to one of: linux_x86_64 linux_aarch64 darwin_x86_64 darwin_aarch64
host_platform() {
    local os arch
    os="$(uname -s | tr '[:upper:]' '[:lower:]')"
    arch="$(uname -m)"
    case "$arch" in
        amd64) arch=x86_64 ;;
        arm64) arch=aarch64 ;;
    esac
    case "${os}_${arch}" in
        linux_x86_64|linux_aarch64|darwin_x86_64|darwin_aarch64) echo "${os}_${arch}" ;;
        *) echo "Unsupported platform: ${os}_${arch}" >&2; return 1 ;;
    esac
}
