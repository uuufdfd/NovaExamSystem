param(
    [int]$Port = 8765,
    [int]$CameraIndex = 0,
    [switch]$CheckOnly,
    [switch]$Stop
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = Split-Path -Parent $ScriptDir
$ServerScript = Join-Path $ScriptDir "windows_camera_server.py"
$LogDir = Join-Path $ProjectRoot "run-logs"
$StdOutLog = Join-Path $LogDir "camera-windows.out.log"
$StdErrLog = Join-Path $LogDir "camera-windows.err.log"

function Write-Ok($msg) { Write-Host "[OK] $msg" -ForegroundColor Green }
function Write-Warn($msg) { Write-Host "[WARN] $msg" -ForegroundColor Yellow }
function Write-Err($msg) { Write-Host "[ERROR] $msg" -ForegroundColor Red }
function Write-Info($msg) { Write-Host "[INFO] $msg" -ForegroundColor Cyan }

function Get-PythonCommand {
    $cmd = Get-Command python -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    $cmd = Get-Command py -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    return $null
}

function Get-CameraServerProcesses {
    Get-CimInstance Win32_Process | Where-Object {
        $_.CommandLine -and
        $_.CommandLine -match "windows_camera_server\.py" -and
        $_.CommandLine -match "--port\s+$Port"
    }
}

function Test-Health {
    param([int]$Port)
    try {
        $res = Invoke-RestMethod -Uri "http://127.0.0.1:$Port/health" -TimeoutSec 3
        if ($res.ok -eq $true) { return $true }
        return $false
    } catch {
        return $false
    }
}

function Print-Urls {
    param([int]$Port)
    Write-Ok "Windows 摄像头代理已启动"
    Write-Host ""
    Write-Host "本机访问：" -ForegroundColor Cyan
    Write-Host "  health: http://127.0.0.1:$Port/health"
    Write-Host "  stream: http://127.0.0.1:$Port/camera.mjpg"
    Write-Host "  PowerShell 环境变量：`$env:QTEXAM_CAMERA_URL=\"http://127.0.0.1:$Port/camera.mjpg\""
    Write-Host ""
    Write-Host "如果给麒麟虚拟机访问，请用 Windows 的局域网/VMware IPv4：" -ForegroundColor Cyan
    try {
        $ips = Get-NetIPAddress -AddressFamily IPv4 -ErrorAction SilentlyContinue | Where-Object {
            $_.IPAddress -match "^(192\.168\.|172\.|10\.)"
        } | Select-Object -ExpandProperty IPAddress -Unique
        foreach ($ip in $ips) {
            Write-Host "  export QTEXAM_CAMERA_URL=\"http://$ip`:$Port/camera.mjpg\""
        }
    } catch {
        Write-Warn "无法列出 IPv4 地址：$($_.Exception.Message)"
    }
}

New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

if ($Stop) {
    $procs = @(Get-CameraServerProcesses)
    if ($procs.Count -eq 0) {
        Write-Warn "没有找到端口 $Port 的 windows_camera_server.py 进程"
        exit 0
    }
    foreach ($p in $procs) {
        Write-Info "停止进程 PID=$($p.ProcessId)"
        Stop-Process -Id $p.ProcessId -Force
    }
    Write-Ok "已停止摄像头代理"
    exit 0
}

if (-not (Test-Path $ServerScript)) {
    Write-Err "找不到摄像头代理脚本：$ServerScript"
    exit 1
}
Write-Ok "摄像头代理脚本存在：$ServerScript"

$python = Get-PythonCommand
if (-not $python) {
    Write-Err "未找到 Python。请先安装 Python，并确认 python 或 py 在 PATH 中。"
    exit 1
}
Write-Ok "Python 可用：$python"

try {
    $ver = & $python --version 2>&1
    Write-Ok "Python 版本：$ver"
} catch {
    Write-Err "Python 无法运行：$($_.Exception.Message)"
    exit 1
}

try {
    $cv2ver = & $python -c "import cv2; print(cv2.__version__)" 2>&1
    if ($LASTEXITCODE -ne 0) { throw $cv2ver }
    Write-Ok "cv2 可用：$cv2ver"
} catch {
    Write-Err "缺少 OpenCV。请执行：python -m pip install opencv-python"
    exit 1
}

if ($CheckOnly) {
    $portOpen = Test-NetConnection 127.0.0.1 -Port $Port -WarningAction SilentlyContinue
    if ($portOpen.TcpTestSucceeded) {
        Write-Ok "端口 $Port 正在监听"
    } else {
        Write-Warn "端口 $Port 未监听"
    }
    if (Test-Health -Port $Port) {
        Write-Ok "/health 正常"
        Print-Urls -Port $Port
        exit 0
    } else {
        Write-Warn "/health 不正常或代理未启动"
        exit 2
    }
}

if (Test-Health -Port $Port) {
    Write-Ok "摄像头代理已在运行"
    Print-Urls -Port $Port
    exit 0
}

Write-Info "启动摄像头代理，端口=$Port，摄像头编号=$CameraIndex"
Write-Info "日志：$StdOutLog / $StdErrLog"

$argList = @(
    $ServerScript,
    "--host", "0.0.0.0",
    "--port", $Port,
    "--camera-index", $CameraIndex
)

$proc = Start-Process -FilePath $python -ArgumentList $argList -WorkingDirectory $ProjectRoot -RedirectStandardOutput $StdOutLog -RedirectStandardError $StdErrLog -WindowStyle Hidden -PassThru
Write-Ok "已启动进程 PID=$($proc.Id)"

for ($i = 1; $i -le 20; $i++) {
    Start-Sleep -Milliseconds 500
    if (Test-Health -Port $Port) {
        Print-Urls -Port $Port
        exit 0
    }
}

Write-Err "摄像头代理启动失败或 /health 未通过"
Write-Host "---- stdout ----"
if (Test-Path $StdOutLog) { Get-Content $StdOutLog -Tail 40 }
Write-Host "---- stderr ----"
if (Test-Path $StdErrLog) { Get-Content $StdErrLog -Tail 40 }
exit 1
