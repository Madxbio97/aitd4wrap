/*
 * DINPUT8 Proxy for Alone in the Dark: The New Nightmare
 * v16 - Video/GL fixes + compatibility + quality-of-life
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
 *   - MSAA anti-aliasing support
 *   - Windowed mode with cursor confinement
 *   - Borderless fullscreen mode
 *   - Resolution override (registry + ChangeDisplaySettings)
 *   - Forced display refresh rate
 *   - Screenshots (F12 -> TGA)
 *   - Alt+Enter fullscreen/borderless toggle
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
#define GL_MULTISAMPLE 0x809D
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
#define GL_FRAMEBUFFER            0x8D40
#define GL_RENDERBUFFER           0x8D41
#define GL_COLOR_ATTACHMENT0      0x8CE0
#define GL_DEPTH_ATTACHMENT       0x8D00
#define GL_STENCIL_ATTACHMENT     0x8D20
#define GL_DEPTH_STENCIL_ATTACHMENT 0x821A
#define GL_FRAMEBUFFER_COMPLETE   0x8CD5
#define GL_READ_FRAMEBUFFER       0x8CA8
#define GL_DRAW_FRAMEBUFFER       0x8CA9
#define GL_DEPTH24_STENCIL8       0x88F0
#define GL_DEPTH_STENCIL          0x84F9
#define GL_DEPTH_BUFFER_BIT       0x00000100
#define GL_UNSIGNED_INT_24_8      0x84FA

/* Alpha test / blending */
#define GL_ALPHA_TEST             0x0BC0
#define GL_BLEND                  0x0BE2
#define GL_SRC_ALPHA              0x0302
#define GL_ONE_MINUS_SRC_ALPHA    0x0303
#define GL_GREATER                0x0204
#define GL_GEQUAL                 0x0206

/* WGL MSAA constants */
#define WGL_DRAW_TO_WINDOW_ARB    0x2001
#define WGL_SUPPORT_OPENGL_ARB    0x2010
#define WGL_DOUBLE_BUFFER_ARB     0x2011
#define WGL_PIXEL_TYPE_ARB        0x2013
#define WGL_COLOR_BITS_ARB        0x2014
#define WGL_DEPTH_BITS_ARB        0x2022
#define WGL_STENCIL_BITS_ARB      0x2023
#define WGL_TYPE_RGBA_ARB         0x202B
#define WGL_SAMPLE_BUFFERS_ARB    0x2041
#define WGL_SAMPLES_ARB           0x2042

/* ---- Configuration ---- */
typedef struct {
    int fix_aspect_ratio;
    int fix_color_depth;
    int fix_texture_filter;
    int fix_texture_clamp;
    int windowed;
    int borderless;
    int msaa_samples;
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
    int ssaa_factor;          /* SSAA: 1=off, 2=4x, 3=9x, 4=16x */
    int fix_alpha_test;       /* convert alpha test to alpha blend (smoother edges) */
    int force_stencil;        /* force 8-bit stencil buffer */
} WrapperConfig;

static WrapperConfig g_config = {1,1,1,1, 0,0,0, 0,0, 16,0,0, 1,0, 1,0, 0,1,1, 0.0f, 0,1,0x7B, 1,0,0};

/* ---- Globals ---- */
static HMODULE g_hRealDInput8 = NULL;
static HMODULE g_hOpenGL      = NULL;
static FILE   *g_logFile      = NULL;
static char    g_gameDir[MAX_PATH] = {0};
static HWND    g_gameHWND     = NULL;
static int     g_glContextReady = 0;

static void LogMsg(const char *fmt, ...) {
    if (!g_logFile) return;
    va_list a; va_start(a,fmt); vfprintf(g_logFile,fmt,a); va_end(a); fflush(g_logFile);
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
typedef void(WINAPI*PFN_glPixelStorei)(GLenum,GLint);
typedef void(WINAPI*PFN_glDisable)(GLenum);
typedef void(WINAPI*PFN_glBlendFunc)(GLenum,GLenum);
typedef void(WINAPI*PFN_glAlphaFunc)(GLenum,GLclampf);
/* FBO extension function types */
typedef void(WINAPI*PFN_glGenFramebuffers)(GLsizei,GLuint*);
typedef void(WINAPI*PFN_glDeleteFramebuffers)(GLsizei,const GLuint*);
typedef void(WINAPI*PFN_glBindFramebuffer)(GLenum,GLuint);
typedef void(WINAPI*PFN_glGenRenderbuffers)(GLsizei,GLuint*);
typedef void(WINAPI*PFN_glDeleteRenderbuffers)(GLsizei,const GLuint*);
typedef void(WINAPI*PFN_glBindRenderbuffer)(GLenum,GLuint);
typedef void(WINAPI*PFN_glRenderbufferStorage)(GLenum,GLenum,GLsizei,GLsizei);
typedef void(WINAPI*PFN_glFramebufferRenderbuffer)(GLenum,GLenum,GLenum,GLuint);
typedef GLenum(WINAPI*PFN_glCheckFramebufferStatus)(GLenum);
typedef void(WINAPI*PFN_glBlitFramebuffer)(GLint,GLint,GLint,GLint,GLint,GLint,GLint,GLint,GLbitfield,GLenum);
typedef int (WINAPI*PFN_ChoosePixelFormat)(HDC,const PIXELFORMATDESCRIPTOR*);
typedef HGLRC(WINAPI*PFN_wglCreateContext)(HDC);
typedef BOOL (WINAPI*PFN_wglMakeCurrent)(HDC,HGLRC);
typedef BOOL (WINAPI*PFN_wglDeleteContext)(HGLRC);
typedef PROC (WINAPI*PFN_wglGetProcAddress)(LPCSTR);
typedef BOOL(WINAPI*PFN_wglChoosePixelFormatARB)(HDC,const int*,const FLOAT*,UINT,int*,UINT*);

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
static PFN_glPixelStorei    real_glPixelStorei=NULL;
static PFN_glDisable        real_glDisable=NULL;
static PFN_glBlendFunc      real_glBlendFunc=NULL;
static PFN_glAlphaFunc      real_glAlphaFunc=NULL;
/* FBO extension pointers (resolved via wglGetProcAddress) */
static PFN_glGenFramebuffers       pfn_glGenFramebuffers=NULL;
static PFN_glDeleteFramebuffers    pfn_glDeleteFramebuffers=NULL;
static PFN_glBindFramebuffer       pfn_glBindFramebuffer=NULL;
static PFN_glGenRenderbuffers      pfn_glGenRenderbuffers=NULL;
static PFN_glDeleteRenderbuffers   pfn_glDeleteRenderbuffers=NULL;
static PFN_glBindRenderbuffer      pfn_glBindRenderbuffer=NULL;
static PFN_glRenderbufferStorage   pfn_glRenderbufferStorage=NULL;
static PFN_glFramebufferRenderbuffer pfn_glFramebufferRenderbuffer=NULL;
static PFN_glCheckFramebufferStatus  pfn_glCheckFramebufferStatus=NULL;
static PFN_glBlitFramebuffer       pfn_glBlitFramebuffer=NULL;
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

/* Hash table for texture replacement cache (open addressing, power-of-2) */
#define TEX_CACHE_BITS  10
#define TEX_CACHE_SIZE  (1 << TEX_CACHE_BITS)  /* 1024 slots */
#define TEX_CACHE_MASK  (TEX_CACHE_SIZE - 1)
#define TEX_HASH_EMPTY  0      /* sentinel: hash==0 means slot is unused   */

static unsigned int g_texSeen[TEX_SEEN_MAX];
static int          g_texSeenCount = 0;

typedef struct { unsigned int hash; int w, h; unsigned char *rgba; } TexEntry;
static TexEntry g_texCache[TEX_CACHE_SIZE];  /* zero-initialized = all empty */
static int      g_texCacheUsed = 0;

/* FNV-1a over dims+format+pixels */
static unsigned int TexHash(int w, int h, GLenum fmt, GLenum tp,
                             const unsigned char *data, int size) {
    unsigned int v = 2166136261u;
#define FNV(b) v = (v ^ (unsigned char)(b)) * 16777619u
    FNV(w); FNV(w>>8); FNV(w>>16); FNV(w>>24);
    FNV(h); FNV(h>>8); FNV(h>>16); FNV(h>>24);
    FNV(fmt); FNV(fmt>>8); FNV(tp); FNV(tp>>8);
    if(data) for(int i=0;i<size;i++) FNV(data[i]);
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
    for(int i=0;i<w*h;i++){             /* TGA pixel order: B G R A */
        fputc(rgba[i*4+2],f);
        fputc(rgba[i*4+1],f);
        fputc(rgba[i*4+0],f);
        fputc(rgba[i*4+3],f);
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
    for(int i=0;i<g_texSeenCount;i++) if(g_texSeen[i]==hash) return 0;
    if(g_texSeenCount<TEX_SEEN_MAX) g_texSeen[g_texSeenCount++]=hash;
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
 *  SSAA (Supersampling Anti-Aliasing) via FBO
 *
 *  Renders at ssaa_factor * game resolution into an off-screen FBO,
 *  then downscales to screen on SwapBuffers with GL_LINEAR filtering.
 * ================================================================== */
static GLuint g_ssaaFBO = 0;          /* framebuffer object */
static GLuint g_ssaaColorRB = 0;      /* color renderbuffer */
static GLuint g_ssaaDepthRB = 0;      /* depth+stencil renderbuffer */
static int    g_ssaaW = 0, g_ssaaH = 0; /* FBO dimensions */
static int    g_ssaaActive = 0;       /* 1 when rendering to FBO */
static int    g_ssaaGameW = 0, g_ssaaGameH = 0; /* game's base resolution */

static void SSAA_ResolveFBOFunctions(void) {
    if (!real_wglGetProcAddress) return;
    /* Try ARB names first, then EXT */
    #define RESOLVE_FBO(name) \
        pfn_##name = (PFN_##name)real_wglGetProcAddress(#name); \
        if(!pfn_##name) pfn_##name = (PFN_##name)real_wglGetProcAddress(#name "EXT");
    RESOLVE_FBO(glGenFramebuffers)
    RESOLVE_FBO(glDeleteFramebuffers)
    RESOLVE_FBO(glBindFramebuffer)
    RESOLVE_FBO(glGenRenderbuffers)
    RESOLVE_FBO(glDeleteRenderbuffers)
    RESOLVE_FBO(glBindRenderbuffer)
    RESOLVE_FBO(glRenderbufferStorage)
    RESOLVE_FBO(glFramebufferRenderbuffer)
    RESOLVE_FBO(glCheckFramebufferStatus)
    RESOLVE_FBO(glBlitFramebuffer)
    #undef RESOLVE_FBO
    LogMsg("SSAA: FBO funcs: GenFB=%p BindFB=%p Blit=%p\n",
        (void*)pfn_glGenFramebuffers, (void*)pfn_glBindFramebuffer,
        (void*)pfn_glBlitFramebuffer);
}

static int SSAA_CreateFBO(int gameW, int gameH) {
    if (!pfn_glGenFramebuffers || !pfn_glBindFramebuffer ||
        !pfn_glGenRenderbuffers || !pfn_glBindRenderbuffer ||
        !pfn_glRenderbufferStorage || !pfn_glFramebufferRenderbuffer ||
        !pfn_glCheckFramebufferStatus || !pfn_glBlitFramebuffer) {
        LogMsg("SSAA: missing FBO functions, disabled\n");
        return 0;
    }

    int factor = g_config.ssaa_factor;
    int fboW = gameW * factor, fboH = gameH * factor;

    /* Delete old FBO if size changed */
    if (g_ssaaFBO && (g_ssaaW != fboW || g_ssaaH != fboH)) {
        pfn_glDeleteFramebuffers(1, &g_ssaaFBO);
        pfn_glDeleteRenderbuffers(1, &g_ssaaColorRB);
        pfn_glDeleteRenderbuffers(1, &g_ssaaDepthRB);
        g_ssaaFBO = 0;
    }
    if (g_ssaaFBO) return 1; /* already created at correct size */

    pfn_glGenFramebuffers(1, &g_ssaaFBO);
    pfn_glBindFramebuffer(GL_FRAMEBUFFER, g_ssaaFBO);

    /* Color renderbuffer */
    pfn_glGenRenderbuffers(1, &g_ssaaColorRB);
    pfn_glBindRenderbuffer(GL_RENDERBUFFER, g_ssaaColorRB);
    pfn_glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, fboW, fboH);
    pfn_glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
        GL_RENDERBUFFER, g_ssaaColorRB);

    /* Depth+stencil renderbuffer */
    pfn_glGenRenderbuffers(1, &g_ssaaDepthRB);
    pfn_glBindRenderbuffer(GL_RENDERBUFFER, g_ssaaDepthRB);
    pfn_glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, fboW, fboH);
    pfn_glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
        GL_RENDERBUFFER, g_ssaaDepthRB);
    pfn_glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT,
        GL_RENDERBUFFER, g_ssaaDepthRB);

    GLenum status = pfn_glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        LogMsg("SSAA: FBO incomplete (status=0x%X), disabled\n", status);
        pfn_glDeleteFramebuffers(1, &g_ssaaFBO);
        pfn_glDeleteRenderbuffers(1, &g_ssaaColorRB);
        pfn_glDeleteRenderbuffers(1, &g_ssaaDepthRB);
        g_ssaaFBO = 0;
        pfn_glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return 0;
    }

    g_ssaaW = fboW; g_ssaaH = fboH;
    g_ssaaGameW = gameW; g_ssaaGameH = gameH;
    LogMsg("SSAA: FBO created %dx%d (game %dx%d, %dx factor)\n",
        fboW, fboH, gameW, gameH, factor);
    return 1;
}

/* Bind SSAA FBO for rendering */
static void SSAA_BeginFrame(void) {
    if (g_ssaaFBO && pfn_glBindFramebuffer) {
        pfn_glBindFramebuffer(GL_FRAMEBUFFER, g_ssaaFBO);
        g_ssaaActive = 1;
    }
}

/* Blit SSAA FBO to default framebuffer with downscaling */
static void SSAA_EndFrame(void) {
    if (!g_ssaaActive || !g_ssaaFBO || !pfn_glBlitFramebuffer) return;

    /* Determine destination rect with aspect ratio correction */
    int dW = g_displayW > 0 ? g_displayW : g_ssaaGameW;
    int dH = g_displayH > 0 ? g_displayH : g_ssaaGameH;
    double tgt = 4.0/3.0, dispAR = (double)dW / dH;
    int dstX=0, dstY=0, dstW=dW, dstH=dH;
    if (dispAR > tgt + 0.01) {
        dstW = (int)(dH * tgt); dstX = (dW - dstW) / 2;
    } else if (dispAR < tgt - 0.01) {
        dstH = (int)(dW / tgt); dstY = (dH - dstH) / 2;
    }

    /* Blit to default framebuffer */
    pfn_glBindFramebuffer(GL_READ_FRAMEBUFFER, g_ssaaFBO);
    pfn_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

    /* Clear default FB (black bars) */
    if (real_glViewport) real_glViewport(0, 0, dW, dH);
    if (real_glClearColor) real_glClearColor(0, 0, 0, 1);
    if (real_glClear) real_glClear(GL_COLOR_BUFFER_BIT);

    pfn_glBlitFramebuffer(
        0, 0, g_ssaaW, g_ssaaH,        /* source: full FBO */
        dstX, dstY, dstX+dstW, dstY+dstH, /* dest: AR-corrected rect */
        GL_COLOR_BUFFER_BIT, GL_LINEAR);

    pfn_glBindFramebuffer(GL_FRAMEBUFFER, 0);
    g_ssaaActive = 0;
}

/* ==================================================================
 *  ALPHA TEST -> ALPHA BLEND FIX
 *
 *  Old games use glAlphaFunc(GL_GREATER, 0.5) with GL_ALPHA_TEST
 *  which creates hard/jagged edges on transparent textures.
 *  We convert this to alpha blending for smoother edges.
 * ================================================================== */
static int g_alphaTestEnabled = 0;

static void WINAPI my_glEnable(GLenum cap) {
    if (!real_glEnable) return;
    if (g_config.fix_alpha_test && cap == GL_ALPHA_TEST) {
        g_alphaTestEnabled = 1;
        real_glEnable(GL_ALPHA_TEST); /* keep alpha test for depth sorting */
        if (real_glEnable) real_glEnable(GL_BLEND);
        if (real_glBlendFunc) real_glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        return;
    }
    real_glEnable(cap);
}

static void WINAPI my_glDisable(GLenum cap) {
    if (!real_glDisable) return;
    if (g_config.fix_alpha_test && cap == GL_ALPHA_TEST) {
        g_alphaTestEnabled = 0;
        real_glDisable(GL_ALPHA_TEST);
        if (real_glDisable) real_glDisable(GL_BLEND);
        return;
    }
    real_glDisable(cap);
}

static void WINAPI my_glAlphaFunc(GLenum func, GLclampf ref) {
    if (!real_glAlphaFunc) return;
    if (g_config.fix_alpha_test) {
        /* Lower the threshold for smoother alpha edges */
        if (ref > 0.1f) ref = 0.1f;
        /* Use GL_GEQUAL for slightly better edge coverage */
        real_glAlphaFunc(GL_GEQUAL, ref);
        return;
    }
    real_glAlphaFunc(func, ref);
}

static DetourHook g_detourEnable   = {0};
static DetourHook g_detourDisable  = {0};
static DetourHook g_detourAlphaFunc= {0};

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

    /* === SSAA MODE: render to FBO, AR correction deferred to blit === */
    if(g_config.ssaa_factor > 1 && pfn_glBindFramebuffer){
        /* Track game's base resolution */
        if(x==0 && y==0 && w*h >= g_ssaaGameW*g_ssaaGameH){
            g_ssaaGameW = w; g_ssaaGameH = h;
        }
        /* Create/resize FBO on first full-screen viewport */
        if(!g_ssaaFBO && g_ssaaGameW > 0){
            if(SSAA_CreateFBO(g_ssaaGameW, g_ssaaGameH))
                SSAA_BeginFrame();
        }
        /* Scale viewport by SSAA factor */
        if(g_ssaaActive){
            int f = g_config.ssaa_factor;
            real_glViewport(x*f, y*f, w*f, h*f);
            /* Store for scissor remapping */
            int refW = g_ssaaGameW > 0 ? g_ssaaGameW : w;
            int refH = g_ssaaGameH > 0 ? g_ssaaGameH : h;
            g_screenW = refW; g_screenH = refH;
            g_vpX = 0; g_vpY = 0;
            g_vpW = refW * f; g_vpH = refH * f;
            return;
        }
    }

    /* === NON-SSAA: apply AR correction directly === */
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

/* Forward declaration */
static int FindMSAAPixelFormat(HDC targetDC, int samples);

static int WINAPI my_ChoosePixelFormat(HDC hdc,const PIXELFORMATDESCRIPTOR*ppfd){
    if(!real_ChoosePixelFormat_fn)return 0;
    if(g_config.msaa_samples > 0){
        int msaaFmt = FindMSAAPixelFormat(hdc, g_config.msaa_samples);
        if(msaaFmt > 0){
            LogMsg("ChoosePixelFormat: using MSAA %dx format %d\n",g_config.msaa_samples,msaaFmt);
            return msaaFmt;
        }
        LogMsg("ChoosePixelFormat: MSAA failed, fallback\n");
    }
    if((g_config.fix_color_depth || g_config.force_stencil) && ppfd){
        PIXELFORMATDESCRIPTOR pfd=*ppfd;
        if(g_config.fix_color_depth && pfd.cColorBits<=16){
            LogMsg("ChoosePixelFormat: %d->32 bit\n",pfd.cColorBits);
            pfd.cColorBits=32;pfd.cRedBits=8;pfd.cGreenBits=8;
            pfd.cBlueBits=8;pfd.cAlphaBits=8;pfd.cDepthBits=24;
        }
        if(g_config.force_stencil && pfd.cStencilBits<8){
            LogMsg("ChoosePixelFormat: stencil %d->8\n",pfd.cStencilBits);
            pfd.cStencilBits=8;
            if(pfd.cDepthBits<24) pfd.cDepthBits=24;
        }
        return real_ChoosePixelFormat_fn(hdc,&pfd);
    }
    return real_ChoosePixelFormat_fn(hdc,ppfd);
}

static BOOL WINAPI my_SetPixelFormat(HDC hdc,int format,const PIXELFORMATDESCRIPTOR*ppfd){
    if(!real_SetPixelFormat_fn) return FALSE;
    if(g_config.msaa_samples > 0 && !g_glContextReady){
        int msaaFmt = FindMSAAPixelFormat(hdc, g_config.msaa_samples);
        if(msaaFmt > 0){ LogMsg("SetPixelFormat: %d -> MSAA %d\n",format,msaaFmt); format=msaaFmt; }
    }
    return real_SetPixelFormat_fn(hdc, format, ppfd);
}

/*
 * UpgradeFilter: upgrades texture filter modes.
 */
static GLint UpgradeFilter(GLenum pn, GLint m){
    if(pn == GL_TEXTURE_MAG_FILTER){
        return (m == GL_NEAREST) ? GL_LINEAR : m;
    }
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

/* ---- MSAA ---- */
static int g_msaaFormatCached = 0;  /* 0=not tried, >0=format, -1=failed */

static int FindMSAAPixelFormat(HDC targetDC, int samples) {
    if (samples <= 0) return 0;
    /* Return cached result if already found */
    if (g_msaaFormatCached > 0) return g_msaaFormatCached;
    if (g_msaaFormatCached < 0) return 0; /* already failed */

    /* Ensure wgl functions are available — load opengl32 if needed */
    if (!real_wglCreateContext || !real_wglMakeCurrent ||
        !real_wglDeleteContext || !real_wglGetProcAddress) {
        HMODULE hGL = GetModuleHandleA("opengl32.dll");
        if (!hGL) {
            hGL = LoadLibraryA("opengl32.dll");
            LogMsg("MSAA: early-loaded opengl32.dll = %p\n", hGL);
        }
        if (hGL) {
            if (!g_hOpenGL) g_hOpenGL = hGL;
            if (!real_wglCreateContext)
                real_wglCreateContext = (PFN_wglCreateContext)GetProcAddress(hGL, "wglCreateContext");
            if (!real_wglMakeCurrent)
                real_wglMakeCurrent = (PFN_wglMakeCurrent)GetProcAddress(hGL, "wglMakeCurrent");
            if (!real_wglDeleteContext)
                real_wglDeleteContext = (PFN_wglDeleteContext)GetProcAddress(hGL, "wglDeleteContext");
            if (!real_wglGetProcAddress)
                real_wglGetProcAddress = (PFN_wglGetProcAddress)GetProcAddress(hGL, "wglGetProcAddress");
        }
    }
    if (!real_wglCreateContext || !real_wglMakeCurrent ||
        !real_wglDeleteContext || !real_wglGetProcAddress) {
        LogMsg("MSAA: wgl functions unavailable\n");
        g_msaaFormatCached = -1;
        return 0;
    }
    if (!real_ChoosePixelFormat_fn) {
        real_ChoosePixelFormat_fn = (PFN_ChoosePixelFormat)
            GetProcAddress(GetModuleHandleA("gdi32.dll"), "ChoosePixelFormat");
    }

    LogMsg("MSAA: attempting to find %dx format...\n", samples);
    WNDCLASSA wc = {0};
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "AITD_MSAA_Dummy";
    RegisterClassA(&wc);
    HWND dummyWnd = CreateWindowExA(0, "AITD_MSAA_Dummy", "", WS_POPUP,
        0, 0, 1, 1, NULL, NULL, wc.hInstance, NULL);
    if (!dummyWnd) { LogMsg("MSAA: dummy window failed\n"); g_msaaFormatCached=-1; return 0; }
    HDC dummyDC = GetDC(dummyWnd);
    PIXELFORMATDESCRIPTOR pfd = {0};
    pfd.nSize = sizeof(pfd); pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA; pfd.cColorBits = 32; pfd.cDepthBits = 24;
    int dummyFmt = real_ChoosePixelFormat_fn(dummyDC, &pfd);
    SetPixelFormat(dummyDC, dummyFmt, &pfd);
    HGLRC dummyCtx = real_wglCreateContext(dummyDC);
    if (!dummyCtx) {
        ReleaseDC(dummyWnd, dummyDC); DestroyWindow(dummyWnd);
        LogMsg("MSAA: dummy context failed\n"); g_msaaFormatCached=-1; return 0;
    }
    real_wglMakeCurrent(dummyDC, dummyCtx);
    PFN_wglChoosePixelFormatARB wglChoosePF =
        (PFN_wglChoosePixelFormatARB)real_wglGetProcAddress("wglChoosePixelFormatARB");
    int result = 0;
    if (wglChoosePF) {
        int attribs[] = {
            WGL_DRAW_TO_WINDOW_ARB, 1, WGL_SUPPORT_OPENGL_ARB, 1,
            WGL_DOUBLE_BUFFER_ARB, 1, WGL_PIXEL_TYPE_ARB, WGL_TYPE_RGBA_ARB,
            WGL_COLOR_BITS_ARB, 32, WGL_DEPTH_BITS_ARB, 24, WGL_STENCIL_BITS_ARB, 8,
            WGL_SAMPLE_BUFFERS_ARB, 1, WGL_SAMPLES_ARB, samples, 0
        };
        int formats[8]; UINT numFormats = 0;
        if (wglChoosePF(targetDC, attribs, NULL, 8, formats, &numFormats) && numFormats > 0) {
            result = formats[0];
            LogMsg("MSAA: found %dx format: %d\n", samples, result);
        } else if (samples > 2) {
            attribs[17] = samples / 2;
            if (wglChoosePF(targetDC, attribs, NULL, 8, formats, &numFormats) && numFormats > 0) {
                result = formats[0];
                LogMsg("MSAA: found %dx format: %d\n", samples/2, result);
            }
        }
    } else {
        LogMsg("MSAA: wglChoosePixelFormatARB not available\n");
    }
    real_wglMakeCurrent(NULL, NULL);
    real_wglDeleteContext(dummyCtx);
    ReleaseDC(dummyWnd, dummyDC);
    DestroyWindow(dummyWnd);
    UnregisterClassA("AITD_MSAA_Dummy", wc.hInstance);
    g_msaaFormatCached = result > 0 ? result : -1;
    return result;
}

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
        if (g_config.msaa_samples > 0 && real_glEnable) {
            real_glEnable(GL_MULTISAMPLE);
            LogMsg("MSAA: glEnable(GL_MULTISAMPLE)\n");
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
        /* SSAA: resolve FBO extension functions */
        if (g_config.ssaa_factor > 1) {
            SSAA_ResolveFBOFunctions();
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
            do { QueryPerformanceCounter(&now); }
            while (now.QuadPart - g_lastSwap.QuadPart < frameTime);
        }
        QueryPerformanceCounter(&g_lastSwap);
    }
    /* Reset aspect-ratio bars flag so they get cleared next frame */
    g_arBarsCleared = 0;
    /* SSAA: blit FBO to screen before swap */
    if (g_ssaaActive) SSAA_EndFrame();
    BOOL result = real_SwapBuffers_fn ? real_SwapBuffers_fn(hdc) : FALSE;
    /* SSAA: re-bind FBO for next frame */
    if (g_ssaaFBO) SSAA_BeginFrame();
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
            /* Alpha test → blend detours */
            if (g_config.fix_alpha_test) {
                if (real_glEnable) {
                    void *tramp = DetourInstall(&g_detourEnable,
                        (void*)real_glEnable, (void*)my_glEnable, "glEnable");
                    if (tramp) real_glEnable = (PFN_glEnable)tramp;
                }
                if (real_glDisable) {
                    void *tramp = DetourInstall(&g_detourDisable,
                        (void*)real_glDisable, (void*)my_glDisable, "glDisable");
                    if (tramp) real_glDisable = (PFN_glDisable)tramp;
                }
                if (real_glAlphaFunc) {
                    void *tramp = DetourInstall(&g_detourAlphaFunc,
                        (void*)real_glAlphaFunc, (void*)my_glAlphaFunc, "glAlphaFunc");
                    if (tramp) real_glAlphaFunc = (PFN_glAlphaFunc)tramp;
                }
            }
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
        if (g_config.fix_color_depth || g_config.msaa_samples > 0) {
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
            if(strcmp(nm,"SetPixelFormat")==0 && g_config.msaa_samples>0){
                FARPROC r=real_GetProcAddress_ptr(hM,nm);
                if(r&&!real_SetPixelFormat_fn) real_SetPixelFormat_fn=(PFN_SetPixelFormat)r;
                return(FARPROC)my_SetPixelFormat;
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
    g_config.msaa_samples       = GetPrivateProfileIntA("Display","MSAASamples",0,p);
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
    /* v16 additions */
    g_config.ssaa_factor        = GetPrivateProfileIntA("Rendering","SSAAFactor",1,p);
    if (g_config.ssaa_factor < 1) g_config.ssaa_factor = 1;
    if (g_config.ssaa_factor > 4) g_config.ssaa_factor = 4;
    g_config.fix_alpha_test     = GetPrivateProfileIntA("Rendering","FixAlphaTest",0,p);
    g_config.force_stencil      = GetPrivateProfileIntA("Rendering","ForceStencil",0,p);
    if (g_config.enable_log) {
        char lp[MAX_PATH]; snprintf(lp,MAX_PATH,"%saitd_wrapper.log",g_gameDir);
        g_logFile=fopen(lp,"w");
        if (g_logFile) setvbuf(g_logFile, NULL, _IONBF, 0);
        LogMsg("=== AITD Wrapper v16 ===\n");
        LogMsg("AR=%d CD=%d TF=%d TC=%d Aniso=%d Fog=%d Dump=%d Repl=%d\n",
            g_config.fix_aspect_ratio, g_config.fix_color_depth,
            g_config.fix_texture_filter, g_config.fix_texture_clamp,
            g_config.aniso_level, g_config.fix_fog,
            g_config.dump_textures, g_config.replace_textures);
        LogMsg("Win=%d BL=%d MSAA=%d VS=%d FPS=%d Res=%dx%d Clip=%d Gamma=%.2f Core1=%d\n",
            g_config.windowed, g_config.borderless, g_config.msaa_samples,
            g_config.vsync, g_config.fps_limit,
            g_config.force_width, g_config.force_height,
            g_config.clip_cursor, g_config.gamma_boost,
            g_config.single_core);
        LogMsg("RefreshRate=%d AltEnter=%d ScreenshotKey=0x%02X\n",
            g_config.force_refresh_rate, g_config.allow_alt_enter,
            g_config.screenshot_key);
        LogMsg("SSAA=%dx AlphaFix=%d Stencil=%d\n",
            g_config.ssaa_factor, g_config.fix_alpha_test,
            g_config.force_stencil);
    }
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
    real_ChoosePixelFormat_fn=(PFN_ChoosePixelFormat)GetProcAddress(GetModuleHandleA("gdi32.dll"),"ChoosePixelFormat");
    real_SetPixelFormat_fn=(PFN_SetPixelFormat)GetProcAddress(GetModuleHandleA("gdi32.dll"),"SetPixelFormat");

    /* IAT-patch ChoosePixelFormat and SetPixelFormat for MSAA + color depth */
    if (g_config.msaa_samples > 0 || g_config.fix_color_depth) {
        PatchIAT(hExe,"GDI32.dll","ChoosePixelFormat",my_ChoosePixelFormat,(void**)&real_ChoosePixelFormat_fn);
        if (!real_ChoosePixelFormat_fn)
            real_ChoosePixelFormat_fn=(PFN_ChoosePixelFormat)GetProcAddress(GetModuleHandleA("gdi32.dll"),"ChoosePixelFormat");
    }
    if (g_config.msaa_samples > 0) {
        PatchIAT(hExe,"GDI32.dll","SetPixelFormat",my_SetPixelFormat,(void**)&real_SetPixelFormat_fn);
        if (!real_SetPixelFormat_fn)
            real_SetPixelFormat_fn=(PFN_SetPixelFormat)GetProcAddress(GetModuleHandleA("gdi32.dll"),"SetPixelFormat");
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
}

/* ---- Crash diagnostics: Vectored Exception Handler ---- */
static LONG WINAPI AE_CrashHandler(EXCEPTION_POINTERS *ep) {
    if (g_logFile && ep && ep->ExceptionRecord) {
        DWORD code = ep->ExceptionRecord->ExceptionCode;
        if (code == 0xC0000005 || code == 0xC0000094 || code == 0xC0000096 ||
            code == 0xC00000FD || code == 0x80000003) {
            void *addr = ep->ExceptionRecord->ExceptionAddress;
            HMODULE hExe = GetModuleHandleA(NULL);
            fprintf(g_logFile,
                "!!! EXCEPTION 0x%08X at %p (exe+0x%X proxy+0x%X)\n",
                code, addr,
                (unsigned int)((char*)addr - (char*)hExe),
                (unsigned int)((char*)addr - (char*)GetModuleHandleA("dinput8.dll")));
            if (code == 0xC0000005 && ep->ExceptionRecord->NumberParameters >= 2) {
                fprintf(g_logFile, "    Access: %s at %p\n",
                    ep->ExceptionRecord->ExceptionInformation[0] ? "WRITE" : "READ",
                    (void*)ep->ExceptionRecord->ExceptionInformation[1]);
            }
            if (ep->ContextRecord) {
#ifdef _M_IX86
                fprintf(g_logFile, "    EAX=%08X EBX=%08X ECX=%08X EDX=%08X\n",
                    (unsigned)ep->ContextRecord->Eax, (unsigned)ep->ContextRecord->Ebx,
                    (unsigned)ep->ContextRecord->Ecx, (unsigned)ep->ContextRecord->Edx);
                fprintf(g_logFile, "    ESP=%08X EBP=%08X ESI=%08X EDI=%08X\n",
                    (unsigned)ep->ContextRecord->Esp, (unsigned)ep->ContextRecord->Ebp,
                    (unsigned)ep->ContextRecord->Esi, (unsigned)ep->ContextRecord->Edi);
#endif
            }
            fflush(g_logFile);
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
        WriteDiag("=== AITD Wrapper v16 ===");
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
            snprintf(m,sizeof(m),"AITD Wrapper v16\n\nAR=%d CD=%d TF=%d TC=%d Fog=%d\n"
                "Windowed=%d Borderless=%d MSAA=%d Clip=%d\n"
                "Res=%dx%d %s Refresh=%d\n"
                "Gamma=%.2f SingleCore=%d AltEnter=%d\nScreenshot=0x%02X",
                g_config.fix_aspect_ratio,g_config.fix_color_depth,
                g_config.fix_texture_filter,g_config.fix_texture_clamp,
                g_config.fix_fog,
                g_config.windowed,g_config.borderless,g_config.msaa_samples,
                g_config.clip_cursor,
                g_config.force_width,g_config.force_height,
                g_config.force_width>0?"(forced)":"(default)",
                g_config.force_refresh_rate,
                g_config.gamma_boost,g_config.single_core,
                g_config.allow_alt_enter,g_config.screenshot_key);
            MessageBoxA(NULL,m,"AITD Wrapper",MB_OK);
        }
        {char sp[MAX_PATH];GetSystemDirectoryA(sp,MAX_PATH);strcat(sp,"\\dinput8.dll");
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

        LogMsg("=== v16 ready ===\n"); WriteDiag("Ready");

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
        LogMsg("Shutdown.\n");
        if(g_logFile){fclose(g_logFile);g_logFile=NULL;}
        if(g_hRealDInput8){FreeLibrary(g_hRealDInput8);g_hRealDInput8=NULL;}
    }
    return TRUE;
}
