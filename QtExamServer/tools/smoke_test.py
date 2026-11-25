#!/usr/bin/env python3
import argparse
from concurrent.futures import ThreadPoolExecutor
import datetime as dt
import json
import os
import socket
import sqlite3
import subprocess
import time


ALREADY_PUBLISHED = "您已发布此次考试"


def request(sock, payload, expect_success=True):
    sock.sendall(json.dumps(payload, ensure_ascii=False).encode("utf-8") + b"\n")
    data = b""
    while not data.endswith(b"\n"):
        chunk = sock.recv(65536)
        if not chunk:
            raise RuntimeError("server closed connection")
        data += chunk
    response = json.loads(data.decode("utf-8"))
    print(json.dumps(response, ensure_ascii=False, indent=2))
    success = bool(response.get("success"))
    if expect_success is True and not success:
        raise RuntimeError(response.get("error", "request failed"))
    if expect_success is False and success:
        raise RuntimeError("request unexpectedly succeeded")
    return response


def single_request(host, port, payload):
    with socket.create_connection((host, port), timeout=5) as sock:
        return request(sock, payload, expect_success=None)


def iso_utc(offset_minutes=0):
    value = dt.datetime.now(dt.UTC) + dt.timedelta(minutes=offset_minutes)
    return value.replace(microsecond=0).strftime("%Y-%m-%dT%H:%M:%SZ")


def login(sock, username, password, role):
    return request(sock, {
        "type": "login",
        "username": username,
        "password": password,
        "role": role,
    })["token"]


def with_token(token, payload):
    payload = dict(payload)
    payload["token"] = token
    return payload


def publish_record_count(args, exam_id):
    if args.sqlite_db:
        with sqlite3.connect(args.sqlite_db) as conn:
            row = conn.execute(
                "SELECT COUNT(*) FROM exam_publish_records WHERE exam_id = ?",
                (exam_id,),
            ).fetchone()
        return int(row[0])
    if args.mysql_db:
        env = os.environ.copy()
        if args.mysql_password:
            env["MYSQL_PWD"] = args.mysql_password
        cmd = [
            "mysql",
            "--batch",
            "--skip-column-names",
            "-h",
            args.mysql_host,
            "-P",
            str(args.mysql_port),
            "-u",
            args.mysql_user,
            args.mysql_db,
            "-e",
            "SELECT COUNT(*) FROM exam_publish_records WHERE exam_id = {};".format(int(exam_id)),
        ]
        result = subprocess.run(cmd, env=env, check=True, capture_output=True, text=True)
        return int(result.stdout.strip())
    return None


def assert_publish_count(args, exam_id, expected):
    count = publish_record_count(args, exam_id)
    if count is not None:
        assert count == expected, f"exam_id={exam_id} publish record count is {count}, expected {expected}"


def create_exam(sock, teacher_token, exam_name, question_ids):
    exam = request(sock, with_token(teacher_token, {
        "type": "create_exam",
        "exam": {
            "name": exam_name,
            "course": "C++ 程序设计",
            "startAt": iso_utc(-5),
            "endAt": iso_utc(1440),
            "durationMinutes": 90,
            "maxSubmissions": 1,
        },
        "questionIds": question_ids,
    }))
    return exam["examId"]


def main():
    parser = argparse.ArgumentParser(description="QtExamServer full workflow smoke test")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=9000)
    parser.add_argument("--sqlite-db", help="SQLite database path used by the server; enables publish-record count checks")
    parser.add_argument("--mysql-host", default="127.0.0.1")
    parser.add_argument("--mysql-port", type=int, default=3306)
    parser.add_argument("--mysql-db", help="MySQL database name used by the server; enables publish-record count checks")
    parser.add_argument("--mysql-user", default="qt_exam")
    parser.add_argument("--mysql-password", default=os.environ.get("QT_EXAM_DB_PASSWORD", ""))
    args = parser.parse_args()

    suffix = str(int(time.time()))
    student_username = "smoke_student_{}".format(suffix)
    teacher_username = "smoke_teacher_{}".format(suffix)
    exam_name = "烟测考试_{}".format(suffix)

    with socket.create_connection((args.host, args.port), timeout=5) as sock:
        request(sock, {"type": "ping"})

        admin_token = login(sock, "admin", "admin123", "admin")
        teacher_token = login(sock, "teacher001", "123456", "teacher")

        request(sock, with_token(admin_token, {
            "type": "create_user",
            "username": student_username,
            "password": "123456",
            "displayName": "烟测学生",
            "role": "student",
            "note": "smoke",
        }))
        request(sock, with_token(admin_token, {
            "type": "create_user",
            "username": teacher_username,
            "password": "123456",
            "displayName": "烟测教师",
            "role": "teacher",
            "note": "delete-me",
        }))
        users = request(sock, with_token(admin_token, {
            "type": "list_users",
            "roleFilter": "all",
        }))["users"]
        smoke_student = next(u for u in users if u["username"] == student_username)
        smoke_teacher = next(u for u in users if u["username"] == teacher_username)
        request(sock, with_token(admin_token, {
            "type": "update_user",
            "userId": smoke_teacher["id"],
            "displayName": "烟测教师改名",
            "role": "teacher",
            "note": "updated",
        }))
        request(sock, with_token(admin_token, {
            "type": "reset_password",
            "userId": smoke_teacher["id"],
            "newPassword": "654321",
        }))
        request(sock, with_token(admin_token, {
            "type": "delete_user",
            "userId": smoke_teacher["id"],
        }))

        request(sock, with_token(teacher_token, {
            "type": "import_questions",
            "questions": [
                {
                    "type": "程序题",
                    "course": "C++ 程序设计",
                    "content": "定义函数指针类型并调用加法函数。",
                    "answer": "typedef 或 using 定义函数指针，传入 add 后调用。",
                    "score": 30,
                    "difficulty": "中等",
                },
                {
                    "type": "简答题",
                    "course": "C++ 程序设计",
                    "content": "说明深拷贝为什么能避免重复释放。",
                    "answer": "每个对象拥有独立内存，析构时不会释放同一地址。",
                    "score": 20,
                    "difficulty": "中等",
                },
            ],
        }))
        questions = request(sock, with_token(teacher_token, {
            "type": "list_questions",
        }))["questions"]
        question_ids = [questions[0]["id"], questions[1]["id"]]

        exam_id = create_exam(sock, teacher_token, exam_name, question_ids)
        assert_publish_count(args, exam_id, 0)

        teacher_exams = request(sock, with_token(teacher_token, {
            "type": "list_exams",
        }))["exams"]
        created_exam = next(e for e in teacher_exams if e["id"] == exam_id)
        assert created_exam["publishStatus"] == "unpublished", "new exam should be unpublished"

        student_token = login(sock, student_username, "123456", "student")
        unpublished_student_exams = request(sock, with_token(student_token, {
            "type": "list_exams",
        }))["exams"]
        assert not any(e["id"] == exam_id for e in unpublished_student_exams), "unpublished exam is visible to student"

        first_publish = request(sock, with_token(teacher_token, {
            "type": "publish_exam",
            "examId": exam_id,
        }))
        assert first_publish.get("message") == "试卷发布成功", "first publish did not return success message"
        assert_publish_count(args, exam_id, 1)

        duplicate_publish = request(sock, with_token(teacher_token, {
            "type": "publish_exam",
            "examId": exam_id,
        }), expect_success=False)
        assert duplicate_publish.get("error") == ALREADY_PUBLISHED, "duplicate publish message mismatch"
        assert_publish_count(args, exam_id, 1)

        fast_exam_id = create_exam(sock, teacher_token, exam_name + "_快速连点", question_ids)
        payload = with_token(teacher_token, {
            "type": "publish_exam",
            "examId": fast_exam_id,
        })
        with ThreadPoolExecutor(max_workers=2) as pool:
            fast_results = list(pool.map(lambda _: single_request(args.host, args.port, payload), range(2)))
        fast_successes = [r for r in fast_results if r.get("success")]
        fast_duplicates = [r for r in fast_results if not r.get("success") and r.get("error") == ALREADY_PUBLISHED]
        assert len(fast_successes) == 1 and len(fast_duplicates) == 1, "rapid duplicate publish was not blocked"
        assert_publish_count(args, fast_exam_id, 1)

        exams = request(sock, with_token(student_token, {
            "type": "list_exams",
        }))["exams"]
        assert any(e["id"] == exam_id for e in exams), "student cannot see published exam"
        assert any(e["id"] == fast_exam_id for e in exams), "student cannot see rapid-published exam"

        detail = request(sock, with_token(student_token, {
            "type": "get_exam_detail",
            "examId": exam_id,
        }))
        risk = request(sock, with_token(student_token, {
            "type": "record_abnormal_event",
            "examId": exam_id,
            "abnormalType": "SCREEN_SWITCH",
            "description": "烟测：切屏次数过多",
        }))
        assert risk["addedScore"] == 6 and risk["riskLevel"] == "低风险", "risk scoring failed"
        answers = [
            {
                "questionId": q["id"],
                "answer": "第 {} 题烟测答案".format(idx + 1),
            }
            for idx, q in enumerate(detail["questions"])
        ]
        request(sock, with_token(student_token, {
            "type": "save_draft",
            "examId": exam_id,
            "answers": answers,
        }))
        request(sock, with_token(student_token, {
            "type": "submit_exam",
            "examId": exam_id,
            "answers": answers,
        }))

        submissions = request(sock, with_token(teacher_token, {
            "type": "list_gradable_submissions",
            "statusFilter": "pending",
        }))["submissions"]
        submission = next(s for s in submissions if s["examId"] == exam_id and s["studentUsername"] == student_username)
        submission_id = submission["id"]
        request(sock, with_token(teacher_token, {
            "type": "lock_submission",
            "submissionId": submission_id,
        }))
        submission_detail = request(sock, with_token(teacher_token, {
            "type": "get_submission_detail",
            "submissionId": submission_id,
        }))
        grades = [
            {
                "answerId": answer["answerId"],
                "score": answer["maxScore"],
                "comment": "烟测通过",
            }
            for answer in submission_detail["answers"]
        ]
        request(sock, with_token(teacher_token, {
            "type": "submit_grade",
            "submissionId": submission_id,
            "answerGrades": grades,
            "comment": "完整业务链路烟测通过",
        }))

        mine = request(sock, with_token(student_token, {
            "type": "list_my_submissions",
        }))["submissions"]
        assert any(s["id"] == submission_id and s["status"] == "graded" for s in mine), "graded submission not visible to student"

        risk_records = request(sock, with_token(teacher_token, {
            "type": "list_risk_records",
        }))["records"]
        risk_record = next(r for r in risk_records if r["examId"] == exam_id and r["studentUsername"] == student_username)
        assert risk_record["riskScore"] >= 6, "risk record not visible to teacher"
        risk_logs = request(sock, with_token(teacher_token, {
            "type": "list_risk_logs",
            "examId": exam_id,
            "studentId": smoke_student["id"],
        }))["logs"]
        assert any(log["abnormalType"] == "SCREEN_SWITCH" for log in risk_logs), "risk logs not saved"

    print("Smoke test passed: publish-once, student visibility, grading and risk workflow are usable.")


if __name__ == "__main__":
    main()
