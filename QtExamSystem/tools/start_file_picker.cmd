@echo off
setlocal
cd /d "%~dp0.."
python tools\windows_file_picker_server.py --host 0.0.0.0 --port 8766
pause
