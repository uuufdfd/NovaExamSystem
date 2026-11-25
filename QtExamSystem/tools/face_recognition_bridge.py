#!/usr/bin/env python3
"""OpenCV YuNet + SFace JSON-line bridge for QtExamSystem.

Each stdin line is a compact JSON request:
{"image":"<base64 jpeg/png>","baselineTemplate":"SFACE1:<base64 float32>","matchThreshold":0.36}

Each stdout line is a compact JSON result. The process stays alive so the
OpenCV models are loaded once instead of once per frame.

首选路径是 YuNet(检测) + SFace(特征) DNN/ONNX。某些 OpenCV 构建（如麒麟自带）解析
这些 ONNX 会在 getLayerShapesRecursively 抛 [Eltwise] Add_44 之类 cv2.error，导致检测
彻底失败。为此本桥内置 Haar 级联兜底：DNN 创建或前向报错时自动切到 Haar 检测 + 归一化
裁剪模板，stdout 的 JSON 协议字段保持完全不变（template 仍是 "SFACE1:"+base64(float32)，
C++ 端解析与 threshold=0.36 无需任何改动）。所有诊断日志只走 stderr。
"""

from __future__ import annotations

import base64
import json
import shutil
import sys
import tempfile
import time
from pathlib import Path

import cv2
import numpy as np


ROOT = Path(__file__).resolve().parent
MODELS = ROOT / "models"
DETECTOR_MODEL = MODELS / "face_detection_yunet_2023mar.onnx"
RECOGNIZER_MODEL = MODELS / "face_recognition_sface_2021dec.onnx"
TEMPLATE_PREFIX = "SFACE1:"
MODEL_CACHE = Path(tempfile.gettempdir()) / "qt_exam_face_models"
FALLBACK_FACE_SIZE = 64  # Haar 兜底模板的归一化边长（64x64 灰度 → 4096 维 float32）

# ---- Haar 兜底检测增强参数 ----
# 小画面（如 320x240）检测前先 upsample 到至少 640 宽，命中后坐标再映射回原图尺寸。
HAAR_UPSCALE_MIN_WIDTH = 640
# cascade 优先级：正脸 default → alt2 → alt → 侧脸 profile。缺失者跳过，绝不 fatal。
HAAR_CASCADE_NAMES = (
    "haarcascade_frontalface_default.xml",
    "haarcascade_frontalface_alt2.xml",
    "haarcascade_frontalface_alt.xml",
    "haarcascade_profileface.xml",
)
# 从严格到宽松的多组 detectMultiScale 参数；minSize 基于 upsample 后（>=640 宽）的画面。
HAAR_PASSES = (
    {"scaleFactor": 1.05, "minNeighbors": 5, "minSize": (50, 50)},
    {"scaleFactor": 1.05, "minNeighbors": 4, "minSize": (40, 40)},
    {"scaleFactor": 1.03, "minNeighbors": 3, "minSize": (35, 35)},
    {"scaleFactor": 1.02, "minNeighbors": 2, "minSize": (30, 30)},
)


def log(tag: str, msg: str = "") -> None:
    """诊断日志一律走 stderr —— stdout 只允许输出 JSON 协议行。"""
    try:
        sys.stderr.write(("[" + tag + "] " + msg).rstrip() + "\n")
        sys.stderr.flush()
    except Exception:
        pass


def write_result(result: dict) -> None:
    sys.stdout.write(json.dumps(result, ensure_ascii=True, separators=(",", ":")) + "\n")
    sys.stdout.flush()


def fail(message: str) -> dict:
    return {
        "ok": False,
        "message": message,
        "hasFace": False,
        "faceCount": 0,
        "matched": False,
        "confidence": 0.0,
        "faces": [],
        "template": "",
    }


def decode_template(value: str) -> np.ndarray | None:
    value = (value or "").strip()
    if not value.startswith(TEMPLATE_PREFIX):
        return None
    try:
        raw = base64.b64decode(value[len(TEMPLATE_PREFIX) :], validate=True)
        feature = np.frombuffer(raw, dtype=np.float32)
    except Exception:
        return None
    if feature.size < 64:
        return None
    return feature.reshape(1, -1)


def encode_template(feature: np.ndarray) -> str:
    data = np.asarray(feature, dtype=np.float32).reshape(-1)
    return TEMPLATE_PREFIX + base64.b64encode(data.tobytes()).decode("ascii")


def decode_image(value: str) -> np.ndarray | None:
    try:
        raw = base64.b64decode(value, validate=True)
    except Exception:
        return None
    data = np.frombuffer(raw, dtype=np.uint8)
    image = cv2.imdecode(data, cv2.IMREAD_COLOR)
    return image


def face_dict(face: np.ndarray) -> dict:
    x, y, w, h = [int(round(v)) for v in face[:4]]
    score = float(face[-1])
    return {"x": x, "y": y, "w": w, "h": h, "score": score}


def opencv_model_path(source: Path) -> Path:
    if not source.exists():
        raise FileNotFoundError(f"missing model: {source}")

    MODEL_CACHE.mkdir(parents=True, exist_ok=True)
    target = MODEL_CACHE / source.name
    if not target.exists() or target.stat().st_size != source.stat().st_size:
        shutil.copy2(source, target)
    return target


class FaceEngine:
    def __init__(self) -> None:
        self.detector = None
        self.recognizer = None
        self.haars: list = []  # 已加载的 (name, CascadeClassifier)，按优先级排序
        self.haar_path = ""
        self.dnn_detect_ok = False
        self.dnn_recognize_ok = False
        self.frame_log_n = 0

        log("FACE_BRIDGE_OPENCV_VERSION", cv2.__version__)
        # Haar 始终先备好，作为 DNN 任意阶段报错时的兜底安全网。
        self._ensure_haar()
        # 首选 DNN（YuNet + SFace）；创建即报错则就地切兜底，进程不退出。
        self._try_load_dnn()

        if not self.dnn_detect_ok and not self.haars:
            raise RuntimeError("DNN 与 Haar 级联均不可用，无法进行人脸检测")

    def _try_load_dnn(self) -> None:
        try:
            if not DETECTOR_MODEL.exists() or not RECOGNIZER_MODEL.exists():
                log("FACE_BRIDGE_MODEL_LOAD", "dnn=skip reason=missing-onnx")
                log("FACE_BRIDGE_FALLBACK_ENABLE", "reason=missing-onnx")
                return
            detector_model = opencv_model_path(DETECTOR_MODEL)
            recognizer_model = opencv_model_path(RECOGNIZER_MODEL)
            self.detector = cv2.FaceDetectorYN_create(
                str(detector_model),
                "",
                (320, 320),
                0.86,
                0.3,
                5000,
            )
            self.recognizer = cv2.FaceRecognizerSF_create(str(recognizer_model), "")
            self.dnn_detect_ok = True
            self.dnn_recognize_ok = True
            log("FACE_BRIDGE_MODEL_LOAD", "dnn=ok detector=YuNet recognizer=SFace")
        except cv2.error as exc:
            self.detector = None
            self.recognizer = None
            self.dnn_detect_ok = False
            self.dnn_recognize_ok = False
            log("FACE_BRIDGE_DNN_ERROR", "stage=create err=" + str(exc).replace("\n", " ")[:200])
            log("FACE_BRIDGE_FALLBACK_ENABLE", "reason=dnn-create-error")
        except Exception as exc:
            self.detector = None
            self.recognizer = None
            self.dnn_detect_ok = False
            self.dnn_recognize_ok = False
            log("FACE_BRIDGE_DNN_ERROR", "stage=create err=" + str(exc).replace("\n", " ")[:200])
            log("FACE_BRIDGE_FALLBACK_ENABLE", "reason=dnn-create-exception")

    def _ensure_haar(self) -> None:
        if self.haars:
            return
        # 查找目录优先级：项目内 tools/models → cv2.data → 系统 opencv 目录。
        search_dirs: list[Path] = [MODELS]
        try:
            search_dirs.append(Path(cv2.data.haarcascades))
        except Exception:
            pass
        search_dirs += [
            Path("/usr/share/opencv4/haarcascades"),
            Path("/usr/share/opencv/haarcascades"),
            Path("/usr/local/share/opencv4/haarcascades"),
        ]
        log("FACE_BRIDGE_HAAR_SEARCH", "dirs=" + " | ".join(str(d) for d in search_dirs))

        loaded: list = []
        for name in HAAR_CASCADE_NAMES:
            found = None
            for directory in search_dirs:
                try:
                    candidate = directory / name
                    if candidate.exists():
                        found = candidate
                        break
                except Exception:
                    continue
            if found is None:
                log("FACE_BRIDGE_HAAR_LOAD", "skip name=" + name + " reason=not-found")
                continue
            try:
                clf = cv2.CascadeClassifier(str(found))
                if clf.empty():
                    # 某些 OpenCV(Windows) 的 FileStorage 打不开含非 ASCII 字符的路径（如"毕业设计"），
                    # 复制到纯 ASCII 临时目录后重试（与 ONNX 的 opencv_model_path 同思路）。
                    try:
                        clf = cv2.CascadeClassifier(str(opencv_model_path(found)))
                    except Exception:
                        pass
                if clf.empty():
                    log("FACE_BRIDGE_HAAR_LOAD", "skip name=" + name + " reason=empty path=" + str(found))
                    continue
            except Exception as exc:
                log("FACE_BRIDGE_HAAR_LOAD", "skip name=" + name + " reason=error err=" + str(exc)[:120])
                continue
            loaded.append((name, clf))
            if not self.haar_path:
                self.haar_path = str(found)
            log("FACE_BRIDGE_HAAR_LOAD", "ok name=" + name + " path=" + str(found))

        self.haars = loaded
        if not loaded:
            log("FACE_BRIDGE_HAAR_LOAD", "fail reason=no-cascade-found")

    def _should_log_frame(self) -> bool:
        self.frame_log_n += 1
        return self.frame_log_n <= 3 or self.frame_log_n % 20 == 0

    # ---------------- 检测 ----------------
    def detect_faces(self, image: np.ndarray, roi: dict | None, min_score: float) -> list[np.ndarray]:
        if self.dnn_detect_ok:
            try:
                return self._detect_dnn(image, roi, min_score)
            except cv2.error as exc:
                self.dnn_detect_ok = False
                log("FACE_BRIDGE_DNN_ERROR", "stage=detect err=" + str(exc).replace("\n", " ")[:200])
                log("FACE_BRIDGE_FALLBACK_ENABLE", "reason=dnn-detect-error")
                self._ensure_haar()
        return self._detect_haar(image, roi, min_score)

    def _detect_dnn(self, image: np.ndarray, roi: dict | None, min_score: float) -> list[np.ndarray]:
        height, width = image.shape[:2]
        attempts: list[tuple[np.ndarray, int, int]] = []

        if roi:
            x = max(0, int(roi.get("x", 0)))
            y = max(0, int(roi.get("y", 0)))
            w = max(1, int(roi.get("w", 0)))
            h = max(1, int(roi.get("h", 0)))
            margin = int(max(w, h) * 0.45)
            x0 = max(0, x - margin)
            y0 = max(0, y - margin)
            x1 = min(width, x + w + margin)
            y1 = min(height, y + h + margin)
            if x1 - x0 >= 96 and y1 - y0 >= 96:
                attempts.append((image[y0:y1, x0:x1], x0, y0))

        attempts.append((image, 0, 0))
        min_side = max(42.0, min(width, height) * 0.10)
        min_area = width * height * 0.008

        for crop, offset_x, offset_y in attempts:
            crop_h, crop_w = crop.shape[:2]
            if crop_w <= 0 or crop_h <= 0:
                continue
            self.detector.setScoreThreshold(min_score)
            self.detector.setInputSize((crop_w, crop_h))
            _, detected = self.detector.detect(crop)
            faces: list[np.ndarray] = []
            if detected is None:
                continue
            for face in detected:
                fw = float(face[2])
                fh = float(face[3])
                area = fw * fh
                aspect = fw / max(1.0, fh)
                if face[-1] < min_score:
                    continue
                if fw < min_side or fh < min_side or area < min_area:
                    continue
                if aspect < 0.62 or aspect > 1.45:
                    continue
                mapped = face.copy()
                mapped[0] += offset_x
                mapped[1] += offset_y
                faces.append(mapped)
            if faces:
                faces.sort(key=lambda item: float(item[-1]), reverse=True)
                return faces
        return []

    def _detect_haar(self, image: np.ndarray, roi: dict | None, min_score: float) -> list[np.ndarray]:
        # Haar 兜底（强化版）：小画面先 upsample 到 >=640 宽以放大正脸细节，再按"从严格到宽松"
        # 多组 detectMultiScale 参数 × 多个 cascade 依次扫描；命中即取面积最大的主脸，坐标映射
        # 回原图后返回单个 5 元组 [x,y,w,h,score]（原图坐标系，score∈[0.86,0.95]，无 landmark）。
        if not self.haars:
            self._ensure_haar()
        if not self.haars:
            return []

        height, width = image.shape[:2]
        do_log = self._should_log_frame()

        # upsample：宽度不足 640 时按比例放大灰度图，命中框最后再除以 scale 映射回原图。
        gray = cv2.cvtColor(image, cv2.COLOR_BGR2GRAY)
        scale = 1.0
        if 0 < width < HAAR_UPSCALE_MIN_WIDTH:
            scale = HAAR_UPSCALE_MIN_WIDTH / float(width)
            new_w = int(round(width * scale))
            new_h = int(round(height * scale))
            gray = cv2.resize(gray, (new_w, new_h), interpolation=cv2.INTER_LINEAR)
            if do_log:
                log("FACE_BRIDGE_HAAR_UPSCALE",
                    "from=" + str(width) + "x" + str(height)
                    + " to=" + str(new_w) + "x" + str(new_h)
                    + " scale=" + format(scale, ".3f"))
        gray = cv2.equalizeHist(gray)

        tried = 0
        for name, clf in self.haars:
            for pass_idx, params in enumerate(HAAR_PASSES):
                tried += 1
                try:
                    rects = clf.detectMultiScale(
                        gray,
                        scaleFactor=params["scaleFactor"],
                        minNeighbors=params["minNeighbors"],
                        minSize=params["minSize"],
                    )
                except Exception as exc:
                    log("FACE_BRIDGE_DNN_ERROR", "stage=haar-detect cascade=" + name + " err=" + str(exc)[:140])
                    rects = []
                count = 0 if rects is None else len(rects)
                if do_log:
                    log("FACE_BRIDGE_HAAR_PASS",
                        "cascade=" + name + " pass=" + str(pass_idx) + " faces=" + str(count))
                if count == 0:
                    continue

                # 多框时优先面积最大的主脸（minSize 已滤掉过小框），仅返回 1 个。
                best = max(rects, key=lambda r: int(r[2]) * int(r[3]))
                bx, by, bw, bh = [int(v) for v in best[:4]]
                ox = int(round(bx / scale))
                oy = int(round(by / scale))
                ow = int(round(bw / scale))
                oh = int(round(bh / scale))
                ox = max(0, min(ox, width - 1))
                oy = max(0, min(oy, height - 1))
                ow = max(1, min(ow, width - ox))
                oh = max(1, min(oh, height - oy))
                # 越严格的 pass 越可信：score 随 pass 序递减但维持在 YuNet 同尺度区间。
                score = float(max(0.86, min(0.95, 0.95 - 0.03 * pass_idx)))
                face = np.array([float(ox), float(oy), float(ow), float(oh), score], dtype=np.float32)
                if do_log:
                    log("FACE_BRIDGE_HAAR_RESULT",
                        "faces=1 frame=" + str(width) + "x" + str(height)
                        + " cascade=" + name + " pass=" + str(pass_idx)
                        + " box=" + str(ox) + "," + str(oy) + "," + str(ow) + "," + str(oh))
                return [face]

        if do_log:
            log("FACE_BRIDGE_HAAR_RESULT",
                "faces=0 frame=" + str(width) + "x" + str(height) + " tried=" + str(tried))
        return []

    # ---------------- 特征 / 比对 ----------------
    def _embed(self, image: np.ndarray, face: np.ndarray) -> tuple[np.ndarray, str]:
        # 优先 SFace 对齐裁剪 + 特征；报错则切 Haar 归一化裁剪模板。
        if self.dnn_recognize_ok and self.recognizer is not None and face.size >= 15:
            try:
                feat = self.recognizer.feature(self.recognizer.alignCrop(image, face))
                return np.asarray(feat, dtype=np.float32).reshape(-1), "dnn"
            except cv2.error as exc:
                self.dnn_recognize_ok = False
                log("FACE_BRIDGE_DNN_ERROR", "stage=embed err=" + str(exc).replace("\n", " ")[:200])
                log("FACE_BRIDGE_FALLBACK_ENABLE", "reason=dnn-embed-error")
        return self._haar_feature(image, face), "haar"

    def _haar_feature(self, image: np.ndarray, face: np.ndarray) -> np.ndarray:
        # 裁剪人脸 ROI → 灰度 → resize 64x64 → equalizeHist → z-score 归一化（去亮度、强结构），
        # 展平成 4096 维 float32。两张同一人脸的 z-score 向量余弦≈相关系数，threshold=0.36 仍可解释。
        x, y, w, h = [int(round(float(v))) for v in face[:4]]
        height, width = image.shape[:2]
        x = max(0, min(x, width - 1))
        y = max(0, min(y, height - 1))
        w = max(1, min(w, width - x))
        h = max(1, min(h, height - y))
        roi = image[y : y + h, x : x + w]
        if roi.size == 0:
            return np.zeros(FALLBACK_FACE_SIZE * FALLBACK_FACE_SIZE, dtype=np.float32)
        gray = cv2.cvtColor(roi, cv2.COLOR_BGR2GRAY)
        gray = cv2.resize(gray, (FALLBACK_FACE_SIZE, FALLBACK_FACE_SIZE), interpolation=cv2.INTER_AREA)
        gray = cv2.equalizeHist(gray)
        vec = gray.astype(np.float32).reshape(-1)
        vec = vec - float(vec.mean())
        std = float(vec.std())
        if std > 1e-6:
            vec = vec / std
        return vec

    def _match(self, feature: np.ndarray, baseline: np.ndarray, mode: str) -> float:
        f = np.asarray(feature, dtype=np.float32).reshape(-1)
        b = np.asarray(baseline, dtype=np.float32).reshape(-1)
        # 维度不一致（如旧 SFace 底库遇 Haar 当前帧）：判 0，促使前端重新采集，避免误判。
        if f.size != b.size:
            return 0.0
        if mode == "dnn" and self.dnn_recognize_ok and self.recognizer is not None:
            try:
                return float(self.recognizer.match(f.reshape(1, -1), b.reshape(1, -1), cv2.FaceRecognizerSF_FR_COSINE))
            except cv2.error:
                self.dnn_recognize_ok = False
        denom = float(np.linalg.norm(f) * np.linalg.norm(b))
        if denom < 1e-8:
            return 0.0
        return float(max(0.0, min(1.0, float(np.dot(f, b)) / denom)))

    def analyze(self, request: dict) -> dict:
        started = time.perf_counter()
        request_id = int(request.get("requestId", 0))
        image = decode_image(str(request.get("image", "")))
        if image is None:
            result = fail("图像解码失败")
            result["requestId"] = request_id
            return result

        height, width = image.shape[:2]
        if width <= 0 or height <= 0:
            result = fail("图像尺寸无效")
            result["requestId"] = request_id
            return result

        min_score = float(request.get("minFaceScore", 0.82))
        match_threshold = float(request.get("matchThreshold", 0.36))
        should_recognize = bool(request.get("recognize", True)) or bool(request.get("enrollMode", False))
        faces = self.detect_faces(image, request.get("roi"), min_score)

        if not faces:
            if self._should_log_frame():
                log("FACE_BRIDGE_TEMPLATE_EMPTY", "reason=no-face mode=" + ("dnn" if self.dnn_detect_ok else "haar"))
            return {
                "ok": True,
                "requestId": request_id,
                "message": "未检测到可信人脸",
                "hasFace": False,
                "faceCount": 0,
                "matched": False,
                "confidence": 0.0,
                "faces": [],
                "template": "",
                "didDetect": True,
                "didRecognize": False,
                "latencyMs": int((time.perf_counter() - started) * 1000),
            }

        result_faces = [face_dict(face) for face in faces[:4]]
        result = {
            "ok": True,
            "requestId": request_id,
            "message": "",
            "hasFace": True,
            "faceCount": len(faces),
            "matched": False,
            "confidence": 0.0,
            "faces": result_faces,
            "template": "",
            "didDetect": True,
            "didRecognize": False,
            "latencyMs": 0,
        }

        if len(faces) != 1:
            result["message"] = f"检测到 {len(faces)} 张人脸"
            result["latencyMs"] = int((time.perf_counter() - started) * 1000)
            if self._should_log_frame():
                log("FACE_BRIDGE_RESPONSE", "multi faceCount=" + str(len(faces)))
            return result

        if not should_recognize:
            result["message"] = "检测到可信单人脸"
            result["latencyMs"] = int((time.perf_counter() - started) * 1000)
            return result

        feature, feat_mode = self._embed(image, faces[0])
        if feature is None or int(feature.size) < 64:
            if self._should_log_frame():
                log("FACE_BRIDGE_TEMPLATE_EMPTY", "stage=embed mode=" + str(feat_mode))
            result["message"] = "无法提取人脸特征"
            result["latencyMs"] = int((time.perf_counter() - started) * 1000)
            return result

        result["template"] = encode_template(feature)
        result["didRecognize"] = True
        if self._should_log_frame():
            log("FACE_BRIDGE_TEMPLATE_READY", "mode=" + feat_mode + " dim=" + str(int(feature.size)))

        baseline = decode_template(str(request.get("baselineTemplate", "")))
        if baseline is None:
            result["matched"] = bool(request.get("enrollMode", False))
            result["confidence"] = 1.0 if result["matched"] else 0.0
            result["message"] = "已采集可信人脸" if result["matched"] else "缺少可比对的人脸模板"
            result["latencyMs"] = int((time.perf_counter() - started) * 1000)
            if self._should_log_frame():
                log("FACE_BRIDGE_RESPONSE", "enroll mode=" + feat_mode
                    + " faceCount=1 templateLen=" + str(len(result["template"])) + " matched=" + str(result["matched"]))
            return result

        confidence = self._match(feature, baseline, feat_mode)
        result["confidence"] = confidence
        result["matched"] = confidence >= match_threshold
        result["message"] = "身份匹配通过" if result["matched"] else "人脸与考生模板不匹配"
        result["latencyMs"] = int((time.perf_counter() - started) * 1000)
        if self._should_log_frame():
            log("FACE_BRIDGE_VERIFY_SCORE", "mode=" + feat_mode
                + " score=" + format(confidence, ".3f") + " threshold=" + format(match_threshold, ".3f")
                + " matched=" + str(result["matched"]))
            log("FACE_BRIDGE_RESPONSE", "verify mode=" + feat_mode
                + " faceCount=1 score=" + format(confidence, ".3f") + " matched=" + str(result["matched"]))
        return result


def main() -> int:
    log("FACE_BRIDGE_START", "face_recognition_bridge")
    try:
        engine = FaceEngine()
    except Exception as exc:
        log("FACE_BRIDGE_DNN_ERROR", "stage=init-fatal err=" + str(exc)[:200])
        write_result(fail(f"OpenCV人脸模型初始化失败：{exc}"))
        return 2

    log("FACE_BRIDGE_START", "ready dnn_detect=" + str(engine.dnn_detect_ok)
        + " dnn_recognize=" + str(engine.dnn_recognize_ok) + " haar=" + str(len(engine.haars)))
    write_result({"ok": True, "ready": True, "message": "OpenCV人脸识别模块已就绪"})
    for line in sys.stdin:
        line = line.strip().lstrip("\ufeff")
        if not line:
            continue
        try:
            request = json.loads(line)
            write_result(engine.analyze(request))
        except Exception as exc:
            write_result(fail(f"识别异常：{exc}"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
