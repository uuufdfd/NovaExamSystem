#!/usr/bin/env bash
set -euo pipefail

sudo apt update
sudo apt install -y \
  build-essential \
  cmake \
  ninja-build \
  qtbase5-dev \
  qtbase5-dev-tools \
  libqt5sql5-sqlite \
  libqt5sql5-mysql \
  mysql-server \
  mysql-client \
  python3

echo "Ubuntu dependencies installed."
