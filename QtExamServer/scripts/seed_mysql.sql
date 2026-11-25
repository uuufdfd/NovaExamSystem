-- =============================================================================
--  新星考试管理系统 · MySQL 种子账号脚本（可选）
--
--  说明：服务端首次连接到「空」的 qt_exam_server 库时，会自动建表并种子默认账号，
--        正常流程下无需手动执行本脚本。它用于：
--          - 不启动服务端，直接在 MySQL 里准备测试账号；
--          - 旧库缺账号时补齐。
--
--  幂等：基于 username 唯一键 + INSERT IGNORE，重复执行不会产生重复账号，
--        也不会覆盖已存在账号（含管理员后台改过的密码）。
--
--  密码与登录逻辑完全一致：
--        password_hash = lowercase( hex( SHA256( "qt-exam-system-v1:" + 明文密码 ) ) )
--        对应 src/database.cpp 的 Database::passwordHash。
--        本脚本未改动任何登录逻辑与 users 表字段含义，is_active=1 表示账号启用。
--
--  执行：mysql -u qt_exam -p qt_exam_server < scripts/seed_mysql.sql
-- =============================================================================

-- 明文对照（仅注释，便于核对，请勿在生产中保留弱口令）：
--   admin      / admin123
--   teacher001 / 123456
--   student001 / 123456
INSERT IGNORE INTO users (username, password_hash, display_name, role, note, is_active, created_at) VALUES
  ('admin',      '6514153e3355bcb000434e18803b9a1332dd56f8b8c3ee3f516d87a0bfd5067e', '系统管理员', 'admin',   '教务管理',      1, DATE_FORMAT(UTC_TIMESTAMP(), '%Y-%m-%dT%H:%i:%sZ')),
  ('teacher001', 'c556502559d4e9e5b78b0c5bdeaa42c6515ad5259a8381f15d7c83b7e07796d5', '演示教师甲',     'teacher', 'C++ 程序设计',  1, DATE_FORMAT(UTC_TIMESTAMP(), '%Y-%m-%dT%H:%i:%sZ')),
  ('student001', 'c556502559d4e9e5b78b0c5bdeaa42c6515ad5259a8381f15d7c83b7e07796d5', '演示学生甲',     'student', '演示班级', 1, DATE_FORMAT(UTC_TIMESTAMP(), '%Y-%m-%dT%H:%i:%sZ'));

-- 可选：若账号已存在但密码被改过、想强制恢复为文档口径的初始密码，
--      取消下面三句注释执行（注意：这会覆盖现有密码）：
-- UPDATE users SET password_hash='6514153e3355bcb000434e18803b9a1332dd56f8b8c3ee3f516d87a0bfd5067e', is_active=1 WHERE username='admin';
-- UPDATE users SET password_hash='c556502559d4e9e5b78b0c5bdeaa42c6515ad5259a8381f15d7c83b7e07796d5', is_active=1 WHERE username='teacher001';
-- UPDATE users SET password_hash='c556502559d4e9e5b78b0c5bdeaa42c6515ad5259a8381f15d7c83b7e07796d5', is_active=1 WHERE username='student001';

SELECT username, role, is_active FROM users WHERE username IN ('admin', 'teacher001', 'student001');
