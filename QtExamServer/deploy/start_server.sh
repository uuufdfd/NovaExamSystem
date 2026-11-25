#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_BIN="${PROJECT_DIR}/build/QtExamServer"
INSTALL_DIR="/opt/qt-exam-server"
SERVICE_NAME="qt-exam-server.service"
SERVICE_FILE="/etc/systemd/system/${SERVICE_NAME}"
ENV_FILE="/etc/qt-exam-server.env"

if [[ "${EUID}" -eq 0 ]]; then
  SUDO=""
else
  SUDO="sudo"
fi

log() {
  printf '%s\n' "$*"
}

ensure_build() {
  if [[ -x "${BUILD_BIN}" ]]; then
    return
  fi
  log "未找到服务端二进制，开始编译..."
  "${SCRIPT_DIR}/build_server.sh"
}

ensure_service_installed() {
  if [[ -f "${SERVICE_FILE}" && -x "${INSTALL_DIR}/QtExamServer" ]]; then
    return
  fi
  if [[ ! -f "${ENV_FILE}" ]]; then
    log "缺少环境文件：${ENV_FILE}"
    log "请先执行 deploy/init_mysql.sh，生成数据库和环境配置后再启动。"
    exit 1
  fi
  log "开始安装 systemd 服务..."
  "${SCRIPT_DIR}/install_service.sh"
}

ensure_build
ensure_service_installed

log "启动 QtExamServer 服务..."
${SUDO} systemctl enable --now "${SERVICE_NAME}"
${SUDO} systemctl --no-pager --full status "${SERVICE_NAME}"
log "QtExamServer 已启动，监听端口固定为 9000。"
