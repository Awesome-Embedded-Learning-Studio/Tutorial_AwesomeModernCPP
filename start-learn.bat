@echo off
cd /d "%~dp0"
echo 启动本地服务器 http://localhost:8123/learn.html ...
start "" cmd /c "python -m http.server 8123"
timeout /t 2 >nul
start "" http://localhost:8123/learn.html
