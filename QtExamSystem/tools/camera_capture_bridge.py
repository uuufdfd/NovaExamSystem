#!/usr/bin/env python3
"""Camera frame bridge for QtExamSystem on Linux.

Protocol (UNCHANGED -- Qt parses these exact stdout lines):
  {"type":"opened","device":"/dev/video0"}
  {"type":"frame","device":"/dev/video0","width":320,"height":240,"image":"<base64 jpeg>"}
  {"type":"error","message":"..."}

stdout = protocol ONLY. All human-readable diagnostics go to stderr:
  [PythonBridge] ... verbose terminal-only diagnostics (device discovery, v4l2-ctl, per-mode attempts)
  [OpenCVCamera] ... milestone lines that Qt also mirrors onto the on-screen status label
"""

from __future__ import annotations

import base64
import glob
import json
import os
import re
import shutil
import signal
import subprocess
import sys
import time
import urllib.error
import urllib.request

try:
    import cv2
except Exception as exc:  # noqa: BLE001 - we want any import failure surfaced cleanly
    sys.stderr.write(
        "[OpenCVCamera] failed to import cv2 (opencv-python): "
        f"{exc}. Install on Kylin/Linux: sudo apt install -y python3-opencv\n"
    )
    sys.stderr.flush()
    sys.stdout.write(
        json.dumps(
            {
                "type": "error",
                "message": "缺少 python3-opencv：当前 Python 无法 import cv2。"
                "请在麒麟终端执行：sudo apt install -y python3-opencv",
            },
            ensure_ascii=True,
            separators=(",", ":"),
        )
        + "\n"
    )
    sys.stdout.flush()
    raise SystemExit(3)


TARGET_OUTPUT_FPS = 12  # emit ~10-12 fps; display side caps to ~10
OUTPUT_WIDTH = 320      # Qt preview decodes 320px-wide frames; keep aspect ratio
JPEG_QUALITY = 45
# A frame whose average pixel value is below this (0-255 scale) is treated as
# "black": used to discard black warm-up frames and to warn when a camera keeps
# handing back black frames even though read() reports success.
BLACK_MEAN_THRESHOLD = 5.0
# Per-mode warm-up budget. Kept modest so trying several formats across several
# devices still completes within the Qt watchdog window.
WARMUP_FRAMES = 12
WARMUP_SLEEP = 0.06
# Consecutive failed reads in the streaming loop before we declare the camera
# lost (e.g. VMware unplugged the USB passthrough) and exit with an error.
MAX_READ_FAILURES = 40

SNAPSHOT_FPS = 10           # ~100ms between polls; adjustable up to 12
SNAPSHOT_MAX_FAILURES = 3   # consecutive failures before emitting error

# Capture formats tried in order. The first that yields a non-black frame wins.
# (label, fourcc-or-None, width, height)
CAPTURE_MODES = [
    ("MJPG", "MJPG", 640, 480),
    ("YUYV", "YUYV", 640, 480),
    ("default", None, 640, 480),
]

_STOP = False


def _handle_term(signum, _frame) -> None:
    """SIGTERM/SIGINT -> set the stop flag ONLY (no raise).

    Raising from inside the handler could interrupt sys.stdout mid-write: a
    ~13KB frame line is written to the pipe in several 8KB chunks, and an
    exception between chunks tears the line (head reaches Qt, tail is flushed
    separately at interpreter exit -> Qt sees a bare base64 fragment). All
    capture loops poll _STOP, so a flag is enough for a clean shutdown.
    """
    global _STOP
    _STOP = True


def _write_stdout_line(line: str) -> None:
    """One protocol line = ONE buffered write + flush, so a line can never be
    torn into a JSON head and a bare base64 tail by interleaving or partial
    flushes. ensure_ascii=True upstream guarantees the line is pure ASCII."""
    sys.stdout.buffer.write((line + "\n").encode("ascii"))
    sys.stdout.buffer.flush()


def emit(payload: dict) -> None:
    _write_stdout_line(json.dumps(payload, ensure_ascii=True, separators=(",", ":")))


def log(message: str) -> None:
    sys.stderr.write(message + "\n")
    sys.stderr.flush()


def frame_empty(frame) -> bool:
    return frame is None or getattr(frame, "size", 0) == 0


def frame_mean(frame) -> float:
    """Average pixel intensity (0-255) across colour channels, 0.0 when empty."""
    if frame_empty(frame):
        return 0.0
    channel_means = cv2.mean(frame)[:3]
    return sum(channel_means) / len(channel_means)


def _dev_index(path: str) -> int:
    """Return the numeric suffix of a /dev/videoN path for sorting."""
    m = re.search(r"(\d+)$", path)
    return int(m.group(1)) if m else 999


def discover_devices() -> list[str]:
    """Return all /dev/video* paths sorted numerically (video0 first)."""
    return sorted(glob.glob("/dev/video[0-9]*"), key=_dev_index)


def v4l2_capture_source(device: str):
    """Return the OpenCV V4L2 source for a /dev/videoN node.

    Some OpenCV builds reject V4L2 string paths with "can't be used to capture
    by name", while the same camera opens by numeric index. Keep all diagnostics
    in /dev/videoN form, but pass the integer index to cv2 where possible.
    """
    match = re.fullmatch(r"/dev/video(\d+)", device)
    return int(match.group(1)) if match else device


def log_environment(devices: list[str]) -> None:
    """Dump everything a human needs to diagnose camera access (stderr only)."""
    log(f"[PythonBridge] python={sys.executable} cv2={getattr(cv2, '__version__', '?')}")
    log(f"[PythonBridge] discovered /dev/video* = {devices if devices else 'NONE'}")
    for path in devices:
        try:
            mode = oct(os.stat(path).st_mode & 0o777)
        except OSError as exc:
            mode = f"stat-failed({exc})"
        log(f"[PythonBridge] {path} perms={mode} accessible={quick_device_accessible(path)}")

    v4l2 = shutil.which("v4l2-ctl")
    if not v4l2:
        log("[PythonBridge] v4l2-ctl not installed (optional: sudo apt install v4l-utils)")
        return
    try:
        result = subprocess.run(
            [v4l2, "--list-devices"],
            capture_output=True,
            text=True,
            timeout=4,
        )
        listing = (result.stdout or "") + (result.stderr or "")
        for line in listing.splitlines():
            if line.strip():
                log(f"[PythonBridge] v4l2-ctl: {line.rstrip()}")
    except Exception as exc:  # noqa: BLE001
        log(f"[PythonBridge] v4l2-ctl --list-devices failed: {exc}")


def quick_device_accessible(path: str) -> bool:
    """Non-blocking open to confirm a device node can be accessed at all.

    Uses O_NONBLOCK so it returns immediately even on a stalled device.
    Returns True when the node exists and is openable; False otherwise.
    """
    try:
        # O_NONBLOCK exists on Linux (the only platform this bridge runs on); fall
        # back to 0 elsewhere so the probe never raises AttributeError.
        flags = os.O_RDONLY | getattr(os, "O_NONBLOCK", 0)
        fd = os.open(path, flags)
        os.close(fd)
        return True
    except OSError:
        return False


def _try_mode(device: str, label: str, fourcc: str | None, width: int, height: int):
    """Open `device` in one capture mode and warm it up.

    Returns (cap, frame) on a bright frame, (cap, black_frame) as a last-resort
    fallback if only black frames appeared, or (None, None) if the mode failed.
    """
    cap = cv2.VideoCapture(v4l2_capture_source(device), cv2.CAP_V4L2)
    if fourcc is not None:
        cap.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc(*fourcc))
    # Request the universally-supported 640x480 baseline. Forcing 320x240 made some
    # UVC / VM cameras hand back all-black frames, so we capture at 640x480 and
    # downscale every emitted frame to OUTPUT_WIDTH below.
    cap.set(cv2.CAP_PROP_FRAME_WIDTH, width)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, height)
    cap.set(cv2.CAP_PROP_FPS, 15)
    cap.set(cv2.CAP_PROP_BUFFERSIZE, 1)

    if not cap.isOpened():
        log(f"[PythonBridge] mode={label} device={device} isOpened=False")
        cap.release()
        return None, None

    fallback_frame = None  # last non-empty (but black) frame
    for _ in range(WARMUP_FRAMES):
        if _STOP:
            cap.release()
            return None, None
        read_ok, frame = cap.read()
        empty = frame_empty(frame)
        if read_ok and not empty:
            mean = frame_mean(frame)
            if mean >= BLACK_MEAN_THRESHOLD:
                log(
                    f"[OpenCVCamera] opened device={device} mode={label} "
                    f"isOpened=True readOk=True frame.empty=False "
                    f"shape={frame.shape} mean={mean:.1f}"
                )
                return cap, frame
            fallback_frame = frame
            log(
                f"[PythonBridge] warmup discarding black frame device={device} "
                f"mode={label} mean={mean:.1f} shape={frame.shape}"
            )
        else:
            log(
                f"[PythonBridge] warmup read failed device={device} mode={label} "
                f"readOk={read_ok} frame.empty={empty}"
            )
        time.sleep(WARMUP_SLEEP)

    if fallback_frame is not None:
        log(
            f"[PythonBridge] mode={label} device={device} only produced black "
            f"frames; will keep this as a fallback"
        )
        return cap, fallback_frame

    log(f"[PythonBridge] mode={label} device={device} produced no usable frame")
    cap.release()
    return None, None


def open_device(device: str):
    """Try each capture mode on `device`. Returns (cap, frame, mode) or (None, None, '')."""
    black_fallback = None  # (cap, frame, label) kept if every mode is black-only
    for label, fourcc, width, height in CAPTURE_MODES:
        if _STOP:
            break
        log(f"[PythonBridge] trying device={device} mode={label} {width}x{height}")
        cap, frame = _try_mode(device, label, fourcc, width, height)
        if cap is None:
            continue
        if frame_mean(frame) >= BLACK_MEAN_THRESHOLD:
            return cap, frame, label
        # Black-only success: remember the first one, keep trying other modes in
        # case another format produces a real (bright) image.
        if black_fallback is None:
            black_fallback = (cap, frame, label)
        else:
            cap.release()

    if black_fallback is not None:
        cap, frame, label = black_fallback
        log(
            f"[OpenCVCamera] warmup only produced black frames device={device} "
            f"mode={label}; streaming anyway so the UI can show a readable warning"
        )
        return cap, frame, label

    return None, None, ""


def stream(cap, device: str) -> int:
    """Main capture/emit loop. Returns a process exit code."""
    actual_w = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH) or 0)
    actual_h = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT) or 0)
    actual_fps = cap.get(cv2.CAP_PROP_FPS) or 0
    log(
        f"[OpenCVCamera] capture actual={actual_w}x{actual_h} fps={actual_fps:.0f} "
        f"-> output {OUTPUT_WIDTH}px wide, paced to ~{TARGET_OUTPUT_FPS} fps"
    )

    frame_interval = 1.0 / TARGET_OUTPUT_FPS
    emitted_frames = 0
    black_streak = 0
    black_warned = False
    read_failures = 0

    while not _STOP:
        loop_start = time.monotonic()

        read_ok, frame = cap.read()
        if not read_ok or frame_empty(frame):
            read_failures += 1
            log(
                f"[PythonBridge] frame read failed device={device} "
                f"isOpened={cap.isOpened()} readOk={read_ok} "
                f"frame.empty={frame_empty(frame)} streak={read_failures}"
            )
            # Camera disappeared (e.g. VMware USB passthrough disconnected) or wedged.
            if read_failures >= MAX_READ_FAILURES:
                still_there = os.path.exists(device)
                emit(
                    {
                        "type": "error",
                        "message": (
                            f"Camera {device} stopped delivering frames "
                            + (
                                "and the device node disappeared. Check VMware USB "
                                "camera passthrough / reconnect the camera."
                                if not still_there
                                else "after many retries. Try reconnecting the camera "
                                "or closing other apps using it."
                            )
                        ),
                    }
                )
                log(
                    f"[OpenCVCamera] giving up on device={device} after "
                    f"{read_failures} failed reads (node_exists={still_there})"
                )
                return 4
            time.sleep(0.03)
            continue

        read_failures = 0
        h, w = frame.shape[:2]
        mean = frame_mean(frame)
        if not emit_frame(device, frame):
            continue
        emitted_frames += 1

        # Surface a sustained black-camera condition to the UI (Qt mirrors any
        # "[OpenCVCamera]" stderr line onto the status label).
        if mean < BLACK_MEAN_THRESHOLD:
            black_streak += 1
        else:
            black_streak = 0
            black_warned = False
        if black_streak >= 15 and not black_warned:
            black_warned = True
            log(
                f"[OpenCVCamera] frames are black mean={mean:.1f} after "
                f"{emitted_frames} frames -- check the camera cover/shutter, camera "
                f"permission, or whether another app is using the camera"
            )
        elif emitted_frames % 30 == 0:
            log(f"[PythonBridge] streaming frames={emitted_frames} last={w}x{h} mean={mean:.1f}")

        elapsed = time.monotonic() - loop_start
        if elapsed < frame_interval:
            time.sleep(frame_interval - elapsed)

    return 0


def _resize_for_output(frame):
    """Downscale to OUTPUT_WIDTH (keeping aspect) so Qt decodes small frames."""
    h, w = frame.shape[:2]
    if w > OUTPUT_WIDTH:
        out_h = max(1, int(h * OUTPUT_WIDTH / w))
        return cv2.resize(frame, (OUTPUT_WIDTH, out_h), interpolation=cv2.INTER_AREA)
    return frame


def emit_frame(device: str, frame) -> bool:
    """The ONLY place a frame reaches stdout. Every capture path (local V4L2
    and HTTP snapshot) must emit through here: resize, JPEG-encode, then one
    complete JSON line via a single write -- no bare base64 can ever leak."""
    out = _resize_for_output(frame)
    ok, encoded = cv2.imencode(".jpg", out, [int(cv2.IMWRITE_JPEG_QUALITY), JPEG_QUALITY])
    if not ok:
        log(f"[PythonBridge] jpeg encode failed device={device}")
        return False
    h, w = out.shape[:2]
    emit(
        {
            "type": "frame",
            "device": device,
            "width": int(w),
            "height": int(h),
            "image": base64.b64encode(encoded.tobytes()).decode("ascii"),
        }
    )
    return True


class SnapshotCamera:
    """Poll /snapshot.jpg for latest JPEG frames.

    Avoids OpenCV VideoCapture MJPEG buffering which causes frozen display.
    Uses urllib.request + numpy.frombuffer + cv2.imdecode.
    """

    def __init__(self, url: str):
        self.url = url
        self._failures = 0
        self._frame_count = 0
        self._start_time = time.monotonic()

    def read(self):
        """Fetch one JPEG frame. Returns (ok, frame) matching cv2.VideoCapture API."""
        import numpy as np

        data = None
        try:
            with urllib.request.urlopen(self.url, timeout=2) as resp:
                data = resp.read()
        except (urllib.error.URLError, OSError) as exc:
            self._failures += 1
            log(f"[SnapshotCamera] fetch failed reason={exc} streak={self._failures}")
            return False, None

        if not data:
            self._failures += 1
            log(f"[SnapshotCamera] fetch failed reason=empty-response streak={self._failures}")
            return False, None

        arr = np.frombuffer(data, dtype=np.uint8)
        frame = cv2.imdecode(arr, cv2.IMREAD_COLOR)
        if frame_empty(frame):
            self._failures += 1
            log(f"[SnapshotCamera] fetch failed reason=decode-error bytes={len(data)} streak={self._failures}")
            return False, None

        self._failures = 0
        self._frame_count += 1
        h, w = frame.shape[:2]

        if self._frame_count == 1 or self._frame_count % 30 == 0:
            log(f"[SnapshotCamera] fetch ok bytes={len(data)} frame={w}x{h}")
            elapsed = time.monotonic() - self._start_time
            fps = self._frame_count / elapsed if elapsed > 0 else 0
            log(f"[SnapshotCamera] fps={fps:.1f} frames={self._frame_count}")

        return True, frame

    @property
    def consecutive_failures(self) -> int:
        return self._failures


def stream_http(url: str) -> int:
    """Read frames from an HTTP(S) source (Windows host camera bridge).

    Always uses snapshot polling (/snapshot.jpg) to avoid OpenCV VideoCapture
    MJPEG buffering which causes frozen/stale frames in the Qt display.
    """
    log(f"[CameraBridge] mode=URL url={url}")
    log(f"[PYTHON_BRIDGE_START] source={url}")
    log(f"[PythonBridge] python={sys.executable} cv2={getattr(cv2, '__version__', '?')} mode=http-snapshot")

    snapshot_url = re.sub(r'/camera\.mjpg\b', '/snapshot.jpg', url)
    log(f"[SnapshotCamera] using snapshot url={snapshot_url}")

    try:
        import numpy  # noqa: F401
    except ImportError as exc:
        emit({"type": "error", "message": f"numpy unavailable for snapshot camera: {exc}"})
        return 3

    cam = SnapshotCamera(snapshot_url)
    frame_interval = 1.0 / SNAPSHOT_FPS
    opened_emitted = False

    while not _STOP:
        loop_start = time.monotonic()

        ok, frame = cam.read()
        if not ok:
            if cam.consecutive_failures >= SNAPSHOT_MAX_FAILURES:
                message = "Windows 摄像头代理不可用，请检查 http://127.0.0.1:8765/health"
                log(f"[SnapshotCamera] giving up after {SNAPSHOT_MAX_FAILURES} consecutive failures")
                log(f"[CAMERA_ERROR] reason=snapshot-consecutive-failures url={snapshot_url}")
                log(f"[CameraBridge] {message}")
                emit({"type": "error", "message": message})
                return 2 if not opened_emitted else 4
            time.sleep(0.1)
            continue

        if not opened_emitted:
            emit({"type": "opened", "device": url})
            opened_emitted = True
            log(f"[CAMERA_PREVIEW] first frame received source={snapshot_url}")

        emit_frame(url, frame)

        elapsed = time.monotonic() - loop_start
        if elapsed < frame_interval:
            time.sleep(frame_interval - elapsed)

    return 0


def main(argv: list[str]) -> int:
    signal.signal(signal.SIGTERM, _handle_term)
    try:
        signal.signal(signal.SIGINT, _handle_term)
    except ValueError:
        pass  # not on the main thread; KeyboardInterrupt still handled below

    # Network camera proxy: if the first argument is an http(s) URL, pull frames
    # over HTTP instead of opening a local /dev/video* node. Local logic below is
    # kept intact for the no-proxy case.
    first_arg = argv[1].strip() if argv[1:] else ""
    if first_arg.lower().startswith(("http://", "https://")):
        try:
            return stream_http(first_arg)
        except KeyboardInterrupt:
            log("[PythonBridge] received stop signal (http mode)")
            return 0

    if argv[1:]:
        # Caller passed explicit paths: re-sort numerically so video0 is always
        # tried before video1, regardless of Qt's enumeration order.
        requested = sorted(argv[1:], key=_dev_index)
    else:
        requested = discover_devices()

    log_environment(discover_devices())

    if not requested:
        message = (
            "No /dev/video* found. Check VMware USB camera passthrough or camera "
            "permission (run: sudo usermod -aG video $USER, then log out and back in)."
        )
        log(f"[OpenCVCamera] {message}")
        emit({"type": "error", "message": message})
        return 2

    # Order: explicit arg(s) first, then the common nodes, then everything discovered.
    ordered: list[str] = []
    for candidate in requested + ["/dev/video0", "/dev/video1", "/dev/video2"] + discover_devices():
        if candidate not in ordered:
            ordered.append(candidate)

    # Pre-filter with an instant O_NONBLOCK probe so we skip the seconds-long stall
    # that cv2.VideoCapture() causes on a broken/absent V4L2 node.
    accessible = [d for d in ordered if quick_device_accessible(d)]
    if not accessible:
        message = (
            f"Found /dev/video* nodes {discover_devices()} but none could be opened. "
            "Check camera permission (sudo usermod -aG video $USER) or VMware USB "
            "camera passthrough."
        )
        log(f"[OpenCVCamera] {message}")
        emit({"type": "error", "message": message})
        return 2

    cap = None
    device = ""
    for candidate in accessible:
        if _STOP:
            return 0
        log(f"[CameraBridge] mode=V4L2 device={candidate}")
        log(f"[OpenCVCamera] trying {candidate} with CAP_V4L2")
        cap, _frame, _mode = open_device(candidate)
        if cap is not None:
            device = candidate
            break

    if cap is None:
        tried = ", ".join(accessible)
        message = (
            f"OpenCV V4L2 could not read a valid frame from any of: {tried}. "
            "The node(s) exist but produced no usable image -- check the camera "
            "is not in use by another app, or reconnect VMware USB passthrough."
        )
        log(f"[OpenCVCamera] {message}")
        emit({"type": "error", "message": message})
        return 2

    emit({"type": "opened", "device": device})
    try:
        return stream(cap, device)
    except KeyboardInterrupt:
        log("[PythonBridge] received stop signal, releasing camera")
        return 0
    finally:
        cap.release()
        log(f"[PythonBridge] released device={device}")


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
