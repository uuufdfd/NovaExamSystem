#!/usr/bin/env bash
# Standalone camera diagnostic for QtExamSystem on Kylin/Linux.
# Run WITHOUT sudo. It only reads state and tries to open the camera the same way
# the Qt app's Python bridge does, so its result should match the in-app behaviour.
#
#   bash tools/check_camera_kylin.sh
#
# It never modifies the system; if your user lacks camera permission it prints the
# exact command you need to run yourself.

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
BRIDGE="${SCRIPT_DIR}/camera_capture_bridge.py"

# Prefer the project's venv interpreter (it has opencv-python), matching the app.
PYTHON=""
for cand in "${QTEXAM_PYTHON:-}" "${PROJECT_DIR}/.venv/bin/python" "${PROJECT_DIR}/venv/bin/python" python3 python; do
    [ -z "${cand}" ] && continue
    if command -v "${cand}" >/dev/null 2>&1 || [ -x "${cand}" ]; then
        PYTHON="${cand}"
        break
    fi
done

echo "==================================================================="
echo " QtExamSystem camera diagnostic (Kylin/Linux)"
echo " project : ${PROJECT_DIR}"
echo " python  : ${PYTHON:-<none found>}"
echo "==================================================================="

echo
echo "----- 1. /dev/video* nodes (ls -l) -----"
if ls /dev/video* >/dev/null 2>&1; then
    ls -l /dev/video*
else
    echo "  NONE FOUND. If this is a VM, the USB camera passthrough is probably"
    echo "  disconnected. In VMware: VM > Removable Devices > <camera> > Connect."
fi

echo
echo "----- 2. current user groups -----"
groups
if groups | tr ' ' '\n' | grep -qx video; then
    echo "  OK: user is in the 'video' group."
else
    echo "  WARNING: user is NOT in the 'video' group. To grant camera access run:"
    echo "      sudo usermod -aG video \$USER"
    echo "  then LOG OUT and back in (or reboot) for it to take effect."
fi

echo
echo "----- 3. v4l2-ctl --list-devices -----"
if command -v v4l2-ctl >/dev/null 2>&1; then
    v4l2-ctl --list-devices || true
else
    echo "  v4l2-ctl not installed (optional). Install with: sudo apt install v4l-utils"
fi

echo
echo "----- 4. python 'import cv2' -----"
if [ -n "${PYTHON}" ]; then
    "${PYTHON}" -c "import cv2; print('  cv2 OK, version', cv2.__version__)" \
        || echo "  cv2 IMPORT FAILED. Install: sudo apt install -y python3-opencv"
else
    echo "  No python interpreter found."
fi

echo
echo "----- 5. quick OpenCV open+read test per /dev/video* -----"
if [ -n "${PYTHON}" ] && ls /dev/video* >/dev/null 2>&1; then
    "${PYTHON}" - <<'PYEOF'
import glob, re, sys
try:
    import cv2
except Exception as exc:
    print("  cannot import cv2:", exc); sys.exit(0)

def idx(p):
    m = re.search(r"(\d+)$", p); return int(m.group(1)) if m else 999

for dev in sorted(glob.glob("/dev/video[0-9]*"), key=idx):
    cap = cv2.VideoCapture(dev, cv2.CAP_V4L2)
    cap.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc(*"MJPG"))
    cap.set(cv2.CAP_PROP_FRAME_WIDTH, 640)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 480)
    opened = cap.isOpened()
    ok, frame = (False, None)
    for _ in range(8):
        ok, frame = cap.read()
        if ok and frame is not None and frame.size:
            break
    if ok and frame is not None and frame.size:
        mean = sum(cv2.mean(frame)[:3]) / 3.0
        tag = "OK" if mean >= 5.0 else "OPENS BUT BLACK"
        print(f"  {dev}: {tag} isOpened={opened} shape={frame.shape} mean={mean:.1f}")
    else:
        print(f"  {dev}: FAILED isOpened={opened} readOk={ok}")
    cap.release()
PYEOF
else
    echo "  Skipped (no python or no /dev/video* devices)."
fi

echo
echo "----- 6. end-to-end bridge smoke test (5s) -----"
if [ -n "${PYTHON}" ] && [ -f "${BRIDGE}" ] && ls /dev/video* >/dev/null 2>&1; then
    echo "  Running the real bridge for 5s; you should see 'opened' + 'frame' JSON on stdout:"
    timeout 5 "${PYTHON}" "${BRIDGE}" 2>&1 | grep -E '"type"|\[OpenCVCamera\]|\[PythonBridge\]' | head -n 20 || true
else
    echo "  Skipped (no python, missing bridge, or no /dev/video* devices)."
fi

echo
echo "==================================================================="
echo " Done. If section 5/6 show 'OK', the Qt app should preview correctly."
echo " If they show 'OPENS BUT BLACK', uncover the lens / close other camera"
echo " apps. If 'FAILED' or no devices, fix permission/passthrough above."
echo "==================================================================="
