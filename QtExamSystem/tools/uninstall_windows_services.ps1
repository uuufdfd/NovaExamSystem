<#
.SYNOPSIS
    Remove QtExamSystem Windows background services (Scheduled Tasks) and stop their processes.

    Removes : QtExamCameraProxy, QtExamFilePicker, and the legacy "QtExamSystem Camera Bridge".
    Stops   : any running windows_camera_server.py / windows_file_picker_server.py processes
              (including a stray legacy 8800 camera bridge).

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File ".\tools\uninstall_windows_services.ps1"
#>
$ErrorActionPreference = "Stop"

$Tasks = @("QtExamCameraProxy", "QtExamFilePicker", "QtExamSystem Camera Bridge")
$ProcPatterns = @("windows_camera_server\.py", "windows_file_picker_server\.py")

function Write-Ok($m)   { Write-Host "[OK]    $m" -ForegroundColor Green }
function Write-Info($m) { Write-Host "[INFO]  $m" -ForegroundColor Cyan }
function Write-Warn($m) { Write-Host "[WARN]  $m" -ForegroundColor Yellow }

Write-Info "QtExamSystem Windows services uninstall"

foreach ($t in $Tasks) {
    $existing = Get-ScheduledTask -TaskName $t -ErrorAction SilentlyContinue
    if ($existing) {
        try { Stop-ScheduledTask -TaskName $t -ErrorAction SilentlyContinue } catch {}
        Unregister-ScheduledTask -TaskName $t -Confirm:$false
        Write-Ok "Removed task: $t"
    } else {
        Write-Info "Task not present: $t"
    }
}

# Stop any lingering server processes (so the camera/port is freed immediately).
$stopped = 0
foreach ($pat in $ProcPatterns) {
    $procs = Get-CimInstance Win32_Process -ErrorAction SilentlyContinue | Where-Object {
        $_.CommandLine -and $_.CommandLine -match $pat
    }
    foreach ($p in $procs) {
        try {
            Stop-Process -Id $p.ProcessId -Force -ErrorAction Stop
            Write-Ok "Stopped PID=$($p.ProcessId) ($pat)"
            $stopped++
        } catch {
            Write-Warn "Could not stop PID=$($p.ProcessId): $($_.Exception.Message)"
        }
    }
}
if ($stopped -eq 0) { Write-Info "No running camera/picker processes found." }

Write-Host ""
Write-Ok "Uninstall complete. Reinstall with:"
Write-Host "  powershell -ExecutionPolicy Bypass -File `".\tools\install_windows_services.ps1`""
