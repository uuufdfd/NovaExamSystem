<#
    DEPRECATED. The old 8800 camera-bridge autostart has been replaced by the
    unified service installer (camera 8765 + file picker 8766, windowless).
    This shim forwards to install_windows_services.ps1 so old commands keep working.
#>
$ErrorActionPreference = "Stop"
$ToolsDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Write-Host "[QtExam] install_windows_camera_autostart.ps1 is deprecated; using install_windows_services.ps1" -ForegroundColor Yellow
& powershell -ExecutionPolicy Bypass -File (Join-Path $ToolsDir "install_windows_services.ps1") @args
exit $LASTEXITCODE
