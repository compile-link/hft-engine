# shellcheck shell=bash
# Pinned shellcheck binary, shared by scripts/shellcheck.sh and
# scripts/actionlint.sh so both lint shell with the same version.
# Source after scripts/lib/fetch-tool.sh; fetch_shellcheck sets SHELLCHECK.
# shellcheck disable=SC2034  # SHELLCHECK is consumed by the sourcing script.
SHELLCHECK_VERSION="0.11.0"

fetch_shellcheck() {
    local platform asset sha
    platform="$(host_platform)"
    case "$platform" in
        linux_x86_64)   sha=8c3be12b05d5c177a04c29e3c78ce89ac86f1595681cab149b65b97c4e227198 ;;
        linux_aarch64)  sha=12b331c1d2db6b9eb13cfca64306b1b157a86eb69db83023e261eaa7e7c14588 ;;
        darwin_x86_64)  sha=3c89db4edcab7cf1c27bff178882e0f6f27f7afdf54e859fa041fca10febe4c6 ;;
        darwin_aarch64) sha=56affdd8de5527894dca6dc3d7e0a99a873b0f004d7aabc30ae407d3f48b0a79 ;;
    esac
    asset="shellcheck-v${SHELLCHECK_VERSION}.${platform/_/.}.tar.xz"
    fetch_tool shellcheck "$SHELLCHECK_VERSION" \
        "https://github.com/koalaman/shellcheck/releases/download/v${SHELLCHECK_VERSION}/${asset}" \
        "$sha" "shellcheck-v${SHELLCHECK_VERSION}/shellcheck"
    SHELLCHECK="$TOOL_BIN"
}
