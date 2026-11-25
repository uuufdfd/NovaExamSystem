#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
BUILD_DIR="${PROJECT_DIR}/build-kylin"

if ! command -v cmake >/dev/null 2>&1; then
    echo "cmake not found. Run deploy/kylin/install_deps_kylin.sh first."
    exit 1
fi

GENERATOR_ARGS=()
if command -v ninja >/dev/null 2>&1; then
    GENERATOR_ARGS=(-G Ninja)
fi

echo "Cleaning old build directory: ${BUILD_DIR}"
rm -rf "${BUILD_DIR}"

CMAKE_QT_ARGS=()
if command -v qtpaths6 >/dev/null 2>&1; then
    QT_PREFIX="$(qtpaths6 --install-prefix 2>/dev/null || true)"
    if [[ -n "${QT_PREFIX}" ]]; then
        CMAKE_QT_ARGS=(-DCMAKE_PREFIX_PATH="${QT_PREFIX}")
        echo "Using Qt prefix: ${QT_PREFIX}"
    fi
fi

if command -v qmake6 >/dev/null 2>&1; then
    qmake6 -v || true
elif command -v qmake >/dev/null 2>&1; then
    qmake -v || true
fi
cmake --version | head -n 1

cmake -S "${PROJECT_DIR}" -B "${BUILD_DIR}" \
    "${GENERATOR_ARGS[@]}" \
    "${CMAKE_QT_ARGS[@]}" \
    -DCMAKE_BUILD_TYPE=Release

cmake --build "${BUILD_DIR}" --parallel

echo "Build completed: ${BUILD_DIR}/QtExamSystem"
if command -v ldd >/dev/null 2>&1; then
    echo "Qt runtime libraries:"
    ldd "${BUILD_DIR}/QtExamSystem" | grep -E 'Qt6(Core|Widgets|Gui|Network|Xml|Multimedia)' || true
fi
