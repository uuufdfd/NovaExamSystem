-- AI 考试风险评分模块 MySQL/MariaDB 迁移脚本。
-- 正常情况下服务端启动会自动完成这些迁移；如果 Ubuntu 上已有旧库，
-- 也可以手动执行：
--   mysql -u qt_exam -p qt_exam_server < deploy/mysql_risk_migration.sql

SET @db_name = DATABASE();

SET @sql = (
  SELECT IF(
    COUNT(*) = 0,
    'ALTER TABLE submissions ADD COLUMN risk_score INTEGER DEFAULT 0',
    'SELECT 1'
  )
  FROM INFORMATION_SCHEMA.COLUMNS
  WHERE TABLE_SCHEMA = @db_name
    AND TABLE_NAME = 'submissions'
    AND COLUMN_NAME = 'risk_score'
);
PREPARE stmt FROM @sql;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

SET @sql = (
  SELECT IF(
    COUNT(*) = 0,
    'ALTER TABLE submissions ADD COLUMN risk_level VARCHAR(20) DEFAULT ''低风险''',
    'SELECT 1'
  )
  FROM INFORMATION_SCHEMA.COLUMNS
  WHERE TABLE_SCHEMA = @db_name
    AND TABLE_NAME = 'submissions'
    AND COLUMN_NAME = 'risk_level'
);
PREPARE stmt FROM @sql;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

CREATE TABLE IF NOT EXISTS exam_abnormal_logs (
    id INTEGER PRIMARY KEY AUTO_INCREMENT,
    exam_id INTEGER NOT NULL,
    student_id INTEGER NOT NULL,
    abnormal_type VARCHAR(40) NOT NULL,
    risk_score INTEGER NOT NULL,
    description LONGTEXT,
    create_time VARCHAR(40),
    FOREIGN KEY(exam_id) REFERENCES exams(id),
    FOREIGN KEY(student_id) REFERENCES users(id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS user_face_profiles (
    id INTEGER PRIMARY KEY AUTO_INCREMENT,
    user_id INTEGER NOT NULL UNIQUE,
    face_template LONGTEXT NOT NULL,
    updated_at VARCHAR(40) NOT NULL,
    FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

SET @sql = (
  SELECT IF(
    COUNT(*) = 0,
    'ALTER TABLE exams ADD COLUMN published_at VARCHAR(40)',
    'SELECT 1'
  )
  FROM INFORMATION_SCHEMA.COLUMNS
  WHERE TABLE_SCHEMA = @db_name
    AND TABLE_NAME = 'exams'
    AND COLUMN_NAME = 'published_at'
);
PREPARE stmt FROM @sql;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

SET @sql = (
  SELECT IF(
    COUNT(*) = 0,
    'ALTER TABLE exams ADD COLUMN closed_at VARCHAR(40)',
    'SELECT 1'
  )
  FROM INFORMATION_SCHEMA.COLUMNS
  WHERE TABLE_SCHEMA = @db_name
    AND TABLE_NAME = 'exams'
    AND COLUMN_NAME = 'closed_at'
);
PREPARE stmt FROM @sql;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

CREATE TABLE IF NOT EXISTS exam_publish_records (
    id INTEGER PRIMARY KEY AUTO_INCREMENT,
    exam_id INTEGER NOT NULL,
    teacher_id INTEGER NOT NULL,
    publish_time VARCHAR(40) NOT NULL,
    status VARCHAR(30) NOT NULL DEFAULT 'published',
    FOREIGN KEY(exam_id) REFERENCES exams(id) ON DELETE CASCADE,
    FOREIGN KEY(teacher_id) REFERENCES users(id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

DELETE FROM exam_publish_records
WHERE id NOT IN (
    SELECT min_id FROM (
        SELECT MIN(id) AS min_id FROM exam_publish_records GROUP BY exam_id
    ) kept
);

SET @sql = (
  SELECT IF(
    COUNT(*) = 0,
    'CREATE UNIQUE INDEX ux_exam_publish_records_exam_id ON exam_publish_records (exam_id)',
    'SELECT 1'
  )
  FROM INFORMATION_SCHEMA.STATISTICS
  WHERE TABLE_SCHEMA = @db_name
    AND TABLE_NAME = 'exam_publish_records'
    AND INDEX_NAME = 'ux_exam_publish_records_exam_id'
);
PREPARE stmt FROM @sql;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

INSERT IGNORE INTO exam_publish_records (exam_id, teacher_id, publish_time, status)
SELECT id, creator_id, COALESCE(published_at, created_at), 'published'
FROM exams
WHERE status = 'published';
