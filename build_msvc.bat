@echo off
REM Build AITD dinput8 proxy DLL (32-bit)
REM Run from "x86 Native Tools Command Prompt for VS"

echo Building dinput8 proxy DLL v16 (32-bit)...
cl.exe /nologo /O2 /W3 /D_CRT_SECURE_NO_WARNINGS /LD dinput8_proxy.c ^
    /link /DEF:dinput8_proxy.def /OUT:dinput8.dll ^
    user32.lib gdi32.lib kernel32.lib winmm.lib

if %ERRORLEVEL% equ 0 (
    echo.
    echo SUCCESS! Created: dinput8.dll
    echo.
    echo Installation:
    echo   1. Copy dinput8.dll to the game directory
    echo   2. Copy aitd_wrapper.ini to the game directory
    echo   3. Launch the game
    echo.
    del /q *.obj 2>nul
) else (
    echo.
    echo BUILD FAILED. Make sure you are using "x86 Native Tools Command Prompt".
)
pause
