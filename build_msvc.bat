@echo off
REM Build AITD dinput8 proxy DLL (32-bit)
REM Run from "x86 Native Tools Command Prompt for VS"

echo Building dinput8 proxy DLL (32-bit)...
cl.exe /nologo /O2 /W3 /D_CRT_SECURE_NO_WARNINGS /LD dinput8_proxy.c /link /DEF:dinput8_proxy.def /OUT:dinput8.dll user32.lib gdi32.lib kernel32.lib

if %ERRORLEVEL% equ 0 (
    echo.
    echo SUCCESS! Created: dinput8.dll
    echo.
    echo Installation:
    echo   1. Copy dinput8.dll to the game directory
    echo   2. Copy aitd_wrapper.ini to the game directory
    echo   3. Launch the game
    echo.
    echo NOTE: If the game directory already has a dinput8.dll,
    echo       rename it to dinput8_original.dll first!
    echo.
    del /q dinput8_proxy.obj dinput8.lib dinput8.exp 2>nul
) else (
    echo.
    echo BUILD FAILED.
    echo Make sure you're using "x86 Native Tools Command Prompt"
)
pause
