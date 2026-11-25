# 人脸模型（不随仓库分发）

从官方 [OpenCV Zoo](https://github.com/opencv/opencv_zoo) 对应模型目录获取：

- `face_detection_yunet_2023mar.onnx`
- `face_recognition_sface_2021dec.onnx`

放在本目录，保留上游许可并核对下载来源。Git LFS 指针不是可用模型。
模型与 OpenCV 版本是否兼容必须在目标机器验证；本轮未验证真实摄像头推理。
Haar 回退与 SFace 不同，不应混用模板或宣称等价识别精度。
本仓库不包含生成式 AI 或大模型自动阅卷功能。
