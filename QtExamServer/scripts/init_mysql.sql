-- =============================================================================
--  新星考试管理系统 · MySQL 初始化脚本（建库 + 建用户 + 授权）
--
--  用途：在 MySQL 中准备好服务端默认连接所需的数据库与账号。
--        数据表与默认账号由服务端「首次启动」时自动创建（见 src/database.cpp
--        的 Database::initialize / seedDefaultUsers），本脚本只负责库与账号。
--
--  执行方式（用 root 或具备建库/建用户权限的账号）：
--    Linux  : sudo mysql < scripts/init_mysql.sql
--    Windows: mysql -u root -p < scripts\init_mysql.sql
--
--  ⚠️ 安全提示：下面的密码 'CHANGE_ME_BEFORE_RUNNING' 仅为示例占位，请改成你自己的强密码，
--     并让服务端通过环境变量 QT_EXAM_DB_PASSWORD 使用同一个密码，不要把真实密码
--     硬编码进源码或提交到仓库。Linux 一键脚本见 deploy/init_mysql.sh。
-- =============================================================================

CREATE DATABASE IF NOT EXISTS `qt_exam_server`
  CHARACTER SET utf8mb4
  COLLATE utf8mb4_unicode_ci;

-- 服务端默认以 127.0.0.1:3306 通过 TCP 连接本机 MySQL。
-- 同时为 localhost(套接字) 与 127.0.0.1(TCP) 建账号，规避 MySQL skip_name_resolve
-- 开/关导致的主机名匹配差异。（远程客户端是连服务端 9000 端口，不直接连 MySQL）
CREATE USER IF NOT EXISTS 'qt_exam'@'localhost' IDENTIFIED BY 'CHANGE_ME_BEFORE_RUNNING';
CREATE USER IF NOT EXISTS 'qt_exam'@'127.0.0.1' IDENTIFIED BY 'CHANGE_ME_BEFORE_RUNNING';

-- 如改了示例密码，取消下面两句注释并替换为你的强密码即可同步：
-- ALTER USER 'qt_exam'@'localhost' IDENTIFIED BY '你的强密码';
-- ALTER USER 'qt_exam'@'127.0.0.1' IDENTIFIED BY '你的强密码';

GRANT ALL PRIVILEGES ON `qt_exam_server`.* TO 'qt_exam'@'localhost';
GRANT ALL PRIVILEGES ON `qt_exam_server`.* TO 'qt_exam'@'127.0.0.1';
FLUSH PRIVILEGES;

SELECT 'qt_exam_server 数据库与 qt_exam 用户已就绪。' AS result;
