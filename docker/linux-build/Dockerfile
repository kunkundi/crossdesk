# syntax=docker/dockerfile:1.7

ARG BASE_IMAGE=nvidia/cuda:12.6.3-devel-ubuntu20.04
FROM ${BASE_IMAGE}

ARG TARGETARCH
ARG CMAKE_VERSION=3.31.6
ARG XMAKE_VERSION=3.0.9
ARG RUST_VERSION=1.92.0
ARG RUSTUP_VERSION=1.28.2

LABEL org.opencontainers.image.title="CrossDesk Linux build environment" \
      org.opencontainers.image.description="Ubuntu 20.04 compatibility build environment for CrossDesk" \
      org.opencontainers.image.source="https://github.com/kunkundi/crossdesk" \
      org.opencontainers.image.licenses="GPL-3.0-only"

ENV DEBIAN_FRONTEND=noninteractive \
    TZ=Etc/UTC \
    LANG=C.UTF-8 \
    LC_ALL=C.UTF-8 \
    RUSTUP_HOME=/opt/rustup \
    CARGO_HOME=/opt/cargo \
    XMAKE_GLOBALDIR=/data \
    XMAKE_ROOT=y \
    CUDA_PATH=/usr/local/cuda \
    CC=/usr/bin/gcc-10 \
    CXX=/usr/bin/g++-10 \
    PATH=/root/.local/bin:/opt/cargo/bin:/usr/local/bin:${PATH}

SHELL ["/bin/bash", "-o", "pipefail", "-c"]

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        autoconf \
        automake \
        binutils \
        bison \
        build-essential \
        ca-certificates \
        ccache \
        curl \
        dbus \
        dpkg-dev \
        file \
        flex \
        gettext \
        gcc-10 \
        g++-10 \
        git \
        libasound2-dev \
        libdbus-1-dev \
        libdrm-dev \
        libegl1-mesa-dev \
        libffi-dev \
        libfontconfig1-dev \
        libfreetype6-dev \
        libgl1-mesa-dev \
        libmount-dev \
        libpulse-dev \
        libreadline-dev \
        libsndio-dev \
        libssl-dev \
        libtool \
        libx11-dev \
        libxcb-xfixes0-dev \
        libxcb-randr0-dev \
        libxcb-shape0-dev \
        libxcb-shm0-dev \
        libxcb-xinerama0-dev \
        libxcb-xkb-dev \
        libxcb-xtest0-dev \
        libxcursor-dev \
        libxext-dev \
        libxfixes-dev \
        libxft-dev \
        libxi-dev \
        libxinerama-dev \
        libxkbcommon-dev \
        libxrandr-dev \
        libxrender-dev \
        libxtst-dev \
        libxv-dev \
        make \
        nasm \
        ninja-build \
        patch \
        patchelf \
        perl \
        pkg-config \
        python3 \
        python3-pip \
        unzip \
        xutils-dev \
        xz-utils \
        yasm \
        zip \
        zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*

# Slint 1.17 requires CMake 3.21 or newer, while Ubuntu 20.04 provides 3.16.
# PyPI publishes pinned manylinux2014 wheels for both amd64 and arm64.
RUN python3 -m pip install --no-cache-dir "cmake==${CMAKE_VERSION}" \
    && cmake --version | grep -F "cmake version ${CMAKE_VERSION}"

# Ubuntu 20.04 only ships PipeWire 0.2. Install the Ubuntu 22.04-era 0.3.48
# headers as a build-only SDK. CrossDesk loads the host's PipeWire 0.3 runtime
# with dlopen(), so no PipeWire shared library is copied into release packages.
COPY docker/linux-build/install-pipewire-sdk.sh \
    /usr/local/bin/install-crossdesk-pipewire-sdk

RUN chmod 0755 /usr/local/bin/install-crossdesk-pipewire-sdk \
    && install-crossdesk-pipewire-sdk \
    && grep -F "#define PW_MICRO 48" \
        /opt/crossdesk-pipewire-sdk/include/pipewire-0.3/pipewire/version.h

RUN export GITHUB_ACTIONS=true \
    && curl -fsSL https://xmake.io/shget.text \
        | bash -s -- "v${XMAKE_VERSION}" \
    && source ~/.xmake/profile \
    && xmake --version | grep -F "v${XMAKE_VERSION}"

RUN case "${TARGETARCH}" in \
        amd64) \
            rustup_arch="x86_64-unknown-linux-gnu"; \
            rustup_sha256="20a06e644b0d9bd2fbdbfd52d42540bdde820ea7df86e92e533c073da0cdd43c" \
            ;; \
        arm64) \
            rustup_arch="aarch64-unknown-linux-gnu"; \
            rustup_sha256="e3853c5a252fca15252d07cb23a1bdd9377a8c6f3efa01531109281ae47f841c" \
            ;; \
        *) echo "Unsupported architecture: ${TARGETARCH}" >&2; exit 1 ;; \
    esac \
    && curl --proto '=https' --tlsv1.2 --fail --show-error --location \
        "https://static.rust-lang.org/rustup/archive/${RUSTUP_VERSION}/${rustup_arch}/rustup-init" \
        -o /tmp/rustup-init \
    && echo "${rustup_sha256}  /tmp/rustup-init" | sha256sum --check - \
    && chmod 0755 /tmp/rustup-init \
    && /tmp/rustup-init -y --no-modify-path --profile minimal \
        --default-toolchain "${RUST_VERSION}" \
    && rm -f /tmp/rustup-init \
    && rustc --version \
    && cargo --version

WORKDIR /tmp/crossdesk-dependencies

COPY xmake.lua ./xmake.lua
COPY apps/desktop/xmake ./apps/desktop/xmake
COPY deps/thirdparty ./deps/thirdparty
COPY deps/submodules/xmake.lua ./deps/submodules/xmake.lua
COPY deps/submodules/minirtc/xmake.lua ./deps/submodules/minirtc/xmake.lua
COPY deps/submodules/minirtc/thirdparty ./deps/submodules/minirtc/thirdparty

# Configure the real project manifests so the image cache stays aligned with
# CrossDesk's exact dependency graph. Source files are intentionally excluded
# from the Docker context; source-only changes therefore reuse this layer.
RUN xmake repo -u --root \
    && xmake f -m release --USE_CUDA=true --USE_WAYLAND=true \
        --cc=gcc-10 --cxx=g++-10 --root -y \
    && rm -rf /data/.xmake/cache /tmp/crossdesk-dependencies \
    && mkdir -p /workspace

WORKDIR /workspace

COPY docker/linux-build/verify-build-image.sh \
    /usr/local/bin/verify-crossdesk-build-image
COPY docker/linux-build/verify-glibc-baseline.sh \
    /usr/local/bin/verify-crossdesk-glibc-baseline

RUN chmod 0755 \
        /usr/local/bin/verify-crossdesk-build-image \
        /usr/local/bin/verify-crossdesk-glibc-baseline \
    && verify-crossdesk-build-image

CMD ["bash"]
