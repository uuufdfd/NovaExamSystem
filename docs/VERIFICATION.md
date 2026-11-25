# 验证记录 · 2026-11-25

## 实际完成

| 检查 | 结果 | 范围 |
|---|---|---|
| Windows 服务端 Release 构建 | 通过 | Qt 6.11.0、MinGW-w64 GCC 13.1.0、Ninja、C++17 |
| Windows 客户端 Release 构建 | 通过 | 同 Kit，Qt Multimedia / resources / Windows 图标链接 |
| 客户端窗口渲染 | 通过有限检查 | 真实 LoginDialog / MainWindow 在 Windows offscreen 平台完成登录截图、演示教师在线登录、题库导航与截图，测试入口返回 0；非完整人工 UI 验收 |
| Node 隔离协议烟测 | 通过 | 分片请求、登录、错误密码、无 token 拒绝、学生管理接口拒绝、发布前后可见性、双请求发布去重、草稿、提交、阅卷、风险 6 分和冷却不重复计分 |
| 原 Python 烟测（发布版修正后） | 通过 | Python 3.14.3；一次性 SQLite，发布记录数量、快速重复发布、账号增改/重置/停用、阅卷与风险日志 |
| Python 语法 | 通过 | 发布版 7 个 `.py` 使用 `ast.parse` |
| Shell 语法 | 通过 | 发布版 9 个 `.sh` 使用 Git Bash `bash -n`；未执行安装脚本 |
| PowerShell 语法 | 通过 | 7 个 `.ps1`，PowerShell Parser；未创建计划任务 |
| 资源与 CMake 引用 | 构建通过 | 两个工程均成功生成目标，qrc 和 RC 资源有效 |
| Markdown 本地链接 | 发布检查 | 见 `scripts/validate-release.cjs`，不包含远端网页可用性保证 |
| XLSX 模板 | ZIP/XML 检查通过 | 5 个 ZIP 项，无隐藏附件；表头有效 Unicode，无替代字符 |
| 原始项目保护 | 通过 | 对复制清单中所有原文件重新计算 SHA-256，未发现变化 |

## 发现并处理

- 中文目录下 Qt `moc` 无法创建自动生成文件；将相同发布源码复制到英文临时路径后构建成功。没有为此修改业务代码，README 明确建议英文路径。
- 原 Python 烟测仍以切屏 30 分为预期，而当前 `RiskScoreManager` 是 6 分。仅修正测试断言为 6 分/低风险，未把业务规则改回旧版本。
- 原客户端构建只复制 `tools/`，发布版增加复制已有 `data/`，防止较深构建目录中找不到导入模板。
- 保留编译器对旧 Qt 时间 API 的弃用警告，不将无关 API 升级混入发布整理。

## 公开阶段追加验证

- 原创几何图形替换旧图标/Logo，正式客户端重新编译、截图渲染与独立 SQLite 协议烟测通过。
- 加强 GUI 检查发现 windeployqt 默认未部署 offscreen 插件，先前仅验证进程存活不足以证明窗口正常渲染。现已显式指定 Kit 平台插件并加载本机中文字体，截图测试返回 0；未隐藏错误或把挂起当成功。
- 既有 .qrc 未启用 AUTORCC，部分品牌资源未注册。仅在发布副本开启 CMake 选项并重新构建，未修改原始毕业设计。

## 截图复现

scripts/capture_ui_main.cpp 是可选测试入口，不参与正式构建。将客户端源码复制到新的临时目录，仅在副本以该测试入口替换 src/main.cpp，按原 CMake 构建；不可覆盖正式或原始项目入口。
启动新建一次性 SQLite 服务端后，设置 NOVA_TEST_PORT、NOVA_SCREENSHOT_DIR；Windows offscreen 环境设置 QT_QPA_PLATFORM=offscreen、QT_QPA_PLATFORM_PLUGIN_PATH 为对应 Kit 的 plugins/platforms，并以 NOVA_CAPTURE_FONT 指向本机已安装中文字体文件。
测试捕获真实登录窗口，在线登录演示教师后切换题目管理并捕获窗口。端口只用于隔离测试，不改变默认 9000；不上传系统字体或运行数据库。

## 可复现命令

先按 README 以匹配 Qt Kit 构建。启动 Node 测试前请确保本机测试端口 `19090` 空闲；该端口只用于隔离测试，应用默认端口仍为 `9000`。

```powershell
$env:SERVER_EXE = (Resolve-Path ./build-server/QtExamServer.exe).Path
node scripts/smoke.cjs
node scripts/validate-release.cjs
```

Node 测试会自行启动/停止一个服务端，SQLite 文件只写入新的系统临时目录，不连接原业务库，不输出会话 token。Node 只是可选验证工具，不是应用运行依赖。

原 Python 测试会打印完整业务响应，日志可能含 token，因此不应提交其原始输出；测试必须面向新建一次性数据库。

## 未实际验证

- 本机 Qt 安装没有 QMYSQL 插件，未执行真实 MySQL 启动、迁移和完整业务流。
- 未使用 Qt 6.5.3 编译，不将 Qt 6.11.0 成功等同于 6.5.3 已验证。
- 未重新进入 Linux/Ubuntu/麒麟机器测试，未验证 ARM 或其他国产发行版。
- 未验证真实摄像头、ONNX 推理、人脸核验准确率、跨机宿主桥接。
- 未做完整人工 UI 操作、长时间运行、并发容量/性能测试或完整安全渗透测试。

截图渲染检查不替代上述验证；未上传论文、答辩材料或原运行数据库。原始构建/烟测日志与审计明细仅留在本机发布目录之外。
