/*
 * DINPUT8 Proxy for Alone in the Dark: The New Nightmare
 * v13 - Video/GL fixes (audio engine split into separate project)
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
 *   - Windowed mode
 *   - Resolution override (registry + ChangeDisplaySettings)
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

/* Anisotropic filtering (EXT_texture_filter_anisotropic) */
#define GL_UNSIGNED_BYTE                  0x1401
#define GL_BGR                            0x80E0
#define GL_BGRA                           0x80E1
#define GL_TEXTURE_MAX_ANISOTROPY_EXT     0x84FE
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF

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
    int msaa_samples;
    int force_width;
    int force_height;
    int aniso_level;      /* anisotropic filtering: 0=off, else max level (e.g. 16) */
    int dump_textures;    /* dump unique textures to TGA: 1=on, 0=off */
    int replace_textures; /* replace textures from TGA files: 1=on, 0=off */
    int vsync;            /* VSync via wglSwapIntervalEXT: 0=off, 1=on */
    int fps_limit;        /* FPS cap: 0=off, else cap value (e.g. 60) */
    int enable_log;
    int show_diagnostics;
} WrapperConfig;

/* {AR,CD,TF,TC, Win,MSAA, FW,FH, Aniso,Dump,Repl, VS,FPS, Log,Diag} */
static WrapperConfig g_config = {1,1,1,1, 0,0, 0,0, 16,0,0, 1,0, 1,0};

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
#define TEX_CACHE_MAX   512    /* max replacement entries cached in memory   */

static unsigned int g_texSeen[TEX_SEEN_MAX];
static int          g_texSeenCount = 0;

typedef struct { unsigned int hash; int w, h; unsigned char *rgba; } TexEntry;
static TexEntry g_texCache[TEX_CACHE_MAX];
static int      g_texCacheCount = 0;

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
    return v;
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

/* Find cached replacement entry. NULL = not in cache yet. */
static TexEntry* TexCacheFind(unsigned int hash) {
    for(int i=0;i<g_texCacheCount;i++) if(g_texCache[i].hash==hash) return &g_texCache[i];
    return NULL;
}

/*
 * Try to load replacement TGA from disk.
 * Always adds a cache entry (with rgba=NULL if file not found),
 * so subsequent calls don't hit disk again.
 */
static TexEntry* TexCacheLoad(unsigned int hash, int w, int h) {
    if(g_texCacheCount>=TEX_CACHE_MAX) return NULL;
    TexEntry *e=&g_texCache[g_texCacheCount++];
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
 *  GL HOOKS
 * ================================================================== */

static int g_screenW=0,g_screenH=0,g_vpX=0,g_vpY=0,g_vpW=0,g_vpH=0;

static void WINAPI my_glViewport(GLint x,GLint y,GLsizei w,GLsizei h){
    if(!real_glViewport)return;
    if(g_config.fix_aspect_ratio && w>0 && h>0){
        g_screenW=w; g_screenH=h;
        double tgt=4.0/3.0, cur=(double)w/(double)h;
        if(cur > tgt+0.01){
            GLsizei nw=(GLsizei)(h*tgt); g_vpX=x+(w-nw)/2; g_vpY=y; g_vpW=nw; g_vpH=h;
            real_glViewport(x,y,w,h);
            if(real_glClearColor)real_glClearColor(0,0,0,1);
            if(real_glClear)real_glClear(GL_COLOR_BUFFER_BIT);
            real_glViewport(g_vpX,g_vpY,g_vpW,g_vpH);
            LogMsg("VP: %dx%d -> pillarbox %d,%d %dx%d\n",w,h,g_vpX,g_vpY,g_vpW,g_vpH);
        } else if(cur < tgt-0.01){
            GLsizei nh=(GLsizei)(w/tgt); g_vpX=x; g_vpY=y+(h-nh)/2; g_vpW=w; g_vpH=nh;
            real_glViewport(x,y,w,h);
            if(real_glClearColor)real_glClearColor(0,0,0,1);
            if(real_glClear)real_glClear(GL_COLOR_BUFFER_BIT);
            real_glViewport(g_vpX,g_vpY,g_vpW,g_vpH);
            LogMsg("VP: %dx%d -> letterbox %d,%d %dx%d\n",w,h,g_vpX,g_vpY,g_vpW,g_vpH);
        } else { g_vpX=x;g_vpY=y;g_vpW=w;g_vpH=h; real_glViewport(x,y,w,h); }
    } else real_glViewport(x,y,w,h);
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
 * Many games (especially old ones) allocate texture storage with
 * glTexImage2D(px=NULL) and then upload actual pixels via glTexSubImage2D.
 * We intercept here for both dump and replacement.
 * Replacement: if xoffset==0 && yoffset==0 and a matching TGA exists,
 *   the entire sub-image is replaced (dimensions may differ for upscaling).
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
    if(g_config.fix_color_depth && ppfd){
        PIXELFORMATDESCRIPTOR pfd=*ppfd;
        if(pfd.cColorBits<=16){
            LogMsg("ChoosePixelFormat: %d->32 bit\n",pfd.cColorBits);
            pfd.cColorBits=32;pfd.cRedBits=8;pfd.cGreenBits=8;
            pfd.cBlueBits=8;pfd.cAlphaBits=8;pfd.cDepthBits=24;
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
 * pn = GL_TEXTURE_MAG_FILTER or GL_TEXTURE_MIN_FILTER
 * MAG filter never uses mipmap modes (they are invalid for MAG).
 * If the game itself set a mipmap filter, we still upgrade NEAREST -> LINEAR.
 */
static GLint UpgradeFilter(GLenum pn, GLint m){
    if(pn == GL_TEXTURE_MAG_FILTER){
        /* MAG: nearest -> linear only */
        return (m == GL_NEAREST) ? GL_LINEAR : m;
    }
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
    /* Anisotropy: apply when MIN_FILTER is being set on any texture */
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

/* ---- MSAA (unchanged from v9) ---- */
static int FindMSAAPixelFormat(HDC targetDC, int samples) {
    if (samples <= 0) return 0;
    if (!real_wglCreateContext || !real_wglMakeCurrent ||
        !real_wglDeleteContext || !real_wglGetProcAddress) return 0;
    LogMsg("MSAA: attempting to find %dx format...\n", samples);
    WNDCLASSA wc = {0};
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "AITD_MSAA_Dummy";
    RegisterClassA(&wc);
    HWND dummyWnd = CreateWindowExA(0, "AITD_MSAA_Dummy", "", WS_POPUP,
        0, 0, 1, 1, NULL, NULL, wc.hInstance, NULL);
    if (!dummyWnd) { LogMsg("MSAA: dummy window failed\n"); return 0; }
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
        LogMsg("MSAA: dummy context failed\n"); return 0;
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
    }
    real_wglMakeCurrent(NULL, NULL);
    real_wglDeleteContext(dummyCtx);
    ReleaseDC(dummyWnd, dummyDC);
    DestroyWindow(dummyWnd);
    UnregisterClassA("AITD_MSAA_Dummy", wc.hInstance);
    return result;
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
    }
    return ok;
}

/* ---- SwapBuffers hook: FPS limiter + VSync fallback ---- */
static BOOL WINAPI my_SwapBuffers(HDC hdc) {
    /* FPS limiter using high-resolution timer */
    if (g_config.fps_limit > 0 && g_perfFreq.QuadPart > 0) {
        LONGLONG frameTime = g_perfFreq.QuadPart / g_config.fps_limit;
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        LONGLONG elapsed = now.QuadPart - g_lastSwap.QuadPart;
        if (g_lastSwap.QuadPart != 0 && elapsed < frameTime) {
            LONGLONG remaining = frameTime - elapsed;
            /* Sleep for bulk of the wait, then spin for precision */
            DWORD sleepMs = (DWORD)(remaining * 1000 / g_perfFreq.QuadPart);
            if (sleepMs > 2) Sleep(sleepMs - 2);
            do { QueryPerformanceCounter(&now); }
            while (now.QuadPart - g_lastSwap.QuadPart < frameTime);
        }
        QueryPerformanceCounter(&g_lastSwap);
    }
    return real_SwapBuffers_fn ? real_SwapBuffers_fn(hdc) : FALSE;
}

/* ---- Windowed mode ---- */
typedef HWND(WINAPI*PFN_CreateWindowExA)(DWORD,LPCSTR,LPCSTR,DWORD,int,int,int,int,HWND,HMENU,HINSTANCE,LPVOID);
static PFN_CreateWindowExA real_CreateWindowExA = NULL;

static HWND WINAPI Hook_CreateWindowExA(DWORD exStyle, LPCSTR className,
    LPCSTR windowName, DWORD style, int x, int y, int w, int h,
    HWND parent, HMENU menu, HINSTANCE inst, LPVOID param)
{
    if (g_config.windowed && !parent) {
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
    HWND hwnd = real_CreateWindowExA(exStyle, className, windowName, style,
        x, y, w, h, parent, menu, inst, param);
    if (hwnd && !parent) { g_gameHWND = hwnd; LogMsg("Game window: %p\n", hwnd); }
    return hwnd;
}

typedef LONG(WINAPI*PFN_ChangeDisplaySettingsA)(DEVMODEA*,DWORD);
static PFN_ChangeDisplaySettingsA real_ChangeDisplaySettingsA=NULL;

static LONG WINAPI Hook_ChangeDisplaySettingsA(DEVMODEA*dm,DWORD fl){
    if (g_config.windowed) { LogMsg("ChangeDisplaySettings: SKIPPED (windowed)\n"); return DISP_CHANGE_SUCCESSFUL; }
    if(dm){
        if(g_config.fix_color_depth && dm->dmBitsPerPel==16){
            LogMsg("CDS: 16->32 bit\n"); dm->dmBitsPerPel=32;}
        if(g_config.force_width>0 && g_config.force_height>0){
            LogMsg("CDS: %lux%lu->%dx%d\n",dm->dmPelsWidth,dm->dmPelsHeight,g_config.force_width,g_config.force_height);
            dm->dmPelsWidth=g_config.force_width; dm->dmPelsHeight=g_config.force_height;
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
    real_wglCreateContext=(PFN_wglCreateContext)  GetProcAddress(hGL,"wglCreateContext");
    real_wglMakeCurrent  =(PFN_wglMakeCurrent)   GetProcAddress(hGL,"wglMakeCurrent");
    real_wglDeleteContext=(PFN_wglDeleteContext)  GetProcAddress(hGL,"wglDeleteContext");
    real_wglGetProcAddress=(PFN_wglGetProcAddress)GetProcAddress(hGL,"wglGetProcAddress");
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
    {"wglSwapBuffers",  (void*)my_SwapBuffers,       (void**)&real_SwapBuffers_fn},
    {NULL,NULL,NULL}
};

static FARPROC WINAPI Hook_GetProcAddress(HMODULE hM,LPCSTR nm){
    if(!nm) return real_GetProcAddress_ptr(hM,nm);

    /* GL interception */

    if(hM && hM==g_hOpenGL){
        for(int i=0;g_glHooks[i].name;i++){
            if(strcmp(nm,g_glHooks[i].name)==0){
                FARPROC r=real_GetProcAddress_ptr(hM,nm);
                if(r&&g_glHooks[i].real&&!(*g_glHooks[i].real)) *g_glHooks[i].real=(void*)r;
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
            return r;
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

/* ---- Registry hook (unchanged from v9) ---- */
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

static BOOL PatchIATQuiet(HMODULE hM,const char*dll,const char*func,void*hook,void**orig){
    if(!hM)return FALSE;
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
                if(orig&&!(*orig))*orig=(void*)ft->u1.Function;
                ft->u1.Function=(ULONG_PTR)hook;
                VirtualProtect(&ft->u1.Function,sizeof(void*),op,&op);
                return TRUE;}
        }
    }
    return FALSE;
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
    if (g_config.enable_log) {
        char lp[MAX_PATH]; snprintf(lp,MAX_PATH,"%saitd_wrapper.log",g_gameDir);
        g_logFile=fopen(lp,"w");
        if (g_logFile) setvbuf(g_logFile, NULL, _IONBF, 0); /* fully unbuffered */
        LogMsg("=== AITD Wrapper v13 (Video Fixes) ===\n");
        LogMsg("AR=%d CD=%d TF=%d TC=%d Aniso=%d Dump=%d Repl=%d Win=%d MSAA=%d VS=%d FPS=%d Res=%dx%d\n",
            g_config.fix_aspect_ratio, g_config.fix_color_depth,
            g_config.fix_texture_filter, g_config.fix_texture_clamp,
            g_config.aniso_level,
            g_config.dump_textures, g_config.replace_textures,
            g_config.windowed, g_config.msaa_samples,
            g_config.vsync, g_config.fps_limit,
            g_config.force_width, g_config.force_height);
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
    if(g_config.windowed)
        PatchIAT(hExe,"USER32.dll","CreateWindowExA",Hook_CreateWindowExA,(void**)&real_CreateWindowExA);
    PatchIAT(hExe,"ADVAPI32.dll","RegQueryValueExA",Hook_RegQueryValueExA,(void**)&real_RegQueryValueExA);
    if(!real_RegQueryValueExA)
        real_RegQueryValueExA=(PFN_RegQueryValueExA)GetProcAddress(GetModuleHandleA("advapi32.dll"),"RegQueryValueExA");
    real_ChoosePixelFormat_fn=(PFN_ChoosePixelFormat)GetProcAddress(GetModuleHandleA("gdi32.dll"),"ChoosePixelFormat");
    real_SetPixelFormat_fn=(PFN_SetPixelFormat)GetProcAddress(GetModuleHandleA("gdi32.dll"),"SetPixelFormat");

    /* SwapBuffers hook for FPS limiter (game may import from gdi32 or opengl32) */
    PatchIAT(hExe,"GDI32.dll","SwapBuffers",my_SwapBuffers,(void**)&real_SwapBuffers_fn);
    if(!real_SwapBuffers_fn)
        real_SwapBuffers_fn=(PFN_SwapBuffers_t)GetProcAddress(GetModuleHandleA("gdi32.dll"),"SwapBuffers");

    /* Texture dump/replace directories */
    if(g_config.dump_textures || g_config.replace_textures)
        TexEnsureDirs();

    /* High-resolution timer for FPS limiter */
    if (g_config.fps_limit > 0) {
        QueryPerformanceFrequency(&g_perfFreq);
        timeBeginPeriod(1);
        LogMsg("FPS limit: %d (freq=%lld Hz)\n",
            g_config.fps_limit, g_perfFreq.QuadPart);
    }
}

/* ---- Crash diagnostics: Vectored Exception Handler ---- */
static LONG WINAPI AE_CrashHandler(EXCEPTION_POINTERS *ep) {
    if (g_logFile && ep && ep->ExceptionRecord) {
        DWORD code = ep->ExceptionRecord->ExceptionCode;
        /* Only log fatal exceptions, not benign ones */
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

/* ---- DllMain ---- */
BOOL WINAPI DllMain(HINSTANCE hDll,DWORD reason,LPVOID reserved){
    (void)reserved;
    if(reason==DLL_PROCESS_ATTACH){
        DisableThreadLibraryCalls(hDll);
        GetModuleFileNameA((HMODULE)hDll,g_gameDir,MAX_PATH);
        char*s=strrchr(g_gameDir,'\\');if(s)*(s+1)='\0';
        WriteDiag("=== AITD Wrapper v13 ===");
        LoadConfig(); WriteDiag("Config OK");
        /* Register crash handler for diagnostics */
        AddVectoredExceptionHandler(1, AE_CrashHandler);
        if(g_config.show_diagnostics){
            char m[512];
            snprintf(m,sizeof(m),"AITD Wrapper v13\n\nAR=%d CD=%d TF=%d TC=%d\n"
                "Windowed=%d MSAA=%d\nRes=%dx%d %s",
                g_config.fix_aspect_ratio,g_config.fix_color_depth,
                g_config.fix_texture_filter,g_config.fix_texture_clamp,
                g_config.windowed,g_config.msaa_samples,
                g_config.force_width,g_config.force_height,
                g_config.force_width>0?"(forced)":"(default)");
            MessageBoxA(NULL,m,"AITD Wrapper",MB_OK);
        }
        {char sp[MAX_PATH];GetSystemDirectoryA(sp,MAX_PATH);strcat(sp,"\\dinput8.dll");
         g_hRealDInput8=LoadLibraryA(sp);
         if(!g_hRealDInput8){WriteDiag("FATAL");return FALSE;}
         real_DirectInput8Create=(PFN_DirectInput8Create)GetProcAddress(g_hRealDInput8,"DirectInput8Create");
         LogMsg("dinput8: %s\n",sp);}
        WriteDiag("dinput8 OK");
        InstallAllHooks(); WriteDiag("Hooks OK");

        if(g_config.windowed){
            ChangeDisplaySettingsA(NULL, 0);
            LogMsg("Display mode reset (windowed)\n");
        }

        LogMsg("=== v13 ready ===\n"); WriteDiag("Ready");

        /* Log key module addresses for crash diagnostics */
        {
            HMODULE hExe2 = GetModuleHandleA(NULL);
            LogMsg("Module map: exe=%p opengl=%p dinput8proxy=%p\n",
                hExe2, g_hOpenGL, hDll);
        }
    } else if(reason==DLL_PROCESS_DETACH){
        if(g_config.windowed) ChangeDisplaySettingsA(NULL, 0);
        LogMsg("Shutdown.\n");
        if(g_logFile){fclose(g_logFile);g_logFile=NULL;}
        if(g_hRealDInput8){FreeLibrary(g_hRealDInput8);g_hRealDInput8=NULL;}
    }
    return TRUE;
}
