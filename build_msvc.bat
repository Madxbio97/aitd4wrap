@echo off
setlocal

REM Build AITD dinput8 proxy DLL (32-bit) - v20 hash-map + PCM cache
REM Run from "x86 Native Tools Command Prompt for VS"

cd /d "%~dp0"
echo Building dinput8 proxy DLL v20 (32-bit)...
echo Working dir: %CD%
echo.

REM Sanity check 1: the source has the v20 marker
findstr /C:"v20-hash-map-cache" dinput8_proxy.c >nul
if errorlevel 1 (
    echo *** WARNING: dinput8_proxy.c does NOT contain v20 marker ***
    echo *** You are likely compiling an old cached copy. ***
    echo *** Download the file again. ***
    echo.
    pause
    exit /b 1
)

REM Sanity check 2: hash map header is present
if not exist aitd_hash_map.h (
    echo *** ERROR: aitd_hash_map.h is missing! ***
    echo *** Place it next to dinput8_proxy.c before building. ***
    echo.
    pause
    exit /b 1
)

cl.exe /nologo /O2 /W3 /D_CRT_SECURE_NO_WARNINGS /LD dinput8_proxy.c ^
    /link /DEF:dinput8_proxy.def /OUT:dinput8.dll ^
    user32.lib gdi32.lib kernel32.lib winmm.lib ole32.lib avrt.lib

if %ERRORLEVEL% equ 0 (
    echo.
    echo SUCCESS! Created: dinput8.dll
    echo.
    echo Installation:
    echo   1. Copy dinput8.dll to the game directory
    echo   2. Copy aitd_wrapper.ini to the game directory
    echo   3. Launch the game
    echo   4. Verify in aitd_wrapper.log:
    echo        "=== AITD Wrapper v20 (hash-map-cache) ==="
    echo        "HASHMAP: 912 Sound2/ entries loaded"
    echo        "PCMCACHE: dir=..."
    echo   5. After a few minutes of gameplay, check aitd_pcm_cache/
    echo      folder — it should fill up with <hash>.wav files.
    echo.
    del /q *.obj 2>nul
    del /q *.exp 2>nul
    del /q *.lib 2>nul
) else (
    echo.
    echo BUILD FAILED. Check errors above.
    echo Make sure you are using "x86 Native Tools Command Prompt".
)
pause
