#!/usr/bin/env bash
set -euo pipefail

sudo apt update
sudo apt install -y \
    cmake \
    ninja-build \
    build-essential \
    git \
    pkg-config \
    qt6-base-dev \
    qt6-declarative-dev \
    qt6-pdf-dev \
    qt6-tools-dev \
    libqt6sql6-sqlite \
    qtkeychain-qt6-dev \
    libcmark-gfm-dev \
    libcmark-gfm-extensions-dev \
    libssl-dev \
    libsecret-1-dev
