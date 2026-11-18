@echo off
chcp 65001 >nul
echo 正在停止后台自动化 Agent 服务...
wmic process where "commandline like '%%automation_agent%%main.py%%'" call terminate >nul 2>&1
taskkill /F /FI "WINDOWTITLE eq 自动化 Agent 服务控制台*" >nul 2>&1
echo 自动化 Agent 服务已停止。
pause
