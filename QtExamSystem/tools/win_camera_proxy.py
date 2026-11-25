#!/usr/bin/env python3
"""Windows host camera -> HTTP proxy for QtExamSystem.

Serves the Windows webcam over HTTP so a Kylin/Ubuntu VM (which often cannot
reach the host's /dev/video0) can pull frames over the virtual network:

    GET /health        -> "OK"            (liveness check)
    GET /snapshot.jpg  -> current frame as JPEG
    GET /mjpeg         -> multipart/x-mixed-replace MJPEG stream

Only the Python standard library + OpenCV are required (no Flask/FastAPI).

Usage (run on the Windows host):
    python tools\\win_camera_proxy.py --host 0.0.0.0 --port 9100 --camera 0

Then on Kylin point the client at it:
    export QTEXAM_CAMERA_SOURCE=http://<WINDOWS_HOST_IP>:9100/snapshot.jpg
"""

from __future__ import annotations

import argparse
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

try:
    import cv2
except Exception as exc:  # noqa: BLE001
    sys.stderr.write(
        f"[CameraProxy] failed to import cv2 (opencv-python): {exc}\n"
        "Install it: pip install opencv-python\n"
    )
    sys.stderr.flush()
    raise SystemExit(3)


def log(message: str) -> None:
    sys.stderr.write(message + "\n")
    sys.stderr.flush()


class CameraGrabber:
    """Background thread that keeps the most recent frame from the webcam."""

    def __init__(self, index: int, width: int, height: int, fps: int, quality: int):
        self.index = index
        self.width = width
        self.height = height
        self.fps = fps
        self.quality = quality
        self._lock = threading.Lock()
        self._frame = None
        self._stop = False
        self._cap = None
        self.opened = False
        self.actual_w = 0
        self.actual_h = 0

    def _open(self) -> bool:
        # CAP_DSHOW avoids the slow MSMF backend startup on Windows.
        cap = cv2.VideoCapture(self.index, getattr(cv2, "CAP_DSHOW", 0))
        if not cap.isOpened():
            cap = cv2.VideoCapture(self.index)
        if cap.isOpened():
            if self.width:
                cap.set(cv2.CAP_PROP_FRAME_WIDTH, self.width)
            if self.height:
                cap.set(cv2.CAP_PROP_FRAME_HEIGHT, self.height)
            if self.fps:
                cap.set(cv2.CAP_PROP_FPS, self.fps)
        self._cap = cap
        return cap.isOpened()

    def run(self) -> None:
        if not self._open():
            log(f"[CameraProxy] ERROR: cannot open camera index={self.index} "
                "(check the webcam is connected and not used by another app)")
            return
        self.opened = True
        self.actual_w = int(self._cap.get(cv2.CAP_PROP_FRAME_WIDTH) or 0)
        self.actual_h = int(self._cap.get(cv2.CAP_PROP_FRAME_HEIGHT) or 0)
        log(f"[CameraProxy] camera index={self.index} opened actual="
            f"{self.actual_w}x{self.actual_h} fps={self._cap.get(cv2.CAP_PROP_FPS):.0f}")
        fail = 0
        while not self._stop:
            ok, frame = self._cap.read()
            if not ok or frame is None:
                fail += 1
                if fail % 50 == 0:
                    log(f"[CameraProxy] read() failing (streak={fail}); camera may be unplugged")
                time.sleep(0.05)
                continue
            fail = 0
            with self._lock:
                self._frame = frame
        self._cap.release()
        log("[CameraProxy] camera released")

    def latest_jpeg(self):
        """Return (jpeg_bytes, width, height) or (None, 0, 0) if no frame yet."""
        with self._lock:
            frame = None if self._frame is None else self._frame.copy()
        if frame is None:
            return None, 0, 0
        ok, encoded = cv2.imencode(".jpg", frame, [int(cv2.IMWRITE_JPEG_QUALITY), self.quality])
        if not ok:
            return None, 0, 0
        h, w = frame.shape[:2]
        return encoded.tobytes(), int(w), int(h)

    def stop(self) -> None:
        self._stop = True


class ProxyHandler(BaseHTTPRequestHandler):
    grabber: CameraGrabber = None  # set in main()

    # Silence the default per-request stderr spam; we log snapshots ourselves.
    def log_message(self, *_args) -> None:  # noqa: D401
        return

    def _send_text(self, code: int, text: str) -> None:
        body = text.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "text/plain; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self) -> None:  # noqa: N802
        path = self.path.split("?", 1)[0]
        if path in ("/health", "/"):
            self._send_text(200, "OK")
            return
        if path in ("/snapshot.jpg", "/snapshot"):
            data, w, h = self.grabber.latest_jpeg()
            if data is None:
                log("[CameraProxy] /snapshot.jpg -> 503 (no frame yet)")
                self._send_text(503, "no frame")
                return
            self.send_response(200)
            self.send_header("Content-Type", "image/jpeg")
            self.send_header("Content-Length", str(len(data)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(data)
            log(f"[CameraProxy] /snapshot.jpg -> 200 {len(data)}B {w}x{h}")
            return
        if path == "/mjpeg":
            self.send_response(200)
            self.send_header("Content-Type", "multipart/x-mixed-replace; boundary=frame")
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            log("[CameraProxy] /mjpeg client connected")
            try:
                while True:
                    data, _w, _h = self.grabber.latest_jpeg()
                    if data is not None:
                        self.wfile.write(b"--frame\r\nContent-Type: image/jpeg\r\n")
                        self.wfile.write(f"Content-Length: {len(data)}\r\n\r\n".encode("ascii"))
                        self.wfile.write(data)
                        self.wfile.write(b"\r\n")
                    time.sleep(1.0 / 15)
            except (BrokenPipeError, ConnectionResetError):
                log("[CameraProxy] /mjpeg client disconnected")
            return
        self._send_text(404, "not found")


def main() -> int:
    parser = argparse.ArgumentParser(description="Windows host camera HTTP proxy")
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=9100)
    parser.add_argument("--camera", type=int, default=0, help="OpenCV camera index")
    parser.add_argument("--width", type=int, default=640)
    parser.add_argument("--height", type=int, default=480)
    parser.add_argument("--fps", type=int, default=15)
    parser.add_argument("--quality", type=int, default=80, help="JPEG quality 1-100")
    args = parser.parse_args()

    grabber = CameraGrabber(args.camera, args.width, args.height, args.fps, args.quality)
    thread = threading.Thread(target=grabber.run, daemon=True)
    thread.start()

    # Give the camera a moment to deliver the first frame (non-fatal if it does not).
    for _ in range(40):
        data, _w, _h = grabber.latest_jpeg()
        if data is not None:
            break
        time.sleep(0.1)

    ProxyHandler.grabber = grabber
    try:
        httpd = ThreadingHTTPServer((args.host, args.port), ProxyHandler)
    except OSError as exc:
        log(f"[CameraProxy] ERROR: cannot bind {args.host}:{args.port} -> {exc}")
        return 1

    log(f"[CameraProxy] serving on http://{args.host}:{args.port}  (camera index={args.camera})")
    log("[CameraProxy]   GET /health   GET /snapshot.jpg   GET /mjpeg")
    if not grabber.opened:
        log("[CameraProxy] WARNING: camera not opened yet; /health works, "
            "/snapshot.jpg will return 503 until a frame arrives")
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        log("[CameraProxy] shutting down (Ctrl+C)")
    finally:
        grabber.stop()
        httpd.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
