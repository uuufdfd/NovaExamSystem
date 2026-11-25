#!/usr/bin/env bash
set -euo pipefail

if ! command -v apt >/dev/null 2>&1; then
    echo "This script expects an apt-based Kylin/Ubuntu environment."
    echo "Please install Qt 6, CMake, a C++ compiler, Python 3, and OpenCV manually."
    exit 1
fi

if command -v cmake >/dev/null 2>&1 \
    && command -v g++ >/dev/null 2>&1 \
    && command -v python3 >/dev/null 2>&1 \
    && { pkg-config --exists Qt6Widgets 2>/dev/null || qmake6 -v >/dev/null 2>&1 || [[ -d /usr/lib/x86_64-linux-gnu/cmake/Qt6 ]]; }; then
    echo "Required build dependencies already exist; skipping apt install."
else
    sudo apt update || true
    sudo apt install -y --fix-missing \
        build-essential \
        cmake \
        ninja-build \
        qt6-base-dev \
        qt6-multimedia-dev \
        qt6-tools-dev \
        python3 \
        python3-pip \
        python3-venv \
        libgl1 \
        libxcb-cursor0 \
        libxcb-xinerama0
fi

OPTIONAL_PACKAGES=(
    v4l-utils
    gstreamer1.0-tools
    gstreamer1.0-plugins-base
    gstreamer1.0-plugins-good
    gstreamer1.0-libav
)

for package in "${OPTIONAL_PACKAGES[@]}"; do
    if ! dpkg -s "${package}" >/dev/null 2>&1; then
        sudo apt install -y "${package}" || echo "Warning: optional package ${package} was not installed."
    fi
done

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
VENV_DIR="${PROJECT_DIR}/.venv"

python3 -m venv "${VENV_DIR}"
if "${VENV_DIR}/bin/python" - <<'PY' >/dev/null 2>&1
import cv2
raise SystemExit(0 if hasattr(cv2, "FaceDetectorYN_create") and hasattr(cv2, "FaceRecognizerSF_create") else 1)
PY
then
    echo "Python OpenCV environment already exists; skipping pip install."
else
    "${VENV_DIR}/bin/python" -m pip install --upgrade pip
    "${VENV_DIR}/bin/python" -m pip install --upgrade numpy opencv-contrib-python
fi

echo "Kylin client dependencies are ready."
echo "Python virtual environment: ${VENV_DIR}"
