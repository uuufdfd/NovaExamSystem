#!/usr/bin/env bash
set -euo pipefail

DB_NAME="${QT_EXAM_DB_NAME:-qt_exam_server}"
DB_USER="${QT_EXAM_DB_USER:-qt_exam}"
DB_PASSWORD="${QT_EXAM_DB_PASSWORD:?Set QT_EXAM_DB_PASSWORD before initialization}"

# Reject characters unsafe for this SQL/environment-file template.
[[ "$DB_NAME" =~ ^[A-Za-z0-9_]+$ && "$DB_USER" =~ ^[A-Za-z0-9_]+$ ]] || exit 1
[[ "$DB_PASSWORD" =~ ^[A-Za-z0-9_@%+=.,:-]{16,}$ ]] || { echo "Use at least 16 supported password characters"; exit 1; }
sudo mysql <<SQL
CREATE DATABASE IF NOT EXISTS \`${DB_NAME}\`
  CHARACTER SET utf8mb4
  COLLATE utf8mb4_unicode_ci;
CREATE USER IF NOT EXISTS '${DB_USER}'@'localhost' IDENTIFIED BY '${DB_PASSWORD}';
CREATE USER IF NOT EXISTS '${DB_USER}'@'127.0.0.1' IDENTIFIED BY '${DB_PASSWORD}';
ALTER USER '${DB_USER}'@'localhost' IDENTIFIED BY '${DB_PASSWORD}';
ALTER USER '${DB_USER}'@'127.0.0.1' IDENTIFIED BY '${DB_PASSWORD}';
GRANT ALL PRIVILEGES ON \`${DB_NAME}\`.* TO '${DB_USER}'@'localhost';
GRANT ALL PRIVILEGES ON \`${DB_NAME}\`.* TO '${DB_USER}'@'127.0.0.1';
FLUSH PRIVILEGES;
SQL

sudo tee /etc/qt-exam-server.env >/dev/null <<EOF
QT_EXAM_DB_HOST=127.0.0.1
QT_EXAM_DB_PORT=3306
QT_EXAM_DB_NAME=${DB_NAME}
QT_EXAM_DB_USER=${DB_USER}
QT_EXAM_DB_PASSWORD=${DB_PASSWORD}
EOF
sudo chmod 600 /etc/qt-exam-server.env

echo "MySQL database initialized: ${DB_NAME}"
echo "MySQL user initialized: ${DB_USER}"
echo "Environment file written: /etc/qt-exam-server.env"
