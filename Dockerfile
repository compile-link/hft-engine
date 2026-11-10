# syntax=docker/dockerfile:1.7
#
# Reproducible build environment for hft-engine.
#
# Stages:
#   toolchain - OS packages, Rust (pinned by rust-toolchain.toml), vcpkg at the
#               baseline pinned in vcpkg.json. Use as a dev/CI environment.
#   deps      - C++ dependencies from vcpkg.json, prebuilt into /opt/vcpkg_installed.
#               Only invalidated when vcpkg.json changes.
#   build     - configures, builds and tests the project with a preset.
#   runtime   - minimal image containing only the hft binary.
#
# Usage:
#   docker build -t hft-engine .                          # runtime image (runs tests)
#   docker build --target build --build-arg PRESET=release -t hft-engine:build .
#   docker build --target toolchain -t hft-engine:toolchain .

ARG UBUNTU_IMAGE=ubuntu:24.04@sha256:fe81560990fae86dc4743202b2cb4d3df98d0bcf949173cecef15ce1127ef852

# ---------------------------------------------------------------------------
FROM ${UBUNTU_IMAGE} AS toolchain

# Must match "builtin-baseline" in vcpkg.json (checked at build time below).
ARG VCPKG_COMMIT=9eec002df1cd109f8f104cd966a763daf9751489

ENV DEBIAN_FRONTEND=noninteractive \
    VCPKG_ROOT=/opt/vcpkg \
    VCPKG_DISABLE_METRICS=1 \
    RUSTUP_HOME=/opt/rustup \
    CARGO_HOME=/opt/cargo \
    PATH=/opt/cargo/bin:/opt/vcpkg:$PATH

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake ninja-build pkg-config \
        git curl ca-certificates zip unzip tar \
        perl autoconf automake libtool linux-libc-dev \
        clang-format clang-tidy \
    && rm -rf /var/lib/apt/lists/*

# Rust: install rustup without a default toolchain; the exact toolchain comes
# from rust-toolchain.toml so there is one source of truth for the version.
# The file must stay at /src: no default toolchain is configured, so rustup
# resolves the version from this file for every cargo invocation in /src and
# below (including /src/strategy_engine).
WORKDIR /src
# Fail a RUN if any command in a pipe fails, not just the last one: otherwise
# a failed rustup download piped into sh would go unnoticed.
SHELL ["/bin/bash", "-o", "pipefail", "-c"]
RUN curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs \
        | sh -s -- -y --no-modify-path --profile minimal --default-toolchain none
COPY rust-toolchain.toml ./
RUN rustup toolchain install && rustc --version && cargo --version

# vcpkg at the pinned baseline commit.
RUN git clone --filter=blob:none https://github.com/microsoft/vcpkg.git "$VCPKG_ROOT" \
    && git -C "$VCPKG_ROOT" checkout --quiet "$VCPKG_COMMIT" \
    && "$VCPKG_ROOT/bootstrap-vcpkg.sh" -disableMetrics

# ---------------------------------------------------------------------------
FROM toolchain AS deps

ARG VCPKG_COMMIT=9eec002df1cd109f8f104cd966a763daf9751489
WORKDIR /src
COPY vcpkg.json ./
RUN grep -q "\"builtin-baseline\": \"${VCPKG_COMMIT}\"" vcpkg.json \
        || { echo "vcpkg.json baseline does not match VCPKG_COMMIT=${VCPKG_COMMIT}" >&2; exit 1; }
RUN vcpkg install --x-install-root=/opt/vcpkg_installed --x-feature=tests \
    && rm -rf "$VCPKG_ROOT/buildtrees" "$VCPKG_ROOT/packages" "$VCPKG_ROOT/downloads"

# Prefetch Rust crates (pinned by Cargo.lock) so source edits don't refetch them.
# Cargo must be able to parse the manifest, which declares a lib target, so a
# stub lib.rs is needed here; the build stage overwrites it with the real source.
COPY strategy_engine/Cargo.toml strategy_engine/Cargo.lock strategy_engine/
RUN mkdir -p strategy_engine/src && touch strategy_engine/src/lib.rs
WORKDIR /src/strategy_engine
RUN cargo fetch --locked
WORKDIR /src

# ---------------------------------------------------------------------------
FROM deps AS build

ARG PRESET=relwithdebinfo
COPY . .
# Reuse the prebuilt dependencies instead of reinstalling into the build dir.
RUN cmake --preset "${PRESET}" -DVCPKG_INSTALLED_DIR=/opt/vcpkg_installed \
    && cmake --build --preset "${PRESET}" -j"$(nproc)" \
    && ctest --preset "${PRESET}" \
    && cp "build/${PRESET}/hft" /usr/local/bin/hft

# ---------------------------------------------------------------------------
FROM ${UBUNTU_IMAGE} AS runtime

# Dependencies are linked statically (vcpkg x64-linux triplet); only libc,
# libstdc++ and CA certificates (for the TLS WebSocket feed) are needed.
# Upgrade first: the pinned base digest can lag behind Ubuntu security
# updates, and this is the image that ships.
RUN apt-get update && apt-get upgrade -y \
    && apt-get install -y --no-install-recommends ca-certificates \
    && rm -rf /var/lib/apt/lists/* \
    && groupadd --system --gid 10001 hft \
    && useradd --system --uid 10001 --gid hft --no-create-home hft
COPY --from=build /usr/local/bin/hft /usr/local/bin/hft
# Numeric, so the user resolves without the image's /etc/passwd (e.g. under
# Kubernetes runAsNonRoot checks).
USER 10001:10001
ENTRYPOINT ["/usr/local/bin/hft"]
CMD ["--help"]
