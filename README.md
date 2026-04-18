# AITD: The New Nightmare — Graphics Fix Wrapper

**dinput8 proxy · v16**

A drop-in DLL wrapper that fixes graphical issues and adds modern quality-of-life features to *Alone in the Dark: The New Nightmare* (2001) on Windows 10/11.

---

## Why This Exists

The PC version of *Alone in the Dark: The New Nightmare* uses OpenGL 1.x and was designed for 640×480 on Windows 98/XP. On modern systems it suffers from:

- Stretched image on widescreen monitors (no 4:3 correction)
- 16-bit color depth (banding, dithering artifacts)
- Nearest-neighbor texture filtering (blocky textures)
- Texture seam artifacts (incorrect GL_CLAMP mode)
- Missing fog on modern GPU drivers
- No VSync (screen tearing)
- No windowed or borderless mode
- Resolution locked to what the game launcher offers
- Broken display on high-DPI screens (Windows scaling)
- Timing issues on multi-core CPUs

This wrapper fixes all of the above without modifying any game files.

---

## Why dinput8 Proxy?

The game loads `opengl32.dll` at runtime via `LoadLibraryA` instead of importing it statically. Windows' KnownDLLs mechanism forces `opengl32.dll` to always load from System32, ignoring any copy placed in the game folder. A simple opengl32.dll wrapper therefore does not work.

Instead, we proxy `dinput8.dll` — a DLL the game **does** import statically. Our proxy loads at game start, hooks `LoadLibraryA` and `GetProcAddress`, and intercepts OpenGL calls when the game resolves them at runtime.

For functions the game calls through the OpenGL ICD dispatch table (bypassing stored pointers), inline hooks (detours) are used: the first bytes of the real function are overwritten with a `JMP` to our wrapper, and a trampoline preserves the original prologue for calling the real implementation.

---

## Features

### Graphics Fixes

| Feature | INI Key | Description |
|---------|---------|-------------|
| **4:3 Aspect Ratio** | `FixAspectRatio` | Pillarbox/letterbox correction on any resolution. Black bars on widescreen, correct proportions always. |
| **32-bit Color** | `FixColorDepth` | Upgrades pixel format and all texture internal formats from 16-bit to 32-bit. Eliminates color banding. |
| **Bilinear Filtering** | `FixTextureFilter` | Upgrades GL_NEAREST to GL_LINEAR for all textures. Smooth instead of blocky. |
| **Texture Clamping** | `FixTextureClamp` | Replaces GL_REPEAT/GL_CLAMP with GL_CLAMP_TO_EDGE. Fixes visible seams at texture borders. |
| **Anisotropic Filtering** | `AnisoLevel` | Sets GL_TEXTURE_MAX_ANISOTROPY_EXT (2–16×). Sharpens textures viewed at oblique angles. |
| **Fog Fix** | `FixFog` | Sets GL_FOG_COORDINATE_SOURCE to GL_FRAGMENT_DEPTH. Fixes black/missing fog on modern NVIDIA/AMD drivers. |

### Rendering Enhancements

| Feature | INI Key | Description |
|---------|---------|-------------|
| **SSAA** | `SSAAFactor` | Supersampling: renders at 2–4× resolution internally, downscales with bilinear filtering. Superior quality for pre-rendered backgrounds and 2D elements. |
| **MSAA** | `MSAASamples` | Multisample anti-aliasing (2/4/8×) via `wglChoosePixelFormatARB`. Uses a dummy context for format detection. |
| **Alpha Blend Fix** | `FixAlphaTest` | Converts hard GL_ALPHA_TEST cutoff to smooth alpha blending. Eliminates jagged edges on foliage, fences, hair. |
| **Stencil Buffer** | `ForceStencil` | Forces 8-bit stencil in pixel format. Required for certain shadow/reflection effects. |

> **Note:** SSAA and MSAA should not be used together. SSAA is generally superior for this game.

### Display Modes

| Feature | INI Key | Description |
|---------|---------|-------------|
| **Fullscreen** | (default) | Classic exclusive fullscreen with optional resolution and refresh rate override. |
| **Windowed** | `Windowed` | Standard window with title bar, centered on screen. |
| **Borderless** | `Borderless` | Borderless fullscreen at desktop resolution. Instant Alt+Tab, no monitor flicker. **Recommended.** |
| **Alt+Enter** | `AllowAltEnter` | Toggle between fullscreen and borderless on the fly. |
| **Resolution Override** | `ForceWidth/Height` | Force any resolution via registry hook + ChangeDisplaySettings. |
| **Refresh Rate** | `ForceRefreshRate` | Force display refresh rate (120/144/165/240 Hz) in fullscreen mode. |

### Quality of Life

| Feature | INI Key | Description |
|---------|---------|-------------|
| **VSync** | `VSync` | Enables wglSwapIntervalEXT. Prevents screen tearing. |
| **FPS Limiter** | `FPSLimit` | High-precision frame limiter (Sleep + spin-wait via QueryPerformanceCounter). Use when VSync is off. |
| **Screenshots** | `ScreenshotKey` | Press F12 (configurable) to save TGA screenshot. Captures only the 4:3 game area, excluding black bars. Saved to `aitd_screenshots\`. |
| **Cursor Clip** | `ClipCursor` | Confines mouse to the game window in windowed/borderless mode. Auto-releases on Alt+Tab, re-clips on focus. |
| **Gamma Boost** | `GammaBoost` | Adjustable gamma via SetDeviceGammaRamp. Original gamma saved and restored on exit. Try 0.1–0.3 for dark scenes. |

### Texture Modding

| Feature | INI Key | Description |
|---------|---------|-------------|
| **Texture Dump** | `DumpTextures` | Exports every unique texture to `aitd_textures\dump\` as uncompressed 32-bit TGA. Files named `<HASH8>_<W>x<H>.tga`. Enable once to capture, then disable. |
| **Texture Replace** | `ReplaceTextures` | Loads replacement TGA files from `aitd_textures\replace\`. Same filename as dump. May have different dimensions (for upscaling). Cached in a hash table for fast lookup. |

### Compatibility

| Feature | INI Key | Description |
|---------|---------|-------------|
| **DPI Awareness** | (automatic) | Calls SetProcessDpiAwarenessContext (Win10 1703+) or SetProcessDPIAware (Vista+). Prevents Windows scaling artifacts. |
| **Single Core** | `SingleCore` | Forces CPU affinity to core 0. Fixes timing bugs and random crashes on multi-core systems. |
| **Crash Handler** | (automatic) | Vectored exception handler logs fatal crashes (access violations, stack overflows) with register dump and module offsets. |

---

## Installation

1. If the game folder already contains `dinput8.dll`, **rename it** (e.g., `dinput8_original.dll`)
2. Copy `dinput8.dll` (this wrapper) to the game directory (where `alone4.exe` lives)
3. Copy `aitd_wrapper.ini` to the same directory
4. Edit `aitd_wrapper.ini` to taste
5. Launch the game

To uninstall, delete `dinput8.dll` and `aitd_wrapper.ini` (restore original if renamed).

---

## Recommended Settings

### Modern system, widescreen monitor, best quality

```ini
[Fixes]
FixAspectRatio=1
FixColorDepth=1
FixTextureFilter=1
FixTextureClamp=1
AnisoLevel=16
FixFog=1

[Rendering]
SSAAFactor=2
FixAlphaTest=1
ForceStencil=1

[Display]
Borderless=1
VSync=1
FPSLimit=0
AllowAltEnter=1
```

### Older/weaker GPU

```ini
[Rendering]
SSAAFactor=1
FixAlphaTest=1
ForceStencil=1

[Display]
Windowed=0
VSync=1
MSAASamples=4
```

---

## Configuration Reference

All settings are in `aitd_wrapper.ini` (ANSI, Windows INI format).

### [Fixes]

| Key | Default | Values | Description |
|-----|---------|--------|-------------|
| `FixAspectRatio` | 1 | 0/1 | 4:3 correction with black bars |
| `FixColorDepth` | 1 | 0/1 | 16-bit → 32-bit color |
| `FixTextureFilter` | 1 | 0/1 | Nearest → bilinear/trilinear |
| `FixTextureClamp` | 1 | 0/1 | Fix texture edge seams |
| `AnisoLevel` | 16 | 0,2,4,8,16 | Anisotropic filtering level |
| `FixFog` | 1 | 0/1 | Fix fog on modern drivers |

### [Rendering]

| Key | Default | Values | Description |
|-----|---------|--------|-------------|
| `SSAAFactor` | 1 | 1–4 | Supersampling factor (1=off) |
| `FixAlphaTest` | 0 | 0/1 | Smooth alpha blending |
| `ForceStencil` | 0 | 0/1 | Force 8-bit stencil buffer |

### [Textures]

| Key | Default | Values | Description |
|-----|---------|--------|-------------|
| `DumpTextures` | 0 | 0/1 | Export textures to TGA |
| `ReplaceTextures` | 0 | 0/1 | Load replacement TGA textures |

### [Display]

| Key | Default | Values | Description |
|-----|---------|--------|-------------|
| `Windowed` | 0 | 0/1 | Windowed mode |
| `Borderless` | 0 | 0/1 | Borderless fullscreen |
| `VSync` | 1 | 0/1 | Vertical sync |
| `FPSLimit` | 0 | 0–999 | FPS cap (0=off) |
| `MSAASamples` | 0 | 0,2,4,8 | MSAA anti-aliasing |
| `ForceWidth` | 0 | px | Override resolution width |
| `ForceHeight` | 0 | px | Override resolution height |
| `ForceRefreshRate` | 0 | Hz | Override refresh rate |
| `ClipCursor` | 1 | 0/1 | Confine cursor to window |
| `AllowAltEnter` | 1 | 0/1 | Alt+Enter mode toggle |
| `GammaBoost` | 0 | 0.0–2.0 | Gamma brightness boost |

### [Features]

| Key | Default | Values | Description |
|-----|---------|--------|-------------|
| `ScreenshotKey` | 0x7B | VK code | Screenshot hotkey (0x7B=F12, 0x2C=PrintScreen, 0=off) |

### [Compatibility]

| Key | Default | Values | Description |
|-----|---------|--------|-------------|
| `SingleCore` | 0 | 0/1 | Force single-core affinity |

### [Debug]

| Key | Default | Values | Description |
|-----|---------|--------|-------------|
| `EnableLog` | 1 | 0/1 | Write `aitd_wrapper.log` |
| `ShowDiagnostics` | 0 | 0/1 | MessageBox with settings at startup |

---

## Building

### Requirements

- **Visual Studio** (2017 or later) with C/C++ desktop workload
- Must build as **x86 (32-bit)** — the game is a 32-bit executable

### Steps

1. Open **x86 Native Tools Command Prompt for VS**
2. Navigate to the source directory
3. Run:

```
build_msvc.bat
```

Output: `dinput8.dll` (32-bit proxy DLL)

### Manual build command

```
cl.exe /nologo /O2 /W3 /D_CRT_SECURE_NO_WARNINGS /LD dinput8_proxy.c ^
    /link /DEF:dinput8_proxy.def /OUT:dinput8.dll ^
    user32.lib gdi32.lib kernel32.lib winmm.lib
```

---

## Architecture

```
Game (alone4.exe)
  │
  ├── imports DINPUT8.dll ──────────── [OUR PROXY]
  │     │
  │     ├── loads real dinput8.dll from System32
  │     ├── IAT hooks:
  │     │     LoadLibraryA       (KERNEL32) ─ catch opengl32 load
  │     │     GetProcAddress     (KERNEL32) ─ intercept GL func resolution
  │     │     ChangeDisplaySettingsA (USER32) ─ resolution/refresh/borderless
  │     │     CreateWindowExA    (USER32) ─ windowed/borderless mode
  │     │     RegQueryValueExA   (ADVAPI32) ─ resolution override
  │     │     ChoosePixelFormat  (GDI32) ─ color depth/MSAA/stencil
  │     │     SetPixelFormat     (GDI32) ─ MSAA format
  │     │     SwapBuffers        (GDI32) ─ FPS limiter/screenshots/SSAA blit
  │     │
  │     └── Inline hooks (detours):
  │           glViewport       ─ aspect ratio / SSAA scaling
  │           glScissor        ─ aspect ratio correction
  │           glEnable         ─ alpha test → blend conversion
  │           glDisable        ─ alpha test → blend conversion
  │           glAlphaFunc      ─ alpha threshold adjustment
  │
  ├── calls LoadLibraryA("opengl32.dll")
  │     └── [HOOK] records module, resolves GL func ptrs,
  │                installs detours on glViewport/glScissor/glEnable etc.
  │
  ├── calls GetProcAddress(hGL, "glTexImage2D")
  │     └── [HOOK] returns our wrapper for hooked functions
  │
  ├── calls GetProcAddress(hGL, "wglGetProcAddress")
  │     └── [HOOK] returns our wglGetProcAddress wrapper
  │           └── intercepts extension queries (SwapBuffers, etc.)
  │
  └── calls wglMakeCurrent(hdc, hglrc)
        └── [HOOK] initializes:
              VSync (wglSwapIntervalEXT)
              MSAA (glEnable GL_MULTISAMPLE)
              Fog fix (glFogi)
              Gamma (SetDeviceGammaRamp)
              SSAA FBO functions (glGenFramebuffers etc.)
```

### Hooking Strategy

The game resolves OpenGL functions in two ways:

1. **GetProcAddress** — Used to build a function dispatch table at startup. Our `Hook_GetProcAddress` intercepts these calls and returns our wrapper functions for hooked entries.

2. **OpenGL ICD dispatch** — Some functions (notably `glViewport`, `glScissor`) are called directly through the Installable Client Driver dispatch table inside `opengl32.dll`, bypassing the stored pointers from `GetProcAddress`. For these, we use **inline hooks (detours)**: we overwrite the first 5 bytes of the real function with a `JMP` to our wrapper, and create a trampoline (saved prologue bytes + `JMP` back) for calling the original.

A minimal x86 instruction length decoder determines safe patch boundaries (≥5 bytes for `JMP rel32`).

### SSAA Pipeline

When `SSAAFactor > 1`:

1. **FBO creation** — On first viewport call, an off-screen framebuffer is created at `gameW × factor` by `gameH × factor` with RGBA8 color and DEPTH24_STENCIL8 renderbuffers.
2. **Rendering** — All `glViewport` calls are scaled by the SSAA factor. The game renders to the FBO at higher resolution.
3. **Blit** — On `SwapBuffers`, `glBlitFramebuffer` downscales the FBO to the default framebuffer with `GL_LINEAR` filtering. Aspect ratio correction (pillarbox/letterbox) is applied during the blit destination rectangle calculation.

---

## Texture Modding Guide

### Dumping textures

1. Set `DumpTextures=1` in `[Textures]`
2. Launch the game, play through areas you want to capture
3. Textures appear in `aitd_textures\dump\` as `<HASH>_<W>x<H>.tga`
4. Set `DumpTextures=0` when done

### Replacing textures

1. Copy a dumped TGA to `aitd_textures\replace\` (keep the same filename)
2. Edit the TGA in any image editor (GIMP, Photoshop, etc.)
3. You may change dimensions (e.g., upscale 256×256 → 1024×1024)
4. Save as **uncompressed 24-bit or 32-bit TGA**
5. Set `ReplaceTextures=1`
6. Launch the game — replacements are loaded and cached automatically

Replacement files are matched by the FNV-1a hash in the filename. The hash covers pixel data, dimensions, and format — it is unique per texture.

---

## Troubleshooting

### Game doesn't start / crashes immediately

- Check `aitd_wrapper.log` and `aitd_wrapper_diag.txt` for error messages
- Set `ShowDiagnostics=1` to see a MessageBox with loaded settings
- Ensure `dinput8.dll` is the 32-bit proxy (not a 64-bit build)
- If the game previously had its own `dinput8.dll`, make sure you renamed it

### No visual changes

- Check the log for `DETOUR: glViewport OK` — if missing, the inline hook failed
- Check for `VP-CALL` messages — if missing, the detour isn't firing
- Check for `TexImage2D:` messages — if present, texture hooks work

### Stretched image in borderless/windowed

- Ensure `FixAspectRatio=1`
- Check log for `CDS: display tracked as WxH` — dimensions should match your monitor
- After Alt+Enter, log should show `Toggle: ... borderless WxH` with correct desktop resolution

### MSAA not working

- Check log for `MSAA: found Nx format` — if missing, your GPU doesn't support the requested sample count
- Try lower values: `MSAASamples=4` or `MSAASamples=2`
- Don't combine with `SSAAFactor > 1`

### Screenshots are black

- Ensure `SwapBuffers: HOOKED` appears in the log
- Screenshots require `glReadPixels` to be available — check for `ResolveGL: ... VP=XXXXX` (non-zero)

### Game runs too fast or erratic timing

- Try `SingleCore=1` in `[Compatibility]`
- Try `FPSLimit=30` or `FPSLimit=60` with `VSync=0`

### Dark/dim image on modern monitor

- Try `GammaBoost=0.2` (increase gradually, max 2.0)
- Gamma is restored on normal exit; if game crashes, reboot or run the game again to restore

---

## File Structure

```
game_directory/
├── alone4.exe              (game executable)
├── dinput8.dll              (THIS WRAPPER)
├── aitd_wrapper.ini         (configuration)
├── aitd_wrapper.log         (runtime log, if EnableLog=1)
├── aitd_wrapper_diag.txt    (diagnostic breadcrumbs)
├── aitd_textures/
│   ├── dump/                (dumped textures)
│   └── replace/             (replacement textures)
└── aitd_screenshots/
    ├── shot_0000.tga
    ├── shot_0001.tga
    └── ...
```

---

## Compatibility

| OS | Status |
|----|--------|
| Windows 11 | ✅ Tested |
| Windows 10 | ✅ Tested |
| Windows 8/8.1 | Should work |
| Windows 7 | Should work (no DPI awareness context) |
| Wine/Proton | Untested |

| GPU | Status |
|-----|--------|
| NVIDIA (modern) | ✅ |
| AMD (modern) | ✅ |
| Intel (integrated) | SSAA may be slow; use MSAA instead |

---

## Version History

| Version | Changes |
|---------|---------|
| **v16** | SSAA supersampling via FBO, alpha test → blend fix, forced stencil buffer |
| **v15** | Borderless fullscreen, screenshots (F12), Alt+Enter toggle, forced refresh rate, inline hooks (detours) for glViewport/glScissor, `wglGetProcAddress` interception |
| **v14** | DPI awareness, single-core affinity, gamma boost, fog fix, cursor confinement, hash table texture cache, `timeEndPeriod` cleanup |
| **v13** | Initial public version: aspect ratio, color depth, texture filtering/clamping, anisotropic filtering, VSync, FPS limiter, MSAA, windowed mode, resolution override, texture dump/replace, crash handler |

---

## License

This project is provided as-is for personal use with legally owned copies of *Alone in the Dark: The New Nightmare*. No game assets are included or modified.

---

## Credits

Built with reverse engineering, OpenGL knowledge, and a love for survival horror.
