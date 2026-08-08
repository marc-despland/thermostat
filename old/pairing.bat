@echo off
REM Pairing script for KETOTEK with Zigbee Gateway (Windows)

setlocal enabledelayedexpansion

REM Default COM port (can be overridden as argument)
set PORT=%1
if "%PORT%"=="" (
    echo Detecting available COM ports...
    for /f "tokens=*" %%a in ('mode') do (
        echo %%a | find /i "COM" >nul && (
            if not defined PORT set PORT=%%a
        )
    )
)

if "%PORT%"=="" (
    echo Error: No COM port found or specified
    echo Usage: %0 [COM port]
    echo Example: %0 COM3
    exit /b 1
)

echo.
echo =================================
echo KETOTEK Pairing Helper
echo =================================
echo Port: %PORT%
echo.
echo Instructions:
echo 1. Connect via serial port
echo 2. Type: permit_join 180
echo 3. Press KETOTEK pairing button
echo 4. Wait for "Device registered" message
echo 5. Type: list_devices
echo.
echo Starting connection...
echo (Use Ctrl+C to exit)
echo.

REM Use available serial tool
if exist "C:\Program Files\PuTTY\putty.exe" (
    echo Using PuTTY...
    "C:\Program Files\PuTTY\putty.exe" -serial %PORT% -sercfg 115200,8,n,1,N
) else if exist "C:\Program Files (x86)\PuTTY\putty.exe" (
    echo Using PuTTY...
    "C:\Program Files (x86)\PuTTY\putty.exe" -serial %PORT% -sercfg 115200,8,n,1,N
) else (
    echo PuTTY not found. Please install it or use VS Code's serial monitor.
    echo You can still use 'idf.py -p %PORT% monitor' command.
    pause
)
