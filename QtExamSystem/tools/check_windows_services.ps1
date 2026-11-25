<#
.SYNOPSIS
    Health check for QtExamSystem Windows background services.

    Reports task existence, port listening, /health, /camera.mjpg reachability,
    current camera index, camera-busy hints, and how to start things if down.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File ".\tools\check_windows_services.ps1"
#>
param(
    [int]$CameraPort = 8765,
    [int]$FilePickerPort = 8766
)

$ErrorActionPreference = "Continue"

$CameraTask = "QtExamCameraProxy"
$PickerTask = "QtExamFilePicker"
$LegacyPort = 8800

function Write-Ok($m)   { Write-Host "[OK]    $m" -ForegroundColor Green }
function Write-Info($m) { Write-Host "[INFO]  $m" -ForegroundColor Cyan }
function Write-Warn($m) { Write-Host "[WARN]  $m" -ForegroundColor Yellow }
function Write-Err($m)  { Write-Host "[ERROR] $m" -ForegroundColor Red }

function Test-PortListening {
    param([string]$ComputerName = "127.0.0.1", [int]$Port, [int]$TimeoutMs = 1500)
    $client = New-Object System.Net.Sockets.TcpClient
    try {
        $iar = $client.BeginConnect($ComputerName, $Port, $null, $null)
        if ($iar.AsyncWaitHandle.WaitOne($TimeoutMs)) {
            $client.EndConnect($iar); return $true
        }
        return $false
    } catch { return $false } finally { $client.Close() }
}

function Get-Health {
    param([int]$Port)
    try { return Invoke-RestMethod -Uri "http://127.0.0.1:$Port/health" -TimeoutSec 3 }
    catch { return $null }
}

# Plain HTTP 200 check (the file picker /health returns text "OK", not JSON).
function Test-Http200 {
    param([string]$Url, [int]$TimeoutSec = 3)
    try {
        $r = Invoke-WebRequest -Uri $Url -TimeoutSec $TimeoutSec -UseBasicParsing
        return @{ Ok = ([int]$r.StatusCode -eq 200); Code = [int]$r.StatusCode; Body = "$($r.Content)".Trim() }
    } catch {
        return @{ Ok = $false; Code = 0; Body = $_.Exception.Message }
    }
}

function Test-MjpgStream {
    param([string]$Url, [int]$TimeoutSec = 5)
    try {
        $req = [System.Net.HttpWebRequest]::Create($Url)
        $req.Timeout = $TimeoutSec * 1000
        $req.ReadWriteTimeout = $TimeoutSec * 1000
        $resp = $req.GetResponse()
        $code = [int]$resp.StatusCode
        $ctype = $resp.ContentType
        $resp.Close()
        return @{ Ok = ($code -eq 200); Code = $code; ContentType = $ctype }
    } catch {
        return @{ Ok = $false; Code = 0; ContentType = $_.Exception.Message }
    }
}

function Show-Task {
    param([string]$Name)
    $t = Get-ScheduledTask -TaskName $Name -ErrorAction SilentlyContinue
    if ($t) {
        Write-Ok "Task '$Name' exists (State=$($t.State))"
        return $true
    }
    Write-Warn "Task '$Name' NOT installed"
    return $false
}

Write-Host "==== QtExamSystem Windows services check ====" -ForegroundColor White

Write-Host "`n-- Scheduled Tasks --"
$camTaskOk  = Show-Task -Name $CameraTask
$pickTaskOk = Show-Task -Name $PickerTask

Write-Host "`n-- Camera proxy ($CameraPort) --"
$camPort = Test-PortListening -Port $CameraPort
if ($camPort) { Write-Ok "Port $CameraPort is listening" } else { Write-Warn "Port $CameraPort is NOT listening" }

$camHealth = Get-Health -Port $CameraPort
if ($camHealth) {
    if ($camHealth.ok) {
        Write-Ok "/health ok=true  cameraIndex=$($camHealth.cameraIndex)  $($camHealth.width)x$($camHealth.height)@$($camHealth.fps)"
    } else {
        Write-Warn "/health ok=false  cameraIndex=$($camHealth.cameraIndex)  error=$($camHealth.error)"
        if ("$($camHealth.error)" -match "cannot open|busy") {
            Write-Warn "Camera appears BUSY/occupied. Close any app using the webcam (e.g. a local Qt client, Camera app, Teams) then retry."
        }
    }
    $mjpg = Test-MjpgStream -Url "http://127.0.0.1:$CameraPort/camera.mjpg"
    if ($mjpg.Ok) { Write-Ok "/camera.mjpg reachable (HTTP $($mjpg.Code), $($mjpg.ContentType))" }
    else { Write-Warn "/camera.mjpg not reachable ($($mjpg.ContentType))" }
} else {
    Write-Warn "/health not responding on $CameraPort"
}

Write-Host "`n-- File picker ($FilePickerPort) --"
$pickPort = Test-PortListening -Port $FilePickerPort
if ($pickPort) { Write-Ok "Port $FilePickerPort is listening" } else { Write-Warn "Port $FilePickerPort is NOT listening" }
$pickHealth = Test-Http200 -Url "http://127.0.0.1:$FilePickerPort/health"
if ($pickHealth.Ok) { Write-Ok "/health OK (HTTP 200, body='$($pickHealth.Body)')" }
else { Write-Warn "/health not OK (HTTP $($pickHealth.Code): $($pickHealth.Body))" }

Write-Host "`n-- Legacy 8800 bridge --"
if (Test-PortListening -Port $LegacyPort -TimeoutMs 800) {
    Write-Err "Legacy port $LegacyPort is STILL listening. Run uninstall_windows_services.ps1 to remove it."
} else {
    Write-Ok "No legacy $LegacyPort listener (good)"
}

$allUp = $camPort -and $pickPort
if (-not $allUp -or -not $camTaskOk -or -not $pickTaskOk) {
    Write-Host "`n-- How to start --" -ForegroundColor Cyan
    if (-not $camTaskOk -or -not $pickTaskOk) {
        Write-Host "  Install/repair tasks:"
        Write-Host "    powershell -ExecutionPolicy Bypass -File `".\tools\install_windows_services.ps1`""
    }
    if ($camTaskOk -and -not $camPort) {
        Write-Host "  Start camera task now:  Start-ScheduledTask -TaskName '$CameraTask'"
    }
    if ($pickTaskOk -and -not $pickPort) {
        Write-Host "  Start picker task now:  Start-ScheduledTask -TaskName '$PickerTask'"
    }
}

Write-Host "`n==== done ====" -ForegroundColor White
