<#
    DEPRECATED. Replaced by uninstall_windows_services.ps1 (removes the new
    QtExamCameraProxy / QtExamFilePicker tasks and the legacy 8800 bridge task).
    This shim forwards to it so old commands keep working.
#>
$ErrorActionPreference = "Stop"
$ToolsDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Write-Host "[QtExam] uninstall_windows_camera_autostart.ps1 is deprecated; using uninstall_windows_services.ps1" -ForegroundColor Yellow
& powershell -ExecutionPolicy Bypass -File (Join-Path $ToolsDir "uninstall_windows_services.ps1") @args
exit $LASTEXITCODE
