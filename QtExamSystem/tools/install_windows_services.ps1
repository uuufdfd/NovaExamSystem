<#
.SYNOPSIS
    Install QtExamSystem Windows background services as per-user logon Scheduled Tasks.

    QtExamCameraProxy  -> camera MJPEG proxy on port 8765 (tools\windows_camera_server.py)
    QtExamFilePicker   -> file picker service on port 8766 (tools\windows_file_picker_server.py)

    Both run windowless (pythonw.exe) at logon, restart on failure, and log to run-logs\.
    The legacy 8800 camera bridge task ("QtExamSystem Camera Bridge") is removed.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File ".\tools\install_windows_services.ps1"
#>
param(
    [int]$CameraPort = 8765,
    [int]$FilePickerPort = 8766,
    [int]$CameraIndex = 0
)

$ErrorActionPreference = "Stop"

$ToolsDir    = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = Split-Path -Parent $ToolsDir
$LogDir      = Join-Path $ProjectRoot "run-logs"
$CameraScript = Join-Path $ToolsDir "windows_camera_server.py"
$PickerScript = Join-Path $ToolsDir "windows_file_picker_server.py"
$CameraLog   = Join-Path $LogDir "camera-proxy.log"
$PickerLog   = Join-Path $LogDir "file-picker.log"

$CameraTask  = "QtExamCameraProxy"
$PickerTask  = "QtExamFilePicker"
$LegacyTasks = @("QtExamSystem Camera Bridge")   # old 8800 bridge -> remove

$CurrentUser = [System.Security.Principal.WindowsIdentity]::GetCurrent().Name

function Write-Ok($m)   { Write-Host "[OK]    $m" -ForegroundColor Green }
function Write-Info($m) { Write-Host "[INFO]  $m" -ForegroundColor Cyan }
function Write-Warn($m) { Write-Host "[WARN]  $m" -ForegroundColor Yellow }
function Write-Err($m)  { Write-Host "[ERROR] $m" -ForegroundColor Red }

# --- resolve the real interpreter (handles Store python shims) and pythonw ---
function Get-PythonExe {
    try {
        $exe = (& python -c "import sys; print(sys.executable)" 2>$null)
        if ($exe -and (Test-Path $exe.Trim())) { return $exe.Trim() }
    } catch {}
    $cmd = Get-Command python -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    $cmd = Get-Command py -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    return $null
}

Write-Info "QtExamSystem Windows services install"
Write-Info "Project : $ProjectRoot"
Write-Info "User    : $CurrentUser"

New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

$PythonExe = Get-PythonExe
if (-not $PythonExe) {
    Write-Err "Python not found. Install Python and ensure 'python' is on PATH."
    exit 1
}
$PythonDir = Split-Path -Parent $PythonExe
$PythonwExe = Join-Path $PythonDir "pythonw.exe"
if (Test-Path $PythonwExe) {
    $LauncherExe = $PythonwExe
    Write-Ok "Windowless interpreter: $PythonwExe"
} else {
    $LauncherExe = $PythonExe
    Write-Warn "pythonw.exe not found next to python.exe; using python.exe (a console window may flash at logon)."
}

# cv2 is required by the camera proxy only.
try {
    $cv2 = (& $PythonExe -c "import cv2; print(cv2.__version__)" 2>$null)
    if ($cv2) { Write-Ok "OpenCV available: $cv2" } else { throw "no output" }
} catch {
    Write-Warn "OpenCV (cv2) not detected. Camera proxy needs it: python -m pip install opencv-python"
}

foreach ($s in @($CameraScript, $PickerScript)) {
    if (-not (Test-Path $s)) { Write-Err "Missing script: $s"; exit 1 }
}

# --- remove legacy 8800 tasks so they never autostart again ---
foreach ($lt in $LegacyTasks) {
    $existing = Get-ScheduledTask -TaskName $lt -ErrorAction SilentlyContinue
    if ($existing) {
        Unregister-ScheduledTask -TaskName $lt -Confirm:$false
        Write-Ok "Removed legacy task: $lt"
    }
}

function Install-Service {
    param([string]$TaskName, [string]$ArgString, [string]$Description)
    $action = New-ScheduledTaskAction -Execute $LauncherExe -Argument $ArgString -WorkingDirectory $ProjectRoot
    $trigger = New-ScheduledTaskTrigger -AtLogOn -User $CurrentUser
    $principal = New-ScheduledTaskPrincipal -UserId $CurrentUser -LogonType Interactive -RunLevel Limited
    $settings = New-ScheduledTaskSettingsSet `
        -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries `
        -MultipleInstances IgnoreNew `
        -RestartCount 3 -RestartInterval (New-TimeSpan -Minutes 1) `
        -ExecutionTimeLimit (New-TimeSpan -Seconds 0) `
        -StartWhenAvailable
    Register-ScheduledTask -TaskName $TaskName -Action $action -Trigger $trigger `
        -Principal $principal -Settings $settings -Description $Description -Force | Out-Null
    Write-Ok "Registered task: $TaskName"
}

$cameraArgs = '"{0}" --host 0.0.0.0 --port {1} --camera-index {2} --log-file "{3}"' -f `
    $CameraScript, $CameraPort, $CameraIndex, $CameraLog
$pickerArgs = '"{0}" --host 0.0.0.0 --port {1} --log-file "{2}"' -f $PickerScript, $FilePickerPort, $PickerLog

Install-Service -TaskName $CameraTask -ArgString $cameraArgs `
    -Description "QtExamSystem camera MJPEG proxy ($CameraPort), windowless autostart."
Install-Service -TaskName $PickerTask -ArgString $pickerArgs `
    -Description "QtExamSystem file picker service ($FilePickerPort), windowless autostart."

# --- start now (don't wait for next logon) ---
Start-ScheduledTask -TaskName $CameraTask
Start-ScheduledTask -TaskName $PickerTask
Write-Info "Started both tasks; probing health..."

function Wait-Health {
    param([int]$Port, [int]$Tries = 16)
    for ($i = 0; $i -lt $Tries; $i++) {
        Start-Sleep -Milliseconds 500
        try {
            $r = Invoke-RestMethod -Uri "http://127.0.0.1:$Port/health" -TimeoutSec 2
            return $r
        } catch {}
    }
    return $null
}

$camHealth = Wait-Health -Port $CameraPort
if ($camHealth) {
    if ($camHealth.ok) { Write-Ok "Camera proxy healthy on $CameraPort (cameraIndex=$($camHealth.cameraIndex))" }
    else { Write-Warn "Camera proxy up on $CameraPort but camera not ready: $($camHealth.error)" }
} else {
    Write-Warn "Camera proxy /health not responding yet on $CameraPort. See $CameraLog"
}

$pickHealth = Wait-Health -Port $FilePickerPort
if ($pickHealth) { Write-Ok "File picker healthy on $FilePickerPort" }
else { Write-Warn "File picker /health not responding yet on $FilePickerPort. See $PickerLog" }

Write-Host ""
Write-Ok "Install complete."
Write-Host "  Camera health : http://127.0.0.1:$CameraPort/health"
Write-Host "  Camera stream : http://127.0.0.1:$CameraPort/camera.mjpg"
Write-Host "  Picker health : http://127.0.0.1:$FilePickerPort/health"
Write-Host ""
Write-Host "  Logs          : $CameraLog"
Write-Host "                  $PickerLog"
Write-Host ""
Write-Host "  Kylin VM camera env (use the Windows host LAN IP):" -ForegroundColor Cyan
Write-Host "    export QTEXAM_CAMERA_URL=http://<WINDOWS_HOST_IP>:$CameraPort/camera.mjpg"
Write-Host ""
Write-Host "  Check : powershell -ExecutionPolicy Bypass -File `".\tools\check_windows_services.ps1`""
Write-Host "  Remove: powershell -ExecutionPolicy Bypass -File `".\tools\uninstall_windows_services.ps1`""
