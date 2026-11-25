#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Simple Windows camera MJPEG proxy for QtExamSystem.
Endpoints:
  /health      JSON health status
  /snapshot.jpg single JPEG frame for the Qt bridge
  /camera.mjpg MJPEG video stream

Default port 8765 is the single, canonical camera proxy port for the whole
project (the old 8800 bridge is retired). --quality / --camera are accepted as
aliases so older launch commands keep working instead of crashing argparse.
"""
import argparse
import json
import os
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

try:
    import cv2
except Exception as exc:
    print(json.dumps({"ok": False, "error": f"cv2 import failed: {exc}"}, ensure_ascii=False), file=sys.stderr)
    raise

class CameraState:
    def __init__(self, index: int, width: int, height: int, fps: int, quality: int = 80):
        self.index = index
        self.width = width
        self.height = height
        self.fps = fps
        self.quality = quality
        self.cap = None
        self.last_error = "not started"

    def open(self):
        if self.cap is not None and self.cap.isOpened():
            return True
        self.cap = cv2.VideoCapture(self.index, cv2.CAP_DSHOW)
        if not self.cap.isOpened():
            self.cap = cv2.VideoCapture(self.index)
        if not self.cap.isOpened():
            self.last_error = f"cannot open camera index {self.index} (busy or missing)"
            return False
        self.cap.set(cv2.CAP_PROP_FRAME_WIDTH, self.width)
        self.cap.set(cv2.CAP_PROP_FRAME_HEIGHT, self.height)
        self.cap.set(cv2.CAP_PROP_FPS, self.fps)
        self.last_error = ""
        return True

    def read_jpeg(self):
        if not self.open():
            return None
        ok, frame = self.cap.read()
        if not ok or frame is None:
            self.last_error = "camera read failed"
            return None
        ok, buf = cv2.imencode('.jpg', frame, [int(cv2.IMWRITE_JPEG_QUALITY), self.quality])
        if not ok:
            self.last_error = "jpeg encode failed"
            return None
        return buf.tobytes()


def make_handler(state: CameraState):
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, fmt, *args):
            sys.stdout.write("[%s] %s\n" % (self.log_date_time_string(), fmt % args))
            sys.stdout.flush()

        def _cors(self):
            self.send_header('Access-Control-Allow-Origin', '*')
            self.send_header('Access-Control-Allow-Methods', 'GET, OPTIONS')
            self.send_header('Access-Control-Allow-Headers', '*')

        def do_OPTIONS(self):
            self.send_response(204)
            self._cors()
            self.end_headers()

        def do_HEAD(self):
            if self.path.startswith('/health'):
                opened = state.open()
                self.send_response(200 if opened else 503)
                self._cors()
                self.send_header('Content-Type', 'application/json; charset=utf-8')
                self.end_headers()
                return

            if self.path.startswith('/snapshot.jpg') or self.path.startswith('/snapshot'):
                self.send_response(200 if state.open() else 503)
                self._cors()
                self.send_header('Content-Type', 'image/jpeg')
                self.end_headers()
                return

            if self.path.startswith('/camera.mjpg') or self.path.startswith('/video'):
                self.send_response(200 if state.open() else 503)
                self._cors()
                self.send_header('Cache-Control', 'no-store, no-cache, must-revalidate, max-age=0')
                self.send_header('Pragma', 'no-cache')
                self.send_header('Content-Type', 'multipart/x-mixed-replace; boundary=frame')
                self.end_headers()
                return

            self.send_response(404)
            self._cors()
            self.end_headers()

        def do_GET(self):
            if self.path.startswith('/health'):
                opened = state.open()
                payload = {
                    "ok": bool(opened),
                    "cameraIndex": state.index,
                    "width": state.width,
                    "height": state.height,
                    "fps": state.fps,
                    "quality": state.quality,
                    "stream": "/camera.mjpg",
                    "error": state.last_error,
                }
                body = json.dumps(payload, ensure_ascii=False).encode('utf-8')
                self.send_response(200 if opened else 503)
                self._cors()
                self.send_header('Content-Type', 'application/json; charset=utf-8')
                self.send_header('Content-Length', str(len(body)))
                self.end_headers()
                self.wfile.write(body)
                return

            if self.path.startswith('/snapshot.jpg') or self.path.startswith('/snapshot'):
                jpg = state.read_jpeg()
                if jpg is None:
                    body = state.last_error.encode('utf-8')
                    self.send_response(503)
                    self._cors()
                    self.send_header('Content-Type', 'text/plain; charset=utf-8')
                    self.send_header('Content-Length', str(len(body)))
                    self.end_headers()
                    self.wfile.write(body)
                    return
                self.send_response(200)
                self._cors()
                self.send_header('Cache-Control', 'no-store, no-cache, must-revalidate, max-age=0')
                self.send_header('Pragma', 'no-cache')
                self.send_header('Content-Type', 'image/jpeg')
                self.send_header('Content-Length', str(len(jpg)))
                self.end_headers()
                self.wfile.write(jpg)
                return

            if self.path.startswith('/camera.mjpg') or self.path.startswith('/video'):
                if not state.open():
                    body = state.last_error.encode('utf-8')
                    self.send_response(503)
                    self._cors()
                    self.send_header('Content-Type', 'text/plain; charset=utf-8')
                    self.send_header('Content-Length', str(len(body)))
                    self.end_headers()
                    self.wfile.write(body)
                    return
                self.send_response(200)
                self._cors()
                self.send_header('Cache-Control', 'no-store, no-cache, must-revalidate, max-age=0')
                self.send_header('Pragma', 'no-cache')
                self.send_header('Content-Type', 'multipart/x-mixed-replace; boundary=frame')
                self.end_headers()
                delay = 1.0 / max(state.fps, 1)
                while True:
                    jpg = state.read_jpeg()
                    if jpg is None:
                        time.sleep(delay)
                        continue
                    try:
                        self.wfile.write(b'--frame\r\n')
                        self.wfile.write(b'Content-Type: image/jpeg\r\n')
                        self.wfile.write(b'Content-Length: ' + str(len(jpg)).encode() + b'\r\n\r\n')
                        self.wfile.write(jpg)
                        self.wfile.write(b'\r\n')
                        self.wfile.flush()
                    except (BrokenPipeError, ConnectionResetError):
                        break
                    time.sleep(delay)
                return

            self.send_response(404)
            self._cors()
            self.end_headers()
    return Handler


def main():
    parser = argparse.ArgumentParser(description="Windows camera MJPEG proxy for QtExamSystem")
    parser.add_argument('--host', default='0.0.0.0')
    parser.add_argument('--port', type=int, default=8765)
    # Accept both --camera-index (canonical) and --camera (legacy) so old
    # launch commands do not crash argparse.
    parser.add_argument('--camera-index', '--camera', type=int, default=0, dest='camera_index',
                        help='OpenCV camera index (0 = default webcam)')
    parser.add_argument('--width', type=int, default=640)
    parser.add_argument('--height', type=int, default=480)
    parser.add_argument('--fps', type=int, default=12)
    parser.add_argument('--quality', type=int, default=80, help='JPEG quality 1-100')
    parser.add_argument('--log-file', default=None,
                        help='Append stdout/stderr to this file (used for windowless autostart)')
    args = parser.parse_args()

    if args.log_file:
        log_path = os.path.abspath(args.log_file)
        os.makedirs(os.path.dirname(log_path), exist_ok=True)
        log_fp = open(log_path, 'a', buffering=1, encoding='utf-8')
        sys.stdout = log_fp
        sys.stderr = log_fp

    quality = min(max(args.quality, 1), 100)
    state = CameraState(args.camera_index, args.width, args.height, args.fps, quality)
    try:
        server = ThreadingHTTPServer((args.host, args.port), make_handler(state))
    except OSError as exc:
        print(f"[ERROR] cannot bind {args.host}:{args.port} -> {exc}")
        sys.stdout.flush()
        return 1
    print(f"[OK] Windows camera server on http://{args.host}:{args.port} (camera {args.camera_index}, quality {quality})")
    print(f"[OK] health: http://127.0.0.1:{args.port}/health")
    print(f"[OK] stream: http://127.0.0.1:{args.port}/camera.mjpg")
    sys.stdout.flush()
    server.serve_forever()
    return 0

if __name__ == '__main__':
    sys.exit(main())
