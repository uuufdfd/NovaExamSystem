#!/usr/bin/env python3
"""Windows host question-bank file proxy for QtExamSystem.

Shares a folder of question-bank files (Word/Excel/CSV/...) over HTTP so the
Kylin client can list & download them, then import through the EXISTING import
flow (client parses -> uploads to the Ubuntu server). This proxy only serves
files; it never talks to the database.

    GET /health                 -> "OK"
    GET /files                  -> JSON list of question-bank files in --root
    GET /download?name=<file>   -> raw file bytes (must be a bare name in --root)

Only the Python standard library is required (http.server) -- no Flask/FastAPI.

Usage (run on the Windows host):
    python tools\\win_question_bank_proxy.py --host 0.0.0.0 --port 9200 --root "<question-directory>"

If Kylin cannot reach port 9200, allow it through the Windows firewall (admin PowerShell):
    New-NetFirewallRule -DisplayName "NovaExam Question Bank Proxy 9200" -Direction Inbound -Protocol TCP -LocalPort 9200 -Action Allow
"""

from __future__ import annotations

import argparse
import datetime
import json
import os
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

# Only these extensions are listed / downloadable.
ALLOWED_EXTS = {".xlsx", ".xls", ".csv", ".docx", ".txt", ".json"}


def log(message: str) -> None:
    sys.stderr.write(message + "\n")
    sys.stderr.flush()


def ext_of(name: str) -> str:
    return os.path.splitext(name)[1].lower().lstrip(".")


def list_files(root: str):
    items = []
    try:
        entries = os.listdir(root)
    except OSError as exc:
        log(f"[QBankProxy] listdir failed root={root} err={exc}")
        return items
    for name in sorted(entries):
        full = os.path.join(root, name)
        if not os.path.isfile(full):
            continue
        if os.path.splitext(name)[1].lower() not in ALLOWED_EXTS:
            continue
        try:
            st = os.stat(full)
            mtime = datetime.datetime.fromtimestamp(st.st_mtime).strftime("%Y-%m-%d %H:%M:%S")
            items.append({"name": name, "size": st.st_size, "mtime": mtime, "type": ext_of(name)})
        except OSError as exc:
            log(f"[QBankProxy] stat failed name={name} err={exc}")
    return items


def safe_resolve(root: str, name: str):
    """Return an absolute path inside root for a bare, allowed filename, else None.

    Blocks path traversal: rejects empty names, names with separators or '..',
    absolute paths, anything resolving outside root, non-files, and disallowed
    extensions.
    """
    if not name or name in (".", ".."):
        return None
    if "/" in name or "\\" in name or os.path.isabs(name):
        return None
    root_real = os.path.realpath(root)
    candidate = os.path.realpath(os.path.join(root_real, name))
    try:
        if os.path.commonpath([candidate, root_real]) != root_real:
            return None
    except ValueError:
        return None  # different drives, etc.
    if os.path.dirname(candidate) != root_real:
        return None  # must sit directly in root, not a subfolder
    if not os.path.isfile(candidate):
        return None
    if os.path.splitext(candidate)[1].lower() not in ALLOWED_EXTS:
        return None
    return candidate


class QBankHandler(BaseHTTPRequestHandler):
    root_dir = ""  # set in main()

    def log_message(self, *_args) -> None:  # silence default per-request spam
        return

    def _send(self, code: int, content_type: str, body: bytes) -> None:
        self.send_response(code)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def _send_json(self, code: int, obj: dict) -> None:
        self._send(code, "application/json; charset=utf-8",
                   json.dumps(obj, ensure_ascii=False).encode("utf-8"))

    def do_GET(self) -> None:  # noqa: N802
        parsed = urlparse(self.path)
        path = parsed.path

        if path in ("/health", "/"):
            self._send(200, "text/plain; charset=utf-8", b"OK")
            return

        if path == "/files":
            files = list_files(self.root_dir)
            log(f"[QBankProxy] /files -> {len(files)} file(s) in {self.root_dir}")
            self._send_json(200, {"success": True, "root": self.root_dir, "files": files})
            return

        if path == "/download":
            params = parse_qs(parsed.query)
            name = (params.get("name") or [""])[0]
            resolved = safe_resolve(self.root_dir, name)
            if resolved is None:
                log(f"[QBankProxy] /download REJECTED name={name!r}")
                self._send_json(400, {"success": False, "error": "invalid or not-allowed file name"})
                return
            try:
                with open(resolved, "rb") as fh:
                    data = fh.read()
            except OSError as exc:
                log(f"[QBankProxy] /download read failed name={name!r} err={exc}")
                self._send_json(500, {"success": False, "error": f"read failed: {exc}"})
                return
            log(f"[QBankProxy] /download -> {name!r} {len(data)} bytes")
            self.send_response(200)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Content-Length", str(len(data)))
            self.send_header("Content-Disposition", "attachment")
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(data)
            return

        self._send_json(404, {"success": False, "error": "not found"})


def main() -> int:
    parser = argparse.ArgumentParser(description="Windows question-bank HTTP proxy")
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=9200)
    parser.add_argument("--root", default=str(Path(__file__).resolve().parents[1] / "data"),
                        help="folder that holds question-bank files to share")
    args = parser.parse_args()

    root = os.path.abspath(args.root)
    if not os.path.isdir(root):
        try:
            os.makedirs(root, exist_ok=True)
            log(f"[QBankProxy] created shared folder: {root}")
        except OSError as exc:
            log(f"[QBankProxy] ERROR: cannot create root {root}: {exc}")
            return 1

    QBankHandler.root_dir = root
    try:
        httpd = ThreadingHTTPServer((args.host, args.port), QBankHandler)
    except OSError as exc:
        log(f"[QBankProxy] ERROR: cannot bind {args.host}:{args.port} -> {exc}")
        return 1

    log(f"[QBankProxy] serving on http://{args.host}:{args.port}")
    log(f"[QBankProxy] shared folder (root): {root}")
    log(f"[QBankProxy] allowed types: {', '.join(sorted(ALLOWED_EXTS))}")
    log("[QBankProxy]   GET /health   GET /files   GET /download?name=<file>")
    log(f"[QBankProxy] put your .xlsx/.docx/.csv question-bank files into: {root}")
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        log("[QBankProxy] shutting down (Ctrl+C)")
    finally:
        httpd.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
