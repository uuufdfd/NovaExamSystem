#!/usr/bin/env python3
"""Windows host file picker for QtExamSystem question-bank / full-exam import.

Run this on the interactive Windows host session. Kylin Qt calls
GET /pick-question-file (shared by question-bank and full-exam import),
this script opens a native Windows file dialog, reads the selected
.xlsx/.csv/.docx file, and returns it as base64 JSON.

成绩导出反向通道：POST /save-file 接收 {"suggestedName","filter","contentBase64"}，
弹出 Windows“另存为”对话框，把 base64 内容写到用户选定的 Windows 路径（如桌面），
返回 {"ok":true,"path":...} / {"ok":false,"cancelled":true} / {"ok":false,"error":...}。

设计要点（2026-06-10 修复“选中文件后 Qt 一直卡在 [HOST_PICKER_START]”）：
- Qt 端用 QNetworkAccessManager，按 Content-Length / 连接关闭判定响应结束，超时 300s。
  因此服务端必须：①完整写出 JSON；②正确设置 Content-Length；③Connection: close + flush + close。
- 旧实现 _send_json 内部无任何日志，无法判断卡在“没发响应”还是“发了没收到”。
  现在每一步都打日志（read start/ok、encode ok、response start/sent），定位无盲区。
- 所有分支（取消/后缀不支持/读失败/编码失败/未知异常）都返回 JSON，绝不让连接悬挂。
- tkinter 非线程安全：用全局锁串行化文件对话框，避免并发请求各自建 Tk 卡死。

Usage:
    python tools\\windows_file_picker_server.py --host 0.0.0.0 --port 8766
"""

from __future__ import annotations

import argparse
import base64
import json
import os
import sys
import threading
from http.server import ThreadingHTTPServer, BaseHTTPRequestHandler
from pathlib import Path

# 支持的导入文件后缀（题库与整套试卷共用）。
SUPPORTED_SUFFIXES = {".xlsx", ".csv", ".docx"}

# tkinter 不是线程安全的，ThreadingHTTPServer 每个请求在独立线程处理；
# 用锁把“弹文件框”串行化，避免两个请求同时创建 Tk 实例互相卡死。
_dialog_lock = threading.Lock()


def log(message: str) -> None:
    # Under pythonw.exe sys.stderr/stdout are None; never crash on logging.
    stream = sys.stderr or sys.stdout
    if stream is None:
        return
    stream.write(message + "\n")
    stream.flush()


def choose_file_path() -> tuple[str | None, str | None]:
    """弹出原生 Windows 文件选择框，返回 (选中路径或 None, 错误信息或 None)。

    只负责拿到路径，不读文件内容；tkinter 的创建与销毁都在本函数内闭环，
    返回前已 destroy，尽量减少 Tcl 在工作线程内的残留。
    """
    try:
        import tkinter as tk
        from tkinter import filedialog
    except Exception as exc:  # noqa: BLE001
        return None, f"tkinter import failed: {exc}"

    root = tk.Tk()
    try:
        root.withdraw()
        root.update()
        try:
            root.attributes("-topmost", True)
            root.lift()
            root.focus_force()
        except Exception:  # noqa: BLE001  顶置失败不影响选择
            pass
        path = filedialog.askopenfilename(
            parent=root,
            title="选择新星考试系统题库/试卷文件（Excel/CSV/Word）",
            filetypes=[
                ("题库/试卷文件（Excel/CSV/Word）", "*.xlsx *.csv *.docx"),
                ("Excel 文件", "*.xlsx"),
                ("CSV 文件", "*.csv"),
                ("Word 文件", "*.docx"),
            ],
        )
    finally:
        try:
            root.destroy()
        except Exception:  # noqa: BLE001
            pass
        try:
            root.update()  # 处理完销毁事件，减少 Tcl 在本线程的残留
        except Exception:  # noqa: BLE001
            pass

    return (path or None), None


def build_pick_payload() -> dict:
    """完整执行“弹框 → 校验后缀 → 读文件 → base64”，任何失败都返回 dict（绝不抛到上层）。

    所有返回都是合法 JSON 可序列化的 dict，保证调用方一定能把响应发出去。
    """
    with _dialog_lock:  # 串行化对话框，避免并发 tkinter
        path, error = choose_file_path()

    if error:
        log(f"[WINDOWS_FILE_PICKER_ERROR] reason=tkinter-unavailable detail={error}")
        return {"ok": False, "reason": "tkinter-unavailable", "detail": error}

    if not path:
        log("[WINDOWS_FILE_PICKER] canceled")
        return {"ok": False, "reason": "canceled"}

    selected = Path(path)
    suffix = selected.suffix.lower()
    if suffix not in SUPPORTED_SUFFIXES:
        log(f"[WINDOWS_FILE_PICKER_ERROR] reason=unsupported-format suffix={suffix} path={selected}")
        return {"ok": False, "reason": "unsupported-format", "detail": suffix}

    try:
        log(f"[WINDOWS_FILE_PICKER] read start path={selected}")
        data = selected.read_bytes()
        log(f"[WINDOWS_FILE_PICKER] read ok bytes={len(data)}")
    except OSError as exc:
        log(f"[WINDOWS_FILE_PICKER_ERROR] reason=read-failed path={selected} detail={exc}")
        return {"ok": False, "reason": "read-failed", "detail": str(exc)}

    try:
        encoded = base64.b64encode(data).decode("ascii")
        log(f"[WINDOWS_FILE_PICKER] encode ok chars={len(encoded)}")
    except Exception as exc:  # noqa: BLE001
        log(f"[WINDOWS_FILE_PICKER_ERROR] reason=encode-failed path={selected} detail={exc}")
        return {"ok": False, "reason": "encode-failed", "detail": str(exc)}

    log(f"[WINDOWS_FILE_PICKER] selected path={selected} size={len(data)}")
    return {
        "ok": True,
        "filename": selected.name,
        "path": str(selected),
        "size": len(data),
        "content_base64": encoded,
    }


def save_filetypes_from_filter(filter_text: str) -> list[tuple[str, str]]:
    filetypes: list[tuple[str, str]] = []
    for item in str(filter_text or "").split(";;"):
        item = item.strip()
        if not item:
            continue
        open_paren = item.rfind("(")
        close_paren = item.rfind(")")
        if 0 <= open_paren < close_paren:
            label = item[:open_paren].strip() or item
            pattern = item[open_paren + 1:close_paren].strip() or "*.*"
        else:
            label = item
            pattern = "*.*"
        filetypes.append((label, pattern))
    return filetypes or [("CSV files", "*.csv"), ("All files", "*.*")]


def choose_save_path(suggested_name: str, filter_text: str = "") -> tuple[str | None, str | None]:
    """弹出原生 Windows“另存为”对话框，返回 (选中路径或 None, 错误信息或 None)。

    与 choose_file_path 同结构（顶置 + 闭环创建/销毁 tkinter）。默认目录优先桌面、
    否则用户主目录，均经 expanduser 推导，绝不硬编码用户名/绝对路径。
    """
    try:
        import tkinter as tk
        from tkinter import filedialog
    except Exception as exc:  # noqa: BLE001
        return None, f"tkinter import failed: {exc}"

    home = os.path.expanduser("~")
    desktop = os.path.join(home, "Desktop")
    initial_dir = desktop if os.path.isdir(desktop) else home

    root = tk.Tk()
    try:
        root.withdraw()
        root.update()
        try:
            root.attributes("-topmost", True)
            root.lift()
            root.focus_force()
        except Exception:  # noqa: BLE001  顶置失败不影响保存
            pass
        path = filedialog.asksaveasfilename(
            parent=root,
            title="保存新星考试系统成绩 CSV 到 Windows",
            initialdir=initial_dir,
            initialfile=suggested_name or "export.csv",
            defaultextension=".csv",
            filetypes=save_filetypes_from_filter(filter_text),
        )
    finally:
        try:
            root.destroy()
        except Exception:  # noqa: BLE001
            pass
        try:
            root.update()
        except Exception:  # noqa: BLE001
            pass

    return (path or None), None


def build_save_payload(body: bytes) -> dict:
    """处理 POST /save-file：解析 JSON → 弹另存为 → 写 base64 内容到 Windows 路径。

    入参 JSON: {"suggestedName": "...", "filter": "...", "contentBase64": "..."}
    返回: 成功 {"ok": True, "path": "..."}；取消 {"ok": False, "cancelled": True}；
          失败 {"ok": False, "error": "..."}。任何分支都返回合法 dict，绝不抛到上层。
    """
    try:
        req = json.loads(body.decode("utf-8")) if body else {}
    except Exception as exc:  # noqa: BLE001
        log(f"[WINDOWS_FILE_PICKER_ERROR] reason=save-invalid-json detail={exc}")
        return {"ok": False, "error": f"invalid-json: {exc}"}

    suggested = str(req.get("suggestedName") or "export.csv")
    filter_text = str(req.get("filter") or "")
    content_b64 = req.get("contentBase64") or ""
    try:
        # 直接写解码后的原始字节（含 Qt 端已加好的 UTF-8 BOM），不做任何文本改写。
        data = base64.b64decode(content_b64, validate=True)
    except Exception as exc:  # noqa: BLE001
        log(f"[WINDOWS_FILE_PICKER_ERROR] reason=save-base64-decode-failed detail={exc}")
        return {"ok": False, "error": f"base64-decode-failed: {exc}"}

    with _dialog_lock:  # 串行化对话框，避免并发 tkinter
        path, error = choose_save_path(suggested, filter_text)

    if error:
        log(f"[WINDOWS_FILE_PICKER_ERROR] reason=save-tkinter-unavailable detail={error}")
        return {"ok": False, "error": error}
    if not path:
        log("[WINDOWS_FILE_PICKER] save canceled")
        return {"ok": False, "cancelled": True}

    try:
        with open(path, "wb") as handle:
            handle.write(data)
    except OSError as exc:
        log(f"[WINDOWS_FILE_PICKER_ERROR] reason=save-write-failed path={path} detail={exc}")
        return {"ok": False, "error": f"write-failed: {exc}"}

    log(f"[WINDOWS_FILE_PICKER] saved path={path} bytes={len(data)}")
    return {"ok": True, "path": str(path)}


class FilePickerHandler(BaseHTTPRequestHandler):
    def log_message(self, *_args) -> None:
        return  # 关闭 http.server 默认访问日志，统一用 log()

    def send_json(self, payload: dict, status: int = 200) -> None:
        """统一 JSON 响应：显式 Content-Length + Connection: close + flush + 主动关连接。

        write 前后各打一条日志，用于区分“响应未发出”与“已发出但客户端未收到”。
        """
        body = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        log(f"[WINDOWS_FILE_PICKER] response start bytes={len(body)}")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(body)
        self.wfile.flush()
        self.close_connection = True
        log("[WINDOWS_FILE_PICKER] response sent")

    def send_text(self, text: str, status: int = 200) -> None:
        body = text.encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "text/plain; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(body)
        self.wfile.flush()
        self.close_connection = True

    def do_GET(self) -> None:  # noqa: N802
        path = self.path.split("?", 1)[0]
        log(f"[WINDOWS_FILE_PICKER] request path={path}")

        # 顶层 try：任何未预期异常也要回 JSON，绝不让连接悬挂导致 Qt 卡 300s。
        try:
            # /health 必须秒回 OK，且绝不触发 tkinter（探活不能弹窗）。
            if path in ("/health", "/"):
                self.send_text("OK")
                log("[WINDOWS_FILE_PICKER] health ok")
                return

            # /pick-question-file 与 /pick-exam-file 共用同一拾取逻辑（题库/整套试卷）。
            if path in ("/pick-question-file", "/pick-exam-file"):
                log(f"[WINDOWS_FILE_PICKER] request pick endpoint={path}")
                payload = build_pick_payload()
                self.send_json(payload)
                return

            self.send_json({"ok": False, "reason": "not-found", "detail": path}, status=404)
        except Exception as exc:  # noqa: BLE001
            log(f"[WINDOWS_FILE_PICKER_ERROR] reason=handler-crash detail={exc}")
            try:
                self.send_json({"ok": False, "reason": "handler-crash", "detail": str(exc)}, status=500)
            except Exception:  # noqa: BLE001  连响应都发不出时只能放弃
                pass

    def do_POST(self) -> None:  # noqa: N802
        path = self.path.split("?", 1)[0]
        log(f"[WINDOWS_FILE_PICKER] request POST path={path}")
        try:
            # /save-file：成绩 CSV 导出到 Windows（弹另存为，写 base64 内容到所选路径）。
            if path == "/save-file":
                try:
                    length = int(self.headers.get("Content-Length", 0) or 0)
                except (TypeError, ValueError):
                    length = 0
                body = self.rfile.read(length) if length > 0 else b""
                log(f"[WINDOWS_FILE_PICKER] save request bytes={len(body)}")
                payload = build_save_payload(body)
                self.send_json(payload)
                return

            self.send_json({"ok": False, "reason": "not-found", "detail": path}, status=404)
        except Exception as exc:  # noqa: BLE001
            log(f"[WINDOWS_FILE_PICKER_ERROR] reason=post-handler-crash detail={exc}")
            try:
                self.send_json({"ok": False, "error": f"handler-crash: {exc}"}, status=500)
            except Exception:  # noqa: BLE001
                pass


class PickerHTTPServer(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True


def main() -> int:
    parser = argparse.ArgumentParser(description="Windows file picker HTTP bridge for QtExamSystem")
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=8766)
    parser.add_argument("--log-file", default=None,
                        help="Append stdout/stderr to this file (used for windowless autostart)")
    args = parser.parse_args()

    if args.log_file:
        log_path = os.path.abspath(args.log_file)
        os.makedirs(os.path.dirname(log_path), exist_ok=True)
        log_fp = open(log_path, "a", buffering=1, encoding="utf-8")
        sys.stdout = log_fp
        sys.stderr = log_fp

    try:
        server = PickerHTTPServer((args.host, args.port), FilePickerHandler)
    except OSError as exc:
        log(f"[WINDOWS_FILE_PICKER_ERROR] reason=cannot-bind host={args.host} port={args.port} detail={exc}")
        return 1

    log(f"[WINDOWS_FILE_PICKER] started host={args.host} port={args.port}")
    log("[WINDOWS_FILE_PICKER] endpoints: GET /health  GET /pick-question-file  GET /pick-exam-file  POST /save-file")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        log("[WINDOWS_FILE_PICKER] shutting down (Ctrl+C)")
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    os.environ.setdefault("PYTHONUTF8", "1")
    raise SystemExit(main())
