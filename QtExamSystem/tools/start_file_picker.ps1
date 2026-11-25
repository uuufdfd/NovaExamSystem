$ErrorActionPreference = "Stop"

$ProjectRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
Set-Location $ProjectRoot

python .\tools\windows_file_picker_server.py --host 0.0.0.0 --port 8766
