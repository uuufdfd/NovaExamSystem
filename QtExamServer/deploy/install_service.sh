#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_BIN="${PROJECT_DIR}/build/QtExamServer"
INSTALL_DIR="/opt/qt-exam-server"
SERVICE_FILE="/etc/systemd/system/qt-exam-server.service"
ENV_FILE="/etc/qt-exam-server.env"

if [[ ! -x "${BUILD_BIN}" ]]; then
  echo "Server binary not found: ${BUILD_BIN}"
  echo "Run deploy/build_server.sh first."
  exit 1
fi

if ! id qt-exam >/dev/null 2>&1; then
  sudo useradd --system --home "${INSTALL_DIR}" --shell /usr/sbin/nologin qt-exam
fi

sudo mkdir -p "${INSTALL_DIR}"

if [[ ! -f "${ENV_FILE}" ]]; then
  echo "Environment file not found: ${ENV_FILE}"
  echo "Run deploy/init_mysql.sh first, or create ${ENV_FILE} manually."
  exit 1
fi

sudo systemctl stop qt-exam-server.service 2>/dev/null || true
sudo cp "${SCRIPT_DIR}/qt-exam-server.service" "${SERVICE_FILE}"
sudo cp "${BUILD_BIN}" "${INSTALL_DIR}/QtExamServer"
sudo chown -R qt-exam:qt-exam "${INSTALL_DIR}"
sudo chmod 755 "${INSTALL_DIR}/QtExamServer"
sudo systemctl daemon-reload
sudo systemctl enable --now qt-exam-server.service

echo "qt-exam-server.service 已设置为开机自启。"
sudo systemctl is-enabled qt-exam-server.service
sudo systemctl status qt-exam-server.service --no-pager
