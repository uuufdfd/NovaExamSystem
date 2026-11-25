# 编译、部署与回退

## 通用前置条件

仓库根目录不是新的顶层 CMake 工程；分别构建 `QtExamSystem` 和 `QtExamServer`。
客户端要求 Qt6；服务端可回退 Qt5。Qt Creator 中请选择实际安装且与 Qt 库 ABI 一致的 Kit。
完整命令见根 README。Windows 建议解压到英文路径，中文路径可能导致 `moc` 无法创建输出文件。

客户端 CMake 自动调用可用的 `windeployqt`，并复制 `tools/`。手动拷贝 `QtExamSystem/data/` 到可执行文件旁可确保导入模板可用。
客户端配置文件在可执行文件同目录；复制 `config/client_config.example.ini` 为 `client_config.ini` 后修改主机，不改变端口 9000。
服务端 `QT_EXAM_DB_PASSWORD` 是实际读取的环境变量；主机、数据库名、用户名通过 CLI 或 systemd 模板参数传递，单独 export 同名变量不会自动改变裸二进制的 CLI 默认值。

## MySQL

必须存在 QMYSQL 插件及匹配的 MySQL/MariaDB 客户端库；缺少驱动不是业务代码错误。
`scripts/init_mysql.sql` 仅建库建用户，运行前替换 `CHANGE_ME_BEFORE_RUNNING`；表由服务端创建。
`seed_mysql.sql` 是可选的演示账号补充，不含完整表结构；普通启动不需要额外执行。
不发布原始数据库、真实考试数据、照片或人脸特征。SQLite 测试通过不等价于 MySQL 验证通过。

## Linux systemd

仅在测试服务器使用；会创建系统用户、写 `/opt`、`/etc` 并启动服务。
先在仓库内构建原部署脚本预期的路径：

```bash
cmake -S QtExamServer -B QtExamServer/build -DCMAKE_BUILD_TYPE=Release
cmake --build QtExamServer/build --parallel 2
cd QtExamServer
read -rs QT_EXAM_DB_PASSWORD
export QT_EXAM_DB_PASSWORD
bash deploy/init_mysql.sh
bash deploy/install_service.sh
systemctl status qt-exam-server --no-pager
```

初始化脚本会更新指定数据库用户密码，仅限新建演示环境；密码需至少 16 位且使用脚本支持字符。切勿对真实业务库执行。
环境文件 `/etc/qt-exam-server.env` 权限为 `600`；服务从环境读取密码，不把密码写入启动参数。
服务模板监听 `0.0.0.0:9000`，必须自行配置防火墙/私网限制。不要为了演示直接开放公网。
故障时先看 `journalctl -u qt-exam-server`；不要把含个人数据的日志上传。

## 人脸与可选 VM 桥接

安装 Python3、OpenCV 和 NumPy，并按 `QtExamSystem/tools/models/README.md` 获取模型。
`QTEXAM_PYTHON` 可指定 Python 解释器；虚拟环境也可由麒麟依赖脚本创建。
默认使用本地摄像头。虚拟机确需宿主机辅助时，显式配置以下环境变量：

| 变量 | 示例用途 |
|---|---|
| `QTEXAM_CAMERA_URL` | 宿主机 8765 的 MJPEG 摄像头地址 |
| `QTEXAM_HOST_FILE_PICKER_URL` | 宿主机 8766 的 `/pick-question-file` |
| `QTEXAM_QUESTION_BANK_SOURCE` | 宿主机 9200 题库目录代理 |

相关工具在 `QtExamSystem/tools/`，需人工阅读后启用。它们缺少生产鉴权，不会随本发布版自动开启、防火墙放行或安装计划任务。
未启用桥接时，Linux 用户可通过文件菜单使用本地/共享目录导入备用入口。

## 冒烟与回退

`QtExamServer/tools/smoke_test.py` 会写入账号、考试、题目和评分，只能连接一次性演示数据库，Python 需支持 `datetime.UTC`（3.11+）。
务必先运行 `--help` 检查参数；不要用它测试真实库。
本次整理独立于原始毕业设计，无需还原原项目。发布版异常时停止发布版进程，回到原客户端/服务端及原配置启动；不要用原数据库验证新测试脚本。
