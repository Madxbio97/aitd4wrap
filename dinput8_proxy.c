/*
 * DINPUT8 Proxy for Alone in the Dark: The New Nightmare
 * v20 - Sound2 hash-to-name map + persistent PCM disk cache
 *        Build tag: v20-hash-map-cache
 *
 * Video fix features:
 *   - Aspect ratio correction (pillarbox / letterbox)
 *   - Color depth upgrade (16-bit -> 32-bit)
 *   - Texture filter upgrade (nearest -> linear)
 *   - Texture clamp fix (REPEAT/CLAMP -> CLAMP_TO_EDGE)
 *   - Anisotropic texture filtering (up to 16x)
 *   - VSync control (wglSwapIntervalEXT)
 *   - FPS limiter (high-precision, Sleep + spin)
 *   - Texture dump to TGA (unique per hash, aitd_textures\dump\)
 *   - Texture replacement from TGA (aitd_textures\replace\)
 *   - MSAA anti-aliasing via multisampled FBO (2x/4x/8x)
 *   - Windowed mode with cursor confinement
 *   - Borderless fullscreen mode
 *   - Resolution override (registry + ChangeDisplaySettings)
 *   - Forced display refresh rate
 *   - Screenshots (F12 -> TGA)
 *   - Alt+Enter fullscreen/borderless toggle
 *
 * Audio fix features (Miles Sound System / mss32.dll):
 *   - Force high-quality digital audio output (44.1kHz, 16-bit, stereo)
 *   - Preserve sample loop state across screen transitions
 *   - Smooth volume fade-in/fade-out for music streams
 *   - Fade-out before stream pause with deferred pause action
 *   - Minimum data size filter for force-loop (prevents beep looping)
 *   - Sample volume boost (amplify quiet sound effects)
 *   - Audio diagnostic logging
 *
 * Compatibility features:
 *   - DPI awareness (prevents Windows scaling)
 *   - Single-core affinity (fixes timing on multi-core CPUs)
 *   - Gamma boost (brightens dark rendering on modern monitors)
 *   - Fog coordinate fix (fixes broken fog on modern drivers)
 *   - Cursor confinement in windowed mode
 *   - Hash table texture cache (faster lookups)
 *   - Proper timeEndPeriod cleanup
 */
#pragma warning(disable: 4996 4273)
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")
#include <tlhelp32.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>

/* v20: auto-generated hash-to-name table for Sound2/ (912 entries) */
#include "aitd_hash_map.h"


/* ---- GL types & constants ---- */
typedef unsigned int GLenum;
typedef int GLint;
typedef int GLsizei;
typedef float GLfloat;
typedef float GLclampf;
typedef unsigned int GLbitfield;
typedef void GLvoid;
typedef unsigned char GLubyte;
typedef char GLchar;
typedef ptrdiff_t GLsizeiptr;
typedef unsigned int GLuint;
typedef unsigned int GLboolean;

#define GL_TEXTURE_MAG_FILTER     0x2800
#define GL_TEXTURE_MIN_FILTER     0x2801
#define GL_TEXTURE_WRAP_S         0x2802
#define GL_TEXTURE_WRAP_T         0x2803
#define GL_NEAREST                0x2600
#define GL_LINEAR                 0x2601
#define GL_NEAREST_MIPMAP_NEAREST 0x2700
#define GL_LINEAR_MIPMAP_NEAREST  0x2701
#define GL_NEAREST_MIPMAP_LINEAR  0x2702
#define GL_LINEAR_MIPMAP_LINEAR   0x2703
#define GL_REPEAT                 0x2901
#define GL_CLAMP                  0x2900
#define GL_CLAMP_TO_EDGE          0x812F
#define GL_COLOR_BUFFER_BIT       0x00004000
#define GL_RGB         0x1907
#define GL_RGBA        0x1908
#define GL_LUMINANCE   0x1909
#define GL_LUMINANCE_ALPHA 0x190A
#define GL_ALPHA       0x1906
#define GL_RGB4        0x804F
#define GL_RGB5        0x8050
#define GL_RGB8        0x8051
#define GL_RGBA2       0x8055
#define GL_RGBA4       0x8056
#define GL_RGB5_A1     0x8057
#define GL_RGBA8       0x8058
#define GL_R3_G3_B2    0x2A10
#define GL_LUMINANCE8  0x8040
#define GL_LUMINANCE8_ALPHA8 0x8045
#define GL_ALPHA8      0x803C
#define GL_FRONT       0x0404
#define GL_PACK_ALIGNMENT 0x0D05

/* Anisotropic filtering (EXT_texture_filter_anisotropic) */
#define GL_UNSIGNED_BYTE                  0x1401
#define GL_BGR                            0x80E0
#define GL_BGRA                           0x80E1
#define GL_TEXTURE_MAX_ANISOTROPY_EXT     0x84FE
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF

/* Fog coordinate fix */
#define GL_FOG_COORDINATE_SOURCE_EXT 0x8450
#define GL_FRAGMENT_DEPTH_EXT        0x8452

/* FBO (GL_EXT_framebuffer_object / GL_ARB_framebuffer_object) */

/* Alpha test / blending */
#define GL_ALPHA_TEST             0x0BC0
#define GL_BLEND                  0x0BE2
#define GL_SRC_ALPHA              0x0302
#define GL_ONE_MINUS_SRC_ALPHA    0x0303
#define GL_GREATER                0x0204
#define GL_GEQUAL                 0x0206


/* ---- Configuration ---- */
typedef struct {
    int fix_aspect_ratio;
    int fix_color_depth;
    int fix_texture_filter;
    int fix_texture_clamp;
    int msaa_samples;         /* 0=off, 2/4/8 = MSAA sample count (default 4) */
    int windowed;
    int borderless;
    int force_width;
    int force_height;
    int aniso_level;
    int dump_textures;
    int replace_textures;
    int vsync;
    int fps_limit;
    int enable_log;
    int show_diagnostics;
    /* v14 additions */
    int single_core;
    int fix_fog;
    int clip_cursor;
    float gamma_boost;
    /* v15 additions */
    int force_refresh_rate;
    int allow_alt_enter;
    int screenshot_key;
    /* v16 additions */
    /* v17 additions: audio */
    int fix_audio_quality;    /* force high-quality MSS output (44100Hz, 16-bit) */
    int preserve_loops;       /* preserve sample state across screen transitions */
    int fade_in_ms;           /* stream fade-in duration (0=off, 50-500) */
    int fade_out_ms;          /* stream fade-out duration (0=off, 100-1000) */
    int sample_volume_boost;  /* volume boost for samples: 0=off, 10-100 = added volume */
    int enable_custom_mixer;  /* 1=custom mixer (32ch, no MSS), 0=MSS with hooks */
    /* v19 additions */
    int enable_wasapi;        /* 1=WASAPI backend for mixer (native rate, low latency) */
    int wasapi_latency_ms;    /* 5-80, default 15 */
    int enable_reverb;        /* 1=Schroeder reverb on mixer output */
    int reverb_wet;           /* 0-100 wet mix */
    int reverb_room_size;     /* 0-100 feedback / decay */
    int reverb_damping;       /* 0-100 HF damping */
    int enable_stream_mgr;    /* 1=custom WAV stream player (replaces MSS streams) */
    int loop_crossfade_ms;    /* 0=off, 5-50 = crossfade duration */
    /* v20 additions */
    int enable_pcm_cache;     /* 1=cache decoded PCM WAVs to disk */
    int enable_dedup_killer;  /* 1=kill duplicate ambient samples (0=let game manage) */
    int enable_zombie_killer; /* 1=kill stale samples (0=let game manage) */
    int dialog_duck_level;    /* 0-100: MSS master vol % during dialog (0=off, 70=default) */
    int enable_pcm_prewarm;   /* 1=load all PCM cache into RAM at startup */
    /* v21 additions */
    /* v21 audio additions */
    int vol_ambient;          /* 0-200: ambient (6xxx) volume %, 100=original */
    int vol_music;            /* 0-200: music stems volume %, 100=original */
    int vol_sfx;              /* 0-200: SFX (0xxx/2xxx/4xxx) volume %, 100=original */
    int eq_high_shelf_db;     /* 0-12: high-shelf boost in dB above ~8kHz, 0=off */
} WrapperConfig;

static WrapperConfig g_config = {
    .fix_aspect_ratio   = 1,
    .fix_color_depth    = 1,
    .fix_texture_filter = 1,
    .fix_texture_clamp  = 1,
    .msaa_samples       = 4,
    .windowed           = 0,
    .borderless         = 0,
    .force_width        = 0,
    .force_height       = 0,
    .aniso_level        = 16,
    .dump_textures      = 0,
    .replace_textures   = 0,
    .vsync              = 1,
    .fps_limit          = 0,
    .enable_log         = 1,
    .show_diagnostics   = 0,
    .single_core        = 0,
    .fix_fog            = 1,
    .clip_cursor        = 1,
    .gamma_boost        = 0.0f,
    .force_refresh_rate = 0,
    .allow_alt_enter    = 1,
    .screenshot_key     = 0x7B,
    .fix_audio_quality  = 1,
    .preserve_loops     = 0,
    .fade_in_ms         = 100,
    .fade_out_ms        = 300,
    .sample_volume_boost = 0,
    .enable_custom_mixer = 0,
    .enable_wasapi       = 0,
    .wasapi_latency_ms   = 15,
    .enable_reverb       = 0,
    .reverb_wet          = 18,
    .reverb_room_size    = 55,
    .reverb_damping      = 60,
    .enable_stream_mgr   = 0,
    .loop_crossfade_ms   = 15,
    .enable_pcm_cache    = 0,
    .enable_dedup_killer = 0,
    .enable_zombie_killer = 1,
    .dialog_duck_level   = 70,
    .enable_pcm_prewarm  = 1,
    .vol_ambient         = 100,
    .vol_music           = 100,
    .vol_sfx             = 100,
    .eq_high_shelf_db    = 0,
};

/* ---- Globals ---- */
static HMODULE g_hRealDInput8 = NULL;
static HMODULE g_hOpenGL      = NULL;
static FILE   *g_logFile      = NULL;
static char    g_gameDir[MAX_PATH] = {0};
static HWND    g_gameHWND     = NULL;
static int     g_glContextReady = 0;

static int g_logMsgCount = 0;
static void LogMsg(const char *fmt, ...) {
    if (!g_logFile) return;
    va_list a; va_start(a,fmt); vfprintf(g_logFile,fmt,a); va_end(a); fflush(g_logFile);
    /* Check log size every 1000 writes — rotate if >10 MB */
    if (++g_logMsgCount % 1000 == 0) {
        long pos = ftell(g_logFile);
        if (pos > 10*1024*1024) {
            char lp[MAX_PATH], bp[MAX_PATH];
            snprintf(lp,MAX_PATH,"%saitd_wrapper.log",g_gameDir);
            snprintf(bp,MAX_PATH,"%saitd_wrapper.log.bak",g_gameDir);
            fclose(g_logFile);
            /* Rename current log to .bak (preserves context for debugging) */
            DeleteFileA(bp); /* remove old backup if any */
            MoveFileA(lp, bp);
            g_logFile = fopen(lp, "w");
            if (g_logFile) {
                setvbuf(g_logFile, NULL, _IONBF, 0);
                fprintf(g_logFile, "=== LOG ROTATED (prev was %ld bytes, saved as .bak) ===\n", pos);
            }
        }
    }
}
static void WriteDiag(const char *msg) {
    char p[MAX_PATH]; snprintf(p,MAX_PATH,"%saitd_wrapper_diag.txt",g_gameDir);
    FILE *f=fopen(p,"a"); if(f){fprintf(f,"%s\n",msg);fclose(f);}
}

/* ---- DirectInput8Create forwarding ---- */
typedef HRESULT(WINAPI*PFN_DirectInput8Create)(HINSTANCE,DWORD,const void*,void**,void*);
static PFN_DirectInput8Create real_DirectInput8Create=NULL;
__declspec(dllexport) HRESULT WINAPI DirectInput8Create(
    HINSTANCE hi,DWORD dv,const void*ri,void**pp,void*pu){
    return real_DirectInput8Create?real_DirectInput8Create(hi,dv,ri,pp,pu):0x80004005L;
}

/* ---- GL function pointers ---- */
typedef void(WINAPI*PFN_glViewport)(GLint,GLint,GLsizei,GLsizei);
typedef void(WINAPI*PFN_glScissor)(GLint,GLint,GLsizei,GLsizei);
typedef void(WINAPI*PFN_glTexParameteri)(GLenum,GLenum,GLint);
typedef void(WINAPI*PFN_glTexParameterf)(GLenum,GLenum,GLfloat);
typedef void(WINAPI*PFN_glTexParameteriv)(GLenum,GLenum,const GLint*);
typedef void(WINAPI*PFN_glTexParameterfv)(GLenum,GLenum,const GLfloat*);
typedef void(WINAPI*PFN_glClearColor)(GLclampf,GLclampf,GLclampf,GLclampf);
typedef void(WINAPI*PFN_glClear)(GLbitfield);
typedef void(WINAPI*PFN_glTexImage2D)(GLenum,GLint,GLint,GLsizei,GLsizei,GLint,GLenum,GLenum,const GLvoid*);
typedef void(WINAPI*PFN_glTexImage1D)(GLenum,GLint,GLint,GLsizei,GLint,GLenum,GLenum,const GLvoid*);
typedef void(WINAPI*PFN_glTexSubImage2D)(GLenum,GLint,GLint,GLint,GLsizei,GLsizei,GLenum,GLenum,const GLvoid*);
typedef void(WINAPI*PFN_glEnable)(GLenum);
typedef void(WINAPI*PFN_glHint)(GLenum,GLenum);
typedef void(WINAPI*PFN_glFogi)(GLenum,GLint);
typedef void(WINAPI*PFN_glReadPixels)(GLint,GLint,GLsizei,GLsizei,GLenum,GLenum,GLvoid*);
typedef void(WINAPI*PFN_glGetIntegerv)(GLenum,GLint*);
typedef GLenum(WINAPI*PFN_glGetError)(void);
#define GL_VIEWPORT_ENUM 0x0BA2
#define GL_NO_ERROR_ENUM 0
typedef void(WINAPI*PFN_glPixelStorei)(GLenum,GLint);
typedef void(WINAPI*PFN_glDisable)(GLenum);
typedef void(WINAPI*PFN_glBlendFunc)(GLenum,GLenum);
typedef void(WINAPI*PFN_glAlphaFunc)(GLenum,GLclampf);

typedef int (WINAPI*PFN_ChoosePixelFormat)(HDC,const PIXELFORMATDESCRIPTOR*);
typedef HGLRC(WINAPI*PFN_wglCreateContext)(HDC);
typedef BOOL (WINAPI*PFN_wglMakeCurrent)(HDC,HGLRC);
typedef BOOL (WINAPI*PFN_wglDeleteContext)(HGLRC);
typedef PROC (WINAPI*PFN_wglGetProcAddress)(LPCSTR);

static PFN_glViewport       real_glViewport=NULL;
static PFN_glScissor        real_glScissor=NULL;
static PFN_glTexParameteri  real_glTexParameteri=NULL;
static PFN_glTexParameterf  real_glTexParameterf=NULL;
static PFN_glTexParameteriv real_glTexParameteriv=NULL;
static PFN_glTexParameterfv real_glTexParameterfv=NULL;
static PFN_glClearColor     real_glClearColor=NULL;
static PFN_glClear          real_glClear=NULL;
static PFN_glTexImage2D     real_glTexImage2D=NULL;
static PFN_glTexImage1D     real_glTexImage1D=NULL;
static PFN_glTexSubImage2D  real_glTexSubImage2D=NULL;
static PFN_glEnable         real_glEnable=NULL;
static PFN_glHint           real_glHint=NULL;
static PFN_glFogi           real_glFogi=NULL;
static PFN_glReadPixels     real_glReadPixels=NULL;
static PFN_glGetIntegerv    real_glGetIntegerv=NULL;
static PFN_glGetError       real_glGetError=NULL;
static PFN_glPixelStorei    real_glPixelStorei=NULL;
static PFN_glDisable        real_glDisable=NULL;
static PFN_glBlendFunc      real_glBlendFunc=NULL;
static PFN_glAlphaFunc      real_glAlphaFunc=NULL;

static PFN_ChoosePixelFormat real_ChoosePixelFormat_fn=NULL;

typedef BOOL(WINAPI*PFN_SetPixelFormat)(HDC,int,const PIXELFORMATDESCRIPTOR*);
static PFN_SetPixelFormat real_SetPixelFormat_fn=NULL;
static PFN_wglCreateContext  real_wglCreateContext=NULL;
static PFN_wglMakeCurrent    real_wglMakeCurrent=NULL;
static PFN_wglDeleteContext  real_wglDeleteContext=NULL;
static PFN_wglGetProcAddress real_wglGetProcAddress=NULL;

/* ---- Extension / new GL function pointers ---- */
typedef BOOL(WINAPI*PFN_wglSwapIntervalEXT)(int);
typedef BOOL(WINAPI*PFN_SwapBuffers_t)(HDC);

static PFN_wglSwapIntervalEXT real_wglSwapIntervalEXT = NULL;
static PFN_SwapBuffers_t      real_SwapBuffers_fn     = NULL;

/* FPS limiter / VSync state */
static LARGE_INTEGER g_perfFreq = {{0}};
static LARGE_INTEGER g_lastSwap = {{0}};
static int           g_vsyncSet = 0;

/* ChangeDisplaySettings — declared early for ToggleDisplayMode */
typedef LONG(WINAPI*PFN_ChangeDisplaySettingsA)(DEVMODEA*,DWORD);
static PFN_ChangeDisplaySettingsA real_ChangeDisplaySettingsA=NULL;

/* Screenshot state */
static int           g_screenshotCounter = 0;
static DWORD         g_lastScreenshotTick = 0;

/* Display mode state for Alt+Enter toggle */
/* 0=fullscreen, 1=windowed, 2=borderless */
static int           g_displayMode = 0;
static DEVMODEA      g_origDevMode;
static int           g_origDevModeSaved = 0;
static DWORD         g_savedStyle = 0;
static DWORD         g_savedExStyle = 0;
static RECT          g_savedWindowRect = {0};




/* ==================================================================
 *  TEXTURE DUMP / REPLACE
 *
 *  Dump:    unique textures (keyed by FNV-1a hash of pixels+dims+format)
 *           are written to aitd_textures\dump\<HASH8>_<W>x<H>.tga
 *           once per run. Enable DumpTextures=1 in [Textures] to capture.
 *
 *  Replace: place a TGA named identically in aitd_textures\replace\.
 *           The file may have different dimensions (useful for upscaling).
 *           Only uncompressed 24/32-bit TGA is supported.
 * ================================================================== */

#define TEX_SEEN_MAX    4096   /* max unique hashes tracked for dump dedup  */
#define TEX_SEEN_MASK   (TEX_SEEN_MAX - 1)  /* power-of-2 for hash set */

/* Hash table for texture replacement cache (open addressing, power-of-2) */
#define TEX_CACHE_BITS  10
#define TEX_CACHE_SIZE  (1 << TEX_CACHE_BITS)  /* 1024 slots */
#define TEX_CACHE_MASK  (TEX_CACHE_SIZE - 1)
#define TEX_HASH_EMPTY  0      /* sentinel: hash==0 means slot is unused   */

static unsigned int g_texSeen[TEX_SEEN_MAX]; /* 0 = empty slot */
static int          g_texSeenCount = 0;

typedef struct { unsigned int hash; int w, h; unsigned char *rgba; } TexEntry;
static TexEntry g_texCache[TEX_CACHE_SIZE];  /* zero-initialized = all empty */
static int      g_texCacheUsed = 0;

/* FNV-1a over dims+format+sampled pixels (first/middle/last 1KB).
 * Avoids O(n) full-texture hash for large textures (4MB+ at 1024x1024 RGBA). */
static unsigned int TexHash(int w, int h, GLenum fmt, GLenum tp,
                             const unsigned char *data, int size) {
    unsigned int v = 2166136261u;
#define FNV(b) v = (v ^ (unsigned char)(b)) * 16777619u
    FNV(w); FNV(w>>8); FNV(w>>16); FNV(w>>24);
    FNV(h); FNV(h>>8); FNV(h>>16); FNV(h>>24);
    FNV(fmt); FNV(fmt>>8); FNV(tp); FNV(tp>>8);
    FNV(size); FNV(size>>8); FNV(size>>16); FNV(size>>24);
    if(data) {
        /* Small textures: hash everything. Large: sample 3 × 1KB regions. */
        #define TEX_HASH_CHUNK 1024
        if (size <= TEX_HASH_CHUNK * 3) {
            for(int i=0;i<size;i++) FNV(data[i]);
        } else {
            int mid = (size - TEX_HASH_CHUNK) / 2;
            int i;
            for(i=0; i<TEX_HASH_CHUNK; i++) FNV(data[i]);
            for(i=0; i<TEX_HASH_CHUNK; i++) FNV(data[mid+i]);
            for(i=0; i<TEX_HASH_CHUNK; i++) FNV(data[size-TEX_HASH_CHUNK+i]);
        }
        #undef TEX_HASH_CHUNK
    }
#undef FNV
    /* Ensure hash is never 0 (our empty sentinel) */
    return v ? v : 2;
}

/* Bytes per pixel for GL_UNSIGNED_BYTE textures. Returns 0 for unsupported. */
static int TexBPP(GLenum fmt) {
    switch(fmt) {
        case GL_RGB:  case 3: case GL_BGR:  return 3;
        case GL_RGBA: case 4: case GL_BGRA: return 4;
        case GL_LUMINANCE:       case 1: return 1;
        case GL_LUMINANCE_ALPHA: case 2: return 2;
        case GL_ALPHA:                   return 1;
        default: return 0;
    }
}

/* Convert GL_UNSIGNED_BYTE texture to packed RGBA. Caller frees result. */
static unsigned char* TexToRGBA(GLenum fmt, int w, int h,
                                 const unsigned char *src) {
    unsigned char *out = (unsigned char*)malloc(w * h * 4);
    if(!out) return NULL;
    for(int i=0; i<w*h; i++){
        unsigned char r=0,g=0,b=0,a=255;
        switch(fmt){
            case GL_RGB:  case 3:
                r=src[i*3]; g=src[i*3+1]; b=src[i*3+2]; break;
            case GL_RGBA: case 4:
                r=src[i*4]; g=src[i*4+1]; b=src[i*4+2]; a=src[i*4+3]; break;
            case GL_BGR:
                b=src[i*3]; g=src[i*3+1]; r=src[i*3+2]; break;
            case GL_BGRA:
                b=src[i*4]; g=src[i*4+1]; r=src[i*4+2]; a=src[i*4+3]; break;
            case GL_LUMINANCE: case 1:
                r=g=b=src[i]; break;
            case GL_LUMINANCE_ALPHA: case 2:
                r=g=b=src[i*2]; a=src[i*2+1]; break;
            case GL_ALPHA:
                r=g=b=0; a=src[i]; break;
        }
        out[i*4]=r; out[i*4+1]=g; out[i*4+2]=b; out[i*4+3]=a;
    }
    return out;
}

/*
 * Write uncompressed 32-bit TGA (BGRA, top-left origin).
 * hdr[17]=0x28: 8 alpha bits, bit5=top-left.
 */
static void WriteTGA(const char *path, int w, int h,
                     const unsigned char *rgba) {
    FILE *f = fopen(path,"wb");
    if(!f) return;
    unsigned char hdr[18]={0};
    hdr[2]=2;                            /* image type: uncompressed RGB */
    hdr[12]=w&0xFF; hdr[13]=(w>>8)&0xFF;
    hdr[14]=h&0xFF; hdr[15]=(h>>8)&0xFF;
    hdr[16]=32;                          /* bpp */
    hdr[17]=0x28;                        /* 8 alpha bits + top-left origin */
    fwrite(hdr,1,18,f);
    /* Buffered write: convert RGBA→BGRA in chunks to avoid per-pixel fputc */
    {
        int total = w * h;
        int chunk = 4096; /* pixels per chunk */
        unsigned char *buf = (unsigned char*)malloc(chunk * 4);
        if (buf) {
            int i = 0;
            while (i < total) {
                int n = total - i;
                int j;
                if (n > chunk) n = chunk;
                for (j = 0; j < n; j++) {
                    buf[j*4+0] = rgba[(i+j)*4+2]; /* B */
                    buf[j*4+1] = rgba[(i+j)*4+1]; /* G */
                    buf[j*4+2] = rgba[(i+j)*4+0]; /* R */
                    buf[j*4+3] = rgba[(i+j)*4+3]; /* A */
                }
                fwrite(buf, 4, n, f);
                i += n;
            }
            free(buf);
        } else {
            /* Fallback: per-pixel if malloc fails */
            int i;
            for(i=0;i<total;i++){
                fputc(rgba[i*4+2],f);
                fputc(rgba[i*4+1],f);
                fputc(rgba[i*4+0],f);
                fputc(rgba[i*4+3],f);
            }
        }
    }
    fclose(f);
}

/*
 * Read uncompressed 24/32-bit TGA into RGBA buffer.
 * Handles both top-left (bit5 of hdr[17]) and bottom-left origin.
 * Returns NULL on error. Caller frees result.
 */
static unsigned char* ReadTGA(const char *path, int *outW, int *outH) {
    FILE *f = fopen(path,"rb");
    if(!f) return NULL;
    unsigned char hdr[18];
    if(fread(hdr,1,18,f)!=18){fclose(f);return NULL;}
    int idLen=hdr[0], imgType=hdr[2];
    int w=hdr[12]|(hdr[13]<<8), h=hdr[14]|(hdr[15]<<8), bpp=hdr[16];
    int topLeft=(hdr[17]>>5)&1;         /* bit5: 0=bottom-left, 1=top-left */
    if(imgType!=2||hdr[1]!=0||(bpp!=24&&bpp!=32)||w<=0||h<=0)
        {fclose(f);return NULL;}
    if(idLen) fseek(f,idLen,SEEK_CUR);
    int srcBpp=bpp/8, srcSize=w*h*srcBpp;
    unsigned char *src=(unsigned char*)malloc(srcSize);
    if(!src){fclose(f);return NULL;}
    if((int)fread(src,1,srcSize,f)!=srcSize){free(src);fclose(f);return NULL;}
    fclose(f);
    unsigned char *rgba=(unsigned char*)malloc(w*h*4);
    if(!rgba){free(src);return NULL;}
    for(int y=0;y<h;y++){
        int sy=topLeft?y:(h-1-y);      /* flip rows if bottom-left origin */
        for(int x=0;x<w;x++){
            int si=(sy*w+x)*srcBpp, di=(y*w+x)*4;
            rgba[di+0]=src[si+2];      /* R (TGA stores B,G,R[,A]) */
            rgba[di+1]=src[si+1];      /* G */
            rgba[di+2]=src[si+0];      /* B */
            rgba[di+3]=(srcBpp==4)?src[si+3]:255;
        }
    }
    free(src);
    *outW=w; *outH=h;
    return rgba;
}

/* Create texture directories if they don't exist */
static void TexEnsureDirs(void) {
    char p[MAX_PATH];
    snprintf(p,MAX_PATH,"%saitd_textures",g_gameDir);        CreateDirectoryA(p,NULL);
    snprintf(p,MAX_PATH,"%saitd_textures\\dump",g_gameDir);    CreateDirectoryA(p,NULL);
    snprintf(p,MAX_PATH,"%saitd_textures\\replace",g_gameDir); CreateDirectoryA(p,NULL);
}

/*
 * Mark hash as seen for dump dedup.
 * Returns 1 if newly seen (should dump), 0 if already dumped.
 */
static int TexMarkSeen(unsigned int hash) {
    unsigned int idx;
    int i;
    if (hash == 0) return 1; /* 0 is empty sentinel — always "new" */
    if (g_texSeenCount >= TEX_SEEN_MAX * 3 / 4) return 1; /* table full, dump anyway */
    idx = hash & TEX_SEEN_MASK;
    for (i = 0; i < TEX_SEEN_MAX; i++) {
        unsigned int slot = (idx + i) & TEX_SEEN_MASK;
        if (g_texSeen[slot] == hash) return 0; /* already seen */
        if (g_texSeen[slot] == 0) {
            g_texSeen[slot] = hash;
            g_texSeenCount++;
            return 1; /* newly seen */
        }
    }
    return 1;
}

/* Find cached replacement entry using hash table. NULL = not found. */
static TexEntry* TexCacheFind(unsigned int hash) {
    if(hash == TEX_HASH_EMPTY) return NULL;
    unsigned int idx = hash & TEX_CACHE_MASK;
    for(int i=0; i<TEX_CACHE_SIZE; i++){
        unsigned int slot = (idx + i) & TEX_CACHE_MASK;
        if(g_texCache[slot].hash == hash) return &g_texCache[slot];
        if(g_texCache[slot].hash == TEX_HASH_EMPTY) return NULL;
    }
    return NULL;
}

/*
 * Try to load replacement TGA from disk.
 * Inserts into hash table (with rgba=NULL if file not found),
 * so subsequent calls don't hit disk again.
 */
static TexEntry* TexCacheLoad(unsigned int hash, int w, int h) {
    if(hash == TEX_HASH_EMPTY) return NULL;
    if(g_texCacheUsed >= TEX_CACHE_SIZE * 3 / 4) return NULL; /* 75% load limit */
    unsigned int idx = hash & TEX_CACHE_MASK;
    TexEntry *e = NULL;
    for(int i=0; i<TEX_CACHE_SIZE; i++){
        unsigned int slot = (idx + i) & TEX_CACHE_MASK;
        if(g_texCache[slot].hash == TEX_HASH_EMPTY){
            e = &g_texCache[slot]; break;
        }
    }
    if(!e) return NULL;
    g_texCacheUsed++;
    e->hash=hash; e->w=0; e->h=0; e->rgba=NULL;
    char path[MAX_PATH];
    snprintf(path,MAX_PATH,"%saitd_textures\\replace\\%08X_%dx%d.tga",
        g_gameDir,hash,w,h);
    int rw=0,rh=0;
    unsigned char *rdata=ReadTGA(path,&rw,&rh);
    if(rdata){e->w=rw;e->h=rh;e->rgba=rdata;
        LogMsg("TEX: replace %08X %dx%d -> %dx%d\n",hash,w,h,rw,rh);}
    return e;
}

/* ==================================================================
 *  INLINE HOOK (DETOUR) for x86 32-bit
 *
 *  Patches the first 5 bytes of a target function with JMP to our hook.
 *  Creates a trampoline (saved bytes + JMP back) for calling the original.
 *  This catches ALL callers regardless of how they obtained the pointer.
 * ================================================================== */

typedef struct {
    BYTE  *target;       /* original function address */
    BYTE  *trampoline;   /* allocated executable: saved bytes + JMP to target+N */
    BYTE   saved[16];    /* copy of overwritten bytes */
    int    patchLen;     /* how many bytes were overwritten (>=5) */
} DetourHook;

/*
 * Minimal x86 instruction length decoder for common function prologues.
 * Returns length of instruction at `p`. Returns 0 for unknown/unsupported.
 * Only needs to handle the first few bytes of typical Windows DLL exports.
 */
static int x86_insn_len(const BYTE *p) {
    /* Prefixes */
    const BYTE *s = p;
    while (*p==0x66||*p==0x67||*p==0xF2||*p==0xF3||
           (*p>=0x26&&*p<=0x3E&&(*p&7)==6)||*p==0x64||*p==0x65) p++;
    BYTE op = *p++;
    if (op==0x90) return (int)(p-s);                    /* NOP */
    if (op==0xCC||op==0xC3) return (int)(p-s);          /* INT3, RET */
    if (op==0x55||op==0x56||op==0x57||op==0x53||        /* PUSH reg */
        op==0x50||op==0x51||op==0x52||op==0x54) return (int)(p-s);
    if (op==0x5D||op==0x5E||op==0x5F||op==0x5B||        /* POP reg */
        op==0x58||op==0x59||op==0x5A||op==0x5C) return (int)(p-s);
    if ((op&0xF8)==0xB8) return (int)(p-s)+4;           /* MOV reg,imm32 */
    if ((op&0xF8)==0xB0) return (int)(p-s)+1;           /* MOV reg8,imm8 */
    if (op==0x68) return (int)(p-s)+4;                  /* PUSH imm32 */
    if (op==0x6A) return (int)(p-s)+1;                  /* PUSH imm8 */
    if (op==0xE9) return (int)(p-s)+4;                  /* JMP rel32 */
    if (op==0xEB) return (int)(p-s)+1;                  /* JMP rel8 */
    if (op==0xE8) return (int)(p-s)+4;                  /* CALL rel32 */
    /* ModR/M byte instructions */
    if (op==0x8B||op==0x89||op==0x8D||op==0x03||op==0x33||
        op==0x2B||op==0x31||op==0x29||op==0x39||op==0x85||
        op==0x01||op==0x09||op==0x21||op==0x3B||op==0x0B||
        op==0x23||op==0x87) {
        BYTE modrm=*p++; BYTE mod=(modrm>>6)&3, rm=modrm&7;
        if (mod==0 && rm==5) p+=4;          /* [disp32] */
        else if (mod==0 && rm==4) { p++; }  /* SIB, no disp */
        else if (mod==1) { if(rm==4)p++; p++; }   /* disp8 (+SIB) */
        else if (mod==2) { if(rm==4)p++; p+=4; }  /* disp32 (+SIB) */
        /* mod==3: register only */
        return (int)(p-s);
    }
    if (op==0x83||op==0x81) {               /* ADD/SUB/CMP r/m, imm */
        BYTE modrm=*p++; BYTE mod=(modrm>>6)&3, rm=modrm&7;
        if (mod==0 && rm==5) p+=4;
        else if (mod==0 && rm==4) p++;
        else if (mod==1) { if(rm==4)p++; p++; }
        else if (mod==2) { if(rm==4)p++; p+=4; }
        p += (op==0x81) ? 4 : 1;           /* imm32 or imm8 */
        return (int)(p-s);
    }
    if (op==0xA1||op==0xA3) return (int)(p-s)+4; /* MOV EAX,[addr] / [addr],EAX */
    if (op==0xFF) {                         /* INC/DEC/CALL/JMP r/m */
        BYTE modrm=*p++; BYTE mod=(modrm>>6)&3, rm=modrm&7;
        if (mod==0 && rm==5) p+=4;
        else if (mod==0 && rm==4) p++;
        else if (mod==1) { if(rm==4)p++; p++; }
        else if (mod==2) { if(rm==4)p++; p+=4; }
        return (int)(p-s);
    }
    if (op==0x0F) {                         /* Two-byte opcodes */
        BYTE op2=*p++;
        if ((op2&0xF0)==0x80) return (int)(p-s)+4; /* Jcc rel32 */
        if ((op2&0xF0)==0x40) {             /* CMOVcc */
            BYTE modrm=*p++; BYTE mod=(modrm>>6)&3, rm=modrm&7;
            if (mod==0 && rm==5) p+=4;
            else if (mod==0 && rm==4) p++;
            else if (mod==1) { if(rm==4)p++; p++; }
            else if (mod==2) { if(rm==4)p++; p+=4; }
            return (int)(p-s);
        }
    }
    return 0; /* unknown */
}

/*
 * Install detour: patches target with JMP to hook,
 * creates trampoline, returns trampoline pointer (= new "real" function).
 * Returns NULL on failure.
 */
static void* DetourInstall(DetourHook *dh, void *target, void *hook, const char *name) {
    dh->target = (BYTE*)target;
    dh->trampoline = NULL;
    dh->patchLen = 0;

    /* Calculate how many bytes to copy (need >=5 for JMP rel32) */
    int total = 0;
    while (total < 5) {
        int len = x86_insn_len(dh->target + total);
        if (len <= 0) {
            LogMsg("DETOUR: %s FAILED - unknown insn at offset %d (byte 0x%02X)\n",
                name, total, dh->target[total]);
            return NULL;
        }
        total += len;
    }
    dh->patchLen = total;

    /* Allocate trampoline: saved bytes + 5-byte JMP back */
    dh->trampoline = (BYTE*)VirtualAlloc(NULL, total + 5,
        MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!dh->trampoline) {
        LogMsg("DETOUR: %s FAILED - VirtualAlloc\n", name);
        return NULL;
    }

    /* Copy saved bytes to trampoline */
    memcpy(dh->saved, dh->target, total);
    memcpy(dh->trampoline, dh->target, total);

    /* Relocate relative branches (CALL rel32, JMP rel32, Jcc rel32) in
     * the copied prologue.  Without this, a relative E8/E9/0F8x would
     * jump to the wrong address from the trampoline's new location. */
    {
        int off = 0;
        while (off < total) {
            BYTE op = dh->trampoline[off];
            int insn_len = x86_insn_len(dh->trampoline + off);
            if (insn_len <= 0) break;
            /* E8 = CALL rel32, E9 = JMP rel32 */
            if ((op == 0xE8 || op == 0xE9) && insn_len == 5 && off + 5 <= total) {
                DWORD orig_rel = *(DWORD*)(dh->trampoline + off + 1);
                /* Original absolute target = target + off + 5 + orig_rel */
                BYTE *abs_target = dh->target + off + 5 + (int)orig_rel;
                /* New relative = abs_target - (trampoline + off + 5) */
                *(DWORD*)(dh->trampoline + off + 1) =
                    (DWORD)abs_target - (DWORD)(dh->trampoline + off + 5);
                LogMsg("DETOUR: %s relocated %s at prologue+%d\n",
                    name, op == 0xE8 ? "CALL" : "JMP", off);
            }
            /* 0F 8x = Jcc rel32 (two-byte opcode) */
            if (op == 0x0F && off + 1 < total &&
                (dh->trampoline[off+1] & 0xF0) == 0x80 && insn_len == 6 && off + 6 <= total) {
                DWORD orig_rel = *(DWORD*)(dh->trampoline + off + 2);
                BYTE *abs_target = dh->target + off + 6 + (int)orig_rel;
                *(DWORD*)(dh->trampoline + off + 2) =
                    (DWORD)abs_target - (DWORD)(dh->trampoline + off + 6);
                LogMsg("DETOUR: %s relocated Jcc at prologue+%d\n", name, off);
            }
            off += insn_len;
        }
    }

    /* Append JMP from trampoline to target+patchLen */
    dh->trampoline[total] = 0xE9;
    *(DWORD*)(dh->trampoline + total + 1) =
        (DWORD)(dh->target + total) - (DWORD)(dh->trampoline + total + 5);

    /* Patch target with JMP to hook */
    DWORD old;
    if (!VirtualProtect(dh->target, total, PAGE_EXECUTE_READWRITE, &old)) {
        LogMsg("DETOUR: %s FAILED - VirtualProtect\n", name);
        VirtualFree(dh->trampoline, 0, MEM_RELEASE);
        dh->trampoline = NULL;
        return NULL;
    }
    dh->target[0] = 0xE9;
    *(DWORD*)(dh->target + 1) = (DWORD)hook - (DWORD)(dh->target + 5);
    /* NOP remaining bytes if patchLen > 5 */
    for (int i = 5; i < total; i++) dh->target[i] = 0x90;
    VirtualProtect(dh->target, total, old, &old);
    FlushInstructionCache(GetCurrentProcess(), dh->target, total);

    LogMsg("DETOUR: %s OK (patch %d bytes at %p, trampoline %p)\n",
        name, total, dh->target, dh->trampoline);
    return dh->trampoline;
}

static DetourHook g_detourViewport = {0};
static DetourHook g_detourScissor  = {0};

/* Display dimensions — declared early, used by SSAA and viewport hooks */
static int g_displayW=0,g_displayH=0;

/* ==================================================================
 *  MSAA via FBO (Multisample Anti-Aliasing)
 *
 *  Renders the game into a multisampled off-screen FBO, then resolves
 *  (averages samples) to the default framebuffer via glBlitFramebuffer.
 *  True geometric AA — clean, sharp edges without texture blur.
 *  No shaders needed; all work done by GPU hardware.
 *  Supports 2x, 4x, 8x sample counts (default 4x).
 * ================================================================== */

/* FBO / MSAA constants */
#define GL_FRAMEBUFFER            0x8D40
#define GL_RENDERBUFFER           0x8D41
#define GL_COLOR_ATTACHMENT0      0x8CE0
#define GL_DEPTH_STENCIL_ATTACHMENT 0x821A
#define GL_FRAMEBUFFER_COMPLETE   0x8CD5
#define GL_DEPTH24_STENCIL8       0x88F0
#define GL_READ_FRAMEBUFFER       0x8CA8
#define GL_DRAW_FRAMEBUFFER       0x8CA9
#define GL_MAX_SAMPLES            0x8D57

/* FBO extension function types */
typedef void(WINAPI*PFN_glGenFramebuffers)(GLsizei,GLuint*);
typedef void(WINAPI*PFN_glDeleteFramebuffers)(GLsizei,const GLuint*);
typedef void(WINAPI*PFN_glBindFramebuffer)(GLenum,GLuint);
typedef void(WINAPI*PFN_glGenRenderbuffers)(GLsizei,GLuint*);
typedef void(WINAPI*PFN_glDeleteRenderbuffers)(GLsizei,const GLuint*);
typedef void(WINAPI*PFN_glBindRenderbuffer)(GLenum,GLuint);
typedef void(WINAPI*PFN_glRenderbufferStorage)(GLenum,GLenum,GLsizei,GLsizei);
typedef void(WINAPI*PFN_glRenderbufferStorageMultisample)(GLenum,GLsizei,GLenum,GLsizei,GLsizei);
typedef void(WINAPI*PFN_glFramebufferRenderbuffer)(GLenum,GLenum,GLenum,GLuint);
typedef GLenum(WINAPI*PFN_glCheckFramebufferStatus)(GLenum);
typedef void(WINAPI*PFN_glBlitFramebuffer)(GLint,GLint,GLint,GLint,GLint,GLint,GLint,GLint,GLbitfield,GLenum);

/* FBO extension pointers */
static PFN_glGenFramebuffers       pfn_glGenFramebuffers=NULL;
static PFN_glDeleteFramebuffers    pfn_glDeleteFramebuffers=NULL;
static PFN_glBindFramebuffer       pfn_glBindFramebuffer=NULL;
static PFN_glGenRenderbuffers      pfn_glGenRenderbuffers=NULL;
static PFN_glDeleteRenderbuffers   pfn_glDeleteRenderbuffers=NULL;
static PFN_glBindRenderbuffer      pfn_glBindRenderbuffer=NULL;
static PFN_glRenderbufferStorage   pfn_glRenderbufferStorage=NULL;
static PFN_glRenderbufferStorageMultisample pfn_glRenderbufferStorageMultisample=NULL;
static PFN_glFramebufferRenderbuffer pfn_glFramebufferRenderbuffer=NULL;
static PFN_glCheckFramebufferStatus  pfn_glCheckFramebufferStatus=NULL;
static PFN_glBlitFramebuffer       pfn_glBlitFramebuffer=NULL;

/* MSAA FBO state */
static GLuint g_msaaFBO = 0;
static GLuint g_msaaColorRB = 0;  /* multisampled color renderbuffer */
static GLuint g_msaaDepthRB = 0;  /* multisampled depth+stencil renderbuffer */
static int    g_msaaW = 0, g_msaaH = 0;
static int    g_msaaReady = 0;
static int    g_msaaActualSamples = 0; /* samples actually allocated */

static void MSAA_ResolveFunctions(void) {
    if (!real_wglGetProcAddress) return;
    #define RESOLVE_FBO(name) \
        pfn_##name = (PFN_##name)real_wglGetProcAddress(#name); \
        if(!pfn_##name) pfn_##name = (PFN_##name)real_wglGetProcAddress(#name "ARB"); \
        if(!pfn_##name) pfn_##name = (PFN_##name)real_wglGetProcAddress(#name "EXT");
    RESOLVE_FBO(glGenFramebuffers)
    RESOLVE_FBO(glDeleteFramebuffers)
    RESOLVE_FBO(glBindFramebuffer)
    RESOLVE_FBO(glGenRenderbuffers)
    RESOLVE_FBO(glDeleteRenderbuffers)
    RESOLVE_FBO(glBindRenderbuffer)
    RESOLVE_FBO(glRenderbufferStorage)
    RESOLVE_FBO(glRenderbufferStorageMultisample)
    RESOLVE_FBO(glFramebufferRenderbuffer)
    RESOLVE_FBO(glCheckFramebufferStatus)
    RESOLVE_FBO(glBlitFramebuffer)
    #undef RESOLVE_FBO
    LogMsg("MSAA: FBO funcs: GenFB=%p BindFB=%p RBMS=%p Blit=%p\n",
        (void*)pfn_glGenFramebuffers, (void*)pfn_glBindFramebuffer,
        (void*)pfn_glRenderbufferStorageMultisample, (void*)pfn_glBlitFramebuffer);
}

static int MSAA_Init(int w, int h) {
    int samples = g_config.msaa_samples;
    GLenum status;
    GLint maxSamples = 0;

    if (!pfn_glGenFramebuffers || !pfn_glBindFramebuffer ||
        !pfn_glGenRenderbuffers || !pfn_glBindRenderbuffer ||
        !pfn_glRenderbufferStorageMultisample || !pfn_glFramebufferRenderbuffer ||
        !pfn_glCheckFramebufferStatus || !pfn_glBlitFramebuffer) {
        LogMsg("MSAA: missing FBO/multisample functions, disabled\n");
        return 0;
    }

    /* Query max supported samples */
    if (real_glGetIntegerv) {
        real_glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
        LogMsg("MSAA: GPU max samples = %d\n", maxSamples);
        if (samples > maxSamples) samples = maxSamples;
    }
    if (samples < 2) { LogMsg("MSAA: samples < 2, disabled\n"); return 0; }

    /* Create multisampled FBO */
    pfn_glGenFramebuffers(1, &g_msaaFBO);
    pfn_glBindFramebuffer(GL_FRAMEBUFFER, g_msaaFBO);

    /* Clear stale GL errors */
    if (real_glGetError) while (real_glGetError() != GL_NO_ERROR_ENUM) {}

    /* Multisampled color renderbuffer */
    pfn_glGenRenderbuffers(1, &g_msaaColorRB);
    pfn_glBindRenderbuffer(GL_RENDERBUFFER, g_msaaColorRB);
    pfn_glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA8, w, h);
    if (real_glGetError && real_glGetError() != GL_NO_ERROR_ENUM) {
        /* Try fewer samples */
        samples = samples / 2;
        if (samples < 2) goto msaa_fail;
        pfn_glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA8, w, h);
        if (real_glGetError && real_glGetError() != GL_NO_ERROR_ENUM) goto msaa_fail;
    }
    pfn_glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
        GL_RENDERBUFFER, g_msaaColorRB);

    /* Multisampled depth+stencil renderbuffer */
    pfn_glGenRenderbuffers(1, &g_msaaDepthRB);
    pfn_glBindRenderbuffer(GL_RENDERBUFFER, g_msaaDepthRB);
    pfn_glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH24_STENCIL8, w, h);
    pfn_glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
        GL_RENDERBUFFER, g_msaaDepthRB);

    status = pfn_glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        LogMsg("MSAA: FBO incomplete (0x%X)\n", status);
        goto msaa_fail;
    }

    g_msaaW = w; g_msaaH = h;
    g_msaaActualSamples = samples;
    g_msaaReady = 1;
    LogMsg("MSAA: initialized %dx%d @ %dx (FBO=%u colorRB=%u depthRB=%u)\n",
        w, h, samples, g_msaaFBO, g_msaaColorRB, g_msaaDepthRB);
    pfn_glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return 1;

msaa_fail:
    LogMsg("MSAA: init failed, disabled\n");
    if (g_msaaFBO) { pfn_glDeleteFramebuffers(1, &g_msaaFBO); g_msaaFBO = 0; }
    if (g_msaaColorRB) { pfn_glDeleteRenderbuffers(1, &g_msaaColorRB); g_msaaColorRB = 0; }
    if (g_msaaDepthRB) { pfn_glDeleteRenderbuffers(1, &g_msaaDepthRB); g_msaaDepthRB = 0; }
    pfn_glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return 0;
}

/* Bind MSAA FBO — game renders into it */
static void MSAA_BeginFrame(void) {
    if (!g_msaaReady || !g_msaaFBO) return;
    pfn_glBindFramebuffer(GL_FRAMEBUFFER, g_msaaFBO);
}

/* Resolve MSAA: blit multisampled FBO to default framebuffer.
 * glBlitFramebuffer averages all samples per pixel (hardware resolve).
 * Also applies aspect ratio correction (pillarbox/letterbox). */
static void MSAA_EndFrame(void) {
    int dW, dH, dstX, dstY, dstW, dstH;
    double tgt, dispAR;

    if (!g_msaaReady || !g_msaaFBO) return;

    pfn_glBindFramebuffer(GL_READ_FRAMEBUFFER, g_msaaFBO);
    pfn_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

    dW = g_displayW > 0 ? g_displayW : g_msaaW;
    dH = g_displayH > 0 ? g_displayH : g_msaaH;

    /* Aspect ratio correction */
    tgt = 4.0/3.0;
    dispAR = (double)dW / dH;
    dstX = 0; dstY = 0; dstW = dW; dstH = dH;
    if (g_config.fix_aspect_ratio) {
        if (dispAR > tgt + 0.01) {
            dstW = (int)(dH * tgt); dstX = (dW - dstW) / 2;
        } else if (dispAR < tgt - 0.01) {
            dstH = (int)(dW / tgt); dstY = (dH - dstH) / 2;
        }
    }

    /* Clear black bars */
    if (real_glViewport) real_glViewport(0, 0, dW, dH);
    if (real_glClearColor) real_glClearColor(0, 0, 0, 1);
    if (real_glClear) real_glClear(GL_COLOR_BUFFER_BIT);

    /* Blit with MSAA resolve + optional scaling */
    pfn_glBlitFramebuffer(
        0, 0, g_msaaW, g_msaaH,           /* source: full MSAA FBO */
        dstX, dstY, dstX+dstW, dstY+dstH, /* dest: AR-corrected rect */
        GL_COLOR_BUFFER_BIT, GL_LINEAR);

    pfn_glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

static void MSAA_Shutdown(void) {
    if (g_msaaFBO && pfn_glDeleteFramebuffers) pfn_glDeleteFramebuffers(1, &g_msaaFBO);
    if (g_msaaColorRB && pfn_glDeleteRenderbuffers) pfn_glDeleteRenderbuffers(1, &g_msaaColorRB);
    if (g_msaaDepthRB && pfn_glDeleteRenderbuffers) pfn_glDeleteRenderbuffers(1, &g_msaaDepthRB);
    g_msaaFBO = 0; g_msaaColorRB = 0; g_msaaDepthRB = 0;
    g_msaaReady = 0;
}



/* ==================================================================
 *  AUDIO HOOKS (Miles Sound System / mss32.dll)
 *
 *  Intercepts MSS v6 AIL_* functions to fix audio on modern Windows:
 *  1. Force high-quality output format in AIL_waveOutOpen
 *  2. Track sample file identity and position for loop preservation
 *  3. Smooth volume transitions (fade) via per-frame interpolation
 *
 *  All AIL_* functions use __stdcall calling convention.
 *  Export names are decorated: _AIL_xxx@N (N = bytes of params).
 *  We use DetourInstall (inline x86 hook) to catch all callers.
 * ================================================================== */

static HMODULE g_hMSS = NULL; /* mss32.dll handle */

/* MSS status constants */
#define SMP_FREE    0
#define SMP_DONE    1
#define SMP_PLAYING 2
#define SMP_STOPPED 3
#define SMP_STARTED 4  /* just started, transitioning to PLAYING */

/* Helper: is sample actively using a channel? */
#define SMP_IS_ACTIVE(s) ((s)==SMP_PLAYING||(s)==SMP_STARTED)

/* ==================================================================
 *  CUSTOM AUDIO MIXER (replaces MSS for sample playback)
 *
 *  32-channel software mixer with waveOut output.
 *  Decodes IMA ADPCM and PCM WAV files on load.
 *  Eliminates MSS channel limits and zombie sample issues.
 * ================================================================== */

#define MIX_CHANNELS     32
/* v19: MIX_RATE is a runtime variable (WASAPI may override to native device rate).
 * g_mixBufSamples is set in Mix_Init() once g_mixRate is known. */
static int g_mixRate       = 44100;
static int g_mixBufSamples = 0;
#define MIX_RATE         g_mixRate
#define MIX_BUFFER_MS    40   /* buffer size in ms */
#define MIX_BUFFER_SAMPLES g_mixBufSamples
#define MIX_NUM_BUFFERS  4

/* ---- IMA ADPCM tables ---- */
static const int g_imaIndexTable[16] = {
    -1, -1, -1, -1, 2, 4, 6, 8,
    -1, -1, -1, -1, 2, 4, 6, 8
};
static const int g_imaStepTable[89] = {
    7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,
    50,55,60,66,73,80,88,97,107,118,130,143,157,173,190,209,230,
    253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,
    1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,2749,3024,
    3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,
    10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,
    27086,29794,32767
};

/* Decode IMA ADPCM to 16-bit PCM. Returns malloc'd buffer, caller frees.
 * v19: proper stereo handling — Microsoft IMA ADPCM interleaves 4-byte groups
 * (8 nibbles = 8 samples) per channel. The v18 version wrote 8 L-samples to
 * the same output slot, destroying the left channel. */
static short* ADPCM_Decode(const unsigned char *data, int data_size,
                            int block_align, int channels,
                            int *out_samples) {
    int header_size, data_per_block, samples_per_block, blocks;
    int total_samples, out_idx, blk, c;
    short *out;

    if (!data || data_size <= 0 || block_align <= 0 ||
        channels < 1 || channels > 2) return NULL;

    header_size = 4 * channels;
    if (block_align <= header_size) return NULL;
    data_per_block = block_align - header_size;

    /* 1 sample from predictor + (data bytes * 2 nibbles) / channel */
    samples_per_block = 1 + (data_per_block * 2) / channels;

    blocks = data_size / block_align;
    if (blocks <= 0) return NULL;

    total_samples = blocks * samples_per_block;
    out = (short*)malloc(total_samples * channels * sizeof(short));
    if (!out) return NULL;

    out_idx = 0;
    for (blk = 0; blk < blocks; blk++) {
        int predictor[2] = {0, 0};
        int step_index[2] = {0, 0};
        const unsigned char *src = data + blk * block_align;

        /* Read per-channel 4-byte headers and write initial sample */
        for (c = 0; c < channels; c++) {
            predictor[c] = (short)(src[0] | (src[1] << 8));
            step_index[c] = src[2];
            if (step_index[c] < 0)  step_index[c] = 0;
            if (step_index[c] > 88) step_index[c] = 88;
            src += 4;
            out[(out_idx + 0) * channels + c] = (short)predictor[c];
        }

        if (channels == 1) {
            int n, samp_out = 1;
            int nibble_count = data_per_block * 2;
            for (n = 0; n < nibble_count && samp_out < samples_per_block; n++) {
                int nibble, step, diff, pred;
                nibble = (n & 1) ? (src[n/2] >> 4) : (src[n/2] & 0x0F);
                step = g_imaStepTable[step_index[0]];
                diff = step >> 3;
                if (nibble & 1) diff += step >> 2;
                if (nibble & 2) diff += step >> 1;
                if (nibble & 4) diff += step;
                if (nibble & 8) diff = -diff;
                pred = predictor[0] + diff;
                if (pred > 32767) pred = 32767;
                if (pred < -32768) pred = -32768;
                predictor[0] = pred;
                step_index[0] += g_imaIndexTable[nibble & 0x0F];
                if (step_index[0] < 0)  step_index[0] = 0;
                if (step_index[0] > 88) step_index[0] = 88;
                out[out_idx + samp_out] = (short)pred;
                samp_out++;
            }
        } else {
            /* Stereo: 4-byte groups alternately L, R, L, R */
            int groups = data_per_block / 4;
            int g, ch_out_idx[2] = {1, 1};
            const unsigned char *gp = src;
            for (g = 0; g < groups; g++) {
                int ch = g & 1;
                int n;
                for (n = 0; n < 8; n++) {
                    int nibble, step, diff, pred;
                    if (ch_out_idx[ch] >= samples_per_block) break;
                    nibble = (n & 1) ? (gp[n/2] >> 4) : (gp[n/2] & 0x0F);
                    step = g_imaStepTable[step_index[ch]];
                    diff = step >> 3;
                    if (nibble & 1) diff += step >> 2;
                    if (nibble & 2) diff += step >> 1;
                    if (nibble & 4) diff += step;
                    if (nibble & 8) diff = -diff;
                    pred = predictor[ch] + diff;
                    if (pred > 32767) pred = 32767;
                    if (pred < -32768) pred = -32768;
                    predictor[ch] = pred;
                    step_index[ch] += g_imaIndexTable[nibble & 0x0F];
                    if (step_index[ch] < 0)  step_index[ch] = 0;
                    if (step_index[ch] > 88) step_index[ch] = 88;
                    out[(out_idx + ch_out_idx[ch]) * 2 + ch] = (short)pred;
                    ch_out_idx[ch]++;
                }
                gp += 4;
            }
        }
        out_idx += samples_per_block;
    }
    *out_samples = out_idx;
    return out;
}

/* ==================================================================
 *  v20: PERSISTENT PCM CACHE (aitd_pcm_cache/<hash>.wav)
 *  Saves ADPCM→PCM decode results to disk, so subsequent runs just
 *  mmap/read them instead of re-decoding. Hashes are FNV-1a of the
 *  first 128 bytes of the source (matches HashMap_Lookup).
 * ================================================================== */

static volatile int g_pcmCacheReady = 0;
static char g_pcmCacheDir[MAX_PATH] = {0};
static int g_pcmCacheHits = 0;
static int g_pcmCacheMisses = 0;

static void PCMCache_Init(void) {
    if (g_pcmCacheReady || !g_config.enable_pcm_cache) return;
    snprintf(g_pcmCacheDir, MAX_PATH, "%saitd_pcm_cache", g_gameDir);
    CreateDirectoryA(g_pcmCacheDir, NULL);
    g_pcmCacheReady = 1;
    LogMsg("PCMCACHE: dir=%s\n", g_pcmCacheDir);
}

/* ==================================================================
 *  PCM PRE-WARM POOL
 *  Loads all aitd_pcm_cache/*.wav files into an in-memory hash table
 *  at startup. Eliminates ALL disk I/O from set_sample_file during
 *  gameplay. ~52 MB for 912 files — acceptable on modern systems.
 *  Checked BEFORE PCMCache_Load (disk) in the lookup chain.
 * ================================================================== */
#define PCMPOOL_BITS  10
#define PCMPOOL_SIZE  (1 << PCMPOOL_BITS)  /* 1024 slots */
#define PCMPOOL_MASK  (PCMPOOL_SIZE - 1)

typedef struct {
    unsigned int   hash;     /* 0 = empty slot */
    unsigned char *buf;
    int            size;
} PCMPoolEntry;

static PCMPoolEntry g_pcmPool[PCMPOOL_SIZE];
static int g_pcmPoolCount = 0;
static volatile int g_pcmPoolReady = 0;
static int g_pcmPoolHits  = 0;
static long long g_pcmPoolBytes = 0;

/* Returns a DUPLICATE buffer. Caller owns it and must free(). */
static unsigned char* PCMPool_Find(unsigned int hash, int *out_size) {
    unsigned int idx, i;
    unsigned char *dup;
    if (!g_pcmPoolReady || hash == 0) return NULL;
    idx = hash & PCMPOOL_MASK;
    for (i = 0; i < PCMPOOL_SIZE; i++) {
        unsigned int slot = (idx + i) & PCMPOOL_MASK;
        if (g_pcmPool[slot].hash == hash && g_pcmPool[slot].buf) {
            dup = (unsigned char*)malloc(g_pcmPool[slot].size);
            if (!dup) return NULL;
            memcpy(dup, g_pcmPool[slot].buf, g_pcmPool[slot].size);
            *out_size = g_pcmPool[slot].size;
            g_pcmPoolHits++;
            return dup;
        }
        if (g_pcmPool[slot].hash == 0) return NULL; /* empty = miss */
    }
    return NULL;
}

static void PCMPool_Insert(unsigned int hash, unsigned char *buf, int size) {
    unsigned int idx, i;
    if (hash == 0 || !buf || g_pcmPoolCount >= PCMPOOL_SIZE * 3 / 4) return;
    idx = hash & PCMPOOL_MASK;
    for (i = 0; i < PCMPOOL_SIZE; i++) {
        unsigned int slot = (idx + i) & PCMPOOL_MASK;
        if (g_pcmPool[slot].hash == 0) {
            g_pcmPool[slot].hash = hash;
            g_pcmPool[slot].buf  = buf;
            g_pcmPool[slot].size = size;
            g_pcmPoolCount++;
            g_pcmPoolBytes += size;
            return;
        }
        if (g_pcmPool[slot].hash == hash) return; /* already present */
    }
}

static void PCMPool_Shutdown(void) {
    int i;
    for (i = 0; i < PCMPOOL_SIZE; i++) {
        if (g_pcmPool[i].buf) { free(g_pcmPool[i].buf); g_pcmPool[i].buf = NULL; }
        g_pcmPool[i].hash = 0;
    }
    g_pcmPoolCount = 0;
    g_pcmPoolReady = 0;
}

/* Scan pcm_cache dir and load all .wav files into pool */
static void PCMPool_Init(void) {
    WIN32_FIND_DATAA fd;
    HANDLE hFind;
    char pattern[MAX_PATH], path[MAX_PATH];
    DWORD t0;

    if (!g_pcmCacheReady || !g_config.enable_pcm_prewarm) return;

    t0 = GetTickCount();
    snprintf(pattern, MAX_PATH, "%s\\*.wav", g_pcmCacheDir);
    hFind = FindFirstFileA(pattern, &fd);
    if (hFind == INVALID_HANDLE_VALUE) {
        LogMsg("PCMPOOL: no files in %s\n", g_pcmCacheDir);
        return;
    }

    memset(g_pcmPool, 0, sizeof(g_pcmPool));

    do {
        unsigned int hash;
        FILE *fp;
        long sz;
        unsigned char *buf;

        /* Parse hash from filename: "abcdef01.wav" → 0xABCDEF01 */
        hash = (unsigned int)strtoul(fd.cFileName, NULL, 16);
        if (hash == 0) continue;

        snprintf(path, MAX_PATH, "%s\\%s", g_pcmCacheDir, fd.cFileName);
        fp = fopen(path, "rb");
        if (!fp) continue;
        fseek(fp, 0, SEEK_END); sz = ftell(fp); fseek(fp, 0, SEEK_SET);
        if (sz < 44 || sz > 50*1024*1024) { fclose(fp); continue; }
        buf = (unsigned char*)malloc(sz);
        if (!buf) { fclose(fp); continue; }
        if (fread(buf, 1, sz, fp) != (size_t)sz) { free(buf); fclose(fp); continue; }
        fclose(fp);
        if (buf[0]!='R'||buf[1]!='I'||buf[2]!='F'||buf[3]!='F') { free(buf); continue; }

        PCMPool_Insert(hash, buf, (int)sz);
    } while (FindNextFileA(hFind, &fd));

    FindClose(hFind);
    g_pcmPoolReady = 1;

    LogMsg("PCMPOOL: pre-warmed %d files (%.1f MB) in %lu ms\n",
        g_pcmPoolCount, (double)g_pcmPoolBytes / (1024.0*1024.0),
        GetTickCount() - t0);
}

static void PCMCache_Path(unsigned int hash, char *out, int out_size) {
    snprintf(out, out_size, "%s\\%08x.wav", g_pcmCacheDir, hash);
}

/* Returns malloc'd buffer + size, or NULL on miss. Caller frees. */
static unsigned char* PCMCache_Load(unsigned int hash, int *out_size) {
    char path[MAX_PATH];
    FILE *fp;
    long sz;
    unsigned char *buf;

    if (!g_pcmCacheReady || hash == 0) return NULL;
    PCMCache_Path(hash, path, MAX_PATH);
    fp = fopen(path, "rb");
    if (!fp) return NULL;
    fseek(fp, 0, SEEK_END); sz = ftell(fp); fseek(fp, 0, SEEK_SET);
    if (sz < 44 || sz > 50*1024*1024) { fclose(fp); return NULL; }
    buf = (unsigned char*)malloc(sz);
    if (!buf) { fclose(fp); return NULL; }
    if (fread(buf, 1, sz, fp) != (size_t)sz) { free(buf); fclose(fp); return NULL; }
    fclose(fp);
    if (buf[0]!='R'||buf[1]!='I'||buf[2]!='F'||buf[3]!='F') { free(buf); return NULL; }
    *out_size = (int)sz;
    g_pcmCacheHits++;
    return buf;
}

/* Save decoded PCM WAV into cache. Returns 1 on success. */
static int PCMCache_Save(unsigned int hash, const unsigned char *pcm_wav, int size) {
    char path[MAX_PATH], tmp[MAX_PATH];
    FILE *fp;
    if (!g_pcmCacheReady || hash == 0 || !pcm_wav || size < 44) return 0;
    PCMCache_Path(hash, path, MAX_PATH);
    /* Write to .tmp then rename to avoid partial files on crash */
    snprintf(tmp, MAX_PATH, "%s.tmp", path);
    fp = fopen(tmp, "wb");
    if (!fp) return 0;
    if (fwrite(pcm_wav, 1, size, fp) != (size_t)size) {
        fclose(fp); DeleteFileA(tmp); return 0;
    }
    fclose(fp);
    DeleteFileA(path);  /* MoveFile would fail if target exists on some FS */
    if (!MoveFileA(tmp, path)) {
        DeleteFileA(tmp);
        return 0;
    }
    g_pcmCacheMisses++;
    return 1;
}

/* ---- Decoded PCM buffer cache (for ADPCM→PCM→MSS passthrough) ---- */
/* v21: increased from 16 to 128 + hash-based lookup.
 * During room transitions 10-15 samples load at once; with only 16 slots
 * keyed by (handle, orig_data) the cache thrashed on every room change,
 * falling through to disk reads.  Hash-based fallback lets us find the
 * same WAV loaded on a *different* handle without touching the disk. */
#define DECODE_CACHE_MAX 128
typedef struct {
    void *handle;
    const void *orig_data;   /* original ADPCM data pointer (for cache check) */
    unsigned char *wav_buf;  /* malloc'd WAV with PCM data */
    int wav_size;
    unsigned int data_hash;  /* FNV-1a of first AUDIO_HASH_BYTES (0 = unset) */
    DWORD last_used;         /* GetTickCount() of last access (for LRU eviction) */
} DecodeCache;
static DecodeCache g_decodeCache[DECODE_CACHE_MAX];
static int g_decodeCacheHits = 0;     /* handle+ptr hits (fastest path) */
static int g_decodeCacheHashHits = 0; /* hash-only hits (avoided disk I/O) */

static void DecodeCache_Store(void *handle, const void *orig_data,
                               unsigned char *wav_buf, int wav_size) {
    int i, free_slot = -1;
    DWORD now = GetTickCount();
    for (i = 0; i < DECODE_CACHE_MAX; i++) {
        if (g_decodeCache[i].handle == handle) {
            if (g_decodeCache[i].wav_buf) free(g_decodeCache[i].wav_buf);
            g_decodeCache[i].orig_data = orig_data;
            g_decodeCache[i].wav_buf = wav_buf;
            g_decodeCache[i].wav_size = wav_size;
            g_decodeCache[i].last_used = now;
            return;
        }
        if (free_slot < 0 && g_decodeCache[i].handle == NULL) free_slot = i;
    }
    if (free_slot < 0) {
        /* Evict least recently used entry (LRU) */
        DWORD oldest = 0xFFFFFFFF;
        int si = 0;
        for (i = 0; i < DECODE_CACHE_MAX; i++) {
            if (g_decodeCache[i].last_used < oldest) {
                oldest = g_decodeCache[i].last_used; si = i;
            }
        }
        free_slot = si;
    }
    if (g_decodeCache[free_slot].wav_buf) free(g_decodeCache[free_slot].wav_buf);
    g_decodeCache[free_slot].handle = handle;
    g_decodeCache[free_slot].orig_data = orig_data;
    g_decodeCache[free_slot].wav_buf = wav_buf;
    g_decodeCache[free_slot].wav_size = wav_size;
    g_decodeCache[free_slot].data_hash = 0; /* caller sets via DecodeCache_SetHash */
    g_decodeCache[free_slot].last_used = now;
}

/* Set the content hash for the most recently stored entry for this handle */
static void DecodeCache_SetHash(void *handle, unsigned int hash) {
    int i;
    for (i = 0; i < DECODE_CACHE_MAX; i++)
        if (g_decodeCache[i].handle == handle && g_decodeCache[i].wav_buf)
            { g_decodeCache[i].data_hash = hash; return; }
}

static DecodeCache* DecodeCache_Find(void *handle, const void *orig_data) {
    int i;
    for (i = 0; i < DECODE_CACHE_MAX; i++)
        if (g_decodeCache[i].handle == handle && g_decodeCache[i].orig_data == orig_data
            && g_decodeCache[i].wav_buf) {
            g_decodeCache[i].last_used = GetTickCount();
            return &g_decodeCache[i];
        }
    return NULL;
}

/* Hash-based fallback: find any cached buffer with the same content hash.
 * Returns a DUPLICATE buffer (caller owns it) so each handle has its own copy.
 * This avoids disk I/O when the same sound is loaded on a different handle
 * (common during room transitions). */
static unsigned char* DecodeCache_FindByHash(unsigned int hash, int *out_size) {
    int i;
    unsigned char *dup;
    if (hash == 0) return NULL;
    for (i = 0; i < DECODE_CACHE_MAX; i++) {
        if (g_decodeCache[i].data_hash == hash && g_decodeCache[i].wav_buf &&
            g_decodeCache[i].wav_size > 0) {
            dup = (unsigned char*)malloc(g_decodeCache[i].wav_size);
            if (!dup) return NULL;
            memcpy(dup, g_decodeCache[i].wav_buf, g_decodeCache[i].wav_size);
            *out_size = g_decodeCache[i].wav_size;
            g_decodeCacheHashHits++;
            return dup;
        }
    }
    return NULL;
}

/* Build a complete WAV file (RIFF header + fmt + data) from decoded PCM samples.
 * Returns malloc'd buffer. Caller stores it in DecodeCache. */
static unsigned char* BuildPCMWav(const short *pcm, int total_samples,
                                   int sample_rate, int num_ch, int *out_size) {
    int data_size = total_samples * num_ch * 2; /* 16-bit samples */
    int wav_size = 44 + data_size;
    unsigned char *buf = (unsigned char*)malloc(wav_size);
    if (!buf) return NULL;

    /* v20: NO modifications to PCM data — original loop points preserved.
     * MSS handles looping for sample-path sounds; our mixer uses its own
     * LoopCrossfadeMs for stream-path sounds. */
    memcpy(buf + 44, pcm, data_size);

    /* RIFF header */
    memcpy(buf, "RIFF", 4);
    *(unsigned int*)(buf + 4) = wav_size - 8;
    memcpy(buf + 8, "WAVE", 4);

    /* fmt chunk */
    memcpy(buf + 12, "fmt ", 4);
    *(unsigned int*)(buf + 16) = 16;
    *(unsigned short*)(buf + 20) = 0x0001;                    /* PCM */
    *(unsigned short*)(buf + 22) = (unsigned short)num_ch;
    *(unsigned int*)(buf + 24) = sample_rate;
    *(unsigned int*)(buf + 28) = sample_rate * num_ch * 2;
    *(unsigned short*)(buf + 32) = (unsigned short)(num_ch * 2);
    *(unsigned short*)(buf + 34) = 16;

    /* data chunk */
    memcpy(buf + 36, "data", 4);
    *(unsigned int*)(buf + 40) = data_size;
    /* PCM data already at buf+44 */

    *out_size = wav_size;
    return buf;
}
#define PENDING_MAX 16
#define PENDING_HAS_VOL   1
#define PENDING_HAS_PAN   2
#define PENDING_HAS_RATE  4
#define PENDING_HAS_LOOP  8
typedef struct {
    void *handle;
    int   flags;
    int   volume;
    int   pan;
    int   playback_rate;
    int   loop_count;
} PendingState;
static PendingState g_pending[PENDING_MAX];

static void Pending_Set(void *handle, int flag, int value) {
    int i, free_slot = -1;
    for (i = 0; i < PENDING_MAX; i++) {
        if (g_pending[i].handle == handle) {
            g_pending[i].flags |= flag;
            if (flag & PENDING_HAS_VOL)  g_pending[i].volume = value;
            if (flag & PENDING_HAS_PAN)  g_pending[i].pan = value;
            if (flag & PENDING_HAS_RATE) g_pending[i].playback_rate = value;
            if (flag & PENDING_HAS_LOOP) g_pending[i].loop_count = value;
            return;
        }
        if (free_slot < 0 && g_pending[i].handle == NULL) free_slot = i;
    }
    if (free_slot >= 0) {
        memset(&g_pending[free_slot], 0, sizeof(PendingState));
        g_pending[free_slot].handle = handle;
        g_pending[free_slot].flags = flag;
        g_pending[free_slot].volume = 127;
        g_pending[free_slot].pan = 64;
        g_pending[free_slot].playback_rate = 0;
        g_pending[free_slot].loop_count = 1;
        if (flag & PENDING_HAS_VOL)  g_pending[free_slot].volume = value;
        if (flag & PENDING_HAS_PAN)  g_pending[free_slot].pan = value;
        if (flag & PENDING_HAS_RATE) g_pending[free_slot].playback_rate = value;
        if (flag & PENDING_HAS_LOOP) g_pending[free_slot].loop_count = value;
    }
}

static PendingState* Pending_Get(void *handle) {
    int i;
    for (i = 0; i < PENDING_MAX; i++)
        if (g_pending[i].handle == handle && g_pending[i].flags) return &g_pending[i];
    return NULL;
}

static void Pending_Clear(void *handle) {
    int i;
    for (i = 0; i < PENDING_MAX; i++)
        if (g_pending[i].handle == handle) { g_pending[i].handle = NULL; g_pending[i].flags = 0; }
}

/* ---- Mixer channel ---- */
typedef struct {
    short       *pcm_data;      /* decoded PCM (malloc'd, mono or stereo) */
    int          pcm_samples;   /* total samples */
    int          pcm_channels;  /* 1=mono, 2=stereo */
    int          sample_rate;   /* original sample rate from WAV header */
    int          playback_rate; /* actual playback rate (game may override) */
    double       src_pos;       /* fractional position for resampling */
    int          volume;        /* 0-127 */
    int          pan;           /* 0=left, 64=center, 127=right */
    int          loop_count;    /* 0=infinite, 1=once, n=n times */
    int          loops_done;    /* loops completed */
    int          status;        /* SMP_FREE/PLAYING/DONE/STOPPED */
    void        *game_handle;   /* MSS sample handle (for mapping), NULL=orphan */
    const void  *last_data_ptr; /* cache: skip re-decode if same data */
    unsigned int data_hash;     /* for dedup/tracking */
    /* v19 additions */
    int          vol_cur;       /* current volume for per-sample ramp (kills zipper) */
    int          xfade_samples; /* loop crossfade length in samples (0=disabled) */
} MixChannel;

static MixChannel g_mixChannels[MIX_CHANNELS];
static CRITICAL_SECTION g_mixCS;
static volatile int g_mixCS_ready = 0;  /* set once after InitializeCriticalSection */
static HWAVEOUT g_hWaveOut = NULL;
static WAVEHDR g_waveHdr[MIX_NUM_BUFFERS];
/* v19: dynamic buffers (allocated in Mix_Init after g_mixRate is known) */
static short  *g_mixBuf[MIX_NUM_BUFFERS] = {0};
static volatile int g_mixReady = 0;

/* Find channel by game handle */
static MixChannel* Mix_FindByHandle(void *handle) {
    int i;
    for (i = 0; i < MIX_CHANNELS; i++)
        if (g_mixChannels[i].game_handle == handle) return &g_mixChannels[i];
    return NULL;
}

/* Allocate a free channel. If handle is in use, orphan old channel. */
static MixChannel* Mix_Alloc(void *handle) {
    int i;
    MixChannel *ch;
    /* If this handle already has a channel, orphan it (let it finish playing) */
    ch = Mix_FindByHandle(handle);
    if (ch) {
        if (ch->status == SMP_PLAYING && ch->loop_count != 0) {
            /* One-shot playing → orphan: remove handle, let it finish */
            ch->game_handle = NULL;
            LogMsg("MIXER: orphan channel (was %p, hash=%08X)\n", handle, ch->data_hash);
        } else {
            /* Ambient loop or done → just reuse */
            return ch;
        }
    }
    /* Find free channel */
    for (i = 0; i < MIX_CHANNELS; i++)
        if (g_mixChannels[i].status == SMP_FREE) return &g_mixChannels[i];
    /* Find DONE channel */
    for (i = 0; i < MIX_CHANNELS; i++)
        if (g_mixChannels[i].status == SMP_DONE) {
            if (g_mixChannels[i].pcm_data) { free(g_mixChannels[i].pcm_data); g_mixChannels[i].pcm_data = NULL; }
            g_mixChannels[i].vol_cur = 0;
            return &g_mixChannels[i];
        }
    /* Evict quietest orphan or quietest channel */
    {
        int quietest = 999, qi = -1;
        /* Prefer evicting orphans */
        for (i = 0; i < MIX_CHANNELS; i++) {
            if (g_mixChannels[i].game_handle == NULL && g_mixChannels[i].volume < quietest) {
                quietest = g_mixChannels[i].volume; qi = i;
            }
        }
        if (qi < 0) {
            for (i = 0; i < MIX_CHANNELS; i++) {
                if (g_mixChannels[i].volume < quietest) {
                    quietest = g_mixChannels[i].volume; qi = i;
                }
            }
        }
        if (qi < 0) qi = 0;
        ch = &g_mixChannels[qi];
        LogMsg("MIXER: EVICT ch[%d] (handle=%p vol=%d status=%d) for %p\n",
            qi, ch->game_handle, ch->volume, ch->status, handle);
        if (ch->pcm_data) { free(ch->pcm_data); ch->pcm_data = NULL; }
        ch->vol_cur = 0;
        return ch;
    }
}

/* Compute crossfade length for a channel once (called from LoadSample). */
static void Mix_SetupCrossfade(MixChannel *ch) {
    int want;
    if (!ch || ch->sample_rate <= 0) return;
    want = (g_config.loop_crossfade_ms * ch->sample_rate) / 1000;
    if (want > ch->pcm_samples / 4) want = ch->pcm_samples / 4;
    if (want < 0) want = 0;
    ch->xfade_samples = want;
}

/* v19: Mix all active channels into output buffer.
 * Adds: loop crossfade (seamless loops), per-sample volume ramp (no zipper). */
static void Mix_Render(short *out, int num_samples) {
    int i, s;
    memset(out, 0, num_samples * 2 * sizeof(short));

    if (!g_mixReady || !g_mixCS_ready) return; /* WASAPI thread may fire before init or after shutdown */

    EnterCriticalSection(&g_mixCS);
    for (i = 0; i < MIX_CHANNELS; i++) {
        MixChannel *ch = &g_mixChannels[i];
        double rate_ratio, src_pos;
        int vol_target, vol_cur;

        if (ch->status != SMP_PLAYING || !ch->pcm_data) continue;

        vol_target = ch->volume;
        vol_cur    = ch->vol_cur;
        if (vol_target <= 0 && vol_cur <= 0) continue;

        rate_ratio = (double)ch->playback_rate / (double)MIX_RATE;
        src_pos    = ch->src_pos;

        for (s = 0; s < num_samples; s++) {
            int pos0 = (int)src_pos;
            float frac = (float)(src_pos - pos0);
            int left, right;

            if (pos0 >= ch->pcm_samples) {
                if (ch->loop_count == 0) {
                    pos0 = 0; src_pos = 0.0;
                } else {
                    ch->loops_done++;
                    if (ch->loops_done >= ch->loop_count) {
                        ch->status = SMP_DONE;
                        if (ch->game_handle == NULL) {
                            free(ch->pcm_data);
                            ch->pcm_data = NULL;
                            ch->status = SMP_FREE;
                        }
                        break;
                    }
                    pos0 = 0; src_pos = 0.0;
                }
            }

            /* Linear interpolation + (for infinite loops) crossfade at tail */
            {
                int pos1 = pos0 + 1;
                int wrap_pos = -1;
                float xf = 0.0f;

                if (pos1 >= ch->pcm_samples) pos1 = pos0;

                /* Crossfade region of an infinite loop */
                if (ch->loop_count == 0 && ch->xfade_samples > 0) {
                    int xf_start = ch->pcm_samples - ch->xfade_samples;
                    if (pos0 >= xf_start) {
                        wrap_pos = pos0 - xf_start;
                        xf = (float)wrap_pos / (float)ch->xfade_samples;
                    }
                }

                /* v20: Hermite 4-tap cubic interpolation (better than linear for
                 * resampling 37800→48000 or 44100→48000 via WASAPI) */
                {
                    int pm1 = pos0 > 0 ? pos0 - 1 : 0;
                    int p2  = pos0 + 2 < ch->pcm_samples ? pos0 + 2 : ch->pcm_samples - 1;
                    float xm1_l, x0_l, x1_l, x2_l;
                    float xm1_r, x0_r, x1_r, x2_r;
                    float c0, c1, c2, c3;

                    if (ch->pcm_channels == 2) {
                        xm1_l = (float)ch->pcm_data[pm1*2];   xm1_r = (float)ch->pcm_data[pm1*2+1];
                        x0_l  = (float)ch->pcm_data[pos0*2];  x0_r  = (float)ch->pcm_data[pos0*2+1];
                        x1_l  = (float)ch->pcm_data[pos1*2];  x1_r  = (float)ch->pcm_data[pos1*2+1];
                        x2_l  = (float)ch->pcm_data[p2*2];    x2_r  = (float)ch->pcm_data[p2*2+1];
                    } else {
                        xm1_l = xm1_r = (float)ch->pcm_data[pm1];
                        x0_l  = x0_r  = (float)ch->pcm_data[pos0];
                        x1_l  = x1_r  = (float)ch->pcm_data[pos1];
                        x2_l  = x2_r  = (float)ch->pcm_data[p2];
                    }
                    /* Hermite basis: left channel */
                    c0 = x0_l;
                    c1 = 0.5f * (x1_l - xm1_l);
                    c2 = xm1_l - 2.5f * x0_l + 2.0f * x1_l - 0.5f * x2_l;
                    c3 = 0.5f * (x2_l - xm1_l) + 1.5f * (x0_l - x1_l);
                    left = (int)(((c3 * frac + c2) * frac + c1) * frac + c0);
                    /* right channel */
                    c0 = x0_r;
                    c1 = 0.5f * (x1_r - xm1_r);
                    c2 = xm1_r - 2.5f * x0_r + 2.0f * x1_r - 0.5f * x2_r;
                    c3 = 0.5f * (x2_r - xm1_r) + 1.5f * (x0_r - x1_r);
                    right = (int)(((c3 * frac + c2) * frac + c1) * frac + c0);
                }

                if (wrap_pos >= 0 && wrap_pos < ch->pcm_samples) {
                    int wl, wr;
                    if (ch->pcm_channels == 2) {
                        wl = ch->pcm_data[wrap_pos * 2];
                        wr = ch->pcm_data[wrap_pos * 2 + 1];
                    } else {
                        wl = wr = ch->pcm_data[wrap_pos];
                    }
                    left  = (int)(left  * (1.0f - xf) + wl * xf);
                    right = (int)(right * (1.0f - xf) + wr * xf);
                }
            }

            /* Per-sample volume ramp */
            if (vol_cur != vol_target) {
                if (vol_cur < vol_target) { vol_cur++; if (vol_cur > vol_target) vol_cur = vol_target; }
                else                      { vol_cur--; if (vol_cur < vol_target) vol_cur = vol_target; }
            }
            left  = left  * vol_cur / 127;
            right = right * vol_cur / 127;

            /* v20: constant-power pan (sin/cos law) — avoids -3dB hole at center */
            {
                float pan_f = (float)ch->pan / 127.0f; /* 0.0=left, 0.5=center, 1.0=right */
                float angle = pan_f * 1.5707963f;        /* 0..π/2 */
                float gain_r = sinf(angle);
                float gain_l = cosf(angle);
                left  = (int)(left  * gain_l);
                right = (int)(right * gain_r);
            }

            {
                int ml = out[s*2]     + left;
                int mr = out[s*2 + 1] + right;
                /* v20: soft clip — 4:1 compression above ±24000,
                 * prevents harsh digital distortion on loud scenes */
                if (ml >  24000) ml = 24000 + (ml - 24000) / 4;
                if (ml < -24000) ml = -24000 + (ml + 24000) / 4;
                if (mr >  24000) mr = 24000 + (mr - 24000) / 4;
                if (mr < -24000) mr = -24000 + (mr + 24000) / 4;
                if (ml >  32767) ml =  32767; if (ml < -32768) ml = -32768;
                if (mr >  32767) mr =  32767; if (mr < -32768) mr = -32768;
                out[s*2]     = (short)ml;
                out[s*2 + 1] = (short)mr;
            }

            src_pos += rate_ratio;
        }
        ch->src_pos = src_pos;
        ch->vol_cur = vol_cur;
    }
    LeaveCriticalSection(&g_mixCS);
}

/* waveOut callback — just signal the mixer thread */
static HANDLE g_mixEvent = NULL;
static HANDLE g_mixThread = NULL;
static volatile int g_mixRunning = 0;

static void CALLBACK Mix_WaveOutProc(HWAVEOUT hwo, UINT uMsg,
    DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR dwParam2) {
    (void)hwo; (void)dwInstance; (void)dwParam1; (void)dwParam2;
    if (uMsg == WOM_DONE && g_mixEvent)
        SetEvent(g_mixEvent);
}

/* Forward decls for v19/v20 audio layer (must precede Mix_ThreadProc) */
static int  WASAPI_Init(int latency_ms);
static void WASAPI_Shutdown(void);
static int  Audio_v19_Init(HMODULE hMSS);
static void Duck_Apply(void *dig_driver);
static int  Reverb_Init(int sample_rate);
static void Reverb_Shutdown(void);
static void Reverb_Process(short *buf, int frames);
static void EQ_Init(int sample_rate);
static void EQ_Process(short *buf, int frames);
static void MusicReplace_Init(void);
static void MusicReplace_InstallHook(void);
static void MusicReplace_Stop(void);
static void MidiChannelPatch_Install(void);
static volatile int  g_wasapiReady;
/* v19 stream forward declarations (for coexist routing in stream hooks below) */
typedef struct StreamPlayer_s StreamPlayer;
struct StreamPlayer_s {
    int         active;
    void       *real_handle;    /* MSS-returned handle (what game sees) */
    void       *dig_driver;
    char        filename[MAX_PATH];
    unsigned char *file_data;
    int         file_size;
    void       *mixer_handle;   /* = (void*)self, used with Mix_* */
    int         loop_count;
    int         target_volume;
};
static int  StreamPlayer_IsOurs(void *h);
static StreamPlayer* StreamPlayer_FindByReal(void *real_h);

/* Mixer thread: waits for buffer completion, renders next buffer */
static DWORD WINAPI Mix_ThreadProc(LPVOID param) {
    int underruns = 0;
    int tick = 0;
    (void)param;
    while (g_mixRunning) {
        int i, rendered = 0;
        WaitForSingleObject(g_mixEvent, 50);
        for (i = 0; i < MIX_NUM_BUFFERS; i++) {
            if (g_waveHdr[i].dwFlags & WHDR_DONE) {
                Mix_Render((short*)g_waveHdr[i].lpData, MIX_BUFFER_SAMPLES);
                /* v19: apply reverb in-place before output */
                if (g_config.enable_reverb)
                    Reverb_Process((short*)g_waveHdr[i].lpData, MIX_BUFFER_SAMPLES);
                /* v21: high-shelf EQ — restore ADPCM-lost high frequencies */
                EQ_Process((short*)g_waveHdr[i].lpData, MIX_BUFFER_SAMPLES);
                g_waveHdr[i].dwFlags &= ~WHDR_DONE;
                waveOutWrite(g_hWaveOut, &g_waveHdr[i], sizeof(WAVEHDR));
                rendered++;
            }
        }
        if (rendered > 2) {
            underruns++;
            if (underruns <= 20)
                LogMsg("MIXER: underrun! %d buffers refilled at once\n", rendered);
        }
        /* Periodic channel stats every ~5 seconds */
        tick++;
        if (tick % 125 == 0) { /* 125 * 40ms = 5sec */
            int playing = 0, stopped = 0, done = 0, ffree = 0;
            EnterCriticalSection(&g_mixCS);
            for (i = 0; i < MIX_CHANNELS; i++) {
                switch (g_mixChannels[i].status) {
                    case SMP_PLAYING: playing++; break;
                    case SMP_STOPPED: stopped++; break;
                    case SMP_DONE:    done++;    break;
                    default:          ffree++;   break;
                }
            }
            LeaveCriticalSection(&g_mixCS);
            LogMsg("MIXER: stats playing=%d stopped=%d done=%d free=%d underruns=%d\n",
                playing, stopped, done, ffree, underruns);
        }
    }
    return 0;
}

/* Forward-decls now placed above this line — removed old duplicate block. */

/* Initialize mixer */
static int Mix_Init(void) {
    WAVEFORMATEX wfx;
    int i;
    MMRESULT mr;

    if (g_mixReady) return 1;

    /* v19: if WASAPI was already brought up, it set g_mixRate.
     * Otherwise use the default (44100). Compute buffer size now. */
    g_mixBufSamples = g_mixRate * MIX_BUFFER_MS / 1000;

    /* Allocate per-buffer audio memory */
    for (i = 0; i < MIX_NUM_BUFFERS; i++) {
        g_mixBuf[i] = (short*)malloc(g_mixBufSamples * 2 * sizeof(short));
        if (!g_mixBuf[i]) {
            LogMsg("MIXER: malloc failed for buf[%d]\n", i);
            return 0;
        }
        memset(g_mixBuf[i], 0, g_mixBufSamples * 2 * sizeof(short));
    }

    /* g_mixCS may already be initialized by Audio_v19_Init (for WASAPI) */
    if (!g_mixCS_ready) { InitializeCriticalSection(&g_mixCS); g_mixCS_ready = 1; }
    memset(g_mixChannels, 0, sizeof(g_mixChannels));

    /* v19: if WASAPI is running, skip waveOut — WASAPI thread will pull from Mix_Render */
    if (g_wasapiReady) {
        g_mixReady = 1;
        LogMsg("MIXER: init OK via WASAPI (%dHz, %d channels, bufSamples=%d)\n",
            g_mixRate, MIX_CHANNELS, g_mixBufSamples);
        return 1;
    }

    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = 2;
    wfx.nSamplesPerSec = MIX_RATE;
    wfx.wBitsPerSample = 16;
    wfx.nBlockAlign = 4;
    wfx.nAvgBytesPerSec = MIX_RATE * 4;
    wfx.cbSize = 0;

    mr = waveOutOpen(&g_hWaveOut, WAVE_MAPPER, &wfx,
        (DWORD_PTR)Mix_WaveOutProc, 0, CALLBACK_FUNCTION);
    if (mr != MMSYSERR_NOERROR) {
        LogMsg("MIXER: waveOutOpen FAILED (%d)\n", mr);
        return 0;
    }

    for (i = 0; i < MIX_NUM_BUFFERS; i++) {
        memset(&g_waveHdr[i], 0, sizeof(WAVEHDR));
        g_waveHdr[i].lpData = (LPSTR)g_mixBuf[i];
        g_waveHdr[i].dwBufferLength = g_mixBufSamples * 4;
        waveOutPrepareHeader(g_hWaveOut, &g_waveHdr[i], sizeof(WAVEHDR));
        memset(g_mixBuf[i], 0, g_mixBufSamples * 4);
        waveOutWrite(g_hWaveOut, &g_waveHdr[i], sizeof(WAVEHDR));
    }

    /* Start mixer thread */
    g_mixEvent = CreateEventA(NULL, FALSE, FALSE, NULL);
    g_mixRunning = 1;
    g_mixThread = CreateThread(NULL, 0, Mix_ThreadProc, NULL, 0, NULL);
    if (g_mixThread) SetThreadPriority(g_mixThread, THREAD_PRIORITY_TIME_CRITICAL);

    g_mixReady = 1;
    LogMsg("MIXER: init OK (%dHz, %d buffers x %dms, %d channels)\n",
        MIX_RATE, MIX_NUM_BUFFERS, MIX_BUFFER_MS, MIX_CHANNELS);
    return 1;
}

/* Shutdown mixer */
static void Mix_Shutdown(void) {
    int i;
    if (!g_mixReady) return;
    g_mixReady = 0;
    /* Stop mixer thread */
    g_mixRunning = 0;
    if (g_mixEvent) SetEvent(g_mixEvent);
    if (g_mixThread) { WaitForSingleObject(g_mixThread, 1000); CloseHandle(g_mixThread); g_mixThread = NULL; }
    if (g_mixEvent) { CloseHandle(g_mixEvent); g_mixEvent = NULL; }
    if (g_hWaveOut) {
        waveOutReset(g_hWaveOut);
        for (i = 0; i < MIX_NUM_BUFFERS; i++)
            waveOutUnprepareHeader(g_hWaveOut, &g_waveHdr[i], sizeof(WAVEHDR));
        waveOutClose(g_hWaveOut);
        g_hWaveOut = NULL;
    }
    for (i = 0; i < MIX_CHANNELS; i++) {
        if (g_mixChannels[i].pcm_data) {
            free(g_mixChannels[i].pcm_data);
            g_mixChannels[i].pcm_data = NULL;
        }
    }
    /* v19: free dynamic mix buffers */
    for (i = 0; i < MIX_NUM_BUFFERS; i++) {
        if (g_mixBuf[i]) { free(g_mixBuf[i]); g_mixBuf[i] = NULL; }
    }
    DeleteCriticalSection(&g_mixCS);
    g_mixCS_ready = 0;
    LogMsg("MIXER: shutdown\n");
}

/* Load WAV data into a mixer channel. Handles PCM and IMA ADPCM. */
static int Mix_LoadSample(void *game_handle, const void *data, int data_size_hint) {
    const unsigned char *p = (const unsigned char *)data;
    MixChannel *ch;
    unsigned short fmt_tag, num_ch, bits;
    unsigned int sample_rate, block_align;
    const unsigned char *data_ptr = NULL;
    int data_chunk_size = 0;

    if (!data || !g_mixReady) {
        LogMsg("MIXER: load FAIL - null data or not ready (%p, %d)\n", data, g_mixReady);
        return 0;
    }

    /* Cache: if same handle + same data pointer, skip entirely (don't restart) */
    {
        MixChannel *existing = Mix_FindByHandle(game_handle);
        if (existing && existing->last_data_ptr == data && existing->pcm_data) {
            return 1; /* same data already loaded, keep playing */
        }
    }
    if (data_size_hint < 44) {
        LogMsg("MIXER: load FAIL - too small (%d bytes)\n", data_size_hint);
        return 0;
    }
    /* Validate RIFF/WAVE header */
    if (p[0]!='R'||p[1]!='I'||p[2]!='F'||p[3]!='F') {
        LogMsg("MIXER: load FAIL - not RIFF (0x%02X%02X%02X%02X)\n", p[0],p[1],p[2],p[3]);
        return 0;
    }
    if (p[8]!='W'||p[9]!='A'||p[10]!='V'||p[11]!='E') {
        LogMsg("MIXER: load FAIL - not WAVE\n");
        return 0;
    }
    if (p[12]!='f'||p[13]!='m'||p[14]!='t'||p[15]!=' ') {
        LogMsg("MIXER: load FAIL - no fmt chunk at offset 12\n");
        return 0;
    }

    fmt_tag = *(unsigned short*)(p + 20);
    num_ch = *(unsigned short*)(p + 22);
    sample_rate = *(unsigned int*)(p + 24);
    block_align = *(unsigned short*)(p + 32);
    bits = *(unsigned short*)(p + 34);

    /* Find data chunk */
    {
        int fmt_size = *(int*)(p + 16);
        int offset = 20 + fmt_size;  /* skip fmt chunk data */
        while (offset + 8 <= data_size_hint) {
            if (p[offset]=='d' && p[offset+1]=='a' && p[offset+2]=='t' && p[offset+3]=='a') {
                data_chunk_size = *(int*)(p + offset + 4);
                data_ptr = p + offset + 8;
                break;
            }
            {
                int chunk_size = *(int*)(p + offset + 4);
                if (chunk_size < 0) break; /* corrupt */
                offset += 8 + chunk_size;
                if (offset & 1) offset++; /* pad */
            }
        }
        if (!data_ptr) {
            LogMsg("MIXER: load FAIL - data chunk not found (fmt_size=%d, file=%d)\n",
                fmt_size, data_size_hint);
            return 0;
        }
    }

    /* Helper: assign decoded PCM to a channel (existing or new).
     * MUST be called with g_mixCS held.  Sets up crossfade in the same
     * critical section to avoid stale-pointer races. */
    #define MIX_ASSIGN_CHANNEL(game_handle, decoded, total_samples, \
                               sample_rate, num_ch, data) do { \
        ch = Mix_FindByHandle(game_handle); \
        if (ch) { \
            /* Existing channel — preserve status/vol/pan/rate */ \
            if (ch->pcm_data) free(ch->pcm_data); \
            ch->pcm_data = (decoded); \
            ch->pcm_samples = (total_samples); \
            ch->src_pos = 0.0; \
            ch->loops_done = 0; \
            ch->sample_rate = (sample_rate); \
            ch->pcm_channels = (num_ch); \
            ch->last_data_ptr = (data); \
            if (ch->playback_rate <= 0) ch->playback_rate = (sample_rate); \
        } else { \
            ch = Mix_Alloc(game_handle); \
            if (ch->pcm_data) free(ch->pcm_data); \
            ch->game_handle = (game_handle); \
            ch->pcm_data = (decoded); \
            ch->pcm_samples = (total_samples); \
            ch->status = SMP_STOPPED; \
            ch->src_pos = 0.0; \
            ch->volume = 127; \
            ch->pan = 64; \
            ch->loop_count = 1; \
            ch->loops_done = 0; \
            ch->sample_rate = (sample_rate); \
            ch->playback_rate = (sample_rate); \
            ch->pcm_channels = (num_ch); \
            ch->last_data_ptr = (data); \
            ch->data_hash = 0; \
            ch->vol_cur = ch->volume; \
            ch->xfade_samples = 0; \
            { \
                PendingState *ps = Pending_Get(game_handle); \
                if (ps) { \
                    if (ps->flags & PENDING_HAS_VOL)  ch->volume = ps->volume; \
                    if (ps->flags & PENDING_HAS_PAN)  ch->pan = ps->pan; \
                    if (ps->flags & PENDING_HAS_RATE) ch->playback_rate = ps->playback_rate; \
                    if (ps->flags & PENDING_HAS_LOOP) ch->loop_count = ps->loop_count; \
                    LogMsg("MIXER: applied pending state: vol=%d pan=%d rate=%d loop=%d\n", \
                        ch->volume, ch->pan, ch->playback_rate, ch->loop_count); \
                    ch->vol_cur = ch->volume; \
                    Pending_Clear(game_handle); \
                } \
            } \
        } \
        /* Configure crossfade while still holding g_mixCS */ \
        Mix_SetupCrossfade(ch); \
    } while(0)

    /* Decode OUTSIDE critical section to avoid blocking mixer thread */
    if (fmt_tag == 0x0011) {
        /* IMA ADPCM */
        int total_samples = 0;
        short *decoded = ADPCM_Decode(data_ptr, data_chunk_size,
            block_align, num_ch, &total_samples);
        if (!decoded) {
            LogMsg("MIXER: ADPCM decode failed for %p\n", game_handle);
            return 0;
        }

        EnterCriticalSection(&g_mixCS);
        MIX_ASSIGN_CHANNEL(game_handle, decoded, total_samples,
                           sample_rate, num_ch, data);
        LeaveCriticalSection(&g_mixCS);

        LogMsg("MIXER: loaded ADPCM %p: %dHz/%dch, %d samples\n",
            game_handle, sample_rate, num_ch, total_samples);
    } else if (fmt_tag == 0x0001) {
        /* PCM — decode outside critical section */
        int total_samples = data_chunk_size / (bits/8) / num_ch;
        short *decoded = (short*)malloc(total_samples * num_ch * sizeof(short));
        if (!decoded) {
            LogMsg("MIXER: PCM malloc failed for %p (%d bytes)\n",
                game_handle, total_samples * num_ch * (int)sizeof(short));
            return 0;
        }
        if (bits == 16) {
            memcpy(decoded, data_ptr, total_samples * num_ch * 2);
        } else if (bits == 8) {
            int j;
            const unsigned char *src8 = data_ptr;
            for (j = 0; j < total_samples * num_ch; j++)
                decoded[j] = (short)((src8[j] - 128) << 8);
        }

        EnterCriticalSection(&g_mixCS);
        MIX_ASSIGN_CHANNEL(game_handle, decoded, total_samples,
                           sample_rate, num_ch, data);
        LeaveCriticalSection(&g_mixCS);

        LogMsg("MIXER: loaded PCM %p: %dHz/%dbit/%dch, %d samples\n",
            game_handle, sample_rate, bits, num_ch, total_samples);
    } else {
        LogMsg("MIXER: unsupported format 0x%04X for %p\n", fmt_tag, game_handle);
        return 0;
    }

    #undef MIX_ASSIGN_CHANNEL
    return ch ? (ch->pcm_data ? 1 : 0) : 0;
}

/* Mixer control functions */
static void Mix_StartSample(void *handle) {
    MixChannel *ch;
    int active = 0, i;
    EnterCriticalSection(&g_mixCS);
    ch = Mix_FindByHandle(handle);
    if (ch && ch->pcm_data) {
        ch->src_pos = 0.0;
        ch->loops_done = 0;
        ch->status = SMP_PLAYING;
        /* Ambient fade-in: infinite-loop samples (loops=0) start from silence.
         * The per-sample volume ramp in Mix_Render will smoothly bring it up
         * to ch->volume over ~2-3ms (volume / sample_rate). Prevents abrupt
         * ambient pop after room transitions. */
        if (ch->loop_count == 0)
            ch->vol_cur = 0;
    }
    for (i = 0; i < MIX_CHANNELS; i++)
        if (g_mixChannels[i].status == SMP_PLAYING) active++;
    LeaveCriticalSection(&g_mixCS);
    {
        static int logCount = 0;
        if (logCount < 200) {
            LogMsg("MIXER: ch_start(%p) active=%d loop=%d\n", handle, active,
                ch ? ch->loop_count : -1);
            logCount++;
        }
    }
}

static void Mix_StopSample(void *handle) {
    MixChannel *ch;
    EnterCriticalSection(&g_mixCS);
    ch = Mix_FindByHandle(handle);
    if (ch) ch->status = SMP_STOPPED;
    LeaveCriticalSection(&g_mixCS);
}

static void Mix_EndSample(void *handle) {
    MixChannel *ch;
    EnterCriticalSection(&g_mixCS);
    ch = Mix_FindByHandle(handle);
    if (ch) {
        ch->status = SMP_FREE;
        ch->game_handle = NULL; /* release channel for reuse */
    }
    LeaveCriticalSection(&g_mixCS);
}

static void Mix_SetVolume(void *handle, int vol) {
    MixChannel *ch;
    EnterCriticalSection(&g_mixCS);
    ch = Mix_FindByHandle(handle);
    if (ch) ch->volume = vol;
    LeaveCriticalSection(&g_mixCS);
}

static int Mix_GetVolume(void *handle) {
    int ret;
    EnterCriticalSection(&g_mixCS);
    { MixChannel *ch = Mix_FindByHandle(handle); ret = ch ? ch->volume : 0; }
    LeaveCriticalSection(&g_mixCS);
    return ret;
}

static int Mix_GetStatus(void *handle) {
    int ret;
    EnterCriticalSection(&g_mixCS);
    { MixChannel *ch = Mix_FindByHandle(handle); ret = ch ? ch->status : SMP_FREE; }
    LeaveCriticalSection(&g_mixCS);
    return ret;
}

static int Mix_GetPosition(void *handle) {
    int ret;
    EnterCriticalSection(&g_mixCS);
    { MixChannel *ch = Mix_FindByHandle(handle);
      ret = ch ? (int)(ch->src_pos) * ch->pcm_channels * 2 : 0; }
    LeaveCriticalSection(&g_mixCS);
    return ret;
}

static void Mix_SetPosition(void *handle, int byte_pos) {
    MixChannel *ch;
    EnterCriticalSection(&g_mixCS);
    ch = Mix_FindByHandle(handle);
    if (ch && ch->pcm_channels > 0) {
        ch->src_pos = (double)(byte_pos / (ch->pcm_channels * 2));
        if ((int)ch->src_pos >= ch->pcm_samples) ch->src_pos = 0.0;
    }
    LeaveCriticalSection(&g_mixCS);
}

static void Mix_SetLoopCount(void *handle, int count) {
    MixChannel *ch;
    EnterCriticalSection(&g_mixCS);
    ch = Mix_FindByHandle(handle);
    if (ch) { ch->loop_count = count; ch->loops_done = 0; }
    LeaveCriticalSection(&g_mixCS);
}

static int Mix_GetLoopCount(void *handle) {
    int ret;
    EnterCriticalSection(&g_mixCS);
    { MixChannel *ch = Mix_FindByHandle(handle); ret = ch ? ch->loop_count : 1; }
    LeaveCriticalSection(&g_mixCS);
    return ret;
}

static void Mix_ResumeSample(void *handle) {
    MixChannel *ch;
    EnterCriticalSection(&g_mixCS);
    ch = Mix_FindByHandle(handle);
    if (ch && ch->status == SMP_STOPPED && ch->pcm_data)
        ch->status = SMP_PLAYING;
    LeaveCriticalSection(&g_mixCS);
}

static void Mix_SetPlaybackRate(void *handle, int rate) {
    MixChannel *ch;
    EnterCriticalSection(&g_mixCS);
    ch = Mix_FindByHandle(handle);
    if (ch && rate > 0) ch->playback_rate = rate;
    LeaveCriticalSection(&g_mixCS);
}

static int Mix_GetPlaybackRate(void *handle) {
    int ret;
    EnterCriticalSection(&g_mixCS);
    { MixChannel *ch = Mix_FindByHandle(handle); ret = ch ? ch->playback_rate : 0; }
    LeaveCriticalSection(&g_mixCS);
    return ret;
}

static void Mix_SetPan(void *handle, int pan) {
    MixChannel *ch;
    EnterCriticalSection(&g_mixCS);
    ch = Mix_FindByHandle(handle);
    if (ch) {
        if (pan < 0) pan = 0;
        if (pan > 127) pan = 127;
        ch->pan = pan;
    }
    LeaveCriticalSection(&g_mixCS);
}

static int Mix_GetPan(void *handle) {
    int ret;
    EnterCriticalSection(&g_mixCS);
    { MixChannel *ch = Mix_FindByHandle(handle); ret = ch ? ch->pan : 64; }
    LeaveCriticalSection(&g_mixCS);
    return ret;
}

/* MSS preference constants */
#define DIG_MIXER_CHANNELS    1
#define DIG_DEFAULT_VOLUME    4
#define DIG_RESAMPLING_TOLERANCE 5
#define DIG_DS_DSBCAPS_CTRL3D   29
#define DIG_DS_MIX_FRAGMENT_CNT 30

/* ---- MSS function pointer types ---- */
typedef int   (__stdcall *PFN_AIL_startup)(void);
typedef void  (__stdcall *PFN_AIL_shutdown)(void);
typedef int   (__stdcall *PFN_AIL_set_preference)(int pref, int value);
typedef void* (__stdcall *PFN_AIL_waveOutOpen)(void **drv, void *phwo, int wDevID, void *lpFmt);
typedef void  (__stdcall *PFN_AIL_set_sample_volume)(void *s, int vol);
typedef int   (__stdcall *PFN_AIL_sample_volume)(void *s);
typedef void  (__stdcall *PFN_AIL_start_sample)(void *s);
typedef void  (__stdcall *PFN_AIL_end_sample)(void *s);
typedef void  (__stdcall *PFN_AIL_stop_sample)(void *s);
typedef void  (__stdcall *PFN_AIL_resume_sample)(void *s);
typedef int   (__stdcall *PFN_AIL_sample_status)(void *s);
typedef int   (__stdcall *PFN_AIL_sample_position)(void *s);
typedef void  (__stdcall *PFN_AIL_set_sample_position)(void *s, int pos);
typedef void  (__stdcall *PFN_AIL_set_sample_loop_count)(void *s, int count);
typedef int   (__stdcall *PFN_AIL_sample_loop_count)(void *s);
typedef int   (__stdcall *PFN_AIL_set_named_sample_file)(void *s, const char *name,
                                     void *data, int size, int flags);
typedef int   (__stdcall *PFN_AIL_set_sample_file)(void *s, void *data, int flags);
typedef void  (__stdcall *PFN_AIL_set_stream_volume)(void *str, int vol);
typedef int   (__stdcall *PFN_AIL_stream_volume)(void *str);
typedef void  (__stdcall *PFN_AIL_start_stream)(void *str);
typedef void  (__stdcall *PFN_AIL_set_stream_loop_count)(void *str, int count);
typedef void  (__stdcall *PFN_AIL_pause_stream)(void *str, int onoff);
typedef int   (__stdcall *PFN_AIL_stream_status)(void *str);
typedef void  (__stdcall *PFN_AIL_set_digital_master_volume)(void *drv, int vol);
typedef int   (__stdcall *PFN_AIL_digital_master_volume)(void *drv);
typedef void  (__stdcall *PFN_AIL_set_sample_playback_rate)(void *s, int rate);
typedef int   (__stdcall *PFN_AIL_sample_playback_rate)(void *s);
typedef void  (__stdcall *PFN_AIL_set_sample_pan)(void *s, int pan);
typedef int   (__stdcall *PFN_AIL_sample_pan)(void *s);
typedef void  (__stdcall *PFN_AIL_init_sample)(void *s);

/* ---- Real function pointers (trampolines) ---- */
static PFN_AIL_startup              real_AIL_startup = NULL;
static PFN_AIL_set_preference       real_AIL_set_preference = NULL;
static PFN_AIL_waveOutOpen          real_AIL_waveOutOpen = NULL;
static PFN_AIL_set_sample_volume    real_AIL_set_sample_volume = NULL;
static PFN_AIL_sample_volume        real_AIL_sample_volume = NULL;
static PFN_AIL_start_sample         real_AIL_start_sample = NULL;
static PFN_AIL_end_sample           real_AIL_end_sample = NULL;
static PFN_AIL_stop_sample          real_AIL_stop_sample = NULL;
static PFN_AIL_resume_sample        real_AIL_resume_sample = NULL;
static PFN_AIL_sample_status        real_AIL_sample_status = NULL;
static PFN_AIL_sample_position      real_AIL_sample_position = NULL;
static PFN_AIL_set_sample_position  real_AIL_set_sample_position = NULL;
static PFN_AIL_set_sample_loop_count real_AIL_set_sample_loop_count = NULL;
static PFN_AIL_sample_loop_count    real_AIL_sample_loop_count = NULL;
static PFN_AIL_set_named_sample_file real_AIL_set_named_sample_file = NULL;
static PFN_AIL_set_sample_file      real_AIL_set_sample_file = NULL;
static PFN_AIL_set_stream_volume    real_AIL_set_stream_volume = NULL;
static PFN_AIL_stream_volume        real_AIL_stream_volume = NULL;
static PFN_AIL_start_stream         real_AIL_start_stream = NULL;
static PFN_AIL_set_stream_loop_count real_AIL_set_stream_loop_count = NULL;
static PFN_AIL_set_sample_playback_rate real_AIL_set_sample_playback_rate = NULL;
static PFN_AIL_sample_playback_rate real_AIL_sample_playback_rate = NULL;
static PFN_AIL_set_sample_pan       real_AIL_set_sample_pan = NULL;
static PFN_AIL_sample_pan           real_AIL_sample_pan = NULL;
static PFN_AIL_init_sample          real_AIL_init_sample = NULL;
static PFN_AIL_pause_stream         real_AIL_pause_stream = NULL;
static PFN_AIL_stream_status        real_AIL_stream_status = NULL;
static PFN_AIL_set_digital_master_volume real_AIL_set_digital_master_volume = NULL;
static PFN_AIL_digital_master_volume    real_AIL_digital_master_volume = NULL;

/* Detour hooks for audio functions */
static DetourHook g_detourAIL_startup = {0};
static DetourHook g_detourAIL_set_preference = {0};
static DetourHook g_detourAIL_waveOutOpen = {0};
static DetourHook g_detourAIL_set_sample_volume = {0};
static DetourHook g_detourAIL_start_sample = {0};
static DetourHook g_detourAIL_end_sample = {0};
static DetourHook g_detourAIL_stop_sample = {0};
static DetourHook g_detourAIL_set_named_sample_file = {0};
static DetourHook g_detourAIL_set_sample_file = {0};
static DetourHook g_detourAIL_set_stream_volume = {0};
static DetourHook g_detourAIL_start_stream = {0};
static DetourHook g_detourAIL_pause_stream = {0};
static DetourHook g_detourAIL_set_digital_master_volume = {0};
static DetourHook g_detourAIL_resume_sample = {0};
static DetourHook g_detourAIL_set_sample_loop_count = {0};
static DetourHook g_detourAIL_set_stream_loop_count = {0};
/* Query hooks (for custom mixer) */
static DetourHook g_detourAIL_sample_status = {0};
static DetourHook g_detourAIL_sample_volume = {0};
static DetourHook g_detourAIL_sample_position = {0};
static DetourHook g_detourAIL_set_sample_position = {0};
static DetourHook g_detourAIL_sample_loop_count = {0};
static DetourHook g_detourAIL_set_sample_playback_rate = {0};
static DetourHook g_detourAIL_sample_playback_rate = {0};
static DetourHook g_detourAIL_set_sample_pan = {0};
static DetourHook g_detourAIL_sample_pan = {0};
static DetourHook g_detourAIL_init_sample = {0};

/* ---- Volume Fade System ---- */
#define AUDIO_MAX_FADES 48

/* Pending actions after fade completes */
#define FADE_ACTION_NONE  0
#define FADE_ACTION_PAUSE 1  /* pause stream after fade-out */

typedef struct {
    void  *handle;      /* sample or stream handle */
    int    is_stream;   /* 0=sample, 1=stream, 2=master */
    void  *drv;         /* driver handle (for master only) */
    int    from_vol;    /* volume at fade start */
    int    to_vol;      /* target volume */
    DWORD  start_tick;  /* GetTickCount() at fade start */
    int    duration_ms; /* fade duration in ms */
    int    pending_action; /* action to perform after fade completes */
    int    active;
} AudioFade;

static AudioFade g_fades[AUDIO_MAX_FADES];
static volatile int  g_audioReady = 0;

static AudioFade* Fade_Find(void *handle) {
    for (int i = 0; i < AUDIO_MAX_FADES; i++)
        if (g_fades[i].active && g_fades[i].handle == handle)
            return &g_fades[i];
    return NULL;
}

static AudioFade* Fade_Alloc(void) {
    /* Find free slot */
    for (int i = 0; i < AUDIO_MAX_FADES; i++)
        if (!g_fades[i].active) return &g_fades[i];
    /* Evict oldest */
    DWORD oldest = 0xFFFFFFFF; int idx = 0;
    for (int i = 0; i < AUDIO_MAX_FADES; i++)
        if (g_fades[i].start_tick < oldest) { oldest = g_fades[i].start_tick; idx = i; }
    g_fades[idx].active = 0;
    return &g_fades[idx];
}

static void Fade_Start(void *handle, int is_stream, void *drv,
                        int from_vol, int to_vol, int duration_ms) {
    if (duration_ms <= 0 || from_vol == to_vol) {
        /* Instant: apply directly */
        if (is_stream == 0 && real_AIL_set_sample_volume)
            real_AIL_set_sample_volume(handle, to_vol);
        else if (is_stream == 1 && real_AIL_set_stream_volume)
            real_AIL_set_stream_volume(handle, to_vol);
        else if (is_stream == 2 && real_AIL_set_digital_master_volume)
            real_AIL_set_digital_master_volume(drv, to_vol);
        return;
    }
    AudioFade *f = Fade_Find(handle);
    if (!f) f = Fade_Alloc();
    f->handle = handle;
    f->is_stream = is_stream;
    f->drv = drv;
    f->from_vol = from_vol;
    f->to_vol = to_vol;
    f->start_tick = GetTickCount();
    f->duration_ms = duration_ms;
    f->pending_action = FADE_ACTION_NONE;
    f->active = 1;
}

/* Fade with a pending action on completion (e.g. pause after fade-out) */
static void Fade_StartWithAction(void *handle, int is_stream, void *drv,
                                  int from_vol, int to_vol, int duration_ms,
                                  int action) {
    Fade_Start(handle, is_stream, drv, from_vol, to_vol, duration_ms);
    if (duration_ms > 0 && from_vol != to_vol) {
        AudioFade *f = Fade_Find(handle);
        if (f) f->pending_action = action;
    }
}

/* Called per frame from my_SwapBuffers — rate limited to ~30Hz */
static void Audio_UpdateFades(void) {
    if (!g_audioReady) return;
    static DWORD lastUpdate = 0;
    DWORD now = GetTickCount();
    if (now - lastUpdate < 33) return; /* ~30Hz max update rate */
    lastUpdate = now;
    for (int i = 0; i < AUDIO_MAX_FADES; i++) {
        AudioFade *f = &g_fades[i];
        if (!f->active) continue;
        DWORD elapsed = now - f->start_tick;
        if ((int)elapsed >= f->duration_ms) {
            /* Fade complete — set final volume */
            if (f->is_stream == 0 && real_AIL_set_sample_volume)
                real_AIL_set_sample_volume(f->handle, f->to_vol);
            else if (f->is_stream == 1 && real_AIL_set_stream_volume)
                real_AIL_set_stream_volume(f->handle, f->to_vol);
            else if (f->is_stream == 2 && real_AIL_set_digital_master_volume)
                real_AIL_set_digital_master_volume(f->drv, f->to_vol);
            /* Execute pending action */
            if (f->pending_action == FADE_ACTION_PAUSE && f->is_stream == 1) {
                if (real_AIL_pause_stream) {
                    real_AIL_pause_stream(f->handle, 1);
                    LogMsg("AUDIO: fade->pause stream %p complete\n", f->handle);
                }
            }
            f->active = 0;
        } else {
            /* Interpolate with exponential curve (perceptually linear) */
            float t = (float)elapsed / (float)f->duration_ms;
            float curve;
            int vol;
            if (f->to_vol > f->from_vol)
                curve = t * t;           /* fade-in: slow start, fast end */
            else
                curve = 1.0f - (1.0f - t) * (1.0f - t); /* fade-out: fast start, slow end */
            vol = f->from_vol + (int)((f->to_vol - f->from_vol) * curve);
            if (f->is_stream == 0 && real_AIL_set_sample_volume)
                real_AIL_set_sample_volume(f->handle, vol);
            else if (f->is_stream == 1 && real_AIL_set_stream_volume)
                real_AIL_set_stream_volume(f->handle, vol);
            else if (f->is_stream == 2 && real_AIL_set_digital_master_volume)
                real_AIL_set_digital_master_volume(f->drv, vol);
        }
    }

}

static void Fade_Cancel(void *handle) {
    AudioFade *f = Fade_Find(handle);
    if (f) f->active = 0;
}

/* ---- Stream target volume memory ---- */
#define STREAM_VOL_MAX 16
typedef struct { void *stream; int target_vol; } StreamVolMem;
static StreamVolMem g_streamVols[STREAM_VOL_MAX];

static void StreamVol_Set(void *str, int vol) {
    int i;
    for (i = 0; i < STREAM_VOL_MAX; i++)
        if (g_streamVols[i].stream == str) { g_streamVols[i].target_vol = vol; return; }
    for (i = 0; i < STREAM_VOL_MAX; i++)
        if (!g_streamVols[i].stream) { g_streamVols[i].stream = str; g_streamVols[i].target_vol = vol; return; }
    g_streamVols[0].stream = str; g_streamVols[0].target_vol = vol;
}

static int StreamVol_Get(void *str, int fallback) {
    int i;
    for (i = 0; i < STREAM_VOL_MAX; i++)
        if (g_streamVols[i].stream == str) return g_streamVols[i].target_vol;
    return fallback;
}

/* ---- Loop Preservation System ---- */
#define AUDIO_MAX_LOOPS 48
#define LOOP_RESUME_WINDOW_MS 3000  /* resume if restarted within 3 seconds */
#define AUDIO_HASH_BYTES 128       /* v20: 128 bytes for unique identity across Sound2/ */

/* FNV-1a hash of the first N bytes of sample data.
 * WAV header contains format, sample rate, data size — enough for unique ID.
 * Returns 0 on failure (NULL data). */
static unsigned int AudioDataHash(const void *data, int nbytes) {
    if (!data || nbytes <= 0) return 0;
    const unsigned char *p = (const unsigned char *)data;
    unsigned int h = 2166136261u;
    for (int i = 0; i < nbytes; i++)
        h = (h ^ p[i]) * 16777619u;
    return h ? h : 1; /* never return 0 (used as "no hash" sentinel) */
}

typedef struct {
    void        *sample;      /* sample handle */
    const void  *file_data;   /* pointer to sample file data */
    unsigned int data_hash;   /* FNV-1a hash of first AUDIO_HASH_BYTES */
    int          position;    /* saved playback position */
    int          loop_count;  /* saved loop count */
    DWORD        stop_tick;   /* when it was stopped */
    int          active;
} AudioLoopState;

static AudioLoopState g_loops[AUDIO_MAX_LOOPS];

static AudioLoopState* Loop_FindBySample(void *sample) {
    for (int i = 0; i < AUDIO_MAX_LOOPS; i++)
        if (g_loops[i].active && g_loops[i].sample == sample)
            return &g_loops[i];
    return NULL;
}

/* Match by sample handle AND content hash.
 * Content hash survives buffer reallocation across screen transitions.
 * Falls back to pointer match if hash is unavailable (0). */
static AudioLoopState* Loop_FindForSample(void *sample, unsigned int hash,
                                           const void *data) {
    if (!sample) return NULL;
    DWORD now = GetTickCount();
    for (int i = 0; i < AUDIO_MAX_LOOPS; i++) {
        if (!g_loops[i].active) continue;
        if (g_loops[i].sample != sample) continue;
        if (g_loops[i].position <= 4096) continue;
        if ((now - g_loops[i].stop_tick) >= LOOP_RESUME_WINDOW_MS) continue;
        /* Match by content hash (primary) or pointer (fallback) */
        if (hash && g_loops[i].data_hash == hash)
            return &g_loops[i];
        if (data && g_loops[i].file_data == data)
            return &g_loops[i];
    }
    return NULL;
}

static AudioLoopState* Loop_Alloc(void) {
    for (int i = 0; i < AUDIO_MAX_LOOPS; i++)
        if (!g_loops[i].active) return &g_loops[i];
    /* Evict oldest */
    DWORD oldest = 0xFFFFFFFF; int idx = 0;
    for (int i = 0; i < AUDIO_MAX_LOOPS; i++)
        if (g_loops[i].stop_tick < oldest) { oldest = g_loops[i].stop_tick; idx = i; }
    g_loops[idx].active = 0;
    return &g_loops[idx];
}

/* ---- Sample-to-file tracking (maps sample handle → current file data + hash) ---- */
#define AUDIO_MAX_SAMPLES 128
typedef struct {
    void        *sample;
    const void  *file_data;
    unsigned int data_hash;    /* FNV-1a of first AUDIO_HASH_BYTES */
    int          data_size;    /* total file data size in bytes */
    char         file_name[64];
} SampleFileMap;
static SampleFileMap g_sampleFiles[AUDIO_MAX_SAMPLES];

static SampleFileMap* SFMap_Find(void *sample) {
    for (int i = 0; i < AUDIO_MAX_SAMPLES; i++)
        if (g_sampleFiles[i].sample == sample) return &g_sampleFiles[i];
    return NULL;
}

static SampleFileMap* SFMap_Set(void *sample, const void *data, int data_size, const char *name) {
    SampleFileMap *m = SFMap_Find(sample);
    if (!m) {
        /* Find empty */
        for (int i = 0; i < AUDIO_MAX_SAMPLES; i++)
            if (!g_sampleFiles[i].sample) { m = &g_sampleFiles[i]; break; }
    }
    if (!m) {
        /* Evict an inactive sample slot (FREE/DONE preferred) */
        int i;
        for (i = 0; i < AUDIO_MAX_SAMPLES; i++) {
            if (g_sampleFiles[i].sample) {
                int st = real_AIL_sample_status ? real_AIL_sample_status(g_sampleFiles[i].sample) : -1;
                if (st == SMP_FREE || st == SMP_DONE) { m = &g_sampleFiles[i]; break; }
            }
        }
        if (!m) m = &g_sampleFiles[0]; /* absolute fallback */
    }
    m->sample = sample;
    m->file_data = data;
    m->data_hash = AudioDataHash(data, AUDIO_HASH_BYTES);
    m->data_size = data_size;
    m->file_name[0] = 0;
    if (name) {
        strncpy(m->file_name, name, 63);
        m->file_name[63] = 0;
    }
    return m;
}

/* ---- Hook implementations ---- */

/* Parse WAV/RIFF data size from header. Returns total file size estimate or 0. */
static int WAV_GetDataSize(const void *data) {
    if (!data) return 0;
    const unsigned char *p = (const unsigned char *)data;
    /* Check RIFF header: "RIFF" at offset 0, file size at offset 4 */
    if (p[0]=='R' && p[1]=='I' && p[2]=='F' && p[3]=='F') {
        unsigned int riff_size = *(const unsigned int *)(p + 4);
        return (int)(riff_size + 8); /* RIFF chunk size + 8 byte header */
    }
    return 0;
}

/* Parse WAV format: returns "RATE/BITS/CH" string for logging */
static __declspec(thread) char g_wavFmtBuf[32];
static const char* WAV_GetFormatStr(const void *data) {
    if (!data) return "?";
    const unsigned char *p = (const unsigned char *)data;
    if (p[0]=='R' && p[1]=='I' && p[2]=='F' && p[3]=='F' &&
        p[8]=='W' && p[9]=='A' && p[10]=='V' && p[11]=='E') {
        /* fmt chunk at offset 12: "fmt " + size(4) + format data */
        if (p[12]=='f' && p[13]=='m' && p[14]=='t' && p[15]==' ') {
            unsigned int rate = *(const unsigned int *)(p + 24);
            unsigned short bits = *(const unsigned short *)(p + 34);
            unsigned short ch = *(const unsigned short *)(p + 22);
            snprintf(g_wavFmtBuf, sizeof(g_wavFmtBuf), "%uHz/%ubit/%uch", rate, bits, ch);
            return g_wavFmtBuf;
        }
    }
    return "?";
}

/* v20: binary search in the auto-generated hash→name table */
static const char* HashMap_Lookup(unsigned int hash) {
    int lo = 0, hi = AITD_HASH_MAP_COUNT - 1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        unsigned int h = g_hashNameTable[mid].hash;
        if (h == hash) return g_hashNameTable[mid].name;
        if (h < hash) lo = mid + 1;
        else          hi = mid - 1;
    }
    return NULL;
}

/* Helper: get sample name for logging. Returns "(unknown)" if not tracked.
 * v20: falls back to hash-to-name map (Sound2/ names auto-identified). */
static const char* SFMap_Name(void *sample) {
    SampleFileMap *m = SFMap_Find(sample);
    if (m && m->file_name[0]) return m->file_name;
    if (m && m->data_hash) {
        const char *mapped = HashMap_Lookup(m->data_hash);
        if (mapped) return mapped;
    }
    return "(unknown)";
}

/* ==================================================================
 *  AUDIO CATEGORY SYSTEM
 *  Classifies samples by hash-map name prefix into categories,
 *  each with an independent volume multiplier (0-200%).
 *  Fixes the game's broken volume design where environmental
 *  ambients (rain, wind) share the music slider.
 * ================================================================== */
#define CAT_UNKNOWN  0  /* no multiplier applied */
#define CAT_AMBIENT  1  /* 6xxx: wind, rain, water, machinery */
#define CAT_MUSIC    2  /* Drums/Bass/Voice/Noise/Synth/Organ/Percu/Strng stems */
#define CAT_SFX      3  /* 0xxx/2xxx/4xxx/bip/Coeur: footsteps, impacts, UI */

static int AudioCategory(const char *name) {
    if (!name || name[0] == '(' /* "(unknown)" */) return CAT_UNKNOWN;
    /* 6xxx = environmental ambient */
    if (name[0] == '6' && name[1] >= '0' && name[1] <= '9') return CAT_AMBIENT;
    /* Adaptive music stems */
    if (_strnicmp(name, "Drums", 5) == 0) return CAT_MUSIC;
    if (_strnicmp(name, "Bass",  4) == 0) return CAT_MUSIC;
    if (_strnicmp(name, "Voice", 5) == 0) return CAT_MUSIC;
    if (_strnicmp(name, "Noise", 5) == 0) return CAT_MUSIC;
    if (_strnicmp(name, "Synth", 5) == 0) return CAT_MUSIC;
    if (_strnicmp(name, "Organ", 5) == 0) return CAT_MUSIC;
    if (_strnicmp(name, "Percu", 5) == 0) return CAT_MUSIC;
    if (_strnicmp(name, "Strng", 5) == 0) return CAT_MUSIC;
    if (_strnicmp(name, "Indus", 5) == 0) return CAT_MUSIC;
    /* SFX: 0xxx, 2xxx, 4xxx, named sounds */
    if (name[0] >= '0' && name[0] <= '5') return CAT_SFX;
    if (_stricmp(name, "bip") == 0) return CAT_SFX;
    if (_strnicmp(name, "Coeur", 5) == 0) return CAT_SFX;
    if (_strnicmp(name, "aitd",  4) == 0) return CAT_AMBIENT;
    if (_strnicmp(name, "Fondeuse", 8) == 0) return CAT_SFX;
    if (_strnicmp(name, "Avion", 5) == 0) return CAT_SFX;
    if (_strnicmp(name, "Dictaphn", 8) == 0) return CAT_SFX;
    return CAT_UNKNOWN;
}

/* Forward decl: music replacement mutes stems */
static volatile int g_musicReplActive;

/* Apply category volume multiplier. Returns adjusted volume (clamped 0-127). */
static int AudioCategoryAdjust(void *sample, int vol) {
    int cat, pct;
    const char *name;
    /* Clamp first — fixes game's vol=-1 integer underflow bug */
    if (vol < 0) vol = 0;
    if (vol > 127) vol = 127;
    name = SFMap_Name(sample);
    cat = AudioCategory(name);
    /* When music replacement is active, mute music stems.
     * NOTE: stems are muted via PlaySequence(vol=0), not here,
     * because all game samples bypass SFMap and appear as CAT_UNKNOWN. */
    /* Check if any multiplier is non-default */
    if (g_config.vol_ambient == 100 && g_config.vol_music == 100 &&
        g_config.vol_sfx == 100)
        return vol;
    switch (cat) {
        case CAT_AMBIENT: pct = g_config.vol_ambient; break;
        case CAT_MUSIC:   pct = g_config.vol_music;   break;
        case CAT_SFX:     pct = g_config.vol_sfx;     break;
        default:          return vol; /* unknown — don't touch */
    }
    vol = vol * pct / 100;
    if (vol > 127) vol = 127;
    if (vol < 0) vol = 0;
    return vol;
}

/* Helper: status code to string (includes numeric value for unknowns) */
static __declspec(thread) char g_statusBuf[16];
static const char* StatusStr(int status) {
    switch(status) {
        case SMP_FREE:    return "FREE(0)";
        case SMP_DONE:    return "DONE(1)";
        case SMP_PLAYING: return "PLAY(2)";
        case SMP_STOPPED: return "STOP(3)";
        case SMP_STARTED: return "STRT(4)";
        default:
            snprintf(g_statusBuf, sizeof(g_statusBuf), "?(%d)", status);
            return g_statusBuf;
    }
}

/* ---- Volume change tracker (throttled logging) ---- */
#define VOL_TRACK_MAX 32
typedef struct {
    void *sample;
    int   last_logged_vol;
    DWORD last_log_tick;
    int   change_count;  /* changes since last log */
} VolTracker;
static VolTracker g_volTrack[VOL_TRACK_MAX];

static void VolTrack_Log(void *s, int vol) {
    VolTracker *vt = NULL;
    for (int i = 0; i < VOL_TRACK_MAX; i++) {
        if (g_volTrack[i].sample == s) { vt = &g_volTrack[i]; break; }
    }
    if (!vt) {
        /* Find empty slot or oldest */
        for (int i = 0; i < VOL_TRACK_MAX; i++) {
            if (!g_volTrack[i].sample) { vt = &g_volTrack[i]; break; }
        }
        if (!vt) vt = &g_volTrack[0];
        vt->sample = s;
        vt->last_logged_vol = -1;
        vt->last_log_tick = 0;
        vt->change_count = 0;
    }
    vt->change_count++;
    DWORD now = GetTickCount();
    /* Log if: volume changed significantly, or every 2 seconds if changing */
    int delta = abs(vol - vt->last_logged_vol);
    if (delta >= 15 || (vt->change_count > 1 && now - vt->last_log_tick >= 2000)) {
        LogMsg("AUDIO: vol %p \"%s\" %d->%d (%d changes)\n",
            s, SFMap_Name(s), vt->last_logged_vol, vol, vt->change_count);
        vt->last_logged_vol = vol;
        vt->last_log_tick = now;
        vt->change_count = 0;
    } else if (vt->last_logged_vol < 0) {
        /* First time this sample is seen */
        vt->last_logged_vol = vol;
        vt->last_log_tick = now;
    }
}

static void VolTrack_Clear(void *s) {
    for (int i = 0; i < VOL_TRACK_MAX; i++)
        if (g_volTrack[i].sample == s) { g_volTrack[i].sample = NULL; break; }
}

/* ---- Zombie sample tracker ---- */
#define ZOMBIE_MAX 32
#define ZOMBIE_STALE_COUNT 10  /* kill after 10 consecutive stale checks (30s) */
typedef struct {
    void *sample;
    int   last_pos;
    int   stale_count; /* how many dumps at same position */
} ZombieTracker;
static ZombieTracker g_zombies[ZOMBIE_MAX];

/* ---- Periodic audio state dump ---- */
static void Audio_DumpState(void) {
    static DWORD lastCheck = 0;
    static DWORD lastDump = 0;
    DWORD now = GetTickCount();
    if (now - lastCheck < 3000) return; /* zombie check every 3 seconds */
    lastCheck = now;
    int doDump = (now - lastDump >= 30000); /* full log dump every 30 seconds */
    if (doDump) lastDump = now;
    int activeCount = 0;
    for (int i = 0; i < AUDIO_MAX_SAMPLES; i++) {
        SampleFileMap *m = &g_sampleFiles[i];
        if (!m->sample) continue;
        int status = real_AIL_sample_status ? real_AIL_sample_status(m->sample) : -1;
        if (SMP_IS_ACTIVE(status) || status == SMP_STOPPED) {
            int vol = real_AIL_sample_volume ? real_AIL_sample_volume(m->sample) : -1;
            int pos = real_AIL_sample_position ? real_AIL_sample_position(m->sample) : -1;
            int loops = real_AIL_sample_loop_count ? real_AIL_sample_loop_count(m->sample) : -1;
            if (doDump) {
                const char *nm = m->file_name[0] ? m->file_name :
                    (m->data_hash ? HashMap_Lookup(m->data_hash) : NULL);
                if (!nm) nm = "(anon)";
                {
                    int rate = real_AIL_sample_playback_rate ? real_AIL_sample_playback_rate(m->sample) : -1;
                    int pan = real_AIL_sample_pan ? real_AIL_sample_pan(m->sample) : -1;
                    LogMsg("AUDIO-STATE: %p \"%s\" status=%s vol=%d pos=%d/%d loops=%d rate=%d pan=%d hash=%08X\n",
                        m->sample, nm,
                        StatusStr(status), vol, pos, m->data_size, loops,
                        rate, pan, m->data_hash);
                }
            }
            activeCount++;

            /* Zombie detection: any sample stuck at same position */
            if (SMP_IS_ACTIVE(status) && pos > 0) {
                /* Immediate kill: finished one-shot (pos at end of data) */
                if (m->data_size > 0 && loops == 1 &&
                    pos >= (m->data_size > 200 ? m->data_size - 200 : 0)) {
                    LogMsg("AUDIO: FINISHED killed %p hash=%08X size=%d pos=%d\n",
                        m->sample, m->data_hash, m->data_size, pos);
                    if (real_AIL_end_sample) real_AIL_end_sample(m->sample);
                    activeCount--;
                    continue;
                }

                ZombieTracker *zt = NULL;
                for (int z = 0; z < ZOMBIE_MAX; z++) {
                    if (g_zombies[z].sample == m->sample) { zt = &g_zombies[z]; break; }
                }
                if (!zt) {
                    for (int z = 0; z < ZOMBIE_MAX; z++) {
                        if (!g_zombies[z].sample) { zt = &g_zombies[z]; break; }
                    }
                }
                if (zt) {
                    if (zt->sample == m->sample && zt->last_pos == pos) {
                        zt->stale_count++;
                        /* Log stale samples even if zombie killer is off */
                        if (zt->stale_count >= ZOMBIE_STALE_COUNT && (zt->stale_count % 5) == 0) {
                            const char *nm2 = m->file_name[0] ? m->file_name :
                                (m->data_hash ? HashMap_Lookup(m->data_hash) : NULL);
                            LogMsg("SND-STALE: %p \"%s\" hash=%08X pos=%d loops=%d stale=%d checks (%s)\n",
                                m->sample, nm2 ? nm2 : "(anon)", m->data_hash, pos, loops,
                                zt->stale_count, (g_config.enable_zombie_killer && loops != 0) ? "will kill" : "NOT killing (ambient)");
                        }
                        /* Don't kill infinite-loop samples (loops=0) — they're ambient sounds */
                        if (g_config.enable_zombie_killer &&
                            zt->stale_count >= ZOMBIE_STALE_COUNT && loops != 0) {
                            LogMsg("AUDIO: ZOMBIE killed %p hash=%08X pos=%d loops=%d (stale %d checks)\n",
                                m->sample, m->data_hash, pos, loops, zt->stale_count);
                            if (real_AIL_end_sample) real_AIL_end_sample(m->sample);
                            zt->sample = NULL;
                            zt->stale_count = 0;
                            activeCount--;
                            continue;
                        }
                    } else {
                        zt->sample = m->sample;
                        zt->last_pos = pos;
                        zt->stale_count = 1;
                    }
                }
            } else {
                /* Clear zombie tracking for non-candidate samples */
                for (int z = 0; z < ZOMBIE_MAX; z++) {
                    if (g_zombies[z].sample == m->sample) {
                        g_zombies[z].sample = NULL; break;
                    }
                }
            }
        }
    }
    if (doDump && activeCount > 0)
        LogMsg("AUDIO-STATE: %d active samples\n", activeCount);
    if (doDump && g_pcmCacheReady)
        LogMsg("PCMCACHE: %d hits, %d misses (saved to disk)\n",
            g_pcmCacheHits, g_pcmCacheMisses);
    if (doDump && (g_decodeCacheHits || g_decodeCacheHashHits))
        LogMsg("RAMCACHE: %d handle-hits, %d hash-hits (avoided disk I/O)\n",
            g_decodeCacheHits, g_decodeCacheHashHits);
    if (doDump && g_pcmPoolReady)
        LogMsg("PCMPOOL: %d hits (%d files, %.1f MB resident)\n",
            g_pcmPoolHits, g_pcmPoolCount, (double)g_pcmPoolBytes / (1024.0*1024.0));
}

static int __stdcall my_AIL_startup(void) {
    int result = real_AIL_startup ? real_AIL_startup() : 0;
    g_audioReady = 1;
    LogMsg("AUDIO: AIL_startup() = %d\n", result);
    /* Now MSS is fully initialized — safe to bring up our WASAPI/mixer/reverb
     * without conflicting with MSS's own waveOut. Called only once. */
    {
        static int v19_inited = 0;
        if (!v19_inited) {
            v19_inited = 1;
            Audio_v19_Init(g_hMSS);
        }
    }
    return result;
}

static int __stdcall my_AIL_set_preference(int pref, int value) {
    int orig_value = value;
    if (g_config.fix_audio_quality) {
        switch (pref) {
            case DIG_MIXER_CHANNELS:
                if (value < 32) { value = 32; }
                break;
            case DIG_RESAMPLING_TOLERANCE:
                value = 0; /* best quality resampling */
                break;
        }
    }
    int result = real_AIL_set_preference ? real_AIL_set_preference(pref, value) : 0;
    if (value != orig_value)
        LogMsg("AUDIO: AIL_set_preference(%d, %d->%d)\n", pref, orig_value, value);
    return result;
}

/*
 * WAVEFORMATEX layout (packed):
 *   WORD  wFormatTag;       // offset 0
 *   WORD  nChannels;        // offset 2
 *   DWORD nSamplesPerSec;   // offset 4
 *   DWORD nAvgBytesPerSec;  // offset 8
 *   WORD  nBlockAlign;      // offset 12
 *   WORD  wBitsPerSample;   // offset 14
 */
static void * __stdcall my_AIL_waveOutOpen(void **drv, void *phwo,
                                            int wDevID, void *lpFmt) {
    /* Log the format but don't modify it — game already uses 44100/16/2.
     * Modifying the format can cause buffer mismatches and audio artifacts. */
    if (lpFmt) {
        unsigned short *fmt = (unsigned short *)lpFmt;
        unsigned int *fmt32 = (unsigned int *)lpFmt;
        LogMsg("AUDIO: AIL_waveOutOpen format: %uHz/%uch/%ubit\n",
            fmt32[1], fmt[1], fmt[7]);
    }
    void *result = real_AIL_waveOutOpen ?
        real_AIL_waveOutOpen(drv, phwo, wDevID, lpFmt) : NULL;
    LogMsg("AUDIO: AIL_waveOutOpen() = %p (drv=%p)\n", result, drv ? *drv : NULL);
    /* Custom mixer mode: decode ADPCM→PCM and feed back to MSS (no separate waveOut) */
    if (g_config.enable_custom_mixer)
        LogMsg("AUDIO: ADPCM decode mode active (MSS passthrough)\n");
    return result;
}

static void __stdcall my_AIL_set_sample_volume(void *s, int vol) {
    if (!s) return;
    /* v21: apply per-category volume multiplier + clamp (fixes vol=-1 bug) */
    vol = AudioCategoryAdjust(s, vol);
    {
        static DWORD lastVolDiag = 0;
        DWORD now = GetTickCount();
        if (now - lastVolDiag >= 500) {
            lastVolDiag = now;
            LogMsg("VOLDIAG: sample_vol handle=%p vol=%d name=\"%s\"\n",
                s, vol, SFMap_Name(s));
        }
    }
    if (g_config.sample_volume_boost > 0 && vol > 0) {
        int boosted = vol + g_config.sample_volume_boost;
        if (boosted > 127) boosted = 127;
        vol = boosted;
    }
    /* Route to mixer if we have this channel, otherwise store pending */
    if (g_mixReady) {
        if (Mix_FindByHandle(s)) {
            Mix_SetVolume(s, vol);
        } else {
            Pending_Set(s, PENDING_HAS_VOL, vol);
            if (real_AIL_set_sample_volume) real_AIL_set_sample_volume(s, vol);
        }
        return;
    }
    if (real_AIL_set_sample_volume) real_AIL_set_sample_volume(s, vol);
}

static void __stdcall my_AIL_set_stream_volume(void *str, int vol) {
    LogMsg("VOLDIAG: stream_vol handle=%p vol=%d\n", str, vol);
    /* v20: our stream? route volume to mixer, keep MSS muted */
    {
        StreamPlayer *sp = StreamPlayer_FindByReal(str);
        if (sp) {
            sp->target_volume = vol;
            Mix_SetVolume(sp->mixer_handle, vol);
            if (real_AIL_set_stream_volume) real_AIL_set_stream_volume(str, 0);
            return;
        }
    }
    /* Remember target volume for fade-in after pause */
    if (str && vol > 0) StreamVol_Set(str, vol);
    /* Fade only streams (background music) for smooth transitions */
    if (g_config.fade_out_ms > 0 && real_AIL_stream_volume && str) {
        int cur = real_AIL_stream_volume(str);
        if (abs(cur - vol) > 15) { /* only fade large jumps */
            LogMsg("AUDIO: stream fade %p: %d -> %d (%dms)\n",
                str, cur, vol, g_config.fade_out_ms);
            Fade_Start(str, 1, NULL, cur, vol, g_config.fade_out_ms);
            return;
        }
    }
    if (real_AIL_set_stream_volume) real_AIL_set_stream_volume(str, vol);
}

static void __stdcall my_AIL_set_digital_master_volume(void *drv, int vol) {
    {
        int cur = real_AIL_digital_master_volume ? real_AIL_digital_master_volume(drv) : -1;
        LogMsg("VOLDIAG: master_vol drv=%p %d->%d\n", drv, cur, vol);
    }
    if (real_AIL_set_digital_master_volume) real_AIL_set_digital_master_volume(drv, vol);
}

static int __stdcall my_AIL_set_named_sample_file(void *s, const char *name,
                                                   void *data, int size, int flags) {
    SFMap_Set(s, data, size, name);
    {
        static int logCount = 0;
        if (logCount < 200) {
            LogMsg("AUDIO: load_sample(%p, \"%s\", size=%d, hash=%08X, flags=%d)\n",
                s, name ? name : "(null)", size,
                AudioDataHash(data, AUDIO_HASH_BYTES), flags);
            logCount++;
        }
    }
    return real_AIL_set_named_sample_file ?
        real_AIL_set_named_sample_file(s, name, data, size, flags) : 0;
}

static int __stdcall my_AIL_set_sample_file(void *s, void *data, int flags) {
    int wav_size = WAV_GetDataSize(data);
    SFMap_Set(s, data, wav_size, NULL);

    /* ADPCM decode mode: decode ADPCM→PCM, pass PCM WAV to MSS */
    if (g_config.enable_custom_mixer && data) {
        const unsigned char *p = (const unsigned char *)data;
        /* Compute content hash once for all cache lookups below */
        unsigned int data_hash = AudioDataHash(data, AUDIO_HASH_BYTES);

        /* Check cache: same handle + same data pointer → reuse decoded WAV */
        DecodeCache *cached = DecodeCache_Find(s, data);
        if (cached) {
            /* Update data_size for FINISHED killer to use PCM bounds */
            SampleFileMap *m = SFMap_Find(s);
            if (m) m->data_size = cached->wav_size;
            g_decodeCacheHits++;
            return real_AIL_set_sample_file ?
                real_AIL_set_sample_file(s, cached->wav_buf, flags) : 0;
        }

        /* v21: hash-based RAM fallback — same content on a different handle.
         * Avoids disk I/O during room transitions when sounds are reused. */
        {
            int dup_size = 0;
            unsigned char *dup_wav = DecodeCache_FindByHash(data_hash, &dup_size);
            if (dup_wav) {
                DecodeCache_Store(s, data, dup_wav, dup_size);
                DecodeCache_SetHash(s, data_hash);
                {
                    SampleFileMap *m = SFMap_Find(s);
                    if (m) m->data_size = dup_size;
                }
                return real_AIL_set_sample_file ?
                    real_AIL_set_sample_file(s, dup_wav, flags) : 0;
            }
        }

        /* v21: pre-warm pool (all cache files loaded into RAM at startup) */
        if (g_pcmPoolReady) {
            int pool_size = 0;
            unsigned char *pool_wav = PCMPool_Find(data_hash, &pool_size);
            if (pool_wav) {
                DecodeCache_Store(s, data, pool_wav, pool_size);
                DecodeCache_SetHash(s, data_hash);
                {
                    SampleFileMap *m = SFMap_Find(s);
                    if (m) m->data_size = pool_size;
                }
                return real_AIL_set_sample_file ?
                    real_AIL_set_sample_file(s, pool_wav, flags) : 0;
            }
        }

        /* v20: persistent disk cache (fallback if pool miss or disabled) */
        if (g_pcmCacheReady) {
            int disk_size = 0;
            unsigned char *disk_wav = PCMCache_Load(data_hash, &disk_size);
            if (disk_wav) {
                /* Keep a copy in the per-handle DecodeCache so we can free correctly */
                DecodeCache_Store(s, data, disk_wav, disk_size);
                DecodeCache_SetHash(s, data_hash);
                {
                    SampleFileMap *m = SFMap_Find(s);
                    if (m) m->data_size = disk_size;
                }
                return real_AIL_set_sample_file ?
                    real_AIL_set_sample_file(s, disk_wav, flags) : 0;
            }
        }

        /* Parse WAV header to check format */
        if (p[0]=='R' && p[1]=='I' && p[2]=='F' && p[3]=='F' &&
            p[8]=='W' && p[9]=='A' && p[10]=='V' && p[11]=='E') {
            /* Find fmt chunk */
            int pos = 12;
            unsigned short fmt_tag = 0;
            unsigned short num_ch = 0;
            unsigned int sample_rate = 0;
            unsigned short block_align = 0;
            const unsigned char *data_ptr = NULL;
            int data_chunk_size = 0;

            while (pos + 8 <= wav_size) {
                unsigned int ck_size = *(unsigned int*)(p + pos + 4);
                if (memcmp(p + pos, "fmt ", 4) == 0 && ck_size >= 16) {
                    fmt_tag = *(unsigned short*)(p + pos + 8);
                    num_ch = *(unsigned short*)(p + pos + 10);
                    sample_rate = *(unsigned int*)(p + pos + 12);
                    block_align = *(unsigned short*)(p + pos + 20);
                }
                if (memcmp(p + pos, "data", 4) == 0) {
                    data_ptr = p + pos + 8;
                    data_chunk_size = ck_size;
                }
                pos += 8 + ((ck_size + 1) & ~1);
            }

            /* Only decode ADPCM; pass PCM through directly */
            if (fmt_tag == 0x0011 && data_ptr && data_chunk_size > 0 && block_align > 0) {
                int total_samples = 0;
                short *pcm = ADPCM_Decode(data_ptr, data_chunk_size,
                    block_align, num_ch, &total_samples);
                if (pcm && total_samples > 0) {
                    int pcm_wav_size = 0;
                    unsigned char *pcm_wav = BuildPCMWav(pcm, total_samples,
                        sample_rate, num_ch, &pcm_wav_size);
                    free(pcm);
                    if (pcm_wav) {
                        int result;
                        DecodeCache_Store(s, data, pcm_wav, pcm_wav_size);
                        DecodeCache_SetHash(s, data_hash);
                        /* v20: also persist to disk cache */
                        if (g_pcmCacheReady) {
                            PCMCache_Save(data_hash, pcm_wav, pcm_wav_size);
                        }
                        /* Update data_size so FINISHED killer uses PCM bounds, keep original hash */
                        {
                            SampleFileMap *m = SFMap_Find(s);
                            if (m) m->data_size = pcm_wav_size;
                        }
                        {
                            static int logCount = 0;
                            if (logCount < 100) {
                                LogMsg("DECODE: %p ADPCM→PCM %dHz/%dch %d samples (%d→%d bytes)\n",
                                    s, sample_rate, num_ch, total_samples,
                                    wav_size, pcm_wav_size);
                                logCount++;
                            }
                        }
                        result = real_AIL_set_sample_file ?
                            real_AIL_set_sample_file(s, pcm_wav, flags) : 0;
                        return result;
                    }
                    /* BuildPCMWav failed — fall through to MSS with original data */
                } else {
                    if (pcm) free(pcm);
                    /* Decode failed — fall through to MSS with original data */
                }
            }
            /* PCM format or decode failed: pass original data to MSS */
        }
    }

    return real_AIL_set_sample_file ? real_AIL_set_sample_file(s, data, flags) : 0;
}

static void __stdcall my_AIL_start_sample(void *s) {
    if (!s) return;

    /* Custom mixer path */
    if (g_mixReady && Mix_FindByHandle(s)) {
        MixChannel *ch = Mix_FindByHandle(s);
        LogMsg("MIXER: start_sample(%p) vol=%d rate=%d pan=%d\n",
            s, ch ? ch->volume : -1, ch ? ch->playback_rate : -1, ch ? ch->pan : -1);
        Mix_StartSample(s);
        return;
    }

    /* MSS path: pre-start cleanup */
    {
        SampleFileMap *cur = SFMap_Find(s);
        unsigned int cur_hash = cur ? cur->data_hash : 0;
        int i;
        for (i = 0; i < AUDIO_MAX_SAMPLES; i++) {
            SampleFileMap *m = &g_sampleFiles[i];
            int st, pos;
            if (!m->sample || m->sample == s) continue;
            st = real_AIL_sample_status ? real_AIL_sample_status(m->sample) : -1;
            if (!SMP_IS_ACTIVE(st)) continue;
            pos = real_AIL_sample_position ? real_AIL_sample_position(m->sample) : 0;

            /* Kill finished one-shot */
            if (m->data_size > 0 && pos > 0) {
                int at_end = pos >= (m->data_size > 200 ? m->data_size - 200 : 0);
                int loops = real_AIL_sample_loop_count ? real_AIL_sample_loop_count(m->sample) : 1;
                if (at_end && loops == 1) {
                    if (real_AIL_end_sample) real_AIL_end_sample(m->sample);
                    continue;
                }
            }
            /* Ambient dedup: kill duplicate infinite-loop sound on another handle */
            if (g_config.enable_dedup_killer && cur_hash && m->data_hash == cur_hash) {
                int loops = real_AIL_sample_loop_count ? real_AIL_sample_loop_count(m->sample) : 1;
                if (loops == 0) {
                    LogMsg("AUDIO: DEDUP killed %p hash=%08X loops=0\n", m->sample, m->data_hash);
                    if (real_AIL_end_sample) real_AIL_end_sample(m->sample);
                    continue;
                }
            }
        }
    }

    if (real_AIL_start_sample) real_AIL_start_sample(s);

    /* MSS path: post-start eviction */
    if (real_AIL_sample_status) {
        int post = real_AIL_sample_status(s);
        if (!SMP_IS_ACTIVE(post)) {
            int quietest_vol = 999, busy = 0, i;
            void *victim = NULL;
            for (i = 0; i < AUDIO_MAX_SAMPLES; i++) {
                int st;
                if (!g_sampleFiles[i].sample || g_sampleFiles[i].sample == s) continue;
                st = real_AIL_sample_status(g_sampleFiles[i].sample);
                if (SMP_IS_ACTIVE(st)) {
                    int v = real_AIL_sample_volume ? real_AIL_sample_volume(g_sampleFiles[i].sample) : 127;
                    busy++;
                    if (v < quietest_vol) { quietest_vol = v; victim = g_sampleFiles[i].sample; }
                }
            }
            if (victim && busy >= 8) {
                if (real_AIL_end_sample) real_AIL_end_sample(victim);
                if (real_AIL_start_sample) real_AIL_start_sample(s);
            }
        }
    }
}

static void __stdcall my_AIL_end_sample(void *s) {
    if (g_mixReady && Mix_FindByHandle(s)) {
        Mix_EndSample(s);
        return;
    }
    Fade_Cancel(s);
    if (real_AIL_end_sample) real_AIL_end_sample(s);
}

static void __stdcall my_AIL_stop_sample(void *s) {
    if (g_mixReady && Mix_FindByHandle(s)) {
        Mix_StopSample(s);
        return;
    }
    Fade_Cancel(s);
    if (real_AIL_stop_sample) real_AIL_stop_sample(s);
}

static void __stdcall my_AIL_resume_sample(void *s) {
    if (g_mixReady && Mix_FindByHandle(s)) { Mix_ResumeSample(s); return; }
    if (real_AIL_resume_sample) real_AIL_resume_sample(s);
}

static void __stdcall my_AIL_set_sample_loop_count_hook(void *s, int count) {
    if (g_mixReady) {
        if (Mix_FindByHandle(s)) {
            Mix_SetLoopCount(s, count);
        } else {
            Pending_Set(s, PENDING_HAS_LOOP, count);
            if (real_AIL_set_sample_loop_count) real_AIL_set_sample_loop_count(s, count);
        }
        return;
    }
    if (real_AIL_set_sample_loop_count) real_AIL_set_sample_loop_count(s, count);
}

/* ---- Query hooks (return mixer state only if we have this channel) ---- */
static int __stdcall my_AIL_sample_status_hook(void *s) {
    if (g_mixReady && Mix_FindByHandle(s))
        return Mix_GetStatus(s);
    return real_AIL_sample_status ? real_AIL_sample_status(s) : SMP_FREE;
}

static int __stdcall my_AIL_sample_volume_hook(void *s) {
    if (g_mixReady && Mix_FindByHandle(s))
        return Mix_GetVolume(s);
    return real_AIL_sample_volume ? real_AIL_sample_volume(s) : 0;
}

static int __stdcall my_AIL_sample_position_hook(void *s) {
    if (g_mixReady && Mix_FindByHandle(s))
        return Mix_GetPosition(s);
    return real_AIL_sample_position ? real_AIL_sample_position(s) : 0;
}

static void __stdcall my_AIL_set_sample_position_hook(void *s, int pos) {
    if (g_mixReady && Mix_FindByHandle(s)) { Mix_SetPosition(s, pos); return; }
    if (real_AIL_set_sample_position) real_AIL_set_sample_position(s, pos);
}

static int __stdcall my_AIL_sample_loop_count_hook(void *s) {
    if (g_mixReady && Mix_FindByHandle(s))
        return Mix_GetLoopCount(s);
    return real_AIL_sample_loop_count ? real_AIL_sample_loop_count(s) : 1;
}

/* ---- Playback rate / pan / init hooks ---- */
static void __stdcall my_AIL_set_sample_playback_rate_hook(void *s, int rate) {
    if (g_mixReady) {
        if (Mix_FindByHandle(s)) {
            Mix_SetPlaybackRate(s, rate);
        } else {
            Pending_Set(s, PENDING_HAS_RATE, rate);
            if (real_AIL_set_sample_playback_rate) real_AIL_set_sample_playback_rate(s, rate);
        }
        return;
    }
    if (real_AIL_set_sample_playback_rate) real_AIL_set_sample_playback_rate(s, rate);
}

static int __stdcall my_AIL_sample_playback_rate_hook(void *s) {
    if (g_mixReady && Mix_FindByHandle(s))
        return Mix_GetPlaybackRate(s);
    return real_AIL_sample_playback_rate ? real_AIL_sample_playback_rate(s) : 0;
}

static void __stdcall my_AIL_set_sample_pan_hook(void *s, int pan) {
    if (g_mixReady) {
        if (Mix_FindByHandle(s)) {
            Mix_SetPan(s, pan);
        } else {
            Pending_Set(s, PENDING_HAS_PAN, pan);
            if (real_AIL_set_sample_pan) real_AIL_set_sample_pan(s, pan);
        }
        return;
    }
    if (real_AIL_set_sample_pan) real_AIL_set_sample_pan(s, pan);
}

static int __stdcall my_AIL_sample_pan_hook(void *s) {
    if (g_mixReady && Mix_FindByHandle(s))
        return Mix_GetPan(s);
    return real_AIL_sample_pan ? real_AIL_sample_pan(s) : 64;
}

/* Room-transition burst detector: tracks init_sample calls.
 * When >3 calls within 200ms, proactively kill all FINISHED/DONE samples
 * on MSS side to free channels before new sounds try to load. */
static DWORD g_initBurstStart = 0;
static int   g_initBurstCount = 0;

static void InitBurst_CleanupMSS(void) {
    int killed = 0, i;
    for (i = 0; i < AUDIO_MAX_SAMPLES; i++) {
        SampleFileMap *m = &g_sampleFiles[i];
        int st, pos, loops;
        if (!m->sample) continue;
        st = real_AIL_sample_status ? real_AIL_sample_status(m->sample) : -1;
        if (!SMP_IS_ACTIVE(st) && st != SMP_STOPPED) continue;
        pos = real_AIL_sample_position ? real_AIL_sample_position(m->sample) : 0;
        loops = real_AIL_sample_loop_count ? real_AIL_sample_loop_count(m->sample) : 1;
        /* Kill one-shots that are near the end */
        if (loops == 1 && m->data_size > 0 &&
            pos >= (m->data_size > 200 ? m->data_size - 200 : 0)) {
            if (real_AIL_end_sample) real_AIL_end_sample(m->sample);
            killed++;
        }
        /* Kill DONE/STOPPED one-shots */
        if ((st == SMP_DONE || st == SMP_STOPPED) && loops != 0) {
            if (real_AIL_end_sample) real_AIL_end_sample(m->sample);
            killed++;
        }
    }
    if (killed > 0)
        LogMsg("BURST: room transition detected, cleaned %d stale MSS samples\n", killed);
}

static void __stdcall my_AIL_init_sample_hook(void *s) {
    if (g_mixReady) Mix_EndSample(s);
    /* Burst detection */
    {
        DWORD now = GetTickCount();
        if (now - g_initBurstStart > 200) {
            g_initBurstStart = now;
            g_initBurstCount = 1;
        } else {
            g_initBurstCount++;
            if (g_initBurstCount == 4) /* threshold: 4 inits in 200ms = room change */
                InitBurst_CleanupMSS();
        }
    }
    if (real_AIL_init_sample) real_AIL_init_sample(s);
}

static void __stdcall my_AIL_set_stream_loop_count_hook(void *str, int count) {
    /* v20: our stream? route to mixer, keep MSS in sync */
    {
        StreamPlayer *sp = StreamPlayer_FindByReal(str);
        if (sp) {
            sp->loop_count = count;
            Mix_SetLoopCount(sp->mixer_handle, count);
            if (real_AIL_set_stream_loop_count) real_AIL_set_stream_loop_count(str, count);
            return;
        }
    }
    {
        static int logCount = 0;
        if (logCount < 100) {
            LogMsg("AUDIO: stream_loop_count %p %d\n", str, count);
            logCount++;
        }
    }
    if (real_AIL_set_stream_loop_count) real_AIL_set_stream_loop_count(str, count);
}

static void __stdcall my_AIL_start_stream(void *str) {
    /* v20: our stream? start in mixer; also let MSS start (muted) to keep
     * internal state consistent for any non-hooked calls */
    {
        StreamPlayer *sp = StreamPlayer_FindByReal(str);
        if (sp) {
            LogMsg("STREAM: start real=%p (mixer=%p, loop=%d)\n", str, sp, sp->loop_count);
            Mix_SetLoopCount(sp->mixer_handle, sp->loop_count);
            Mix_StartSample(sp->mixer_handle);
            if (real_AIL_start_stream) real_AIL_start_stream(str);
            if (real_AIL_set_stream_volume) real_AIL_set_stream_volume(str, 0);
            Duck_Apply(sp->dig_driver);  /* v20: duck MSS while dialog plays */
            return;
        }
    }
    LogMsg("AUDIO: start_stream(%p)\n", str);
    if (g_config.fade_in_ms > 0 && real_AIL_set_stream_volume &&
        real_AIL_stream_volume && str) {
        /* Use remembered target volume, fall back to current */
        int cur_vol = real_AIL_stream_volume(str);
        int target_vol = StreamVol_Get(str, cur_vol);
        if (target_vol <= 0) target_vol = cur_vol;
        if (target_vol <= 0) target_vol = 127;

        /* Crossfade: fade out ALL other active fades on streams */
        if (g_config.fade_out_ms > 0) {
            int i;
            for (i = 0; i < AUDIO_MAX_FADES; i++) {
                if (g_fades[i].active && g_fades[i].is_stream == 1 &&
                    g_fades[i].handle != str) {
                    int cv = real_AIL_stream_volume(g_fades[i].handle);
                    if (cv > 0) {
                        Fade_StartWithAction(g_fades[i].handle, 1, NULL, cv, 0,
                            g_config.fade_out_ms, FADE_ACTION_PAUSE);
                    }
                }
            }
        }

        /* Fade in new stream */
        real_AIL_set_stream_volume(str, 0);
        if (real_AIL_start_stream) real_AIL_start_stream(str);
        Fade_Start(str, 1, NULL, 0, target_vol, g_config.fade_in_ms);
        LogMsg("AUDIO: stream fade-in %p: 0 -> %d (%dms)\n",
            str, target_vol, g_config.fade_in_ms);
        return;
    }
    if (real_AIL_start_stream) real_AIL_start_stream(str);
}

static void __stdcall my_AIL_pause_stream(void *str, int onoff) {
    /* v20: our stream? pause/resume mixer, also pass to MSS */
    {
        StreamPlayer *sp = StreamPlayer_FindByReal(str);
        if (sp) {
            if (onoff) Mix_StopSample(sp->mixer_handle);
            else       Mix_ResumeSample(sp->mixer_handle);
            if (real_AIL_pause_stream) real_AIL_pause_stream(str, onoff);
            return;
        }
    }
    if (onoff && g_config.fade_out_ms > 0 && real_AIL_stream_volume &&
        real_AIL_set_stream_volume && str) {
        int cur = real_AIL_stream_volume(str);
        if (cur > 0) {
            Fade_StartWithAction(str, 1, NULL, cur, 0,
                g_config.fade_out_ms, FADE_ACTION_PAUSE);
            LogMsg("AUDIO: stream fade-out->pause %p: %d -> 0 (%dms)\n",
                str, cur, g_config.fade_out_ms);
            return;
        }
    }
    if (real_AIL_pause_stream) real_AIL_pause_stream(str, onoff);
    if (!onoff && str) {
        Fade_Cancel(str);
    }
}

/* ---- MSS function resolution and hook installation ---- */

static void Audio_ResolveFunctions(HMODULE hMSS) {
    if (!hMSS) return;
    /* Resolve using decorated names (stdcall @N suffix).
     * Uses void** cast since PFN types don't match var names. */
    #define RESOLVE_AIL(var, name) \
        *(void**)&(var) = (void*)GetProcAddress(hMSS, name)

    RESOLVE_AIL(real_AIL_startup,              "_AIL_startup@0");
    RESOLVE_AIL(real_AIL_set_preference,       "_AIL_set_preference@8");
    RESOLVE_AIL(real_AIL_waveOutOpen,          "_AIL_waveOutOpen@16");
    RESOLVE_AIL(real_AIL_set_sample_volume,    "_AIL_set_sample_volume@8");
    RESOLVE_AIL(real_AIL_sample_volume,        "_AIL_sample_volume@4");
    RESOLVE_AIL(real_AIL_start_sample,         "_AIL_start_sample@4");
    RESOLVE_AIL(real_AIL_end_sample,           "_AIL_end_sample@4");
    RESOLVE_AIL(real_AIL_stop_sample,          "_AIL_stop_sample@4");
    RESOLVE_AIL(real_AIL_resume_sample,        "_AIL_resume_sample@4");
    RESOLVE_AIL(real_AIL_sample_status,        "_AIL_sample_status@4");
    RESOLVE_AIL(real_AIL_sample_position,      "_AIL_sample_position@4");
    RESOLVE_AIL(real_AIL_set_sample_position,  "_AIL_set_sample_position@8");
    RESOLVE_AIL(real_AIL_set_sample_loop_count,"_AIL_set_sample_loop_count@8");
    RESOLVE_AIL(real_AIL_set_sample_playback_rate,"_AIL_set_sample_playback_rate@8");
    RESOLVE_AIL(real_AIL_sample_playback_rate,"_AIL_sample_playback_rate@4");
    RESOLVE_AIL(real_AIL_set_sample_pan,      "_AIL_set_sample_pan@8");
    RESOLVE_AIL(real_AIL_sample_pan,           "_AIL_sample_pan@4");
    RESOLVE_AIL(real_AIL_init_sample,          "_AIL_init_sample@4");
    RESOLVE_AIL(real_AIL_sample_loop_count,    "_AIL_sample_loop_count@4");
    RESOLVE_AIL(real_AIL_set_named_sample_file,"_AIL_set_named_sample_file@20");
    RESOLVE_AIL(real_AIL_set_sample_file,      "_AIL_set_sample_file@12");
    RESOLVE_AIL(real_AIL_set_stream_volume,    "_AIL_set_stream_volume@8");
    RESOLVE_AIL(real_AIL_stream_volume,        "_AIL_stream_volume@4");
    RESOLVE_AIL(real_AIL_start_stream,         "_AIL_start_stream@4");
    RESOLVE_AIL(real_AIL_set_stream_loop_count,"_AIL_set_stream_loop_count@8");
    RESOLVE_AIL(real_AIL_pause_stream,         "_AIL_pause_stream@8");
    RESOLVE_AIL(real_AIL_stream_status,        "_AIL_stream_status@4");
    RESOLVE_AIL(real_AIL_set_digital_master_volume, "_AIL_set_digital_master_volume@8");
    RESOLVE_AIL(real_AIL_digital_master_volume,"_AIL_digital_master_volume@4");
    #undef RESOLVE_AIL

    LogMsg("AUDIO: MSS resolved: startup=%p waveOut=%p setSmpVol=%p smpPos=%p\n",
        (void*)real_AIL_startup, (void*)real_AIL_waveOutOpen,
        (void*)real_AIL_set_sample_volume, (void*)real_AIL_sample_position);
    LogMsg("AUDIO: MSS resolved: startSmp=%p endSmp=%p stopSmp=%p resumeSmp=%p\n",
        (void*)real_AIL_start_sample, (void*)real_AIL_end_sample,
        (void*)real_AIL_stop_sample, (void*)real_AIL_resume_sample);
    LogMsg("AUDIO: MSS resolved: setFile=%p setNamedFile=%p loopCnt=%p setLoopCnt=%p\n",
        (void*)real_AIL_set_sample_file, (void*)real_AIL_set_named_sample_file,
        (void*)real_AIL_sample_loop_count, (void*)real_AIL_set_sample_loop_count);
    LogMsg("AUDIO: MSS resolved: setStrmVol=%p strmVol=%p startStrm=%p masterVol=%p\n",
        (void*)real_AIL_set_stream_volume, (void*)real_AIL_stream_volume,
        (void*)real_AIL_start_stream, (void*)real_AIL_set_digital_master_volume);
}

static void Audio_InstallHooks(void) {
    void *tramp;
    /* Macro: installs detour, updates real pointer to trampoline.
     * Uses *(void**)& cast to avoid MSVC type mismatch warnings. */
    #define INSTALL_AIL_HOOK(real, hook, detour, name) do { \
        if (real) { \
            tramp = DetourInstall(&detour, (void*)(real), (void*)(hook), name); \
            if (tramp) *(void**)&(real) = tramp; \
        } \
    } while(0)

    if (g_config.fix_audio_quality) {
        INSTALL_AIL_HOOK(real_AIL_startup,     my_AIL_startup,
            g_detourAIL_startup, "AIL_startup");
        INSTALL_AIL_HOOK(real_AIL_set_preference, my_AIL_set_preference,
            g_detourAIL_set_preference, "AIL_set_preference");
        INSTALL_AIL_HOOK(real_AIL_waveOutOpen, my_AIL_waveOutOpen,
            g_detourAIL_waveOutOpen, "AIL_waveOutOpen");
    }

    /* Hook sample volume — for boost and/or diagnostic tracking */
    INSTALL_AIL_HOOK(real_AIL_set_sample_volume, my_AIL_set_sample_volume,
        g_detourAIL_set_sample_volume, "AIL_set_sample_volume");

    if (g_config.fade_out_ms > 0 || g_config.fade_in_ms > 0) {
        /* Fade stream (music) volumes — sample volumes must be instant
         * because the game updates them constantly for 3D attenuation */
        INSTALL_AIL_HOOK(real_AIL_set_stream_volume, my_AIL_set_stream_volume,
            g_detourAIL_set_stream_volume, "AIL_set_stream_volume");
        /* Hook pause_stream for fade-out before pause */
        INSTALL_AIL_HOOK(real_AIL_pause_stream, my_AIL_pause_stream,
            g_detourAIL_pause_stream, "AIL_pause_stream");
    }

    /* Hook sample file tracking — needed for diagnostics, loop preservation, and loop-click fix */
    INSTALL_AIL_HOOK(real_AIL_set_named_sample_file, my_AIL_set_named_sample_file,
        g_detourAIL_set_named_sample_file, "AIL_set_named_sample_file");
    INSTALL_AIL_HOOK(real_AIL_set_sample_file, my_AIL_set_sample_file,
        g_detourAIL_set_sample_file, "AIL_set_sample_file");
    INSTALL_AIL_HOOK(real_AIL_start_sample, my_AIL_start_sample,
        g_detourAIL_start_sample, "AIL_start_sample");
    /* Always hook end/stop for diagnostics + loop preservation */
    INSTALL_AIL_HOOK(real_AIL_end_sample, my_AIL_end_sample,
        g_detourAIL_end_sample, "AIL_end_sample");
    INSTALL_AIL_HOOK(real_AIL_stop_sample, my_AIL_stop_sample,
        g_detourAIL_stop_sample, "AIL_stop_sample");

    /* Always hook start_stream for fade-in + diagnostics */
    INSTALL_AIL_HOOK(real_AIL_start_stream, my_AIL_start_stream,
        g_detourAIL_start_stream, "AIL_start_stream");

    /* Diagnostic hooks — always installed for logging */
    INSTALL_AIL_HOOK(real_AIL_resume_sample, my_AIL_resume_sample,
        g_detourAIL_resume_sample, "AIL_resume_sample");
    INSTALL_AIL_HOOK(real_AIL_set_sample_loop_count, my_AIL_set_sample_loop_count_hook,
        g_detourAIL_set_sample_loop_count, "AIL_set_sample_loop_count");
    INSTALL_AIL_HOOK(real_AIL_set_stream_loop_count, my_AIL_set_stream_loop_count_hook,
        g_detourAIL_set_stream_loop_count, "AIL_set_stream_loop_count");
    INSTALL_AIL_HOOK(real_AIL_set_digital_master_volume, my_AIL_set_digital_master_volume,
        g_detourAIL_set_digital_master_volume, "AIL_set_digital_master_volume");

    /* Query hooks — intercept so game sees our mixer state */
    INSTALL_AIL_HOOK(real_AIL_sample_status, my_AIL_sample_status_hook,
        g_detourAIL_sample_status, "AIL_sample_status");
    INSTALL_AIL_HOOK(real_AIL_sample_volume, my_AIL_sample_volume_hook,
        g_detourAIL_sample_volume, "AIL_sample_volume");
    INSTALL_AIL_HOOK(real_AIL_sample_position, my_AIL_sample_position_hook,
        g_detourAIL_sample_position, "AIL_sample_position");
    INSTALL_AIL_HOOK(real_AIL_set_sample_position, my_AIL_set_sample_position_hook,
        g_detourAIL_set_sample_position, "AIL_set_sample_position");
    INSTALL_AIL_HOOK(real_AIL_sample_loop_count, my_AIL_sample_loop_count_hook,
        g_detourAIL_sample_loop_count, "AIL_sample_loop_count");

    /* Playback rate & pan hooks (critical for custom mixer) */
    INSTALL_AIL_HOOK(real_AIL_set_sample_playback_rate, my_AIL_set_sample_playback_rate_hook,
        g_detourAIL_set_sample_playback_rate, "AIL_set_sample_playback_rate");
    INSTALL_AIL_HOOK(real_AIL_sample_playback_rate, my_AIL_sample_playback_rate_hook,
        g_detourAIL_sample_playback_rate, "AIL_sample_playback_rate");
    INSTALL_AIL_HOOK(real_AIL_set_sample_pan, my_AIL_set_sample_pan_hook,
        g_detourAIL_set_sample_pan, "AIL_set_sample_pan");
    INSTALL_AIL_HOOK(real_AIL_sample_pan, my_AIL_sample_pan_hook,
        g_detourAIL_sample_pan, "AIL_sample_pan");
    INSTALL_AIL_HOOK(real_AIL_init_sample, my_AIL_init_sample_hook,
        g_detourAIL_init_sample, "AIL_init_sample");

    #undef INSTALL_AIL_HOOK

    LogMsg("AUDIO: hooks installed (quality=%d loops=%d fadeIn=%dms fadeOut=%dms boost=%d mixer=%d)\n",
        g_config.fix_audio_quality, g_config.preserve_loops,
        g_config.fade_in_ms, g_config.fade_out_ms, g_config.sample_volume_boost,
        g_config.enable_custom_mixer);
}


/* ==================================================================
 *  v19 AUDIO LAYER — WASAPI, Reverb, Stream Manager
 *  (ADPCM fix and Mix_Render with crossfade are inlined above)
 * ================================================================== */

#include <initguid.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <avrt.h>
#include <mmreg.h>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "avrt.lib")

/* LoadConfig_v19 — called from LoadConfig() to read new ini keys */
static void LoadConfig_v19(const char *ini_path) {
    /* v20: log which ini is being read + whether it actually exists */
    {
        DWORD attr = GetFileAttributesA(ini_path);
        int exists = (attr != INVALID_FILE_ATTRIBUTES) && !(attr & FILE_ATTRIBUTE_DIRECTORY);
        char fullpath[MAX_PATH];
        GetFullPathNameA(ini_path, MAX_PATH, fullpath, NULL);
        LogMsg("CONFIG: reading ini '%s' (resolved='%s', exists=%d)\n",
            ini_path, fullpath, exists);
        if (!exists) {
            LogMsg("CONFIG: *** ini NOT FOUND — all v19/v20 options will default to 0 ***\n");
        }
    }
    g_config.enable_wasapi     = GetPrivateProfileIntA("Audio","EnableWASAPI",      0, ini_path);
    g_config.wasapi_latency_ms = GetPrivateProfileIntA("Audio","WASAPILatencyMs",  15, ini_path);
    if (g_config.wasapi_latency_ms < 5)  g_config.wasapi_latency_ms = 5;
    if (g_config.wasapi_latency_ms > 80) g_config.wasapi_latency_ms = 80;

    g_config.enable_reverb     = GetPrivateProfileIntA("Audio","EnableReverb",      0, ini_path);
    g_config.reverb_wet        = GetPrivateProfileIntA("Audio","ReverbWet",        18, ini_path);
    g_config.reverb_room_size  = GetPrivateProfileIntA("Audio","ReverbRoomSize",   55, ini_path);
    g_config.reverb_damping    = GetPrivateProfileIntA("Audio","ReverbDamping",    60, ini_path);
    if (g_config.reverb_wet > 100) g_config.reverb_wet = 100;
    if (g_config.reverb_room_size > 100) g_config.reverb_room_size = 100;
    if (g_config.reverb_damping > 100) g_config.reverb_damping = 100;

    g_config.enable_stream_mgr = GetPrivateProfileIntA("Audio","EnableStreamMgr",   0, ini_path);
    g_config.loop_crossfade_ms = GetPrivateProfileIntA("Audio","LoopCrossfadeMs",  15, ini_path);
    if (g_config.loop_crossfade_ms < 0)  g_config.loop_crossfade_ms = 0;
    if (g_config.loop_crossfade_ms > 100) g_config.loop_crossfade_ms = 100;
    /* v20 */
    g_config.enable_pcm_cache  = GetPrivateProfileIntA("Audio","EnablePCMCache",    0, ini_path);
    g_config.enable_dedup_killer  = GetPrivateProfileIntA("Audio","EnableDedupKiller",  0, ini_path);
    g_config.enable_zombie_killer = GetPrivateProfileIntA("Audio","EnableZombieKiller", 1, ini_path);
    g_config.dialog_duck_level    = GetPrivateProfileIntA("Audio","DialogDuckLevel",   70, ini_path);
    if (g_config.dialog_duck_level < 0)   g_config.dialog_duck_level = 0;
    if (g_config.dialog_duck_level > 100) g_config.dialog_duck_level = 100;
    g_config.enable_pcm_prewarm   = GetPrivateProfileIntA("Audio","EnablePCMPrewarm",   1, ini_path);

    /* v21: per-category volume + EQ */
    g_config.vol_ambient       = GetPrivateProfileIntA("Audio","VolAmbient",       100, ini_path);
    g_config.vol_music         = GetPrivateProfileIntA("Audio","VolMusic",         100, ini_path);
    g_config.vol_sfx           = GetPrivateProfileIntA("Audio","VolSFX",           100, ini_path);
    if (g_config.vol_ambient < 0)   g_config.vol_ambient = 0;
    if (g_config.vol_ambient > 200) g_config.vol_ambient = 200;
    if (g_config.vol_music < 0)     g_config.vol_music = 0;
    if (g_config.vol_music > 200)   g_config.vol_music = 200;
    if (g_config.vol_sfx < 0)       g_config.vol_sfx = 0;
    if (g_config.vol_sfx > 200)     g_config.vol_sfx = 200;
    g_config.msaa_samples      = GetPrivateProfileIntA("Rendering","MSAASamples",        4, ini_path);
    if (g_config.msaa_samples < 0) g_config.msaa_samples = 0;
    if (g_config.msaa_samples > 16) g_config.msaa_samples = 16;
    g_config.eq_high_shelf_db  = GetPrivateProfileIntA("Audio","EQHighShelfDb",      3, ini_path);
    if (g_config.eq_high_shelf_db < 0)  g_config.eq_high_shelf_db = 0;
    if (g_config.eq_high_shelf_db > 12) g_config.eq_high_shelf_db = 12;

    /* v20: echo what we actually got */
    LogMsg("CONFIG: [Audio] EnableCustomMixer=%d EnableWASAPI=%d EnableReverb=%d EnableStreamMgr=%d EnablePCMCache=%d PCMPrewarm=%d\n",
        g_config.enable_custom_mixer, g_config.enable_wasapi, g_config.enable_reverb,
        g_config.enable_stream_mgr, g_config.enable_pcm_cache, g_config.enable_pcm_prewarm);
    LogMsg("CONFIG: [Audio] EnableDedupKiller=%d EnableZombieKiller=%d\n",
        g_config.enable_dedup_killer, g_config.enable_zombie_killer);
    LogMsg("CONFIG: [Audio] VolAmbient=%d%% VolMusic=%d%% VolSFX=%d%% EQ=+%ddB\n",
        g_config.vol_ambient, g_config.vol_music, g_config.vol_sfx,
        g_config.eq_high_shelf_db);
}

/* ================================================================
 *  [14] SCHROEDER REVERB
 * ================================================================ */

#define REV_NUM_COMBS   4
#define REV_NUM_AP      2
#define REV_STEREO_OFFSET 23

static const int g_revCombLen[REV_NUM_COMBS] = { 1116, 1188, 1277, 1356 };
static const int g_revApLen[REV_NUM_AP]      = {  225,  556 };

typedef struct {
    float *buf;
    int    size, pos;
    float  lp_state;
} RevComb;

typedef struct {
    float *buf;
    int    size, pos;
} RevAllpass;

typedef struct {
    int        ready;
    RevComb    combL[REV_NUM_COMBS];
    RevComb    combR[REV_NUM_COMBS];
    RevAllpass apL[REV_NUM_AP];
    RevAllpass apR[REV_NUM_AP];
    float      feedback;
    float      damp;
    float      wet, dry;
    float      ap_fb;
} Reverb;

static Reverb g_reverb;
static CRITICAL_SECTION g_reverbCS;
static volatile int g_reverbCSReady = 0;

static int Rev_ScaleLen(int base, int sr) {
    if (sr <= 44100) return base;
    return (int)((long long)base * sr / 44100);
}

static int Reverb_Init(int sample_rate) {
    int i, len;
    memset(&g_reverb, 0, sizeof(g_reverb));
    if (!g_reverbCSReady) { InitializeCriticalSection(&g_reverbCS); g_reverbCSReady = 1; }

    for (i = 0; i < REV_NUM_COMBS; i++) {
        len = Rev_ScaleLen(g_revCombLen[i], sample_rate);
        g_reverb.combL[i].buf = (float*)calloc(len, sizeof(float));
        g_reverb.combL[i].size = len;
        len = Rev_ScaleLen(g_revCombLen[i] + REV_STEREO_OFFSET, sample_rate);
        g_reverb.combR[i].buf = (float*)calloc(len, sizeof(float));
        g_reverb.combR[i].size = len;
        if (!g_reverb.combL[i].buf || !g_reverb.combR[i].buf) return 0;
    }
    for (i = 0; i < REV_NUM_AP; i++) {
        len = Rev_ScaleLen(g_revApLen[i], sample_rate);
        g_reverb.apL[i].buf = (float*)calloc(len, sizeof(float));
        g_reverb.apL[i].size = len;
        len = Rev_ScaleLen(g_revApLen[i] + REV_STEREO_OFFSET, sample_rate);
        g_reverb.apR[i].buf = (float*)calloc(len, sizeof(float));
        g_reverb.apR[i].size = len;
        if (!g_reverb.apL[i].buf || !g_reverb.apR[i].buf) return 0;
    }

    g_reverb.feedback = 0.7f + (g_config.reverb_room_size / 100.0f) * 0.28f;
    g_reverb.damp     = (g_config.reverb_damping / 100.0f) * 0.5f;
    g_reverb.wet      = (g_config.reverb_wet / 100.0f) * 0.5f;
    g_reverb.dry      = 1.0f;
    g_reverb.ap_fb    = 0.5f;
    g_reverb.ready    = 1;

    LogMsg("REVERB: init OK @ %dHz (fb=%.2f damp=%.2f wet=%.2f)\n",
        sample_rate, g_reverb.feedback, g_reverb.damp, g_reverb.wet);
    return 1;
}

static void Reverb_Shutdown(void) {
    int i;
    if (!g_reverb.ready) return;
    EnterCriticalSection(&g_reverbCS);
    g_reverb.ready = 0;
    for (i = 0; i < REV_NUM_COMBS; i++) {
        free(g_reverb.combL[i].buf); free(g_reverb.combR[i].buf);
    }
    for (i = 0; i < REV_NUM_AP; i++) {
        free(g_reverb.apL[i].buf); free(g_reverb.apR[i].buf);
    }
    LeaveCriticalSection(&g_reverbCS);
    if (g_reverbCSReady) { DeleteCriticalSection(&g_reverbCS); g_reverbCSReady = 0; }
    memset(&g_reverb, 0, sizeof(g_reverb));
}

static void Reverb_Process(short *buf, int frames) {
    int i, c;
    const float inv32k = 1.0f / 32768.0f;
    /* Snapshot reverb parameters under CS, then process without holding the lock.
     * The comb/allpass buffers are only accessed by the mixer thread, so no
     * concurrent mutation — the CS only guards against Reverb_Shutdown. */
    float feedback, damp, wet, dry, ap_fb;
    if (!g_reverb.ready) return;

    EnterCriticalSection(&g_reverbCS);
    if (!g_reverb.ready) { LeaveCriticalSection(&g_reverbCS); return; }
    feedback = g_reverb.feedback;
    damp     = g_reverb.damp;
    wet      = g_reverb.wet;
    dry      = g_reverb.dry;
    ap_fb    = g_reverb.ap_fb;
    LeaveCriticalSection(&g_reverbCS);

    for (i = 0; i < frames; i++) {
        float in_l = buf[i*2 + 0] * inv32k;
        float in_r = buf[i*2 + 1] * inv32k;
        float in_mono = (in_l + in_r) * 0.5f * 0.015f;
        float out_l = 0, out_r = 0;
        float y;

        for (c = 0; c < REV_NUM_COMBS; c++) {
            {
                RevComb *k = &g_reverb.combL[c];
                float x = k->buf[k->pos];
                out_l += x;
                k->lp_state = x * (1.0f - damp) + k->lp_state * damp;
                k->buf[k->pos] = in_mono + k->lp_state * feedback;
                k->pos++; if (k->pos >= k->size) k->pos = 0;
            }
            {
                RevComb *k = &g_reverb.combR[c];
                float x = k->buf[k->pos];
                out_r += x;
                k->lp_state = x * (1.0f - damp) + k->lp_state * damp;
                k->buf[k->pos] = in_mono + k->lp_state * feedback;
                k->pos++; if (k->pos >= k->size) k->pos = 0;
            }
        }

        for (c = 0; c < REV_NUM_AP; c++) {
            {
                RevAllpass *k = &g_reverb.apL[c];
                float x = k->buf[k->pos];
                y = -out_l + x;
                k->buf[k->pos] = out_l + x * ap_fb;
                out_l = y;
                k->pos++; if (k->pos >= k->size) k->pos = 0;
            }
            {
                RevAllpass *k = &g_reverb.apR[c];
                float x = k->buf[k->pos];
                y = -out_r + x;
                k->buf[k->pos] = out_r + x * ap_fb;
                out_r = y;
                k->pos++; if (k->pos >= k->size) k->pos = 0;
            }
        }

        {
            float fl = in_l * dry + out_l * wet;
            float fr = in_r * dry + out_r * wet;
            int il, ir;
            if (fl >  1.0f) fl = 1.0f - (1.0f/(fl+1.0f));
            if (fl < -1.0f) fl = -1.0f - (-1.0f/(fl-1.0f));
            if (fr >  1.0f) fr = 1.0f - (1.0f/(fr+1.0f));
            if (fr < -1.0f) fr = -1.0f - (-1.0f/(fr-1.0f));
            il = (int)(fl * 32767.0f);
            ir = (int)(fr * 32767.0f);
            if (il > 32767) il = 32767; if (il < -32768) il = -32768;
            if (ir > 32767) ir = 32767; if (ir < -32768) ir = -32768;
            buf[i*2 + 0] = (short)il;
            buf[i*2 + 1] = (short)ir;
        }
    }
    /* CS was released before the loop — no leave needed here */
}

/* ================================================================
 *  HIGH-SHELF EQ
 *  Boosts frequencies above ~8 kHz to compensate for ADPCM
 *  compression artifacts. IMA ADPCM 4:1 discards high-frequency
 *  detail; this restores clarity and "air" to the sound.
 *  Uses a simple one-pole emphasis filter per channel.
 * ================================================================ */

static float g_eqCoeff   = 0.0f;  /* low-pass coefficient: exp(-2π·fc/sr) */
static float g_eqGain    = 0.0f;  /* emphasis amount: 10^(dB/20) - 1.0 */
static float g_eqLpL     = 0.0f;  /* left channel LP state */
static float g_eqLpR     = 0.0f;  /* right channel LP state */
static volatile int g_eqReady = 0;

static void EQ_Init(int sample_rate) {
    float fc = 8000.0f;  /* cutoff frequency */
    float db;
    if (g_config.eq_high_shelf_db <= 0) { g_eqReady = 0; return; }
    db = (float)g_config.eq_high_shelf_db;
    /* LP coeff: how much of previous sample to keep (higher = lower cutoff) */
    g_eqCoeff = (float)exp(-2.0 * 3.14159265 * (double)fc / (double)sample_rate);
    /* Gain: linear emphasis above cutoff */
    g_eqGain = (float)pow(10.0, (double)db / 20.0) - 1.0f;
    g_eqLpL = 0.0f;
    g_eqLpR = 0.0f;
    g_eqReady = 1;
    LogMsg("EQ: high-shelf +%d dB @ %.0f Hz (coeff=%.4f gain=%.3f)\n",
        g_config.eq_high_shelf_db, fc, g_eqCoeff, g_eqGain);
}

/* Process stereo int16 buffer in-place.
 * For each sample: extract high-freq component via HP = x - LP,
 * then add emphasis * HP back to the signal. */
static void EQ_Process(short *buf, int frames) {
    int i;
    float coeff, gain, lpL, lpR;
    if (!g_eqReady) return;
    coeff = g_eqCoeff;
    gain  = g_eqGain;
    lpL   = g_eqLpL;
    lpR   = g_eqLpR;
    for (i = 0; i < frames; i++) {
        float inL = (float)buf[i*2 + 0];
        float inR = (float)buf[i*2 + 1];
        float hpL, hpR;
        int outL, outR;
        /* One-pole LP: lp += (1-coeff) * (in - lp) */
        lpL += (1.0f - coeff) * (inL - lpL);
        lpR += (1.0f - coeff) * (inR - lpR);
        /* HP = original - lowpassed */
        hpL = inL - lpL;
        hpR = inR - lpR;
        /* Add emphasis to high frequencies */
        outL = (int)(inL + gain * hpL);
        outR = (int)(inR + gain * hpR);
        /* Clamp */
        if (outL > 32767) outL = 32767; if (outL < -32768) outL = -32768;
        if (outR > 32767) outR = 32767; if (outR < -32768) outR = -32768;
        buf[i*2 + 0] = (short)outL;
        buf[i*2 + 1] = (short)outR;
    }
    g_eqLpL = lpL;
    g_eqLpR = lpR;
}

/* ================================================================
 *  MUSIC REPLACEMENT SYSTEM
 *  Intercepts the game's PlaySequence function (at 0x4991D7) to
 *  detect when a named MIDI sequence starts. If a pre-mixed WAV
 *  exists in aitd_music/, it's played through our mixer while the
 *  original stems are muted via the category volume system.
 *
 *  The game's adaptive music system still runs (state machine,
 *  transitions), but all stem audio is silenced. Our WAV provides
 *  the music instead.
 * ================================================================ */

/* Mapping table: MIDB DSEQ sequence name -> WAV filename */
typedef struct { const char *seq; const char *wav; } MusicMap;
static const MusicMap g_musicMapTable[] = {
    /* Confirmed DSEQ matches */
    { "intro_a1", "Intro_A1.wav" },
    { "intro_b1", "Intro_B1.wav" },
    { "intro_c1", "Intro_C1.wav" },
    { "intro_c2", "Intro_C2.wav" },
    { "outro_a1", "Outro_A1.wav" },
    { "teneb_a1", "Teneb_A1.wav" },
    { "brume_b1", "Brume_B1.wav" },
    { "bibli_a1", "Bibli_A1.wav" },
    /* Name-based matches (MIDB filename = OGG name) */
    { "act_a1",   "Act_A1.wav"   },
    { "act_b1",   "Act_B1.wav"   },
    { "act_b2",   "Act_B2.wav"   },
    { "act_b3",   "Act_B3.wav"   },
    { "act_c1",   "Act_C1.wav"   },
    { "act_c2",   "Act_C2.wav"   },
    { "dark_a",   "Dark_A.wav"   },
    { "dark_b",   "Dark_B.wav"   },
    { "dark_c",   "Dark_C.wav"   },
    { "dark_d",   "Dark_D.wav"   },
    { "dark_e",   "Dark_E.wav"   },
    { "dark_f",   "Dark_F.wav"   },
    { "dark_g",   "Dark_G.wav"   },
    { "dark_h",   "Dark_H.wav"   },
    { "goth_a1",  "Goth_A1.wav"  },
    { "goth_b1",  "Goth_B1.wav"  },
    { "inv_a1",   "Inv_A1.wav"   },
    { "inv_b1",   "Inv_B1.wav"   },
    { "inv_c1",   "Inv_C1.wav"   },
    { "inv_c2",   "Inv_C2.wav"   },
    { "inv_d1",   "Inv_D1.wav"   },
    { "inv_e1",   "Inv_E1.wav"   },
    { "inv_f1",   "Inv_F1.wav"   },
    { "inv_g1",   "Inv_G1.wav"   },
    { "labo_a",   "Labo_A.wav"   },
    { "mansn_a",  "Mansn_A.wav"  },
    { "mansn_b",  "Mansn_B.wav"  },
    { "mansn_c",  "Mansn_C.wav"  },
    { "mansn_d",  "Mansn_D.wav"  },
    { "susp_c1",  "Susp_C1.wav"  },
    { "decv_bibli","Decv_Bibli.wav"},
    { "decv_fort", "Decv_Fort.wav" },
    { "decv_grtte","Decv_Grtte.wav"},
    { "decv_plnet","Decv_Plnet.wav"},
    { NULL, NULL }
};

/* Music replacement state (g_musicReplActive declared earlier as forward decl) */
static void          *g_musicMixHandle  = NULL; /* our fake handle for mixer channel */
static unsigned char *g_musicWavData    = NULL; /* WAV file in memory */
static int            g_musicWavSize    = 0;
static char           g_musicCurSeq[64] = {0};  /* current sequence name */
static char           g_musicDir[MAX_PATH] = {0}; /* path to aitd_music/ */
static int            g_musicDirReady   = 0;

/* Sentinel handle for the music mixer channel */
static int g_musicHandleSentinel = 0xA17DFA7; /* "AITDFAT" */

static void MusicReplace_Init(void) {
    snprintf(g_musicDir, MAX_PATH, "%saitd_music\\", g_gameDir);
    /* Check if directory exists */
    DWORD attr = GetFileAttributesA(g_musicDir);
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) {
        g_musicDirReady = 1;
        LogMsg("MUSIC: replacement dir '%s' found\n", g_musicDir);
    } else {
        LogMsg("MUSIC: no replacement dir '%s'\n", g_musicDir);
    }
}

static const char* MusicReplace_FindWav(const char *seq_name) {
    const MusicMap *m;
    for (m = g_musicMapTable; m->seq; m++) {
        if (_stricmp(seq_name, m->seq) == 0)
            return m->wav;
    }
    return NULL;
}

static void MusicReplace_Stop(void) {
    if (!g_musicReplActive) return;
    LogMsg("MUSIC: stopping replacement for '%s'\n", g_musicCurSeq);
    if (g_mixReady)
        Mix_EndSample(g_musicMixHandle);
    if (g_musicWavData) { free(g_musicWavData); g_musicWavData = NULL; }
    g_musicWavSize = 0;
    g_musicCurSeq[0] = 0;
    g_musicReplActive = 0;
}

static int MusicReplace_Start(const char *seq_name) {
    const char *wav_name;
    char path[MAX_PATH];
    FILE *fp;
    int sz;
    unsigned char *buf;

    if (!g_musicDirReady || !g_mixReady) return 0;

    wav_name = MusicReplace_FindWav(seq_name);
    if (!wav_name) return 0;

    /* Same sequence already playing? Keep it. */
    if (g_musicReplActive && _stricmp(g_musicCurSeq, seq_name) == 0) {
        return 1;  /* silent keep — log throttled in Hook_PlaySequence */
    }

    /* Stop previous if different */
    if (g_musicReplActive)
        MusicReplace_Stop();

    /* Load WAV file */
    snprintf(path, MAX_PATH, "%s%s", g_musicDir, wav_name);
    fp = fopen(path, "rb");
    if (!fp) {
        LogMsg("MUSIC: WAV not found: %s\n", path);
        return 0;
    }
    fseek(fp, 0, SEEK_END);
    sz = (int)ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz < 44 || sz > 200*1024*1024) { /* sanity: 44 bytes min, 200 MB max */
        fclose(fp); return 0;
    }
    buf = (unsigned char*)malloc(sz);
    if (!buf) { fclose(fp); return 0; }
    if ((int)fread(buf, 1, sz, fp) != sz) { free(buf); fclose(fp); return 0; }
    fclose(fp);

    /* Verify it's a WAV */
    if (buf[0]!='R'||buf[1]!='I'||buf[2]!='F'||buf[3]!='F') {
        LogMsg("MUSIC: not a WAV: %s\n", path);
        free(buf); return 0;
    }

    g_musicWavData = buf;
    g_musicWavSize = sz;
    g_musicMixHandle = &g_musicHandleSentinel;

    /* Load into mixer — this decodes WAV (PCM or ADPCM) into a channel */
    if (!Mix_LoadSample(g_musicMixHandle, g_musicWavData, g_musicWavSize)) {
        LogMsg("MUSIC: mixer load failed for %s\n", wav_name);
        free(g_musicWavData); g_musicWavData = NULL;
        return 0;
    }

    /* Set volume and start looping */
    Mix_SetVolume(g_musicMixHandle, 127);
    Mix_SetLoopCount(g_musicMixHandle, 0);  /* 0 = infinite loop */
    Mix_StartSample(g_musicMixHandle);

    strncpy(g_musicCurSeq, seq_name, 63);
    g_musicCurSeq[63] = 0;
    g_musicReplActive = 1;

    LogMsg("MUSIC: started '%s' -> %s (%d bytes)\n", seq_name, wav_name, sz);
    return 1;
}

/* === PlaySequence detour === */
/* Game function at 0x4991D7:
 *   int __cdecl PlaySequence(const char *name, int vol1, int vol2,
 *                            int p3, int p4, void *out);
 * 6 args, returns int. Prologue: 55 8B EC 83 EC 10 (6 bytes). */

typedef int (__cdecl *PFN_PlaySequence)(const char*, int, int, int, int, void*);
static DetourHook g_detourPlaySequence = {0};

static int __cdecl Hook_PlaySequence(const char *name, int vol1, int vol2,
                                      int p3, int p4, void *out)
{
    PFN_PlaySequence origFn = (PFN_PlaySequence)g_detourPlaySequence.trampoline;
    int result;
    int hasReplacement = 0;

    /* Throttle log: only log when sequence changes or every 60s */
    {
        static char lastLogged[64] = {0};
        static DWORD lastLogTime = 0;
        DWORD now = GetTickCount();
        if (!name || _stricmp(lastLogged, name) != 0 || now - lastLogTime > 60000) {
            LogMsg("MUSIC: PlaySequence('%s', vol=%d/%d) active=%d cur='%s'\n",
                name ? name : "(null)", vol1, vol2, g_musicReplActive, g_musicCurSeq);
            if (name) { strncpy(lastLogged, name, 63); lastLogged[63]=0; }
            lastLogTime = now;
        }
    }

    /* Check if we have a WAV for this sequence */
    if (name && g_musicDirReady && MusicReplace_FindWav(name))
        hasReplacement = 1;

    /* Call original — if we have a replacement, force stems to vol=0.
     * This silences the MIDI stems through the game's own volume mechanism,
     * without touching other sequences (weather, ambient, SFX). */
    if (hasReplacement)
        result = origFn(name, 0, 0, p3, p4, out);
    else
        result = origFn(name, vol1, vol2, p3, p4, out);

    /* Start our WAV if we have a replacement.
     * For sequences WITHOUT a replacement, we need to decide:
     * - Ambient/weather sequences (orage, pluie, vent, etc.) should
     *   coexist with music → don't stop.
     * - Unknown music sequences should stop the current replacement
     *   (e.g., game transitioned to a scene we don't have music for). */
    if (hasReplacement) {
        MusicReplace_Start(name);
    } else if (g_musicReplActive && name) {
        /* Check if this is a known ambient/weather sequence that should
         * coexist with music. If not, it's probably a music change → stop. */
        int isAmbient =
            _strnicmp(name, "orage",  5) == 0 ||
            _strnicmp(name, "pluie",  5) == 0 ||
            _strnicmp(name, "vent",   4) == 0 ||
            _strnicmp(name, "goutte", 6) == 0 ||
            _strnicmp(name, "eau_",   4) == 0 ||
            _strnicmp(name, "poele",  5) == 0 ||
            _strnicmp(name, "cave",   4) == 0 ||
            _strnicmp(name, "airc",   4) == 0 ||
            _strnicmp(name, "airv",   4) == 0;
        if (!isAmbient) {
            LogMsg("MUSIC: non-ambient '%s' without WAV -> stopping replacement\n", name);
            MusicReplace_Stop();
        }
    }

    return result;
}

static void MusicReplace_InstallHook(void) {
    void *target = (void*)0x4991D7;
    /* Verify prologue is intact (55 8B EC 83 EC 10) */
    unsigned char *p = (unsigned char*)target;
    if (p[0] != 0x55 || p[1] != 0x8B || p[2] != 0xEC) {
        LogMsg("MUSIC: PlaySequence prologue mismatch at %p (got %02X %02X %02X), hook skipped\n",
            target, p[0], p[1], p[2]);
        return;
    }
    if (DetourInstall(&g_detourPlaySequence, target, (void*)Hook_PlaySequence, "PlaySequence")) {
        LogMsg("MUSIC: PlaySequence hooked at %p (trampoline %p)\n",
            target, g_detourPlaySequence.trampoline);
    } else {
        LogMsg("MUSIC: PlaySequence hook FAILED\n");
    }
}

/* ================================================================
 *  MIDI CHANNEL LIMIT PATCH
 * ================================================================
 *
 * Original bug: the MIDI player has only 5 channel struct slots
 * (stride 0x144) before hitting another data structure at 0x522678
 * (stride 0x240). The MIDB files use up to 16 channels, but
 * ApplyVolume only loops 0-4 and ApplySound only loops 0-2.
 *
 * Fix: relocate the overlapping 0x240-stride struct to new memory,
 * then increase the channel loop limits from 5/3 to 16.
 *
 * This patches 4 loop-limit bytes + 27 address references in .text.
 */

/* Addresses of all `MOV ECX, 0x522678` instructions in .text.
 * Each is 5 bytes: B9 78 26 52 00.  We patch the 4-byte address. */
static const DWORD g_midiRelocRefs[] = {
    0x497B2A, 0x497B5C, 0x497B79, 0x497B97, 0x497BB2, 0x497BC4,
    0x497C04, 0x497C25, 0x497C38, 0x497C4B, 0x497C5E, 0x497C82,
    0x497CB5, 0x497CDF, 0x497D19, 0x497D4F, 0x497DB4,
    0x497E98, 0x497EE6,
    0x49B484, 0x49B6D7, 0x49B79D, 0x49B7F5, 0x49B82C,
    0x49B875, 0x49B8AC, 0x49BC6C
};

/* Loop-limit patches: address, expected_old, new_value
 * NOTE: ApplySound (0x49A30A) is NOT patched because it uses a separate
 * 3-element pointer array at 0x5225E0, not the 0x144-stride struct.
 * Increasing its limit reads garbage from MIDI channel memory → crash.
 * MidiFunc4 (0x499B5B) is also kept original — bounds validator for
 * code paths that include ApplySound's call chain. */
static const struct { DWORD addr; BYTE old_val; BYTE new_val; const char *name; }
g_midiLimitPatches[] = {
    { 0x499679, 0x05, 0x10, "ApplyVolume"  },  /* CMP EAX, 5 -> 16 */
    { 0x499125, 0x05, 0x10, "MidiValidate" },  /* CMP ECX, 5 -> 16 */
};

static void MidiChannelPatch_Install(void)
{
    DWORD oldProt;
    int i, patched_refs = 0, patched_limits = 0;

    /* Safety: verify this is the expected EXE version before patching
     * absolute addresses.  Check module size + known prologue bytes at
     * two addresses to fingerprint the target executable. */
    {
        HMODULE hExe = GetModuleHandleA(NULL);
        IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER*)hExe;
        IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS*)((BYTE*)hExe + dos->e_lfanew);
        DWORD imageSize = nt->OptionalHeader.SizeOfImage;
        /* Expected values for the retail AITD:TNN executable */
        const DWORD expectedSize = 0x1C1000; /* actual SizeOfImage for alone4.exe */
        const BYTE expectedPrologue[] = { 0x55, 0x8B, 0xEC }; /* push ebp; mov ebp, esp */

        /* Check 1: image size must match (catches wrong EXE, Steam vs GOG) */
        if (imageSize != expectedSize) {
            LogMsg("MIDIPATCH: EXE image size mismatch (got 0x%X, expected 0x%X) — patch skipped\n",
                   imageSize, expectedSize);
            LogMsg("MIDIPATCH: This patch only works with the specific retail build.\n");
            return;
        }
        /* Check 2: verify a few addresses are actually code we expect */
        {
            DWORD probeAddrs[] = { 0x4991D7, 0x499679, 0x499125 };
            int j;
            for (j = 0; j < 3; j++) {
                __try {
                    const BYTE *p = (const BYTE*)(ULONG_PTR)probeAddrs[j];
                    if (p[0] != expectedPrologue[0] && p[0] != 0x83 /*CMP*/ &&
                        p[0] != 0x3B /*CMP*/ && p[0] != 0x05 /*limit*/) {
                        LogMsg("MIDIPATCH: probe at %p unexpected byte %02X — patch skipped\n",
                               p, p[0]);
                        return;
                    }
                } __except(EXCEPTION_EXECUTE_HANDLER) {
                    LogMsg("MIDIPATCH: probe at 0x%X caused exception — patch skipped\n",
                           probeAddrs[j]);
                    return;
                }
            }
        }
        LogMsg("MIDIPATCH: EXE verified (size=0x%X) — proceeding\n", imageSize);
    }

    /* 1. Allocate new home for the 0x240-stride struct.
     *    Original: 0x522678 (BSS, zeroed at startup).
     *    We allocate 0x4800 bytes = 32 entries * 0x240, plenty of room. */
    void *newBlock = VirtualAlloc(NULL, 0x4800,
                                  MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!newBlock) {
        LogMsg("MIDIPATCH: VirtualAlloc failed (%lu), patch skipped\n", GetLastError());
        return;
    }
    memset(newBlock, 0, 0x4800);

    /* Copy any existing data from original location (BSS, likely zeros,
     * but copy anyway in case game init ran before us). */
    memcpy(newBlock, (void*)0x522678, 0x2400);

    LogMsg("MIDIPATCH: relocated struct 0x522678 -> %p (0x4800 bytes)\n", newBlock);

    /* 2. Make .text section writable for patching */
    if (!VirtualProtect((void*)0x401000, 0xC0000, PAGE_EXECUTE_READWRITE, &oldProt)) {
        LogMsg("MIDIPATCH: VirtualProtect failed (%lu)\n", GetLastError());
        VirtualFree(newBlock, 0, MEM_RELEASE);
        return;
    }

    /* 3. Patch all 27 references: B9 [78 26 52 00] -> B9 [new address] */
    for (i = 0; i < (int)(sizeof(g_midiRelocRefs)/sizeof(g_midiRelocRefs[0])); i++) {
        unsigned char *p = (unsigned char *)(ULONG_PTR)g_midiRelocRefs[i];
        DWORD oldAddr = *(DWORD*)p;
        if (oldAddr != 0x522678) {
            LogMsg("MIDIPATCH: ref %d at %p: expected 0x522678, got %08X — skip\n",
                   i, p, oldAddr);
            continue;
        }
        *(DWORD*)p = (DWORD)(ULONG_PTR)newBlock;
        patched_refs++;
    }

    /* 4. Patch 4 loop limits */
    for (i = 0; i < (int)(sizeof(g_midiLimitPatches)/sizeof(g_midiLimitPatches[0])); i++) {
        unsigned char *p = (unsigned char *)(ULONG_PTR)g_midiLimitPatches[i].addr;
        if (*p != g_midiLimitPatches[i].old_val) {
            LogMsg("MIDIPATCH: limit '%s' at %p: expected %02X, got %02X — skip\n",
                   g_midiLimitPatches[i].name, p, g_midiLimitPatches[i].old_val, *p);
            continue;
        }
        *p = g_midiLimitPatches[i].new_val;
        patched_limits++;
    }

    /* 5. Restore .text protection */
    VirtualProtect((void*)0x401000, 0xC0000, oldProt, &oldProt);

    LogMsg("MIDIPATCH: done — %d/%d refs relocated, %d/%d limits patched\n",
           patched_refs, (int)(sizeof(g_midiRelocRefs)/sizeof(g_midiRelocRefs[0])),
           patched_limits, (int)(sizeof(g_midiLimitPatches)/sizeof(g_midiLimitPatches[0])));
}

/* ================================================================
 *  [7][8] WASAPI BACKEND
 * ================================================================ */

static const CLSID CLSID_MMDeviceEnumerator_L = {0xBCDE0395,0xE52F,0x467C,{0x8E,0x3D,0xC4,0x57,0x92,0x91,0x69,0x2E}};
static const IID   IID_IMMDeviceEnumerator_L = {0xA95664D2,0x9614,0x4F35,{0xA7,0x46,0xDE,0x8D,0xB6,0x36,0x17,0xE6}};
static const IID   IID_IAudioClient_L        = {0x1CB9AD4C,0xDBFA,0x4C32,{0xB1,0x78,0xC2,0xF5,0x68,0xA7,0x03,0xB2}};
static const IID   IID_IAudioRenderClient_L  = {0xF294ACFC,0x3146,0x4483,{0xA7,0xBF,0xAD,0xDC,0xA7,0xC2,0x60,0xE2}};

static IMMDeviceEnumerator *g_pEnum         = NULL;
static IMMDevice           *g_pDevice       = NULL;
static IAudioClient        *g_pAudioClient  = NULL;
static IAudioRenderClient  *g_pRenderClient = NULL;
static WAVEFORMATEX        *g_pDevFormat    = NULL;
static UINT32               g_wasapiBufFrames = 0;
static HANDLE               g_hWasapiEvent  = NULL;
static HANDLE               g_hWasapiThread = NULL;
static volatile int         g_wasapiRunning = 0;
static volatile int         g_wasapiReady   = 0;
static int                  g_wasapiChannels = 2;
static int                  g_wasapiIsFloat = 0;
static int                  g_wasapiComInited = 0; /* 1 if we called CoInitializeEx successfully */

static DWORD WINAPI WASAPI_ThreadProc(LPVOID p) {
    DWORD taskIndex = 0;
    HANDLE avrt;
    short *tmp = NULL;
    int tmp_size = 0;
    int wasapi_callbacks = 0;
    int wasapi_underruns = 0;
    (void)p;

    avrt = AvSetMmThreadCharacteristicsA("Pro Audio", &taskIndex);

    while (g_wasapiRunning) {
        HRESULT hr;
        UINT32 padding = 0, frames_avail;
        BYTE *data = NULL;

        if (WaitForSingleObject(g_hWasapiEvent, 200) != WAIT_OBJECT_0) continue;

        hr = g_pAudioClient->lpVtbl->GetCurrentPadding(g_pAudioClient, &padding);
        if (FAILED(hr)) continue;

        frames_avail = g_wasapiBufFrames - padding;
        if (frames_avail == 0) continue;

        if ((int)frames_avail > tmp_size) {
            free(tmp);
            tmp = (short*)malloc(frames_avail * 2 * sizeof(short));
            tmp_size = (int)frames_avail;
            if (!tmp) { tmp_size = 0; continue; }
        }

        hr = g_pRenderClient->lpVtbl->GetBuffer(g_pRenderClient, frames_avail, &data);
        if (FAILED(hr) || !data) continue;

        Mix_Render(tmp, frames_avail);

        if (g_config.enable_reverb && g_reverb.ready)
            Reverb_Process(tmp, frames_avail);

        /* v21: high-shelf EQ */
        EQ_Process(tmp, frames_avail);

        if (g_wasapiIsFloat) {
            float *out = (float*)data;
            int ch = g_wasapiChannels;
            UINT32 i;
            int c2;
            const float inv32k = 1.0f / 32768.0f;
            for (i = 0; i < frames_avail; i++) {
                float l = tmp[i*2 + 0] * inv32k;
                float r = tmp[i*2 + 1] * inv32k;
                if (ch >= 2) {
                    out[i*ch + 0] = l;
                    out[i*ch + 1] = r;
                    for (c2 = 2; c2 < ch; c2++) out[i*ch + c2] = 0.0f;
                } else {
                    out[i*ch + 0] = (l + r) * 0.5f;
                }
            }
        } else {
            if (g_wasapiChannels == 2) {
                memcpy(data, tmp, frames_avail * 4);
            } else {
                short *out = (short*)data;
                UINT32 i;
                int ch = g_wasapiChannels;
                int c2;
                for (i = 0; i < frames_avail; i++) {
                    short l = tmp[i*2 + 0], r = tmp[i*2 + 1];
                    if (ch >= 2) {
                        out[i*ch + 0] = l;
                        out[i*ch + 1] = r;
                        for (c2 = 2; c2 < ch; c2++) out[i*ch + c2] = 0;
                    } else {
                        out[i*ch + 0] = (short)(((int)l + r) / 2);
                    }
                }
            }
        }

        g_pRenderClient->lpVtbl->ReleaseBuffer(g_pRenderClient, frames_avail, 0);

        /* Detect potential underrun: if we had to fill >80% of the buffer,
         * the audio pipeline is struggling to keep up */
        if (frames_avail > g_wasapiBufFrames * 4 / 5)
            wasapi_underruns++;

        /* Periodic stats every ~5 seconds (~225 callbacks at 48kHz/1056 frames) */
        wasapi_callbacks++;
        if (wasapi_callbacks % 225 == 0) {
            int playing = 0, stopped = 0, done = 0, ffree = 0, i2;
            EnterCriticalSection(&g_mixCS);
            for (i2 = 0; i2 < MIX_CHANNELS; i2++) {
                switch (g_mixChannels[i2].status) {
                    case SMP_PLAYING: playing++; break;
                    case SMP_STOPPED: stopped++; break;
                    case SMP_DONE:    done++;    break;
                    default:          ffree++;   break;
                }
            }
            LeaveCriticalSection(&g_mixCS);
            LogMsg("WASAPI: stats playing=%d stopped=%d done=%d free=%d underruns=%d\n",
                playing, stopped, done, ffree, wasapi_underruns);
        }
    }

    free(tmp);
    if (avrt) AvRevertMmThreadCharacteristics(avrt);
    return 0;
}

static int WASAPI_Init(int latency_ms) {
    HRESULT hr;
    REFERENCE_TIME requested;
    HRESULT com_hr;
    int com_initialized_here = 0;

    /* v20 robust COM init: game may already have COM in STA mode. Try
     * MTA first; if that fails with RPC_E_CHANGED_MODE, COM is alive in
     * another apartment and we can still use it. If nothing initialised
     * COM yet, RPC_E_CHANGED_MODE would not occur. */
    com_hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (com_hr == S_OK) { com_initialized_here = 1; g_wasapiComInited = 1; }
    else if (com_hr == S_FALSE) { com_initialized_here = 1; g_wasapiComInited = 1; }  /* ours but already set */
    else if ((unsigned)com_hr == 0x80010106u /* RPC_E_CHANGED_MODE */) {
        LogMsg("WASAPI: COM already in STA mode (fine, using existing apartment)\n");
    } else {
        LogMsg("WASAPI: CoInitializeEx returned 0x%08X (continuing anyway)\n", com_hr);
    }

    /* Wrap all COM calls in SEH — a stray bad pointer from another DLL
     * can surface as an AV here and we want to gracefully fall back to
     * waveOut instead of killing the game. */
    __try {
        hr = CoCreateInstance(&CLSID_MMDeviceEnumerator_L, NULL,
            CLSCTX_ALL, &IID_IMMDeviceEnumerator_L, (void**)&g_pEnum);
        if (FAILED(hr)) {
            LogMsg("WASAPI: CoCreateInstance failed 0x%08X\n", hr);
            goto wasapi_init_fail;
        }

        hr = g_pEnum->lpVtbl->GetDefaultAudioEndpoint(g_pEnum, eRender, eConsole, &g_pDevice);
        if (FAILED(hr)) {
            LogMsg("WASAPI: GetDefaultAudioEndpoint failed 0x%08X\n", hr);
            goto wasapi_init_fail;
        }

        hr = g_pDevice->lpVtbl->Activate(g_pDevice, &IID_IAudioClient_L,
            CLSCTX_ALL, NULL, (void**)&g_pAudioClient);
        if (FAILED(hr)) {
            LogMsg("WASAPI: Activate failed 0x%08X\n", hr);
            goto wasapi_init_fail;
        }

        hr = g_pAudioClient->lpVtbl->GetMixFormat(g_pAudioClient, &g_pDevFormat);
        if (FAILED(hr)) {
            LogMsg("WASAPI: GetMixFormat failed 0x%08X\n", hr);
            goto wasapi_init_fail;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        LogMsg("WASAPI: SEH exception during enumeration — falling back to waveOut\n");
        goto wasapi_init_fail;
    }

    g_mixRate        = g_pDevFormat->nSamplesPerSec;
    g_wasapiChannels = g_pDevFormat->nChannels;
    g_wasapiIsFloat  = 0;
    if (g_pDevFormat->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
        g_wasapiIsFloat = 1;
    } else if (g_pDevFormat->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        WAVEFORMATEXTENSIBLE *ext = (WAVEFORMATEXTENSIBLE*)g_pDevFormat;
        if (ext->SubFormat.Data1 == 0x00000003) g_wasapiIsFloat = 1;
    }

    LogMsg("WASAPI: device format = %dHz/%dch %s (bits=%u)\n",
        g_mixRate, g_wasapiChannels,
        g_wasapiIsFloat ? "float32" : "int16",
        g_pDevFormat->wBitsPerSample);

    __try {
        requested = (REFERENCE_TIME)latency_ms * 10000;
        hr = g_pAudioClient->lpVtbl->Initialize(g_pAudioClient,
            AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
            AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
            AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
            requested, 0, g_pDevFormat, NULL);
        if (FAILED(hr)) {
            REFERENCE_TIME def_period, min_period;
            LogMsg("WASAPI: Initialize retry 0x%08X\n", hr);
            if (SUCCEEDED(g_pAudioClient->lpVtbl->GetDevicePeriod(g_pAudioClient, &def_period, &min_period))) {
                hr = g_pAudioClient->lpVtbl->Initialize(g_pAudioClient,
                    AUDCLNT_SHAREMODE_SHARED,
                    AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
                    AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                    AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
                    def_period, 0, g_pDevFormat, NULL);
            }
            if (FAILED(hr)) {
                LogMsg("WASAPI: Initialize failed 0x%08X\n", hr);
                goto wasapi_init_fail;
            }
        }

        g_hWasapiEvent = CreateEventA(NULL, FALSE, FALSE, NULL);
        g_pAudioClient->lpVtbl->SetEventHandle(g_pAudioClient, g_hWasapiEvent);

        hr = g_pAudioClient->lpVtbl->GetBufferSize(g_pAudioClient, &g_wasapiBufFrames);
        if (FAILED(hr)) { LogMsg("WASAPI: GetBufferSize failed\n"); goto wasapi_init_fail; }

        hr = g_pAudioClient->lpVtbl->GetService(g_pAudioClient,
            &IID_IAudioRenderClient_L, (void**)&g_pRenderClient);
        if (FAILED(hr)) { LogMsg("WASAPI: GetService(render) failed\n"); goto wasapi_init_fail; }

        g_wasapiReady   = 1;
        g_wasapiRunning = 1;
        g_hWasapiThread = CreateThread(NULL, 0, WASAPI_ThreadProc, NULL, 0, NULL);
        if (g_hWasapiThread)
            SetThreadPriority(g_hWasapiThread, THREAD_PRIORITY_TIME_CRITICAL);

        g_pAudioClient->lpVtbl->Start(g_pAudioClient);

        LogMsg("WASAPI: started (%dHz %dch, buf=%u frames = %.1fms latency)\n",
            g_mixRate, g_wasapiChannels, g_wasapiBufFrames,
            (double)g_wasapiBufFrames * 1000.0 / g_mixRate);
        return 1;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        LogMsg("WASAPI: SEH exception during init — falling back to waveOut\n");
        goto wasapi_init_fail;
    }

wasapi_init_fail:
    /* Clean up any partially-initialised COM objects so we don't leak. */
    if (g_pRenderClient) { g_pRenderClient->lpVtbl->Release(g_pRenderClient); g_pRenderClient = NULL; }
    if (g_pAudioClient)  { g_pAudioClient->lpVtbl->Release(g_pAudioClient);   g_pAudioClient  = NULL; }
    if (g_pDevice)       { g_pDevice->lpVtbl->Release(g_pDevice);             g_pDevice       = NULL; }
    if (g_pEnum)         { g_pEnum->lpVtbl->Release(g_pEnum);                 g_pEnum         = NULL; }
    if (g_pDevFormat)    { CoTaskMemFree(g_pDevFormat);                       g_pDevFormat    = NULL; }
    if (g_hWasapiEvent)  { CloseHandle(g_hWasapiEvent);                       g_hWasapiEvent  = NULL; }
    if (g_wasapiComInited) { CoUninitialize(); g_wasapiComInited = 0; }
    g_wasapiReady = 0;
    return 0;
}

static void WASAPI_Shutdown(void) {
    if (!g_wasapiReady) return;
    g_wasapiRunning = 0;
    if (g_hWasapiEvent) SetEvent(g_hWasapiEvent);
    if (g_hWasapiThread) {
        WaitForSingleObject(g_hWasapiThread, 1000);
        CloseHandle(g_hWasapiThread);
        g_hWasapiThread = NULL;
    }
    if (g_pAudioClient) g_pAudioClient->lpVtbl->Stop(g_pAudioClient);

    if (g_hWasapiEvent)   { CloseHandle(g_hWasapiEvent); g_hWasapiEvent = NULL; }
    if (g_pRenderClient)  { g_pRenderClient->lpVtbl->Release(g_pRenderClient); g_pRenderClient = NULL; }
    if (g_pAudioClient)   { g_pAudioClient->lpVtbl->Release(g_pAudioClient); g_pAudioClient = NULL; }
    if (g_pDevice)        { g_pDevice->lpVtbl->Release(g_pDevice); g_pDevice = NULL; }
    if (g_pEnum)          { g_pEnum->lpVtbl->Release(g_pEnum); g_pEnum = NULL; }
    if (g_pDevFormat)     { CoTaskMemFree(g_pDevFormat); g_pDevFormat = NULL; }
    if (g_wasapiComInited) { CoUninitialize(); g_wasapiComInited = 0; }

    g_wasapiReady = 0;
    LogMsg("WASAPI: shutdown\n");
}

/* ================================================================
 *  [15] CUSTOM STREAM PLAYER
 * ================================================================ */

#define STREAM_MAX 8

/* StreamPlayer struct is forward-declared near the top of the file */

static StreamPlayer g_streamPool[STREAM_MAX];
static CRITICAL_SECTION g_streamCS;
static volatile int g_streamCSReady = 0;

/* v20: dialog ducking — reduce MSS master vol when dialog streams play */
static int g_duckActive = 0;
static int g_duckOrigVol = 127;
static void *g_duckDrv = NULL;

static void Duck_Apply(void *dig_driver) {
    if (!g_config.dialog_duck_level || g_config.dialog_duck_level >= 100) return;
    if (g_duckActive) return;
    g_duckDrv = dig_driver;
    g_duckOrigVol = real_AIL_digital_master_volume ? real_AIL_digital_master_volume(dig_driver) : 127;
    {
        int ducked = g_duckOrigVol * g_config.dialog_duck_level / 100;
        if (real_AIL_set_digital_master_volume)
            real_AIL_set_digital_master_volume(dig_driver, ducked);
        LogMsg("DUCK: ON master %d -> %d (%d%%)\n", g_duckOrigVol, ducked, g_config.dialog_duck_level);
    }
    g_duckActive = 1;
}

static void Duck_Release(void) {
    if (!g_duckActive) return;
    if (g_duckDrv && real_AIL_set_digital_master_volume)
        real_AIL_set_digital_master_volume(g_duckDrv, g_duckOrigVol);
    LogMsg("DUCK: OFF master restored to %d\n", g_duckOrigVol);
    g_duckActive = 0;
}

static int StreamPlayer_AnyActive(void) {
    int i;
    if (!g_streamCSReady) return 0;
    for (i = 0; i < STREAM_MAX; i++)
        if (g_streamPool[i].active) return 1;
    return 0;
}

static void Stream_Init(void) {
    if (g_streamCSReady) return;
    InitializeCriticalSection(&g_streamCS);
    memset(g_streamPool, 0, sizeof(g_streamPool));
    g_streamCSReady = 1;
}

static int StreamPlayer_IsOurs(void *h) {
    int i;
    if (!h || !g_streamCSReady) return 0;
    for (i = 0; i < STREAM_MAX; i++)
        if (g_streamPool[i].active && g_streamPool[i].real_handle == h) return 1;
    return 0;
}

static StreamPlayer* StreamPlayer_FindByReal(void *real_h) {
    int i;
    if (!real_h || !g_streamCSReady) return NULL;
    for (i = 0; i < STREAM_MAX; i++)
        if (g_streamPool[i].active && g_streamPool[i].real_handle == real_h)
            return &g_streamPool[i];
    return NULL;
}

static StreamPlayer* StreamPlayer_Alloc(void) {
    int i;
    for (i = 0; i < STREAM_MAX; i++)
        if (!g_streamPool[i].active) {
            memset(&g_streamPool[i], 0, sizeof(StreamPlayer));
            g_streamPool[i].active = 1;
            return &g_streamPool[i];
        }
    return NULL;
}

static unsigned char* Stream_ReadFile(const char *filename, int *out_size) {
    FILE *fp;
    long sz;
    unsigned char *buf;

    if (!filename || !*filename) return NULL;
    fp = fopen(filename, "rb");
    if (!fp) {
        char full[MAX_PATH*2];
        snprintf(full, sizeof(full), "%s%s", g_gameDir, filename);
        fp = fopen(full, "rb");
        if (!fp) return NULL;
    }
    fseek(fp, 0, SEEK_END); sz = ftell(fp); fseek(fp, 0, SEEK_SET);
    if (sz <= 0 || sz > 30*1024*1024) { fclose(fp); return NULL; } /* 30 MB max for streams */
    buf = (unsigned char*)malloc(sz);
    if (!buf) { fclose(fp); return NULL; }
    if (fread(buf, 1, sz, fp) != (size_t)sz) { free(buf); fclose(fp); return NULL; }
    fclose(fp);
    *out_size = (int)sz;
    return buf;
}

typedef void* (__stdcall *PFN_AIL_open_stream)(void *dig, const char *name, int mem);
typedef void  (__stdcall *PFN_AIL_close_stream)(void *stream);
typedef void  (__stdcall *PFN_AIL_service_stream)(void *stream, int force);

static PFN_AIL_open_stream    real_AIL_open_stream    = NULL;
static PFN_AIL_close_stream   real_AIL_close_stream   = NULL;
static PFN_AIL_service_stream real_AIL_service_stream = NULL;

static DetourHook g_detourAIL_open_stream    = {0};
static DetourHook g_detourAIL_close_stream   = {0};
static DetourHook g_detourAIL_service_stream = {0};

static void* __stdcall my_AIL_open_stream(void *dig, const char *name, int mem) {
    void *real_h = NULL;
    StreamPlayer *sp;
    int data_size = 0;
    unsigned char *data;

    /* Always let MSS open its own stream first. Game receives a real MSS
     * handle — safe to pass to ANY MSS function (even ones we don't hook). */
    if (!real_AIL_open_stream) return NULL;
    real_h = real_AIL_open_stream(dig, name, mem);

    if (!g_config.enable_stream_mgr || !g_mixReady || !real_h) {
        return real_h;
    }

    data = Stream_ReadFile(name, &data_size);
    if (!data) {
        LogMsg("STREAM: '%s' -> MSS only (can't read)\n", name ? name : "");
        return real_h;
    }
    if (data_size < 12 || memcmp(data, "RIFF", 4) != 0 || memcmp(data+8, "WAVE", 4) != 0) {
        LogMsg("STREAM: '%s' -> MSS only (not WAV)\n", name ? name : "");
        free(data);
        return real_h;
    }

    Stream_Init();
    EnterCriticalSection(&g_streamCS);
    sp = StreamPlayer_Alloc();
    if (!sp) {
        LeaveCriticalSection(&g_streamCS);
        free(data);
        return real_h;
    }
    sp->real_handle   = real_h;
    sp->dig_driver    = dig;
    strncpy(sp->filename, name ? name : "", MAX_PATH-1);
    sp->file_data     = data;
    sp->file_size     = data_size;
    sp->mixer_handle  = (void*)sp;
    sp->loop_count    = 1;
    sp->target_volume = 127;
    LeaveCriticalSection(&g_streamCS);

    /* Silence MSS output so it doesn't play simultaneously */
    if (real_AIL_set_stream_volume) real_AIL_set_stream_volume(real_h, 0);

    Mix_LoadSample(sp->mixer_handle, sp->file_data, sp->file_size);
    LogMsg("STREAM: open '%s' real=%p +mixer=%p (%d bytes)\n",
        name ? name : "", real_h, sp, data_size);
    return real_h;  /* game gets real MSS handle */
}

static void __stdcall my_AIL_close_stream(void *stream) {
    StreamPlayer *sp = StreamPlayer_FindByReal(stream);
    if (sp) {
        LogMsg("STREAM: close real=%p (mixer=%p)\n", stream, sp);
        if (g_mixReady) Mix_EndSample(sp->mixer_handle);
        EnterCriticalSection(&g_streamCS);
        free(sp->file_data);
        memset(sp, 0, sizeof(*sp));
        LeaveCriticalSection(&g_streamCS);
        /* v20: unduck MSS if no more dialogs playing */
        if (!StreamPlayer_AnyActive()) Duck_Release();
    }
    /* Always close the real MSS stream */
    if (real_AIL_close_stream) real_AIL_close_stream(stream);
}

static void __stdcall my_AIL_service_stream(void *stream, int force) {
    /* v20: MSS always services its own buffer even for our streams
     * (which are silenced) to keep MSS internal state consistent */
    if (real_AIL_service_stream) real_AIL_service_stream(stream, force);
}

static void Stream_InstallHooks(HMODULE hMSS) {
    void *tramp;
    if (!g_config.enable_stream_mgr || !hMSS) return;

    *(void**)&real_AIL_open_stream    = (void*)GetProcAddress(hMSS, "_AIL_open_stream@12");
    *(void**)&real_AIL_close_stream   = (void*)GetProcAddress(hMSS, "_AIL_close_stream@4");
    *(void**)&real_AIL_service_stream = (void*)GetProcAddress(hMSS, "_AIL_service_stream@8");

    Stream_Init();

    #define INSTALL_STREAM(real, hook, detour, nm) do { \
        if (real) { \
            tramp = DetourInstall(&detour, (void*)(real), (void*)(hook), nm); \
            if (tramp) *(void**)&(real) = tramp; \
        } \
    } while(0)

    INSTALL_STREAM(real_AIL_open_stream,    my_AIL_open_stream,
        g_detourAIL_open_stream, "AIL_open_stream");
    INSTALL_STREAM(real_AIL_close_stream,   my_AIL_close_stream,
        g_detourAIL_close_stream, "AIL_close_stream");
    INSTALL_STREAM(real_AIL_service_stream, my_AIL_service_stream,
        g_detourAIL_service_stream, "AIL_service_stream");

    #undef INSTALL_STREAM

    LogMsg("STREAM: custom stream manager installed\n");
}

/* ================================================================
 *  UNIFIED v19 INIT / SHUTDOWN
 * ================================================================ */

static int Audio_v19_Init(HMODULE hMSS) {
    LogMsg("AUDIO-v20: Init begin (hMSS=%p enable_wasapi=%d enable_custom_mixer=%d enable_reverb=%d enable_stream_mgr=%d enable_pcm_cache=%d)\n",
        hMSS, g_config.enable_wasapi, g_config.enable_custom_mixer,
        g_config.enable_reverb, g_config.enable_stream_mgr, g_config.enable_pcm_cache);

    if (g_config.wasapi_latency_ms <= 0) g_config.wasapi_latency_ms = 15;

    /* v20: initialize persistent PCM cache first (if enabled) */
    LogMsg("AUDIO-v20: calling PCMCache_Init\n");
    PCMCache_Init();
    /* v21: pre-warm all cached files into RAM */
    PCMPool_Init();

    /* v20: initialize mixer critical section early — WASAPI thread needs it
     * and may fire before Mix_Init completes */
    if (!g_mixCS_ready) {
        InitializeCriticalSection(&g_mixCS);
        g_mixCS_ready = 1;
    }

    if (g_config.enable_wasapi) {
        LogMsg("AUDIO-v20: calling WASAPI_Init(%d ms)\n", g_config.wasapi_latency_ms);
        if (!WASAPI_Init(g_config.wasapi_latency_ms)) {
            LogMsg("AUDIO-v20: WASAPI init failed, using waveOut\n");
            g_config.enable_wasapi = 0;
        }
    }

    if (g_config.enable_custom_mixer || g_config.enable_wasapi ||
        g_config.enable_stream_mgr || g_config.enable_reverb) {
        LogMsg("AUDIO-v20: calling Mix_Init (rate=%d)\n", g_mixRate);
        if (!g_mixReady) Mix_Init();
    }

    if (g_config.enable_reverb) {
        LogMsg("AUDIO-v20: calling Reverb_Init(%d)\n", g_mixRate);
        if (!Reverb_Init(g_mixRate)) {
            LogMsg("AUDIO-v20: reverb init failed\n");
            g_config.enable_reverb = 0;
        }
    }

    /* v21: high-shelf EQ */
    EQ_Init(g_mixRate);

    /* v21: music replacement (WAV soundtrack) */
    MusicReplace_Init();
    MusicReplace_InstallHook();

    /* v22: MIDI channel limit patch — relocate overlapping struct,
     * extend processing from 5/3 channels to 16 */
    MidiChannelPatch_Install();

    if (g_config.enable_stream_mgr && hMSS) {
        LogMsg("AUDIO-v20: calling Stream_InstallHooks\n");
        Stream_InstallHooks(hMSS);
    }

    LogMsg("AUDIO-v20: init complete (WASAPI=%d mixer=%d reverb=%d stream=%d xfade=%dms PCMcache=%d)\n",
        g_wasapiReady, g_mixReady, g_reverb.ready,
        g_config.enable_stream_mgr, g_config.loop_crossfade_ms,
        g_pcmCacheReady);
    LogMsg("HASHMAP: %d Sound2/ entries loaded (hash_bytes=%d)\n",
        AITD_HASH_MAP_COUNT, AITD_HASH_MAP_BYTES);
    return 1;
}

static void Audio_v19_Shutdown(void) {
    /* Clean up stream players */
    if (g_streamCSReady) {
        int i;
        EnterCriticalSection(&g_streamCS);
        for (i = 0; i < STREAM_MAX; i++) {
            if (g_streamPool[i].active && g_streamPool[i].file_data) {
                free(g_streamPool[i].file_data);
                g_streamPool[i].file_data = NULL;
            }
            g_streamPool[i].active = 0;
        }
        LeaveCriticalSection(&g_streamCS);
        DeleteCriticalSection(&g_streamCS);
        g_streamCSReady = 0;
    }
    MusicReplace_Stop();
    PCMPool_Shutdown();
    Reverb_Shutdown();
    WASAPI_Shutdown();
}

/* ==================================================================
 *  GL HOOKS
 * ================================================================== */

/*
 * Aspect ratio correction state.
 *
 * g_displayW/H  = actual window/display dimensions.
 * g_gameVpW/H   = game's internal "full screen" viewport (auto-detected).
 * g_vpX/Y/W/H   = computed 4:3 target area in display coords (for scissor).
 * g_screenW/H    = game's logical screen (for scissor coordinate mapping).
 * g_arBarsCleared = 1 after black bars cleared this frame (reset in SwapBuffers).
 *
 * Unified approach: always compute a 4:3 target rectangle in display
 * coordinates, then map the game's viewport into it with scaling.
 */
static int g_gameVpW=0,g_gameVpH=0;
static int g_screenW=0,g_screenH=0,g_vpX=0,g_vpY=0,g_vpW=0,g_vpH=0;
static int g_arBarsCleared=0;

static void WINAPI my_glViewport(GLint x,GLint y,GLsizei w,GLsizei h){
    /* Diagnostic: log first 10 viewport calls */
    {
        static int vpCallCount=0;
        if(vpCallCount<10){
            LogMsg("VP-CALL #%d: x=%d y=%d w=%d h=%d real=%p disp=%dx%d\n",
                vpCallCount,x,y,w,h,(void*)real_glViewport,g_displayW,g_displayH);
            vpCallCount++;
        }
    }
    if(!real_glViewport)return;

    /* Apply AR correction */
    if(!g_config.fix_aspect_ratio || w<=0 || h<=0){
        real_glViewport(x,y,w,h); return;
    }

    double tgt=4.0/3.0;
    int dW = g_displayW > 0 ? g_displayW : w;
    int dH = g_displayH > 0 ? g_displayH : h;
    double dispAR = (double)dW / dH;

    if(fabs(dispAR - tgt) < 0.01 && w == dW && h == dH){
        g_vpX=x; g_vpY=y; g_vpW=w; g_vpH=h;
        g_screenW=w; g_screenH=h;
        real_glViewport(x,y,w,h);
        return;
    }

    /* Track game's largest viewport */
    if(x==0 && y==0 && w*h >= g_gameVpW*g_gameVpH){
        g_gameVpW=w; g_gameVpH=h;
    }
    int refW = g_gameVpW > 0 ? g_gameVpW : w;
    int refH = g_gameVpH > 0 ? g_gameVpH : h;

    /* Compute 4:3 target in display coords */
    int tgtW, tgtH, tgtX, tgtY;
    if(dispAR > tgt + 0.01){
        tgtH=dH; tgtW=(int)(dH*tgt); tgtX=(dW-tgtW)/2; tgtY=0;
    } else if(dispAR < tgt - 0.01){
        tgtW=dW; tgtH=(int)(dW/tgt); tgtX=0; tgtY=(dH-tgtH)/2;
    } else {
        tgtW=dW; tgtH=dH; tgtX=0; tgtY=0;
    }

    double sx=(double)tgtW/refW, sy=(double)tgtH/refH;
    GLint nx=tgtX+(GLint)(x*sx), ny=tgtY+(GLint)(y*sy);
    GLsizei nw=(GLsizei)(w*sx), nh=(GLsizei)(h*sy);

    g_screenW=refW; g_screenH=refH;
    g_vpX=tgtX; g_vpY=tgtY; g_vpW=tgtW; g_vpH=tgtH;

    if((tgtW<dW||tgtH<dH) && x==0 && y==0 && w==refW && h==refH){
        real_glViewport(0,0,dW,dH);
        if(real_glClearColor) real_glClearColor(0,0,0,1);
        if(real_glClear) real_glClear(GL_COLOR_BUFFER_BIT);
    }
    real_glViewport(nx,ny,nw,nh);
}
static void WINAPI my_glScissor(GLint x,GLint y,GLsizei w,GLsizei h){
    if(!real_glScissor)return;
    if(g_config.fix_aspect_ratio && g_vpW>0 && g_screenW>0){
        double sx=(double)g_vpW/g_screenW, sy=(double)g_vpH/g_screenH;
        real_glScissor(g_vpX+(GLint)(x*sx),g_vpY+(GLint)(y*sy),(GLsizei)(w*sx),(GLsizei)(h*sy));
    } else real_glScissor(x,y,w,h);
}

static GLint UpgradeFmt(GLint f){
    switch(f){
        case GL_RGB4:case GL_RGB5:case GL_R3_G3_B2:case GL_RGB:case 3:return GL_RGB8;
        case GL_RGBA2:case GL_RGBA4:case GL_RGB5_A1:case GL_RGBA:case 4:return GL_RGBA8;
        case GL_LUMINANCE:case 1:return GL_LUMINANCE8;
        case GL_LUMINANCE_ALPHA:case 2:return GL_LUMINANCE8_ALPHA8;
        case GL_ALPHA:return GL_ALPHA8;
        default:return f;
    }
}

static void WINAPI my_glTexImage2D(GLenum tg,GLint lv,GLint ifmt,GLsizei w,GLsizei h,
    GLint bd,GLenum fmt,GLenum tp,const GLvoid*px){
    if(!real_glTexImage2D)return;

    /* Texture dump / replace — base level only, GL_UNSIGNED_BYTE, supported format */
    if(lv==0 && px && w>0 && h>0 && tp==GL_UNSIGNED_BYTE &&
       (g_config.dump_textures || g_config.replace_textures)) {
        int bpp=TexBPP(fmt);
        if(bpp>0){
            const unsigned char *data=(const unsigned char*)px;
            unsigned int hash=TexHash(w,h,fmt,tp,data,w*h*bpp);

            /* Replace: check cache, load from disk if not seen yet */
            if(g_config.replace_textures){
                TexEntry *e=TexCacheFind(hash);
                if(!e) e=TexCacheLoad(hash,w,h);
                if(e && e->rgba){
                    GLint riFmt=g_config.fix_color_depth?GL_RGBA8:GL_RGBA;
                    real_glTexImage2D(tg,lv,riFmt,e->w,e->h,0,
                        GL_RGBA,GL_UNSIGNED_BYTE,e->rgba);
                    return; /* replacement used — skip original upload */
                }
            }

            /* Dump: write once per unique hash */
            if(g_config.dump_textures && TexMarkSeen(hash)){
                unsigned char *rgba=TexToRGBA(fmt,w,h,data);
                if(rgba){
                    char path[MAX_PATH];
                    snprintf(path,MAX_PATH,
                        "%saitd_textures\\dump\\%08X_%dx%d.tga",
                        g_gameDir,hash,w,h);
                    WriteTGA(path,w,h,rgba);
                    free(rgba);
                    LogMsg("TEX: dump %08X %dx%d fmt=0x%X\n",hash,w,h,fmt);
                }
            }
        }
    }

    /* Normal upload path */
    if(g_config.fix_color_depth){GLint u=UpgradeFmt(ifmt);
        if(u!=ifmt){LogMsg("TexImage2D: 0x%X->0x%X %dx%d\n",ifmt,u,w,h);ifmt=u;}}
    real_glTexImage2D(tg,lv,ifmt,w,h,bd,fmt,tp,px);
}
static void WINAPI my_glTexImage1D(GLenum tg,GLint lv,GLint ifmt,GLsizei w,
    GLint bd,GLenum fmt,GLenum tp,const GLvoid*px){
    if(!real_glTexImage1D)return;
    if(g_config.fix_color_depth){GLint u=UpgradeFmt(ifmt);if(u!=ifmt)ifmt=u;}
    real_glTexImage1D(tg,lv,ifmt,w,bd,fmt,tp,px);
}

/*
 * glTexSubImage2D hook.
 */
static void WINAPI my_glTexSubImage2D(GLenum tg, GLint lv,
    GLint xoff, GLint yoff, GLsizei w, GLsizei h,
    GLenum fmt, GLenum tp, const GLvoid *px)
{
    if(!real_glTexSubImage2D)return;

    if(lv==0 && px && w>0 && h>0 && tp==GL_UNSIGNED_BYTE &&
       (g_config.dump_textures || g_config.replace_textures)) {
        int bpp=TexBPP(fmt);
        if(bpp>0){
            const unsigned char *data=(const unsigned char*)px;
            unsigned int hash=TexHash(w,h,fmt,tp,data,w*h*bpp);

            /* Replace — only when sub-image starts at origin */
            if(g_config.replace_textures && xoff==0 && yoff==0){
                TexEntry *e=TexCacheFind(hash);
                if(!e) e=TexCacheLoad(hash,w,h);
                if(e && e->rgba){
                    real_glTexSubImage2D(tg,lv,xoff,yoff,e->w,e->h,
                        GL_RGBA,GL_UNSIGNED_BYTE,e->rgba);
                    return;
                }
            }

            /* Dump */
            if(g_config.dump_textures && TexMarkSeen(hash)){
                unsigned char *rgba=TexToRGBA(fmt,w,h,data);
                if(rgba){
                    char path[MAX_PATH];
                    snprintf(path,MAX_PATH,
                        "%saitd_textures\\dump\\%08X_%dx%d.tga",
                        g_gameDir,hash,w,h);
                    WriteTGA(path,w,h,rgba);
                    free(rgba);
                    LogMsg("TEX: subimg %08X %dx%d+(%d,%d) fmt=0x%X\n",
                        hash,w,h,xoff,yoff,fmt);
                }
            }
        }
    }

    real_glTexSubImage2D(tg,lv,xoff,yoff,w,h,fmt,tp,px);
}

static int WINAPI my_ChoosePixelFormat(HDC hdc,const PIXELFORMATDESCRIPTOR*ppfd){
    if(!real_ChoosePixelFormat_fn)return 0;
    if(g_config.fix_color_depth && ppfd){
        PIXELFORMATDESCRIPTOR pfd=*ppfd;
        if(pfd.cColorBits<=16){
            LogMsg("ChoosePixelFormat: %d->32 bit\n",pfd.cColorBits);
            pfd.cColorBits=32;pfd.cRedBits=8;pfd.cGreenBits=8;
            pfd.cBlueBits=8;pfd.cAlphaBits=8;pfd.cDepthBits=24;
        }
        /* Always request 8-bit stencil for depth buffer quality */
        if(pfd.cStencilBits<8){ pfd.cStencilBits=8; if(pfd.cDepthBits<24) pfd.cDepthBits=24; }
        return real_ChoosePixelFormat_fn(hdc,&pfd);
    }
    return real_ChoosePixelFormat_fn(hdc,ppfd);
}

/* my_SetPixelFormat removed — MSAA is now via FBO, not pixel format */


/* Upgrade texture filters to bilinear/trilinear for smooth rendering.
 * Combined with GL_CLAMP_TO_EDGE this is seamless on tiled backgrounds. */
static GLint UpgradeFilter(GLenum pn, GLint m){
    if(pn == GL_TEXTURE_MAG_FILTER)
        return (m == GL_NEAREST) ? GL_LINEAR : m;
    /* MIN filter */
    switch(m){
        case GL_NEAREST:
        case GL_LINEAR:
            return GL_LINEAR;
        case GL_NEAREST_MIPMAP_NEAREST:
        case GL_NEAREST_MIPMAP_LINEAR:
        case GL_LINEAR_MIPMAP_NEAREST:
            return GL_LINEAR_MIPMAP_LINEAR;
        default:
            return m;
    }
}
static void WINAPI my_glTexParameteri(GLenum t,GLenum pn,GLint p){
    if(!real_glTexParameteri)return;
    if(g_config.fix_texture_filter&&(pn==GL_TEXTURE_MAG_FILTER||pn==GL_TEXTURE_MIN_FILTER))
        p=UpgradeFilter(pn,p);
    if(g_config.fix_texture_clamp&&(pn==GL_TEXTURE_WRAP_S||pn==GL_TEXTURE_WRAP_T))
        if(p==GL_REPEAT||p==GL_CLAMP) p=GL_CLAMP_TO_EDGE;
    real_glTexParameteri(t,pn,p);
    if(g_config.aniso_level>1 && pn==GL_TEXTURE_MIN_FILTER && real_glTexParameterf)
        real_glTexParameterf(t, GL_TEXTURE_MAX_ANISOTROPY_EXT,
            (GLfloat)g_config.aniso_level);
}
static void WINAPI my_glTexParameterf(GLenum t,GLenum pn,GLfloat p){
    if(!real_glTexParameterf)return;
    if(g_config.fix_texture_filter&&(pn==GL_TEXTURE_MAG_FILTER||pn==GL_TEXTURE_MIN_FILTER))
        p=(GLfloat)UpgradeFilter(pn,(GLint)p);
    if(g_config.fix_texture_clamp&&(pn==GL_TEXTURE_WRAP_S||pn==GL_TEXTURE_WRAP_T))
        if((GLint)p==GL_REPEAT||(GLint)p==GL_CLAMP) p=(GLfloat)GL_CLAMP_TO_EDGE;
    real_glTexParameterf(t,pn,p);
    if(g_config.aniso_level>1 && pn==GL_TEXTURE_MIN_FILTER)
        real_glTexParameterf(t, GL_TEXTURE_MAX_ANISOTROPY_EXT,
            (GLfloat)g_config.aniso_level);
}
static void WINAPI my_glTexParameteriv(GLenum t,GLenum pn,const GLint*ps){
    if(!real_glTexParameteriv||!ps)return;
    GLint p=ps[0];int changed=0;
    if(g_config.fix_texture_filter&&(pn==GL_TEXTURE_MAG_FILTER||pn==GL_TEXTURE_MIN_FILTER))
        {p=UpgradeFilter(pn,p);changed=1;}
    if(g_config.fix_texture_clamp&&(pn==GL_TEXTURE_WRAP_S||pn==GL_TEXTURE_WRAP_T))
        if(p==GL_REPEAT||p==GL_CLAMP){p=GL_CLAMP_TO_EDGE;changed=1;}
    if(changed){real_glTexParameteriv(t,pn,&p);}
    else real_glTexParameteriv(t,pn,ps);
    if(g_config.aniso_level>1 && pn==GL_TEXTURE_MIN_FILTER && real_glTexParameterf)
        real_glTexParameterf(t, GL_TEXTURE_MAX_ANISOTROPY_EXT,
            (GLfloat)g_config.aniso_level);
}
static void WINAPI my_glTexParameterfv(GLenum t,GLenum pn,const GLfloat*ps){
    if(!real_glTexParameterfv||!ps)return;
    GLfloat p=ps[0];int changed=0;
    if(g_config.fix_texture_filter&&(pn==GL_TEXTURE_MAG_FILTER||pn==GL_TEXTURE_MIN_FILTER))
        {p=(GLfloat)UpgradeFilter(pn,(GLint)p);changed=1;}
    if(g_config.fix_texture_clamp&&(pn==GL_TEXTURE_WRAP_S||pn==GL_TEXTURE_WRAP_T))
        if((GLint)p==GL_REPEAT||(GLint)p==GL_CLAMP){p=(GLfloat)GL_CLAMP_TO_EDGE;changed=1;}
    if(changed){real_glTexParameterfv(t,pn,&p);}
    else real_glTexParameterfv(t,pn,ps);
    if(g_config.aniso_level>1 && pn==GL_TEXTURE_MIN_FILTER && real_glTexParameterf)
        real_glTexParameterf(t, GL_TEXTURE_MAX_ANISOTROPY_EXT,
            (GLfloat)g_config.aniso_level);
}

/* MSAA pixel format selection removed — using FBO-based MSAA instead */

/* ---- Gamma boost ---- */
static int  g_gammaApplied = 0;
static WORD g_origGammaRamp[3][256];

static void SaveOriginalGamma(HDC hdc) {
    if (hdc && GetDeviceGammaRamp(hdc, g_origGammaRamp)) {
        g_gammaApplied = 1;
        LogMsg("Gamma: original ramp saved\n");
    }
}

static void ApplyGammaBoost(HDC hdc) {
    if (g_config.gamma_boost < 0.001f || !hdc) return;
    WORD ramp[3][256];
    for (int i = 0; i < 256; i++) {
        float v = powf(i / 255.0f, 1.0f / (1.0f + g_config.gamma_boost));
        WORD w = (WORD)(v * 65535.0f);
        if (w > 65535) w = 65535;
        ramp[0][i] = ramp[1][i] = ramp[2][i] = w;
    }
    if (SetDeviceGammaRamp(hdc, ramp))
        LogMsg("Gamma: boost %.2f applied\n", g_config.gamma_boost);
    else
        LogMsg("Gamma: SetDeviceGammaRamp FAILED\n");
}

static void RestoreOriginalGamma(void) {
    if (!g_gammaApplied) return;
    HDC hdc = GetDC(g_gameHWND ? g_gameHWND : GetDesktopWindow());
    if (hdc) {
        SetDeviceGammaRamp(hdc, g_origGammaRamp);
        ReleaseDC(g_gameHWND ? g_gameHWND : GetDesktopWindow(), hdc);
    }
    LogMsg("Gamma: original ramp restored\n");
}

static BOOL WINAPI my_wglMakeCurrent(HDC hdc, HGLRC hglrc) {
    if (!real_wglMakeCurrent) return FALSE;
    BOOL ok = real_wglMakeCurrent(hdc, hglrc);
    if (ok && hglrc && !g_glContextReady) {
        g_glContextReady = 1;
        /* Acquire extension function pointers now that context is current */
        if (real_wglGetProcAddress) {
            real_wglSwapIntervalEXT = (PFN_wglSwapIntervalEXT)
                real_wglGetProcAddress("wglSwapIntervalEXT");
            if (real_wglSwapIntervalEXT && !g_vsyncSet) {
                real_wglSwapIntervalEXT(g_config.vsync ? 1 : 0);
                g_vsyncSet = 1;
                LogMsg("VSync: wglSwapIntervalEXT(%d) OK\n", g_config.vsync);
            } else if (!real_wglSwapIntervalEXT) {
                LogMsg("VSync: wglSwapIntervalEXT NOT AVAILABLE\n");
            }
        }
        /* Fog coordinate fix for modern drivers */
        if (g_config.fix_fog) {
            if (!real_glFogi && g_hOpenGL)
                real_glFogi = (PFN_glFogi)GetProcAddress(g_hOpenGL, "glFogi");
            if (real_glFogi) {
                real_glFogi(GL_FOG_COORDINATE_SOURCE_EXT, GL_FRAGMENT_DEPTH_EXT);
                LogMsg("Fog: GL_FOG_COORDINATE_SOURCE -> GL_FRAGMENT_DEPTH\n");
            } else {
                LogMsg("Fog: glFogi not found\n");
            }
        }
        /* Gamma boost */
        if (g_config.gamma_boost > 0.001f) {
            SaveOriginalGamma(hdc);
            ApplyGammaBoost(hdc);
        }
        /* MSAA: resolve FBO + multisample functions */
        if (g_config.msaa_samples >= 2) {
            MSAA_ResolveFunctions();
        }
    }
    return ok;
}

/* ==================================================================
 *  SCREENSHOT (F12 -> TGA)
 *
 *  Reads the front buffer after SwapBuffers, crops to the 4:3 area
 *  (skipping black pillarbox/letterbox bars), saves as TGA.
 * ================================================================== */
static void TakeScreenshot(void) {
    if (!real_glReadPixels) return;

    /* Determine capture region: use AR-corrected area if available */
    GLint  cx = g_vpX, cy = g_vpY;
    GLsizei cw = g_vpW, ch = g_vpH;
    if (cw <= 0 || ch <= 0) {
        cw = g_displayW > 0 ? g_displayW : g_screenW;
        ch = g_displayH > 0 ? g_displayH : g_screenH;
        cx = 0; cy = 0;
    }
    if (cw <= 0 || ch <= 0) return;

    unsigned char *pixels = (unsigned char*)malloc(cw * ch * 3);
    if (!pixels) return;

    if (real_glPixelStorei) real_glPixelStorei(GL_PACK_ALIGNMENT, 1);
    real_glReadPixels(cx, cy, cw, ch, GL_RGB, GL_UNSIGNED_BYTE, pixels);

    /* Build filename: aitd_screenshots\shot_NNNN.tga */
    char dir[MAX_PATH], path[MAX_PATH];
    snprintf(dir, MAX_PATH, "%saitd_screenshots", g_gameDir);
    CreateDirectoryA(dir, NULL);
    snprintf(path, MAX_PATH, "%s\\shot_%04d.tga", dir, g_screenshotCounter++);

    /* Write TGA: glReadPixels returns bottom-to-top, flip on write */
    FILE *f = fopen(path, "wb");
    if (f) {
        unsigned char hdr[18] = {0};
        hdr[2] = 2;
        hdr[12] = cw & 0xFF; hdr[13] = (cw >> 8) & 0xFF;
        hdr[14] = ch & 0xFF; hdr[15] = (ch >> 8) & 0xFF;
        hdr[16] = 24;
        hdr[17] = 0x20; /* top-left origin */
        fwrite(hdr, 1, 18, f);
        for (int y = ch - 1; y >= 0; y--) {
            unsigned char *row = pixels + y * cw * 3;
            for (int x = 0; x < cw; x++) {
                fputc(row[x*3+2], f); /* B */
                fputc(row[x*3+1], f); /* G */
                fputc(row[x*3+0], f); /* R */
            }
        }
        fclose(f);
        LogMsg("Screenshot: %s (%dx%d)\n", path, cw, ch);
    }
    free(pixels);
}

/* ==================================================================
 *  ALT+ENTER TOGGLE (fullscreen <-> borderless)
 * ================================================================== */
static void ToggleDisplayMode(void) {
    if (!g_gameHWND || !g_config.allow_alt_enter) return;

    if (g_displayMode == 0) {
        /* Fullscreen -> Borderless: reset CDS, make popup */
        g_savedStyle = GetWindowLongA(g_gameHWND, GWL_STYLE);
        g_savedExStyle = GetWindowLongA(g_gameHWND, GWL_EXSTYLE);
        GetWindowRect(g_gameHWND, &g_savedWindowRect);
        real_ChangeDisplaySettingsA(NULL, 0);
        SetWindowLongA(g_gameHWND, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowLongA(g_gameHWND, GWL_EXSTYLE, 0);
        int sx = GetSystemMetrics(SM_CXSCREEN);
        int sy = GetSystemMetrics(SM_CYSCREEN);
        SetWindowPos(g_gameHWND, HWND_TOP, 0, 0, sx, sy,
            SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        g_displayW = sx; g_displayH = sy;
        g_displayMode = 2;
        LogMsg("Toggle: fullscreen -> borderless %dx%d\n", sx, sy);

    } else if (g_displayMode == 2 && g_origDevModeSaved) {
        /* Borderless -> Fullscreen: restore CDS */
        real_ChangeDisplaySettingsA(&g_origDevMode, CDS_FULLSCREEN);
        g_displayW = g_origDevMode.dmPelsWidth;
        g_displayH = g_origDevMode.dmPelsHeight;
        if (g_savedStyle) {
            SetWindowLongA(g_gameHWND, GWL_STYLE, g_savedStyle);
            SetWindowLongA(g_gameHWND, GWL_EXSTYLE, g_savedExStyle);
            SetWindowPos(g_gameHWND, HWND_TOP,
                g_savedWindowRect.left, g_savedWindowRect.top,
                g_savedWindowRect.right - g_savedWindowRect.left,
                g_savedWindowRect.bottom - g_savedWindowRect.top,
                SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        }
        g_displayMode = 0;
        LogMsg("Toggle: borderless -> fullscreen %dx%d\n", g_displayW, g_displayH);

    } else if (g_displayMode == 1) {
        /* Windowed -> Borderless */
        g_savedStyle = GetWindowLongA(g_gameHWND, GWL_STYLE);
        g_savedExStyle = GetWindowLongA(g_gameHWND, GWL_EXSTYLE);
        GetWindowRect(g_gameHWND, &g_savedWindowRect);
        SetWindowLongA(g_gameHWND, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowLongA(g_gameHWND, GWL_EXSTYLE, 0);
        int sx2 = GetSystemMetrics(SM_CXSCREEN);
        int sy2 = GetSystemMetrics(SM_CYSCREEN);
        SetWindowPos(g_gameHWND, HWND_TOP, 0, 0, sx2, sy2,
            SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        g_displayW = sx2; g_displayH = sy2;
        g_displayMode = 2;
        LogMsg("Toggle: windowed -> borderless %dx%d\n", sx2, sy2);
    }
}


/* ---- SwapBuffers hook: FPS limiter + screenshots ---- */
static BOOL WINAPI my_SwapBuffers(HDC hdc) {
    /* Diagnostic: log first call */
    { static int sbLogged=0; if(!sbLogged){ LogMsg("SwapBuffers: HOOKED (hdc=%p)\n",hdc); sbLogged=1; } }
    /* Screenshot check (with 500ms cooldown) */
    if (g_config.screenshot_key) {
        if (GetAsyncKeyState(g_config.screenshot_key) & 0x8000) {
            DWORD now_tick = GetTickCount();
            if (now_tick - g_lastScreenshotTick > 500) {
                g_lastScreenshotTick = now_tick;
                TakeScreenshot();
            }
        }
    }
    if (g_config.fps_limit > 0 && g_perfFreq.QuadPart > 0) {
        LONGLONG frameTime = g_perfFreq.QuadPart / g_config.fps_limit;
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        LONGLONG elapsed = now.QuadPart - g_lastSwap.QuadPart;
        if (g_lastSwap.QuadPart != 0 && elapsed < frameTime) {
            LONGLONG remaining = frameTime - elapsed;
            DWORD sleepMs = (DWORD)(remaining * 1000 / g_perfFreq.QuadPart);
            if (sleepMs > 2) Sleep(sleepMs - 2);
            do { YieldProcessor(); QueryPerformanceCounter(&now); }
            while (now.QuadPart - g_lastSwap.QuadPart < frameTime);
        }
        QueryPerformanceCounter(&g_lastSwap);
    }
    /* Reset aspect-ratio bars flag so they get cleared next frame */
    g_arBarsCleared = 0;
    /* Update audio volume fades */
    Audio_UpdateFades();
    /* Periodic audio state dump for diagnostics */
    Audio_DumpState();
    /* MSAA: auto-init on first SwapBuffers with a valid viewport */
    if (g_config.msaa_samples >= 2 && !g_msaaReady && pfn_glGenFramebuffers) {
        int baseW = 0, baseH = 0;
        if (g_gameVpW > 0 && g_gameVpH > 0) {
            baseW = g_gameVpW; baseH = g_gameVpH;
        } else if (real_glGetIntegerv) {
            GLint vp[4] = {0};
            real_glGetIntegerv(GL_VIEWPORT_ENUM, vp);
            if (vp[2] > 0 && vp[3] > 0) { baseW = vp[2]; baseH = vp[3]; }
        }
        if (baseW > 0 && baseH > 0)
            MSAA_Init(baseW, baseH);
    }
    /* MSAA: resolve multisampled FBO to screen before swap */
    if (g_msaaReady) MSAA_EndFrame();
    BOOL result = real_SwapBuffers_fn ? real_SwapBuffers_fn(hdc) : FALSE;
    /* MSAA: re-bind FBO for next frame */
    if (g_msaaReady) MSAA_BeginFrame();
    return result;
}

/* ---- Cursor confinement in windowed mode ---- */
static void ClipCursorToGameWindow(void) {
    if (g_gameHWND && (g_config.windowed || g_config.borderless) && g_config.clip_cursor) {
        RECT r;
        GetClientRect(g_gameHWND, &r);
        MapWindowPoints(g_gameHWND, NULL, (POINT*)&r, 2);
        ClipCursor(&r);
    }
}
static void ReleaseCursorClip(void) {
    ClipCursor(NULL);
}

/* ---- Windowed mode ---- */
typedef HWND(WINAPI*PFN_CreateWindowExA)(DWORD,LPCSTR,LPCSTR,DWORD,int,int,int,int,HWND,HMENU,HINSTANCE,LPVOID);
static PFN_CreateWindowExA real_CreateWindowExA = NULL;

/* Subclass proc for cursor confinement on focus changes */
static WNDPROC g_origWndProc = NULL;
static LRESULT CALLBACK GameSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_ACTIVATE:
            if (LOWORD(wParam) != WA_INACTIVE)
                ClipCursorToGameWindow();
            else
                ReleaseCursorClip();
            break;
        case WM_MOVE:
        case WM_SIZE:
            if (GetForegroundWindow() == hwnd)
                ClipCursorToGameWindow();
            break;
        case WM_DESTROY:
            ReleaseCursorClip();
            break;
        case WM_SYSKEYDOWN:
            /* Alt+Enter toggle */
            if (g_config.allow_alt_enter && wParam == VK_RETURN &&
                (lParam & (1 << 29))) { /* Alt bit */
                ToggleDisplayMode();
                return 0; /* consume the message */
            }
            break;
    }
    return CallWindowProcA(g_origWndProc, hwnd, msg, wParam, lParam);
}

static HWND WINAPI Hook_CreateWindowExA(DWORD exStyle, LPCSTR className,
    LPCSTR windowName, DWORD style, int x, int y, int w, int h,
    HWND parent, HMENU menu, HINSTANCE inst, LPVOID param)
{
    if (!parent) {
        if (g_config.borderless) {
            /* Borderless fullscreen: WS_POPUP, full screen size, no CDS */
            LogMsg("CreateWindowEx: intercepted for borderless (style=0x%X, %dx%d)\n", style, w, h);
            style = WS_POPUP | WS_VISIBLE;
            exStyle = 0;
            int sx = GetSystemMetrics(SM_CXSCREEN), sy = GetSystemMetrics(SM_CYSCREEN);
            x = 0; y = 0; w = sx; h = sy;
            LogMsg("CreateWindowEx: borderless %dx%d\n", w, h);
        } else if (g_config.windowed) {
            LogMsg("CreateWindowEx: intercepted (style=0x%X, %dx%d)\n", style, w, h);
            style &= ~(WS_POPUP | WS_MAXIMIZE);
            style |= WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
            exStyle &= ~WS_EX_TOPMOST;
            int cw = g_config.force_width > 0 ? g_config.force_width : w;
            int ch = g_config.force_height > 0 ? g_config.force_height : h;
            RECT rc = {0, 0, cw, ch};
            AdjustWindowRectEx(&rc, style, FALSE, exStyle);
            int ww = rc.right - rc.left, wh = rc.bottom - rc.top;
            int sx = GetSystemMetrics(SM_CXSCREEN), sy = GetSystemMetrics(SM_CYSCREEN);
            x = (sx - ww) / 2; y = (sy - wh) / 2; w = ww; h = wh;
            LogMsg("CreateWindowEx: windowed %dx%d at %d,%d\n", cw, ch, x, y);
        }
    }
    HWND hwnd = real_CreateWindowExA(exStyle, className, windowName, style,
        x, y, w, h, parent, menu, inst, param);
    if (hwnd && !parent) {
        g_gameHWND = hwnd;
        LogMsg("Game window: %p\n", hwnd);
        /* Track display dimensions from window client area if not yet set */
        if (g_displayW == 0 || g_displayH == 0) {
            RECT cr; GetClientRect(hwnd, &cr);
            g_displayW = cr.right - cr.left;
            g_displayH = cr.bottom - cr.top;
            LogMsg("Display: tracked from window %dx%d\n", g_displayW, g_displayH);
        }
        /* Subclass for cursor confinement + Alt+Enter */
        if (g_config.windowed || g_config.borderless || g_config.allow_alt_enter) {
            g_origWndProc = (WNDPROC)SetWindowLongPtrA(hwnd, GWLP_WNDPROC,
                (LONG_PTR)GameSubclassProc);
            if (g_origWndProc)
                LogMsg("Window: subclassed (clip=%d alt_enter=%d)\n",
                    g_config.clip_cursor, g_config.allow_alt_enter);
        }
    }
    return hwnd;
}

static LONG WINAPI Hook_ChangeDisplaySettingsA(DEVMODEA*dm,DWORD fl){
    /* Block display mode changes in windowed, borderless, or runtime-toggled borderless */
    if (g_config.windowed || g_config.borderless || g_displayMode == 2) {
        const char *reason = g_displayMode == 2 ? "borderless-toggle" :
                             g_config.borderless ? "borderless" : "windowed";
        LogMsg("ChangeDisplaySettings: SKIPPED (%s)\n", reason);
        /* In runtime borderless, keep desktop resolution */
        if (g_displayMode == 2) {
            g_displayW = GetSystemMetrics(SM_CXSCREEN);
            g_displayH = GetSystemMetrics(SM_CYSCREEN);
        } else if (dm) {
            if (g_config.force_width > 0 && g_config.force_height > 0) {
                g_displayW = g_config.force_width;
                g_displayH = g_config.force_height;
            } else {
                g_displayW = dm->dmPelsWidth;
                g_displayH = dm->dmPelsHeight;
            }
        }
        if ((g_config.borderless || g_displayMode == 2) && g_displayW == 0) {
            g_displayW = GetSystemMetrics(SM_CXSCREEN);
            g_displayH = GetSystemMetrics(SM_CYSCREEN);
        }
        LogMsg("CDS: display tracked as %dx%d (no mode change)\n", g_displayW, g_displayH);
        /* Save original DEVMODE for later restore even when skipping */
        if (dm && !g_origDevModeSaved) {
            memcpy(&g_origDevMode, dm, sizeof(DEVMODEA));
            g_origDevModeSaved = 1;
            LogMsg("CDS: saved original DEVMODE %lux%lu for restore\n",
                dm->dmPelsWidth, dm->dmPelsHeight);
        }
        return DISP_CHANGE_SUCCESSFUL;
    }
    if(dm){
        if(g_config.fix_color_depth && dm->dmBitsPerPel==16){
            LogMsg("CDS: 16->32 bit\n"); dm->dmBitsPerPel=32;}
        if(g_config.force_width>0 && g_config.force_height>0){
            LogMsg("CDS: %lux%lu->%dx%d\n",dm->dmPelsWidth,dm->dmPelsHeight,g_config.force_width,g_config.force_height);
            dm->dmPelsWidth=g_config.force_width; dm->dmPelsHeight=g_config.force_height;
        }
        /* Forced refresh rate */
        if(g_config.force_refresh_rate > 0){
            LogMsg("CDS: refresh %lu->%d Hz\n", dm->dmDisplayFrequency, g_config.force_refresh_rate);
            dm->dmDisplayFrequency = g_config.force_refresh_rate;
            dm->dmFields |= DM_DISPLAYFREQUENCY;
        }
        /* Track actual display dimensions for aspect ratio correction */
        g_displayW = dm->dmPelsWidth;
        g_displayH = dm->dmPelsHeight;
        LogMsg("CDS: display tracked as %dx%d\n", g_displayW, g_displayH);
        /* Save DEVMODE for Alt+Enter restore */
        if (!g_origDevModeSaved) {
            memcpy(&g_origDevMode, dm, sizeof(DEVMODEA));
            g_origDevModeSaved = 1;
        }
    }
    return real_ChangeDisplaySettingsA(dm,fl);
}

/* ---- Resolve GL functions ---- */
static void ResolveGLFunctions(HMODULE hGL){
    real_glViewport      =(PFN_glViewport)      GetProcAddress(hGL,"glViewport");
    real_glScissor       =(PFN_glScissor)        GetProcAddress(hGL,"glScissor");
    real_glTexParameteri =(PFN_glTexParameteri)  GetProcAddress(hGL,"glTexParameteri");
    real_glTexParameterf =(PFN_glTexParameterf)  GetProcAddress(hGL,"glTexParameterf");
    real_glTexParameteriv=(PFN_glTexParameteriv) GetProcAddress(hGL,"glTexParameteriv");
    real_glTexParameterfv=(PFN_glTexParameterfv) GetProcAddress(hGL,"glTexParameterfv");
    real_glClearColor    =(PFN_glClearColor)     GetProcAddress(hGL,"glClearColor");
    real_glClear         =(PFN_glClear)          GetProcAddress(hGL,"glClear");
    real_glTexImage2D    =(PFN_glTexImage2D)     GetProcAddress(hGL,"glTexImage2D");
    real_glTexImage1D    =(PFN_glTexImage1D)     GetProcAddress(hGL,"glTexImage1D");
    real_glTexSubImage2D =(PFN_glTexSubImage2D)  GetProcAddress(hGL,"glTexSubImage2D");
    real_glEnable        =(PFN_glEnable)         GetProcAddress(hGL,"glEnable");
    real_glHint          =(PFN_glHint)           GetProcAddress(hGL,"glHint");
    real_glFogi          =(PFN_glFogi)           GetProcAddress(hGL,"glFogi");
    real_glReadPixels    =(PFN_glReadPixels)     GetProcAddress(hGL,"glReadPixels");
    real_glGetIntegerv   =(PFN_glGetIntegerv)    GetProcAddress(hGL,"glGetIntegerv");
    real_glGetError      =(PFN_glGetError)       GetProcAddress(hGL,"glGetError");
    real_glPixelStorei   =(PFN_glPixelStorei)    GetProcAddress(hGL,"glPixelStorei");
    real_glDisable       =(PFN_glDisable)        GetProcAddress(hGL,"glDisable");
    real_glBlendFunc     =(PFN_glBlendFunc)      GetProcAddress(hGL,"glBlendFunc");
    real_glAlphaFunc     =(PFN_glAlphaFunc)      GetProcAddress(hGL,"glAlphaFunc");
    real_wglCreateContext=(PFN_wglCreateContext)  GetProcAddress(hGL,"wglCreateContext");
    real_wglMakeCurrent  =(PFN_wglMakeCurrent)   GetProcAddress(hGL,"wglMakeCurrent");
    real_wglDeleteContext=(PFN_wglDeleteContext)  GetProcAddress(hGL,"wglDeleteContext");
    real_wglGetProcAddress=(PFN_wglGetProcAddress)GetProcAddress(hGL,"wglGetProcAddress");
    LogMsg("ResolveGL: VP=%p Scis=%p TexImg2D=%p MakeCur=%p wglGPA=%p\n",
        (void*)real_glViewport, (void*)real_glScissor,
        (void*)real_glTexImage2D, (void*)real_wglMakeCurrent,
        (void*)real_wglGetProcAddress);
}

/* ---- Hook: LoadLibraryA ---- */
typedef HMODULE(WINAPI*PFN_LoadLibraryA)(LPCSTR);
static PFN_LoadLibraryA real_LoadLibraryA=NULL;
static int StrContainsI(const char*h,const char*n){
    if(!h||!n)return 0; size_t hl=strlen(h),nl=strlen(n);
    if(nl>hl)return 0;
    for(size_t i=0;i<=hl-nl;i++){size_t j;
        for(j=0;j<nl;j++){char a=h[i+j],b=n[j];
            if(a>='A'&&a<='Z')a+=32;if(b>='A'&&b<='Z')b+=32;if(a!=b)break;}
        if(j==nl)return 1;} return 0;
}
static HMODULE WINAPI Hook_LoadLibraryA(LPCSTR fn){
    HMODULE h=real_LoadLibraryA(fn);
    if(h&&fn){
        if(StrContainsI(fn,"opengl32")){
            g_hOpenGL=h; ResolveGLFunctions(h);
            LogMsg(">>> opengl32.dll loaded: %p\n",h);


            /* Install inline hooks (detours) on functions the game calls
             * directly through the ICD dispatch, bypassing GetProcAddress. */
            if (g_config.fix_aspect_ratio && real_glViewport) {
                void *tramp = DetourInstall(&g_detourViewport,
                    (void*)real_glViewport, (void*)my_glViewport, "glViewport");
                if (tramp) real_glViewport = (PFN_glViewport)tramp;
            }
            if (g_config.fix_aspect_ratio && real_glScissor) {
                void *tramp = DetourInstall(&g_detourScissor,
                    (void*)real_glScissor, (void*)my_glScissor, "glScissor");
                if (tramp) real_glScissor = (PFN_glScissor)tramp;
            }
        }
        /* Detect mss32.dll (Miles Sound System) and install audio hooks */
        if(StrContainsI(fn,"mss32") && !g_hMSS){
            g_hMSS = h;
            LogMsg(">>> mss32.dll loaded: %p\n", h);
            Audio_ResolveFunctions(h);
            Audio_InstallHooks();
            /* Audio_v19_Init moved into my_AIL_startup — called after MSS
             * has fully initialized itself (avoids waveOut conflict). */
        }
    }
    return h;
}

/* ---- Hook: GetProcAddress ---- */
typedef FARPROC(WINAPI*PFN_GetProcAddress)(HMODULE,LPCSTR);
static PFN_GetProcAddress real_GetProcAddress_ptr=NULL;

typedef struct{const char*name;void*hook;void**real;}HookEntry;
static HookEntry g_glHooks[]={
    {"glViewport",      (void*)my_glViewport,      (void**)&real_glViewport},
    {"glScissor",       (void*)my_glScissor,        (void**)&real_glScissor},
    {"glTexParameteri", (void*)my_glTexParameteri,  (void**)&real_glTexParameteri},
    {"glTexParameterf", (void*)my_glTexParameterf,  (void**)&real_glTexParameterf},
    {"glTexParameteriv",(void*)my_glTexParameteriv, (void**)&real_glTexParameteriv},
    {"glTexParameterfv",(void*)my_glTexParameterfv, (void**)&real_glTexParameterfv},
    {"glTexImage2D",    (void*)my_glTexImage2D,     (void**)&real_glTexImage2D},
    {"glTexImage1D",    (void*)my_glTexImage1D,     (void**)&real_glTexImage1D},
    {"glTexSubImage2D", (void*)my_glTexSubImage2D,  (void**)&real_glTexSubImage2D},
    {"wglMakeCurrent",  (void*)my_wglMakeCurrent,   (void**)&real_wglMakeCurrent},
    {"SwapBuffers",     (void*)my_SwapBuffers,       (void**)&real_SwapBuffers_fn},
    {"wglSwapBuffers",  (void*)my_SwapBuffers,       (void**)&real_SwapBuffers_fn},
    {NULL,NULL,NULL}
};

/*
 * wglGetProcAddress hook — critical for games that resolve GL functions
 * through wglGetProcAddress instead of GetProcAddress.
 * Without this, all GL hooks (viewport, textures, swap) are bypassed.
 */
static PROC WINAPI my_wglGetProcAddress(LPCSTR name) {
    if (name) {
        /* Check our hook table first */
        for (int i = 0; g_glHooks[i].name; i++) {
            if (strcmp(name, g_glHooks[i].name) == 0) {
                PROC r = real_wglGetProcAddress(name);
                if (r && g_glHooks[i].real && !(*g_glHooks[i].real))
                    *g_glHooks[i].real = (void*)r;
                LogMsg("wglGetProcAddress(\"%s\") -> HOOKED\n", name);
                return (PROC)g_glHooks[i].hook;
            }
        }
        /* Also intercept ChoosePixelFormat variants */
        if (g_config.fix_color_depth) {
            if (strcmp(name, "wglChoosePixelFormat") == 0 || strcmp(name, "ChoosePixelFormat") == 0) {
                PROC r = real_wglGetProcAddress(name);
                if (r && !real_ChoosePixelFormat_fn)
                    real_ChoosePixelFormat_fn = (PFN_ChoosePixelFormat)r;
                return (PROC)my_ChoosePixelFormat;
            }
        }
    }
    return real_wglGetProcAddress(name);
}

static FARPROC WINAPI Hook_GetProcAddress(HMODULE hM,LPCSTR nm){
    if(!nm) return real_GetProcAddress_ptr(hM,nm);

    /* Diagnostic: log ALL GL-related GetProcAddress calls */
    if((nm[0]=='g' && nm[1]=='l') || (nm[0]=='w' && nm[1]=='g' && nm[2]=='l') ||
       (nm[0]=='S' && strcmp(nm,"SwapBuffers")==0) ||
       (nm[0]=='C' && strcmp(nm,"ChoosePixelFormat")==0) ||
       (nm[0]=='S' && strcmp(nm,"SetPixelFormat")==0)) {
        LogMsg("GPA: mod=%p (opengl=%p match=%d) func=\"%s\"\n",
            hM, g_hOpenGL, (hM==g_hOpenGL), nm);
    }

    if(hM && hM==g_hOpenGL){
        for(int i=0;g_glHooks[i].name;i++){
            if(strcmp(nm,g_glHooks[i].name)==0){
                FARPROC r=real_GetProcAddress_ptr(hM,nm);
                if(r&&g_glHooks[i].real&&!(*g_glHooks[i].real)) *g_glHooks[i].real=(void*)r;
                LogMsg("GPA-HOOK: \"%s\" real=%p hook=%p stored=%p\n",
                    nm, (void*)r, g_glHooks[i].hook,
                    g_glHooks[i].real ? *g_glHooks[i].real : NULL);
                return(FARPROC)g_glHooks[i].hook;
            }
        }
        if(g_config.fix_color_depth &&
           (strcmp(nm,"wglChoosePixelFormat")==0||strcmp(nm,"ChoosePixelFormat")==0)){
            FARPROC r=real_GetProcAddress_ptr(hM,nm);
            if(r&&!real_ChoosePixelFormat_fn) real_ChoosePixelFormat_fn=(PFN_ChoosePixelFormat)r;
            return(FARPROC)my_ChoosePixelFormat;
        }
        if(strcmp(nm,"wglCreateContext")==0){
            FARPROC r=real_GetProcAddress_ptr(hM,nm);
            if(r&&!real_wglCreateContext) real_wglCreateContext=(PFN_wglCreateContext)r;
            return r;
        }
        if(strcmp(nm,"wglDeleteContext")==0){
            FARPROC r=real_GetProcAddress_ptr(hM,nm);
            if(r&&!real_wglDeleteContext) real_wglDeleteContext=(PFN_wglDeleteContext)r;
            return r;
        }
        if(strcmp(nm,"wglGetProcAddress")==0){
            FARPROC r=real_GetProcAddress_ptr(hM,nm);
            if(r&&!real_wglGetProcAddress) real_wglGetProcAddress=(PFN_wglGetProcAddress)r;
            return (FARPROC)my_wglGetProcAddress;
        }
    }
    /* GDI32 interception */
    if(hM){
        char mn[MAX_PATH]; mn[0]=0;
        if(GetModuleFileNameA(hM,mn,MAX_PATH)&&StrContainsI(mn,"gdi32")){
            if(strcmp(nm,"ChoosePixelFormat")==0){
                FARPROC r=real_GetProcAddress_ptr(hM,nm);
                if(r&&!real_ChoosePixelFormat_fn) real_ChoosePixelFormat_fn=(PFN_ChoosePixelFormat)r;
                return(FARPROC)my_ChoosePixelFormat;
            }
            if(strcmp(nm,"SwapBuffers")==0){
                FARPROC r=real_GetProcAddress_ptr(hM,nm);
                if(r&&!real_SwapBuffers_fn) real_SwapBuffers_fn=(PFN_SwapBuffers_t)r;
                return(FARPROC)my_SwapBuffers;
            }
        }
        if(hM==g_hOpenGL && (strcmp(nm,"wglChoosePixelFormat")==0||strcmp(nm,"ChoosePixelFormat")==0)){
            FARPROC r=real_GetProcAddress_ptr(hM,nm);
            if(r&&!real_ChoosePixelFormat_fn) real_ChoosePixelFormat_fn=(PFN_ChoosePixelFormat)r;
            return(FARPROC)my_ChoosePixelFormat;
        }
    }
    return real_GetProcAddress_ptr(hM,nm);
}

/* ---- Registry hook ---- */
typedef LONG(WINAPI*PFN_RegQueryValueExA)(HKEY,LPCSTR,LPDWORD,LPDWORD,LPBYTE,LPDWORD);
static PFN_RegQueryValueExA real_RegQueryValueExA=NULL;

static int IsResWidthKey(const char*name){
    if(!name)return 0;
    return (_stricmp(name,"ScreenWidth")==0 || _stricmp(name,"Width")==0 ||
            _stricmp(name,"ResX")==0 || _stricmp(name,"ResolutionWidth")==0 ||
            _stricmp(name,"XRes")==0 || _stricmp(name,"DisplayWidth")==0 ||
            _stricmp(name,"ScreenX")==0);
}
static int IsResHeightKey(const char*name){
    if(!name)return 0;
    return (_stricmp(name,"ScreenHeight")==0 || _stricmp(name,"Height")==0 ||
            _stricmp(name,"ResY")==0 || _stricmp(name,"ResolutionHeight")==0 ||
            _stricmp(name,"YRes")==0 || _stricmp(name,"DisplayHeight")==0 ||
            _stricmp(name,"ScreenY")==0);
}
static int IsColorDepthKey(const char*name){
    if(!name)return 0;
    return (_stricmp(name,"BitDepth")==0 || _stricmp(name,"BitsPerPixel")==0 ||
            _stricmp(name,"ColorDepth")==0 || _stricmp(name,"Depth")==0 ||
            _stricmp(name,"BPP")==0);
}

static LONG WINAPI Hook_RegQueryValueExA(HKEY hKey,LPCSTR lpValueName,
    LPDWORD lpReserved,LPDWORD lpType,LPBYTE lpData,LPDWORD lpcbData)
{
    LONG result = real_RegQueryValueExA(hKey,lpValueName,lpReserved,lpType,lpData,lpcbData);
    if(result==ERROR_SUCCESS && lpValueName && lpData && lpcbData && *lpcbData==sizeof(DWORD)){
        DWORD val = *(DWORD*)lpData;
        LogMsg("RegQuery: \"%s\" = %lu\n", lpValueName, val);
        /* VOLDIAG: flag volume-related keys */
        if (_stricmp(lpValueName,"VolumeAmbiance")==0 || _stricmp(lpValueName,"VolumeBruitages")==0 ||
            _stricmp(lpValueName,"VolumeDialogue")==0 || _stricmp(lpValueName,"VolumeMusique")==0 ||
            _stricmp(lpValueName,"VolumeGlobal")==0 || _strnicmp(lpValueName,"Volume",6)==0 ||
            _strnicmp(lpValueName,"Vol",3)==0 || _strnicmp(lpValueName,"Sound",5)==0 ||
            _strnicmp(lpValueName,"Audio",5)==0 || _strnicmp(lpValueName,"Music",5)==0) {
            LogMsg("VOLDIAG: RegRead \"%s\" = %lu  <<<< VOLUME KEY\n", lpValueName, val);
        }
        if(g_config.force_width>0 && IsResWidthKey(lpValueName)){
            *(DWORD*)lpData = g_config.force_width;
            LogMsg("  -> overridden to %d\n", g_config.force_width);
        }
        if(g_config.force_height>0 && IsResHeightKey(lpValueName)){
            *(DWORD*)lpData = g_config.force_height;
            LogMsg("  -> overridden to %d\n", g_config.force_height);
        }
        if(g_config.fix_color_depth && IsColorDepthKey(lpValueName) && val<=16){
            *(DWORD*)lpData = 32;
            LogMsg("  -> overridden to 32\n");
        }
    }
    return result;
}

/* ---- Registry write hook (diagnostics: catch volume slider saves) ---- */
typedef LONG(WINAPI*PFN_RegSetValueExA)(HKEY,LPCSTR,DWORD,DWORD,const BYTE*,DWORD);
static PFN_RegSetValueExA real_RegSetValueExA=NULL;

static LONG WINAPI Hook_RegSetValueExA(HKEY hKey, LPCSTR lpValueName,
    DWORD Reserved, DWORD dwType, const BYTE *lpData, DWORD cbData)
{
    /* Log all DWORD writes — catches volume sliders, resolution changes, etc. */
    if (lpValueName && lpData && dwType == REG_DWORD && cbData == sizeof(DWORD)) {
        DWORD val = *(const DWORD*)lpData;
        LogMsg("VOLDIAG: RegSet \"%s\" = %lu\n", lpValueName, val);
    }
    return real_RegSetValueExA(hKey, lpValueName, Reserved, dwType, lpData, cbData);
}

/* ---- IAT patching ---- */
static BOOL PatchIAT(HMODULE hM,const char*dll,const char*func,void*hook,void**orig){
    if(!hM)hM=GetModuleHandleA(NULL);
    PIMAGE_DOS_HEADER dos=(PIMAGE_DOS_HEADER)hM;
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE)return FALSE;
    PIMAGE_NT_HEADERS nt=(PIMAGE_NT_HEADERS)((BYTE*)hM+dos->e_lfanew);
    if(nt->Signature!=IMAGE_NT_SIGNATURE)return FALSE;
    DWORD rva=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    if(!rva)return FALSE;
    PIMAGE_IMPORT_DESCRIPTOR imp=(PIMAGE_IMPORT_DESCRIPTOR)((BYTE*)hM+rva);
    for(;imp->Name;imp++){
        if(_stricmp((char*)((BYTE*)hM+imp->Name),dll)!=0)continue;
        PIMAGE_THUNK_DATA ot=(PIMAGE_THUNK_DATA)((BYTE*)hM+imp->OriginalFirstThunk);
        PIMAGE_THUNK_DATA ft=(PIMAGE_THUNK_DATA)((BYTE*)hM+imp->FirstThunk);
        for(;ot->u1.AddressOfData;ot++,ft++){
            if(ot->u1.Ordinal&IMAGE_ORDINAL_FLAG)continue;
            PIMAGE_IMPORT_BY_NAME n=(PIMAGE_IMPORT_BY_NAME)((BYTE*)hM+ot->u1.AddressOfData);
            if(strcmp((char*)n->Name,func)!=0)continue;
            DWORD op;
            if(VirtualProtect(&ft->u1.Function,sizeof(void*),PAGE_READWRITE,&op)){
                if(orig)*orig=(void*)ft->u1.Function;
                ft->u1.Function=(ULONG_PTR)hook;
                VirtualProtect(&ft->u1.Function,sizeof(void*),op,&op);
                LogMsg("IAT: %s!%s\n",dll,func);return TRUE;}
        }
    }
    LogMsg("IAT miss: %s!%s\n",dll,func);return FALSE;
}

static void LoadConfig(void) {
    char p[MAX_PATH]; snprintf(p,MAX_PATH,"%saitd_wrapper.ini",g_gameDir);
    g_config.fix_aspect_ratio   = GetPrivateProfileIntA("Fixes","FixAspectRatio",1,p);
    g_config.fix_color_depth    = GetPrivateProfileIntA("Fixes","FixColorDepth",1,p);
    g_config.fix_texture_filter = GetPrivateProfileIntA("Fixes","FixTextureFilter",1,p);
    g_config.fix_texture_clamp  = GetPrivateProfileIntA("Fixes","FixTextureClamp",1,p);
    g_config.windowed           = GetPrivateProfileIntA("Display","Windowed",0,p);
    g_config.force_width        = GetPrivateProfileIntA("Display","ForceWidth",0,p);
    g_config.force_height       = GetPrivateProfileIntA("Display","ForceHeight",0,p);
    g_config.aniso_level        = GetPrivateProfileIntA("Fixes","AnisoLevel",16,p);
    g_config.dump_textures      = GetPrivateProfileIntA("Textures","DumpTextures",0,p);
    g_config.replace_textures   = GetPrivateProfileIntA("Textures","ReplaceTextures",0,p);
    g_config.vsync              = GetPrivateProfileIntA("Display","VSync",1,p);
    g_config.fps_limit          = GetPrivateProfileIntA("Display","FPSLimit",0,p);
    g_config.enable_log         = GetPrivateProfileIntA("Debug","EnableLog",1,p);
    g_config.show_diagnostics   = GetPrivateProfileIntA("Debug","ShowDiagnostics",0,p);
    /* v14 additions */
    g_config.single_core        = GetPrivateProfileIntA("Compatibility","SingleCore",0,p);
    g_config.fix_fog            = GetPrivateProfileIntA("Fixes","FixFog",1,p);
    g_config.clip_cursor        = GetPrivateProfileIntA("Display","ClipCursor",1,p);
    /* v15 additions */
    g_config.borderless         = GetPrivateProfileIntA("Display","Borderless",0,p);
    g_config.force_refresh_rate = GetPrivateProfileIntA("Display","ForceRefreshRate",0,p);
    g_config.allow_alt_enter    = GetPrivateProfileIntA("Display","AllowAltEnter",1,p);
    g_config.screenshot_key     = 0x7B; /* VK_F12 default */
    {
        char skbuf[32] = {0};
        GetPrivateProfileStringA("Features","ScreenshotKey","0x7B",skbuf,sizeof(skbuf),p);
        g_config.screenshot_key = (int)strtol(skbuf, NULL, 0); /* auto-detect hex/dec */
    }
    {
        char buf[32] = {0};
        GetPrivateProfileStringA("Display","GammaBoost","0",buf,sizeof(buf),p);
        g_config.gamma_boost = (float)atof(buf);
        if (g_config.gamma_boost < 0.0f) g_config.gamma_boost = 0.0f;
        if (g_config.gamma_boost > 2.0f) g_config.gamma_boost = 2.0f;
    }
    g_config.msaa_samples       = GetPrivateProfileIntA("Rendering","MSAASamples",4,p);
    /* v17 additions: audio */
    g_config.fix_audio_quality  = GetPrivateProfileIntA("Audio","FixAudioQuality",1,p);
    g_config.preserve_loops     = GetPrivateProfileIntA("Audio","PreserveLoops",0,p);
    g_config.fade_in_ms         = GetPrivateProfileIntA("Audio","FadeInMs",100,p);
    if (g_config.fade_in_ms < 0) g_config.fade_in_ms = 0;
    if (g_config.fade_in_ms > 2000) g_config.fade_in_ms = 2000;
    g_config.fade_out_ms        = GetPrivateProfileIntA("Audio","FadeOutMs",300,p);
    if (g_config.fade_out_ms < 0) g_config.fade_out_ms = 0;
    if (g_config.fade_out_ms > 2000) g_config.fade_out_ms = 2000;
    g_config.sample_volume_boost = GetPrivateProfileIntA("Audio","SampleVolumeBoost",0,p);
    g_config.enable_custom_mixer = GetPrivateProfileIntA("Audio","EnableCustomMixer",0,p);
    if (g_config.sample_volume_boost < 0) g_config.sample_volume_boost = 0;
    if (g_config.sample_volume_boost > 127) g_config.sample_volume_boost = 127;
    if (g_config.enable_log) {
        char lp[MAX_PATH]; snprintf(lp,MAX_PATH,"%saitd_wrapper.log",g_gameDir);
        g_logFile=fopen(lp,"w");
        if (g_logFile) setvbuf(g_logFile, NULL, _IONBF, 0);
        LogMsg("=== AITD Wrapper v20 (hash-map-cache) ===\n");
        LogMsg("AR=%d CD=%d TF=%d TC=%d Aniso=%d Fog=%d Dump=%d Repl=%d\n",
            g_config.fix_aspect_ratio, g_config.fix_color_depth,
            g_config.fix_texture_filter, g_config.fix_texture_clamp,
            g_config.aniso_level, g_config.fix_fog,
            g_config.dump_textures, g_config.replace_textures);
        LogMsg("Win=%d BL=%d VS=%d FPS=%d Res=%dx%d Clip=%d Gamma=%.2f Core1=%d\n",
            g_config.windowed, g_config.borderless,
            g_config.vsync, g_config.fps_limit,
            g_config.force_width, g_config.force_height,
            g_config.clip_cursor, g_config.gamma_boost,
            g_config.single_core);
        LogMsg("RefreshRate=%d AltEnter=%d ScreenshotKey=0x%02X\n",
            g_config.force_refresh_rate, g_config.allow_alt_enter,
            g_config.screenshot_key);
        LogMsg("MSAA=%dx\n", g_config.msaa_samples);
        LogMsg("AudioQuality=%d PreserveLoops=%d FadeIn=%dms FadeOut=%dms Boost=%d Mixer=%d\n",
            g_config.fix_audio_quality, g_config.preserve_loops,
            g_config.fade_in_ms, g_config.fade_out_ms, g_config.sample_volume_boost,
            g_config.enable_custom_mixer);
    }
    /* v19/v20 additions — read new keys + log what we got */
    LoadConfig_v19(p);
}

static void InstallAllHooks(void){
    HMODULE hExe=GetModuleHandleA(NULL);
    PatchIAT(hExe,"KERNEL32.dll","LoadLibraryA",Hook_LoadLibraryA,(void**)&real_LoadLibraryA);
    PatchIAT(hExe,"KERNEL32.dll","GetProcAddress",Hook_GetProcAddress,(void**)&real_GetProcAddress_ptr);
    if(!real_GetProcAddress_ptr)
        real_GetProcAddress_ptr=(PFN_GetProcAddress)GetProcAddress(GetModuleHandleA("kernel32.dll"),"GetProcAddress");
    PatchIAT(hExe,"USER32.dll","ChangeDisplaySettingsA",Hook_ChangeDisplaySettingsA,(void**)&real_ChangeDisplaySettingsA);
    if(!real_ChangeDisplaySettingsA)
        real_ChangeDisplaySettingsA=(PFN_ChangeDisplaySettingsA)GetProcAddress(GetModuleHandleA("user32.dll"),"ChangeDisplaySettingsA");
    /* Always hook CreateWindowExA — needed for windowed, borderless, and Alt+Enter */
    PatchIAT(hExe,"USER32.dll","CreateWindowExA",Hook_CreateWindowExA,(void**)&real_CreateWindowExA);
    if(!real_CreateWindowExA)
        real_CreateWindowExA=(PFN_CreateWindowExA)GetProcAddress(GetModuleHandleA("user32.dll"),"CreateWindowExA");
    PatchIAT(hExe,"ADVAPI32.dll","RegQueryValueExA",Hook_RegQueryValueExA,(void**)&real_RegQueryValueExA);
    if(!real_RegQueryValueExA)
        real_RegQueryValueExA=(PFN_RegQueryValueExA)GetProcAddress(GetModuleHandleA("advapi32.dll"),"RegQueryValueExA");
    PatchIAT(hExe,"ADVAPI32.dll","RegSetValueExA",Hook_RegSetValueExA,(void**)&real_RegSetValueExA);
    if(!real_RegSetValueExA)
        real_RegSetValueExA=(PFN_RegSetValueExA)GetProcAddress(GetModuleHandleA("advapi32.dll"),"RegSetValueExA");
    real_ChoosePixelFormat_fn=(PFN_ChoosePixelFormat)GetProcAddress(GetModuleHandleA("gdi32.dll"),"ChoosePixelFormat");
    real_SetPixelFormat_fn=(PFN_SetPixelFormat)GetProcAddress(GetModuleHandleA("gdi32.dll"),"SetPixelFormat");

    /* IAT-patch ChoosePixelFormat for color depth upgrade */
    if (g_config.fix_color_depth) {
        PatchIAT(hExe,"GDI32.dll","ChoosePixelFormat",my_ChoosePixelFormat,(void**)&real_ChoosePixelFormat_fn);
        if (!real_ChoosePixelFormat_fn)
            real_ChoosePixelFormat_fn=(PFN_ChoosePixelFormat)GetProcAddress(GetModuleHandleA("gdi32.dll"),"ChoosePixelFormat");
    }
    PatchIAT(hExe,"GDI32.dll","SwapBuffers",my_SwapBuffers,(void**)&real_SwapBuffers_fn);
    if(!real_SwapBuffers_fn)
        real_SwapBuffers_fn=(PFN_SwapBuffers_t)GetProcAddress(GetModuleHandleA("gdi32.dll"),"SwapBuffers");

    if(g_config.dump_textures || g_config.replace_textures)
        TexEnsureDirs();

    if (g_config.fps_limit > 0) {
        QueryPerformanceFrequency(&g_perfFreq);
        timeBeginPeriod(1);
        LogMsg("FPS limit: %d (freq=%lld Hz)\n",
            g_config.fps_limit, g_perfFreq.QuadPart);
    }

    /* Single-core affinity for old game timing compatibility */
    if (g_config.single_core) {
        SetProcessAffinityMask(GetCurrentProcess(), 1);
        LogMsg("CPU: forced single-core affinity\n");
    }

    /* Check if mss32.dll is already loaded (static import) */
    if (!g_hMSS) {
        HMODULE hMSS = GetModuleHandleA("mss32.dll");
        if (!hMSS) hMSS = GetModuleHandleA("Mss32.dll");
        if (!hMSS) hMSS = GetModuleHandleA("MSS32.dll");
        if (hMSS) {
            g_hMSS = hMSS;
            LogMsg(">>> mss32.dll found (static): %p\n", hMSS);
            Audio_ResolveFunctions(hMSS);
            Audio_InstallHooks();
            /* Audio_v19_Init is now called from my_AIL_startup after MSS
             * has fully initialized itself (avoids waveOut conflict). */
        }
    }
}

/* ---- Crash diagnostics: Vectored Exception Handler ---- */
static volatile LONG g_crashGuard = 0;
static LONG WINAPI AE_CrashHandler(EXCEPTION_POINTERS *ep) {
    if (g_logFile && ep && ep->ExceptionRecord) {
        DWORD code = ep->ExceptionRecord->ExceptionCode;
        if (code == 0xC0000005 || code == 0xC0000094 || code == 0xC0000096 ||
            code == 0xC00000FD || code == 0x80000003) {
            /* Interlock: only one thread writes crash info at a time */
            if (InterlockedCompareExchange(&g_crashGuard, 1, 0) != 0)
                return EXCEPTION_CONTINUE_SEARCH;
            {
                void *addr = ep->ExceptionRecord->ExceptionAddress;
                HMODULE hExe = GetModuleHandleA(NULL);
                /* Write to both main log and separate crash file for safety */
                FILE *cf = NULL;
                { char cp[MAX_PATH]; snprintf(cp,MAX_PATH,"%saitd_crash.log",g_gameDir); cf=fopen(cp,"a"); }
                FILE *targets[2] = { g_logFile, cf };
                int t;
                for (t = 0; t < 2; t++) {
                    FILE *f = targets[t];
                    if (!f) continue;
                    fprintf(f,
                        "!!! EXCEPTION 0x%08X at %p (exe+0x%X proxy+0x%X)\n",
                        code, addr,
                        (unsigned int)((char*)addr - (char*)hExe),
                        (unsigned int)((char*)addr - (char*)GetModuleHandleA("dinput8.dll")));
                    if (code == 0xC0000005 && ep->ExceptionRecord->NumberParameters >= 2) {
                        fprintf(f, "    Access: %s at %p\n",
                            ep->ExceptionRecord->ExceptionInformation[0] ? "WRITE" : "READ",
                            (void*)ep->ExceptionRecord->ExceptionInformation[1]);
                    }
                    if (ep->ContextRecord) {
#ifdef _M_IX86
                        fprintf(f, "    EAX=%08X EBX=%08X ECX=%08X EDX=%08X\n",
                            (unsigned)ep->ContextRecord->Eax, (unsigned)ep->ContextRecord->Ebx,
                            (unsigned)ep->ContextRecord->Ecx, (unsigned)ep->ContextRecord->Edx);
                        fprintf(f, "    ESP=%08X EBP=%08X ESI=%08X EDI=%08X\n",
                            (unsigned)ep->ContextRecord->Esp, (unsigned)ep->ContextRecord->Ebp,
                            (unsigned)ep->ContextRecord->Esi, (unsigned)ep->ContextRecord->Edi);
#endif
                    }
                    fflush(f);
                }
                if (cf) fclose(cf);
            }
            InterlockedExchange(&g_crashGuard, 0);
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

/* ---- DPI Awareness ---- */
static void SetDPIAware(void) {
    HMODULE hUser = GetModuleHandleA("user32.dll");
    if (!hUser) return;
    /* Try SetProcessDpiAwarenessContext (Win10 1703+) */
    typedef BOOL(WINAPI*PFN_SetProcessDpiAwarenessContext)(HANDLE);
    PFN_SetProcessDpiAwarenessContext fn =
        (PFN_SetProcessDpiAwarenessContext)GetProcAddress(hUser, "SetProcessDpiAwarenessContext");
    if (fn) {
        /* DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE = (HANDLE)-3 */
        if (fn((HANDLE)(LONG_PTR)-3)) {
            LogMsg("DPI: PerMonitorAware via SetProcessDpiAwarenessContext\n");
            return;
        }
    }
    /* Fallback: SetProcessDPIAware (Vista+) */
    typedef BOOL(WINAPI*PFN_SetProcessDPIAware)(void);
    PFN_SetProcessDPIAware fn2 =
        (PFN_SetProcessDPIAware)GetProcAddress(hUser, "SetProcessDPIAware");
    if (fn2 && fn2()) {
        LogMsg("DPI: aware via SetProcessDPIAware\n");
        return;
    }
    LogMsg("DPI: awareness not set (old OS?)\n");
}

/* ---- DllMain ---- */
BOOL WINAPI DllMain(HINSTANCE hDll,DWORD reason,LPVOID reserved){
    (void)reserved;
    if(reason==DLL_PROCESS_ATTACH){
        DisableThreadLibraryCalls(hDll);
        GetModuleFileNameA((HMODULE)hDll,g_gameDir,MAX_PATH);
        char*s=strrchr(g_gameDir,'\\');if(s)*(s+1)='\0';
        WriteDiag("=== AITD Wrapper v20 ===");
        /* DPI awareness before anything else */
        SetDPIAware();
        LoadConfig(); WriteDiag("Config OK");
        /* Set initial display mode */
        if (g_config.borderless) g_displayMode = 2;
        else if (g_config.windowed) g_displayMode = 1;
        else g_displayMode = 0;
        /* Register crash handler for diagnostics */
        AddVectoredExceptionHandler(1, AE_CrashHandler);
        if(g_config.show_diagnostics){
            char m[512];
            snprintf(m,sizeof(m),"AITD Wrapper v20\n\nAR=%d CD=%d TF=%d TC=%d Fog=%d\n"
                "Windowed=%d Borderless=%d Clip=%d\n"
                "Res=%dx%d %s Refresh=%d\n"
                "Gamma=%.2f SingleCore=%d AltEnter=%d\nScreenshot=0x%02X",
                g_config.fix_aspect_ratio,g_config.fix_color_depth,
                g_config.fix_texture_filter,g_config.fix_texture_clamp,
                g_config.fix_fog,
                g_config.windowed,g_config.borderless,g_config.clip_cursor,
                g_config.force_width,g_config.force_height,
                g_config.force_width>0?"(forced)":"(default)",
                g_config.force_refresh_rate,
                g_config.gamma_boost,g_config.single_core,
                g_config.allow_alt_enter,g_config.screenshot_key);
            MessageBoxA(NULL,m,"AITD Wrapper",MB_OK);
        }
        {char sp[MAX_PATH],sd[MAX_PATH];GetSystemDirectoryA(sd,MAX_PATH);snprintf(sp,MAX_PATH,"%s\\dinput8.dll",sd);
         g_hRealDInput8=LoadLibraryA(sp);
         if(!g_hRealDInput8){WriteDiag("FATAL");return FALSE;}
         real_DirectInput8Create=(PFN_DirectInput8Create)GetProcAddress(g_hRealDInput8,"DirectInput8Create");
         LogMsg("dinput8: %s\n",sp);}
        WriteDiag("dinput8 OK");
        InstallAllHooks(); WriteDiag("Hooks OK");

        if(g_config.windowed || g_config.borderless){
            ChangeDisplaySettingsA(NULL, 0);
            LogMsg("Display mode reset (%s)\n",
                g_config.borderless ? "borderless" : "windowed");
        }

        LogMsg("=== v20 ready ===\n"); WriteDiag("Ready");

        {
            HMODULE hExe2 = GetModuleHandleA(NULL);
            LogMsg("Module map: exe=%p opengl=%p dinput8proxy=%p\n",
                hExe2, g_hOpenGL, hDll);
        }
    } else if(reason==DLL_PROCESS_DETACH){
        /* Restore gamma ramp before shutdown */
        RestoreOriginalGamma();
        /* Release cursor */
        ReleaseCursorClip();
        if(g_config.windowed || g_config.borderless || g_displayMode == 2)
            ChangeDisplaySettingsA(NULL, 0);
        /* Clean up timer period */
        if(g_config.fps_limit > 0) timeEndPeriod(1);
        /* Shutdown custom mixer + WASAPI + reverb + streams (v19/v20) */
        Audio_v19_Shutdown();
        Mix_Shutdown();
        /* Shutdown MSAA FBO */
        MSAA_Shutdown();
        LogMsg("Shutdown.\n");
        if(g_logFile){fclose(g_logFile);g_logFile=NULL;}
        if(g_hRealDInput8){FreeLibrary(g_hRealDInput8);g_hRealDInput8=NULL;}
    }
    return TRUE;
}
