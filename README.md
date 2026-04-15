# AITD: The New Nightmare - Graphics Fix (dinput8 proxy)

## Why dinput8 proxy?

The game loads `opengl32.dll` at runtime via `LoadLibraryA` instead of importing
it statically. Windows' KnownDLLs mechanism forces `opengl32.dll` to always load
from System32, ignoring any copy in the game folder. So a simple opengl32.dll
wrapper doesn't work.

Instead, we proxy `dinput8.dll` — a DLL the game **does** import statically.
Our proxy loads at game start, hooks `LoadLibraryA` and `GetProcAddress`, and
intercepts OpenGL calls when the game resolves them at runtime.

## Fixes

| Fix | How it works |
|-----|-------------|
| **4:3 Aspect Ratio** | Hooks `glViewport`/`glScissor` via GetProcAddress interception |
| **32-bit Color** | Hooks `ChangeDisplaySettingsA` via IAT + intercepts pixel format calls |
| **Texture Filtering** | Hooks `glTexParameteri/f/iv/fv` via GetProcAddress interception |

## Building

Open **x86 Native Tools Command Prompt for VS** and run:

```
build_msvc.bat
```

**IMPORTANT:** Must be x86 (32-bit), not x64!

## Installation

1. If the game folder already has `dinput8.dll`, rename it first
2. Copy `dinput8.dll` (our proxy) to the game directory
3. Copy `aitd_wrapper.ini` to the game directory
4. Launch the game

## Verifying it works

With default config (`ShowDiagnostics=1`), a MessageBox will appear on game start
confirming the wrapper loaded. Also check for these files in the game directory:

- `aitd_wrapper_diag.txt` — created immediately on DLL load (step-by-step status)
- `aitd_wrapper.log` — detailed log of all intercepted calls

## Uninstalling

Delete `dinput8.dll` and `aitd_wrapper.ini` from the game directory.
If you renamed the original, rename it back.

## Configuration

Edit `aitd_wrapper.ini`:

```ini
[Fixes]
FixAspectRatio=1    ; 1=on, 0=off
FixColorDepth=1     ; 1=on, 0=off
FixTextureFilter=1  ; 1=on, 0=off

[Debug]
EnableLog=1         ; detailed log file
ShowDiagnostics=1   ; startup MessageBox (disable after confirming)
```

## Architecture

```
Game (alone4.exe)
  |
  +-- imports DINPUT8.dll --> [OUR PROXY]
  |     |
  |     +-- loads real dinput8.dll from System32
  |     +-- hooks LoadLibraryA (KERNEL32)
  |     +-- hooks GetProcAddress (KERNEL32)
  |     +-- hooks ChangeDisplaySettingsA (USER32)
  |
  +-- calls LoadLibraryA("opengl32.dll")
  |     |
  |     +--> [HOOK] notes the GL module handle
  |
  +-- calls GetProcAddress(hGL, "glViewport")
        |
        +--> [HOOK] returns our wrapper function
```
