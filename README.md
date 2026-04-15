# AITD: The New Nightmare - Video Fix Wrapper (dinput8 proxy)

**v12** — video/GL fixes only. Audio engine is a separate project.

## Why dinput8 proxy?

The game loads `opengl32.dll` at runtime via `LoadLibraryA` instead of importing
it statically. Windows' KnownDLLs mechanism forces `opengl32.dll` to always load
from System32, ignoring any copy in the game folder. So a simple opengl32.dll
wrapper doesn't work.

We proxy `dinput8.dll` — a DLL the game **does** import statically. Our proxy
loads at game start, hooks `LoadLibraryA` and `GetProcAddress`, and intercepts
OpenGL calls when the game resolves them at runtime.

## Video Fixes

| Fix | INI key | How it works |
|-----|---------|-------------|
| **4:3 Aspect Ratio** | `FixAspectRatio` | Hooks `glViewport`/`glScissor` |
| **32-bit Color** | `FixColorDepth` | Hooks pixel format selection + `ChangeDisplaySettingsA` |
| **Texture Filtering** | `FixTextureFilter` | Hooks `glTexParameteri/f/iv/fv` — nearest→linear |
| **Texture Clamping** | `FixTextureClamp` | Hooks `glTexParameteri/f/iv/fv` — fixes seams via `CLAMP_TO_EDGE` |
| **Anisotropic Filter** | `AnisoLevel` | Sets `GL_TEXTURE_MAX_ANISOTROPY_EXT` whenever MIN_FILTER is set |
| **Mipmap Generation** | `GenMipmaps` | Calls `glGenerateMipmap` after every base-level `glTexImage2D` upload; upgrades MIN filter to trilinear |
| **VSync** | `VSync` | Calls `wglSwapIntervalEXT(1)` once context is ready |
| **FPS Limiter** | `FPSLimit` | Hooks `SwapBuffers` — Sleep + spin-wait for accuracy |
| **MSAA** | `MSAASamples` | Uses `wglChoosePixelFormatARB` via a dummy context |
| **Windowed Mode** | `Windowed` | Hooks `CreateWindowExA` + `ChangeDisplaySettingsA` |
| **Resolution Override** | `ForceWidth/Height` | Hooks `RegQueryValueExA` + `ChangeDisplaySettingsA` |

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

## Recommended settings

For a modern system at any widescreen resolution:

```ini
[Fixes]
FixAspectRatio=1
FixColorDepth=1
FixTextureFilter=1
FixTextureClamp=1
AnisoLevel=16
GenMipmaps=1

[Display]
VSync=1
FPSLimit=0
MSAASamples=4
```

## Notes on GenMipmaps

Mipmaps are pre-scaled copies of a texture used at distance. The game doesn't
generate them, so without this fix, distant geometry shimmers (aliasing). When
`GenMipmaps=1`, the MIN filter is automatically promoted from `GL_LINEAR` to
`GL_LINEAR_MIPMAP_LINEAR` (trilinear). This requires `FixTextureFilter=1`.

`AnisoLevel` and `GenMipmaps` complement each other: anisotropy fixes texture
blurring at oblique angles, mipmaps fix aliasing at distance.

## Notes on FPS Limiter

The FPS limiter uses `QueryPerformanceCounter` with a Sleep + spin-wait loop
for precision. It is applied inside the `SwapBuffers` hook before the actual
swap. Use it only when `VSync=0` — when VSync is on, the driver already limits
to the monitor refresh rate and adding `FPSLimit` introduces unnecessary latency.

## Architecture

```
Game (alone4.exe)
  |
  +-- imports DINPUT8.dll --> [OUR PROXY]
  |     |
  |     +-- loads real dinput8.dll from System32
  |     +-- hooks LoadLibraryA       (KERNEL32) -- to catch opengl32 load
  |     +-- hooks GetProcAddress     (KERNEL32) -- to intercept GL functions
  |     +-- hooks ChangeDisplaySettingsA (USER32)
  |     +-- hooks CreateWindowExA    (USER32) [windowed mode]
  |     +-- hooks RegQueryValueExA   (ADVAPI32) [resolution]
  |     +-- hooks SwapBuffers        (GDI32) [FPS limiter]
  |
  +-- calls LoadLibraryA("opengl32.dll")
  |     +--> [HOOK] records module handle, resolves GL func ptrs
  |
  +-- calls GetProcAddress(hGL, "glViewport")
  |     +--> [HOOK] returns our wrapper
  |
  +-- calls wglMakeCurrent(hdc, hglrc)
        +--> [HOOK] acquires glGenerateMipmap + wglSwapIntervalEXT,
                    sets VSync, enables MSAA
```
