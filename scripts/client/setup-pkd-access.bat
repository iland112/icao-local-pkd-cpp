@echo off
chcp 65001 >nul 2>&1
title ICAO Local PKD - HTTPS 설정

NET SESSION >nul 2>&1
if %errorLevel% neq 0 (
    echo 관리자 권한으로 재실행합니다...
    powershell -Command "Start-Process cmd -ArgumentList '/c \"\"%~f0\"\"' -Verb RunAs"
    exit /b
)

cd /d "%~dp0"
powershell -ExecutionPolicy Bypass -NoProfile -File "%~dp0setup-pkd-access.ps1"
pause
