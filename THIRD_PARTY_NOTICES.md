# 第三方依赖与授权核对

本仓库不打包 Qt/OpenCV 二进制、Python 运行时、模型权重或 Haar XML。

| 依赖 | 用途 | 授权/处理 |
|---|---|---|
| Qt Core / Widgets / Network / Xml / Multimedia / Sql | 桌面界面、网络、数据库、摄像头 | 按所安装版本及模块的 Qt 商业或开源许可使用；发布二进制时另行满足动态链接及许可义务 |
| OpenCV Python / NumPy | 人脸推理与图像处理 | 用户自行安装，遵守各发行版本许可 |
| YuNet | 人脸检测 | 上游该模型目录采用 MIT，未随本仓库再分发 |
| SFace | 人脸特征比较 | 上游该模型目录采用 Apache-2.0，未随本仓库再分发 |
| 平台字体 | 文本显示 | 使用用户系统已安装字体，不分发字体文件 |

模型许可来源（2026-11-25 核对）：
- [YuNet License](https://github.com/opencv/opencv_zoo/blob/main/models/face_detection_yunet/LICENSE)
- [SFace License](https://github.com/opencv/opencv_zoo/blob/main/models/face_recognition_sface/LICENSE)
- [Qt 开源许可](https://www.qt.io/licensing/open-source-lgpl-obligations)

## 本项目自有内容与排除项

项目所有者已于 2026-11-25 确认公开，自有代码采用 MIT，学校不存在其已知的额外公开限制。项目样式、源码内置少量演示题及模板属于该自有项目发布内容，不引入外部题库汇编。

原 PNG/ICO 图标、Logo 未附可靠的创作来源记录，已从公开快照排除，替换为 scripts/generate_branding.py 以几何图元生成的原创 N 标记（MIT）。生成器不读取旧素材，不依赖外部图标、字体或 AI 模型；Pillow 仅为可选开发工具，不打包其库文件。
含旧 Logo 的历史截图不再分发。新截图由实际 Qt 窗口及合成演示库捕获，不是 AI 界面图；仅使用本机字体渲染，不分发字体文件。

Qt/OpenCV/NumPy、可选 Pillow 和模型继续适用各自发行版许可证，本项目 MIT 不重新授权第三方组件。部署二进制前仍需核对对应模块、插件及运行库的具体条款。
