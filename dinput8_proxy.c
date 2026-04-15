/*
 * DINPUT8 Proxy for Alone in the Dark: The New Nightmare
 * v11 - Custom audio engine replacing MSS/mss32.dll
 *
 * Audio engine features:
 *   - IMA ADPCM decoder (correct implementation)
 *   - Software mixer: 16 samples + 4 streams
 *   - Linear-interpolation resampler (any rate -> output Hz)
 *   - waveOut quad-buffered output at configurable rate/16/stereo
 *   - Per-sample volume, pan, loop, playback rate
 *   - Per-stream volume, pan, loop
 *   - 64-sample crossfade at loop boundaries (click prevention)
 *   - Double-buffered stream decode (no stalls in mixer)
 *   - TryEnterCriticalSection in audio thread (hang prevention)
 *   - Configurable via INI: UseCustomAudio=1 to enable
 */
#pragma warning(disable: 4996 4273)
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <mmsystem.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>

#pragma comment(lib, "winmm.lib")

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
    /* Audio */
    int fix_audio_crackle;
    int audio_latency;
    int audio_buffer_size;
    int audio_use_waveout;
    /* Custom audio engine */
    int use_custom_audio;  /* 1=use our engine, 0=pass-through to MSS */
    int audio_master_volume; /* 0-127, default 127 */
    int audio_output_rate;  /* output sample rate, default 22050 */
    int enable_log;
    int show_diagnostics;
} WrapperConfig;

static WrapperConfig g_config = {1,1,1,1, 0,0, 0,0, 1,0,0,0, 1,127, 1,0};

/* ---- Globals ---- */
static HMODULE g_hRealDInput8 = NULL;
static HMODULE g_hOpenGL      = NULL;
static HMODULE g_hMSS32       = NULL;
static FILE   *g_logFile      = NULL;
static char    g_gameDir[MAX_PATH] = {0};
static HWND    g_gameHWND     = NULL;
static int     g_glContextReady = 0;
static int     g_digConfigLogCount = 0;

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
static PFN_glEnable         real_glEnable=NULL;
static PFN_glHint           real_glHint=NULL;
static PFN_ChoosePixelFormat real_ChoosePixelFormat_fn=NULL;

typedef BOOL(WINAPI*PFN_SetPixelFormat)(HDC,int,const PIXELFORMATDESCRIPTOR*);
static PFN_SetPixelFormat real_SetPixelFormat_fn=NULL;
static PFN_wglCreateContext  real_wglCreateContext=NULL;
static PFN_wglMakeCurrent    real_wglMakeCurrent=NULL;
static PFN_wglDeleteContext  real_wglDeleteContext=NULL;
static PFN_wglGetProcAddress real_wglGetProcAddress=NULL;


/* ================================================================== */
/*                                                                    */
/*   CUSTOM AUDIO ENGINE - replaces Miles Sound System (mss32.dll)    */
/*                                                                    */
/* ================================================================== */

/* ---- MSS type definitions ---- */
typedef void* HDIGDRIVER;
typedef void* HSAMPLE;
typedef void* HSTREAM;
typedef int   MSS_S32;
typedef unsigned int MSS_U32;

/* MSS sample status codes */
#define SMP_FREE     1
#define SMP_DONE     2
#define SMP_PLAYING  4
#define SMP_STOPPED  8
#define SMP_PLAYING_RELEASED 16

/* ---- Audio engine constants ---- */
static int      AE_OUTPUT_RATE = 22050; /* configurable, loaded from INI */
#define AE_OUTPUT_CHANNELS  2
#define AE_OUTPUT_BITS      16
#define AE_BUFFER_MS        20    /* milliseconds per buffer */
#define AE_NUM_BUFFERS      6     /* 6 buffers = 120ms total latency */
#define AE_MAX_SAMPLES      16    /* max simultaneous sample handles */
#define AE_MAX_STREAMS      4     /* max simultaneous stream handles */
#define AE_STREAM_DECODE_FRAMES 16384  /* frames to decode per stream chunk */

/* ---- IMA ADPCM tables ---- */
static const int g_imaIndexTable[16] = {
    -1, -1, -1, -1, 2, 4, 6, 8,
    -1, -1, -1, -1, 2, 4, 6, 8
};

static const int g_imaStepTable[89] = {
        7,     8,     9,    10,    11,    12,    13,    14,
       16,    17,    19,    21,    23,    25,    28,    31,
       34,    37,    41,    45,    50,    55,    60,    66,
       73,    80,    88,    97,   107,   118,   130,   143,
      157,   173,   190,   209,   230,   253,   279,   307,
      337,   371,   408,   449,   494,   544,   598,   658,
      724,   796,   876,   963,  1060,  1166,  1282,  1411,
     1552,  1707,  1878,  2066,  2272,  2499,  2749,  3024,
     3327,  3660,  4026,  4428,  4871,  5358,  5894,  6484,
     7132,  7845,  8630,  9493, 10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
    32767
};

/* ---- IMA ADPCM decoder state ---- */
typedef struct {
    int predictor;
    int step_index;
} ImaAdpcmState;

static int ImaAdpcm_DecodeSample(ImaAdpcmState *state, int nibble) {
    int step = g_imaStepTable[state->step_index];
    int diff = step >> 3;
    if (nibble & 1) diff += step >> 2;
    if (nibble & 2) diff += step >> 1;
    if (nibble & 4) diff += step;
    if (nibble & 8) diff = -diff;

    state->predictor += diff;
    if (state->predictor > 32767)  state->predictor = 32767;
    if (state->predictor < -32768) state->predictor = -32768;

    state->step_index += g_imaIndexTable[nibble & 0x0F];
    if (state->step_index < 0)  state->step_index = 0;
    if (state->step_index > 88) state->step_index = 88;

    return state->predictor;
}

/*
 * Decode one IMA ADPCM block for mono.
 * Block layout: [predictor:2][index:1][reserved:1][nibbles...]
 * Returns number of samples decoded.
 */
static int ImaAdpcm_DecodeBlockMono(const unsigned char *block, int blockSize,
                                     short *out, int maxSamples) {
    if (blockSize < 4) return 0;
    ImaAdpcmState st;
    st.predictor  = (short)(block[0] | (block[1] << 8));
    st.step_index = block[2];
    if (st.step_index > 88) st.step_index = 88;

    int n = 0;
    if (n < maxSamples) out[n++] = (short)st.predictor;

    for (int i = 4; i < blockSize && n < maxSamples; i++) {
        unsigned char byte = block[i];
        out[n++] = (short)ImaAdpcm_DecodeSample(&st, byte & 0x0F);
        if (n < maxSamples)
            out[n++] = (short)ImaAdpcm_DecodeSample(&st, (byte >> 4) & 0x0F);
    }
    return n;
}

/*
 * Decode one IMA ADPCM block for stereo.
 * Stereo blocks have interleaved 4-byte headers for each channel,
 * then interleaved 4-sample packets (8 nibbles = 4 bytes per channel).
 */
static int ImaAdpcm_DecodeBlockStereo(const unsigned char *block, int blockSize,
                                       short *outL, short *outR, int maxSamples) {
    if (blockSize < 8) return 0;
    ImaAdpcmState stL, stR;
    stL.predictor  = (short)(block[0] | (block[1] << 8));
    stL.step_index = block[2];
    stR.predictor  = (short)(block[4] | (block[5] << 8));
    stR.step_index = block[6];
    if (stL.step_index > 88) stL.step_index = 88;
    if (stR.step_index > 88) stR.step_index = 88;

    int n = 0;
    if (n < maxSamples) { outL[n] = (short)stL.predictor; outR[n] = (short)stR.predictor; n++; }

    int pos = 8;
    while (pos + 8 <= blockSize && n < maxSamples) {
        /* 4 bytes = 8 nibbles for left channel */
        for (int i = 0; i < 4 && n + i < maxSamples; i++) {
            unsigned char byte = block[pos + i];
            /* Within each 4-byte packet, samples are packed low-nibble first */
            if (n + i * 2 < maxSamples)
                outL[n + i * 2]     = (short)ImaAdpcm_DecodeSample(&stL, byte & 0x0F);
            if (n + i * 2 + 1 < maxSamples)
                outL[n + i * 2 + 1] = (short)ImaAdpcm_DecodeSample(&stL, (byte >> 4) & 0x0F);
        }
        pos += 4;
        /* 4 bytes = 8 nibbles for right channel */
        for (int i = 0; i < 4 && n + i < maxSamples; i++) {
            unsigned char byte = block[pos + i];
            if (n + i * 2 < maxSamples)
                outR[n + i * 2]     = (short)ImaAdpcm_DecodeSample(&stR, byte & 0x0F);
            if (n + i * 2 + 1 < maxSamples)
                outR[n + i * 2 + 1] = (short)ImaAdpcm_DecodeSample(&stR, (byte >> 4) & 0x0F);
        }
        pos += 4;
        n += 8; /* 8 samples decoded per iteration */
    }
    if (n > maxSamples) n = maxSamples;
    return n;
}

/* ---- WAV parser ---- */
typedef struct {
    int format_tag;       /* 1=PCM, 17=IMA ADPCM */
    int channels;
    int sample_rate;
    int avg_bytes_sec;
    int block_align;
    int bits_per_sample;
    int extra_size;       /* cbSize from WAVEFORMATEX */
    int samples_per_block; /* for ADPCM: from extra data */
    const unsigned char *data;
    int data_size;
} WavInfo;

static int ParseWav(const unsigned char *mem, int memSize, WavInfo *info) {
    memset(info, 0, sizeof(*info));
    if (memSize < 44) return 0;
    if (memcmp(mem, "RIFF", 4) != 0) return 0;
    if (memcmp(mem + 8, "WAVE", 4) != 0) return 0;

    int pos = 12;
    int foundFmt = 0, foundData = 0;
    while (pos + 8 <= memSize) {
        char id[5] = {0};
        memcpy(id, mem + pos, 4);
        int chunkSize = *(int*)(mem + pos + 4);
        if (chunkSize < 0) break;

        if (memcmp(id, "fmt ", 4) == 0 && chunkSize >= 16) {
            info->format_tag      = *(unsigned short*)(mem + pos + 8);
            info->channels        = *(unsigned short*)(mem + pos + 10);
            info->sample_rate     = *(int*)(mem + pos + 12);
            info->avg_bytes_sec   = *(int*)(mem + pos + 16);
            info->block_align     = *(unsigned short*)(mem + pos + 20);
            info->bits_per_sample = *(unsigned short*)(mem + pos + 22);
            if (chunkSize >= 18)
                info->extra_size  = *(unsigned short*)(mem + pos + 24);
            /* IMA ADPCM: samples_per_block in extra data */
            if (info->format_tag == 17 && chunkSize >= 20 && info->extra_size >= 2)
                info->samples_per_block = *(unsigned short*)(mem + pos + 26);
            foundFmt = 1;
        }
        if (memcmp(id, "data", 4) == 0) {
            info->data = mem + pos + 8;
            info->data_size = chunkSize;
            if (info->data + info->data_size > mem + memSize)
                info->data_size = (int)(mem + memSize - info->data);
            foundData = 1;
        }
        pos += 8 + ((chunkSize + 1) & ~1); /* chunks are word-aligned */
    }
    return foundFmt && foundData;
}

/* ---- Audio sample structure ---- */
typedef struct {
    int in_use;         /* slot is allocated */
    int status;         /* SMP_FREE / SMP_DONE / SMP_PLAYING / SMP_STOPPED */
    unsigned int magic; /* 0xA5A5A5A5 for validation */

    /* Decoded PCM data (always 16-bit signed) */
    short *pcm_data;    /* interleaved if stereo */
    int pcm_frames;     /* number of frames (samples per channel) */
    int pcm_channels;   /* 1 or 2 */
    int pcm_rate;       /* native sample rate of the data */

    /* Playback state */
    int playback_rate;  /* current rate in Hz (can differ from pcm_rate) */
    double position;    /* fractional frame position for resampling */
    int volume;         /* 0-127 */
    int pan;            /* 0(L) - 64(C) - 127(R) */
    int loop_count;     /* set by game: 0=infinite, 1=once, N=N times */
    int loops_done;     /* how many loops completed */
    int loop_fade;      /* >0: fade-in counter after loop wrap (click prevention) */
    int prev_smpL;      /* last output value for smooth transitions */
    int prev_smpR;
} AE_Sample;

/* ---- Audio stream structure ---- */
typedef struct {
    int in_use;
    int status;
    unsigned int magic;

    /* File info */
    char filename[MAX_PATH];
    const unsigned char *file_mem;  /* if loaded into memory */
    int file_mem_size;
    FILE *file_handle;              /* if streaming from disk */

    /* WAV format */
    int format_tag;
    int channels;
    int sample_rate;
    int bits_per_sample;
    int block_align;
    int samples_per_block;
    int data_offset;    /* offset of 'data' chunk in file */
    int data_size;

    /* Decode buffer (decoded PCM, 16-bit) - double buffered */
    short *decode_buf;
    int decode_buf_alloc; /* allocated frames */
    int decode_buf_frames; /* valid frames in buffer */
    int decode_buf_pos;    /* read position in frames */

    /* Back buffer for pre-decoding (swap with front when mixer needs more) */
    short *decode_back;
    int back_frames;       /* decoded frames in back buffer */
    int back_ready;        /* 1 = back buffer has data ready for swap */

    /* Stream position */
    int data_read;      /* bytes consumed from data chunk */
    ImaAdpcmState adpcm_state[2]; /* per-channel ADPCM state */

    /* Playback */
    int volume;
    int pan;
    int loop_count;
    int loops_done;
    double position;    /* fractional frame for resampling */
} AE_Stream;

/* ---- Engine globals ---- */
static AE_Sample  g_aeSamples[AE_MAX_SAMPLES];
static AE_Stream  g_aeStreams[AE_MAX_STREAMS];
static CRITICAL_SECTION g_aeCS;
static int        g_aeInitialized = 0;
static int        g_aeDriverOpen  = 0;
static int        g_aeMasterVol   = 127; /* 0-127 */
static char       g_aeLastError[256] = "No error";

/* waveOut output */
static HWAVEOUT   g_aeWaveOut = NULL;
static WAVEHDR    g_aeWaveHdr[AE_NUM_BUFFERS];
static short     *g_aeWaveBuf[AE_NUM_BUFFERS];
static int        g_aeBufFrames = 0;  /* frames per buffer */
static volatile int g_aeRunning = 0;

/* Fake driver handle: just a non-NULL sentinel */
static int g_aeFakeDriverData = 0xAE10;
#define AE_FAKE_DRIVER ((HDIGDRIVER)&g_aeFakeDriverData)
#define AE_SAMPLE_MAGIC 0xA5A5A5A5
#define AE_STREAM_MAGIC 0x5E5E5E5E

/* Timer subsystem */
#define AE_MAX_TIMERS 8
typedef void (__stdcall *AE_TimerCB)(MSS_U32 user);
typedef struct {
    int in_use;
    AE_TimerCB callback;
    MSS_U32 user;
    MSS_U32 frequency;
    volatile int running;
    HANDLE thread;
} AE_Timer;
static AE_Timer g_aeTimers[AE_MAX_TIMERS];

/* ---- Forward declarations for pass-through mode ---- */
typedef MSS_S32 (__stdcall *PFN_AIL_startup)(void);
typedef MSS_S32 (__stdcall *PFN_AIL_shutdown)(void);
typedef MSS_S32 (__stdcall *PFN_AIL_set_preference)(MSS_U32, MSS_S32);
typedef MSS_S32 (__stdcall *PFN_AIL_waveOutOpen)(HDIGDRIVER*, void*, MSS_S32, WAVEFORMATEX*);
typedef void    (__stdcall *PFN_AIL_waveOutClose)(HDIGDRIVER);
typedef MSS_S32 (__stdcall *PFN_AIL_digital_configuration)(HDIGDRIVER, MSS_S32*, MSS_S32*, char*);
typedef HSAMPLE (__stdcall *PFN_AIL_allocate_sample_handle)(HDIGDRIVER);
typedef void    (__stdcall *PFN_AIL_release_sample_handle)(HSAMPLE);
typedef MSS_S32 (__stdcall *PFN_AIL_set_sample_file)(HSAMPLE, const void*, MSS_S32);
typedef void    (__stdcall *PFN_AIL_set_sample_loop_count)(HSAMPLE, MSS_S32);
typedef void    (__stdcall *PFN_AIL_start_sample)(HSAMPLE);
typedef void    (__stdcall *PFN_AIL_stop_sample)(HSAMPLE);
typedef void    (__stdcall *PFN_AIL_end_sample)(HSAMPLE);
typedef MSS_S32 (__stdcall *PFN_AIL_sample_status)(HSAMPLE);
typedef void    (__stdcall *PFN_AIL_set_sample_playback_rate)(HSAMPLE, MSS_S32);
typedef MSS_S32 (__stdcall *PFN_AIL_sample_playback_rate)(HSAMPLE);
typedef MSS_S32 (__stdcall *PFN_AIL_sample_loop_count)(HSAMPLE);
typedef void    (__stdcall *PFN_AIL_set_sample_volume)(HSAMPLE, MSS_S32);
typedef MSS_S32 (__stdcall *PFN_AIL_sample_volume)(HSAMPLE);
typedef void    (__stdcall *PFN_AIL_set_sample_pan)(HSAMPLE, MSS_S32);
typedef MSS_S32 (__stdcall *PFN_AIL_sample_pan)(HSAMPLE);
typedef HSTREAM (__stdcall *PFN_AIL_open_stream)(HDIGDRIVER, const char*, MSS_S32);
typedef void    (__stdcall *PFN_AIL_close_stream)(HSTREAM);
typedef void    (__stdcall *PFN_AIL_start_stream)(HSTREAM);
typedef void    (__stdcall *PFN_AIL_pause_stream)(HSTREAM, MSS_S32);
typedef MSS_S32 (__stdcall *PFN_AIL_stream_status)(HSTREAM);
typedef void    (__stdcall *PFN_AIL_set_stream_volume)(HSTREAM, MSS_S32);
typedef void    (__stdcall *PFN_AIL_set_stream_pan)(HSTREAM, MSS_S32);
typedef void    (__stdcall *PFN_AIL_set_stream_loop_count)(HSTREAM, MSS_S32);
typedef MSS_S32 (__stdcall *PFN_AIL_stream_loop_count)(HSTREAM);
typedef void    (__stdcall *PFN_AIL_set_stream_playback_rate)(HSTREAM, MSS_S32);
typedef MSS_S32 (__stdcall *PFN_AIL_stream_position)(HSTREAM);
typedef void    (__stdcall *PFN_AIL_set_stream_position)(HSTREAM, MSS_S32);
typedef void    (__stdcall *PFN_AIL_set_digital_master_volume)(HDIGDRIVER, MSS_S32);
typedef MSS_S32 (__stdcall *PFN_AIL_digital_master_volume)(HDIGDRIVER);
typedef char*   (__stdcall *PFN_AIL_last_error)(void);
typedef void    (__stdcall *PFN_AIL_set_sample_address)(HSAMPLE, const void*, MSS_U32);
typedef void    (__stdcall *PFN_AIL_set_sample_type)(HSAMPLE, MSS_S32, MSS_U32);

/* Real function pointers (for pass-through mode) */
static PFN_AIL_startup               real_AIL_startup = NULL;
static PFN_AIL_shutdown              real_AIL_shutdown = NULL;
static PFN_AIL_set_preference        real_AIL_set_preference = NULL;
static PFN_AIL_waveOutOpen           real_AIL_waveOutOpen = NULL;
static PFN_AIL_waveOutClose          real_AIL_waveOutClose = NULL;
static PFN_AIL_digital_configuration real_AIL_digital_configuration = NULL;
static PFN_AIL_allocate_sample_handle real_AIL_allocate_sample_handle = NULL;
static PFN_AIL_release_sample_handle real_AIL_release_sample_handle = NULL;
static PFN_AIL_set_sample_file       real_AIL_set_sample_file = NULL;
static PFN_AIL_set_sample_loop_count real_AIL_set_sample_loop_count = NULL;
static PFN_AIL_start_sample          real_AIL_start_sample = NULL;
static PFN_AIL_stop_sample           real_AIL_stop_sample = NULL;
static PFN_AIL_end_sample            real_AIL_end_sample = NULL;
static PFN_AIL_sample_status         real_AIL_sample_status = NULL;
static PFN_AIL_set_sample_playback_rate real_AIL_set_sample_playback_rate = NULL;
static PFN_AIL_sample_playback_rate  real_AIL_sample_playback_rate = NULL;
static PFN_AIL_sample_loop_count     real_AIL_sample_loop_count = NULL;
static PFN_AIL_set_sample_volume     real_AIL_set_sample_volume = NULL;
static PFN_AIL_sample_volume         real_AIL_sample_volume = NULL;
static PFN_AIL_set_sample_pan        real_AIL_set_sample_pan = NULL;
static PFN_AIL_sample_pan            real_AIL_sample_pan = NULL;
static PFN_AIL_open_stream           real_AIL_open_stream = NULL;
static PFN_AIL_close_stream          real_AIL_close_stream = NULL;
static PFN_AIL_start_stream          real_AIL_start_stream = NULL;
static PFN_AIL_pause_stream          real_AIL_pause_stream = NULL;
static PFN_AIL_stream_status         real_AIL_stream_status = NULL;
static PFN_AIL_set_stream_volume     real_AIL_set_stream_volume = NULL;
static PFN_AIL_set_stream_pan        real_AIL_set_stream_pan = NULL;
static PFN_AIL_set_stream_loop_count real_AIL_set_stream_loop_count = NULL;
static PFN_AIL_stream_loop_count     real_AIL_stream_loop_count = NULL;
static PFN_AIL_set_stream_playback_rate real_AIL_set_stream_playback_rate = NULL;
static PFN_AIL_stream_position       real_AIL_stream_position = NULL;
static PFN_AIL_set_stream_position   real_AIL_set_stream_position = NULL;
static PFN_AIL_set_digital_master_volume real_AIL_set_digital_master_volume = NULL;
static PFN_AIL_digital_master_volume real_AIL_digital_master_volume = NULL;
static PFN_AIL_last_error            real_AIL_last_error = NULL;
static PFN_AIL_set_sample_address    real_AIL_set_sample_address = NULL;
static PFN_AIL_set_sample_type       real_AIL_set_sample_type = NULL;


/* ==================================================================
 *  SOFTWARE MIXER
 * ================================================================== */

/* Forward declarations for stream helpers */
static void AE_StreamFillBuffer(AE_Stream *st);
static int  AE_StreamDecodeInto(AE_Stream *st, short *outBuf, int maxFrames);
static void AE_PreDecodeStreams(void);

/*
 * Mix all active samples and streams into a stereo 16-bit output buffer.
 * Called from the waveOut callback with g_aeCS held.
 * outBuf: interleaved stereo, outFrames: number of frames to fill.
 */
static void AE_MixAudio(short *outBuf, int outFrames) {
    /* Use 32-bit accumulators to avoid overflow */
    static int *mixBuf = NULL;
    static int mixBufAlloc = 0;
    int totalSamples = outFrames * AE_OUTPUT_CHANNELS;

    if (mixBufAlloc < totalSamples) {
        if (mixBuf) free(mixBuf);
        mixBuf = (int*)malloc(totalSamples * sizeof(int));
        if (!mixBuf) { mixBufAlloc = 0; memset(outBuf, 0, outFrames * AE_OUTPUT_CHANNELS * sizeof(short)); return; }
        mixBufAlloc = totalSamples;
    }
    memset(mixBuf, 0, totalSamples * sizeof(int));

    /* Mix each active sample */
    for (int si = 0; si < AE_MAX_SAMPLES; si++) {
        AE_Sample *s = &g_aeSamples[si];
        if (!s->in_use || s->status != SMP_PLAYING) continue;
        if (!s->pcm_data || s->pcm_frames == 0) continue;

        /* Calculate volume: sample vol * master vol / 127 */
        int vol = (s->volume * g_aeMasterVol) / 127;
        /* MSS pan law: 0=left, 64=center, 127=right
         * Center (64) = full volume on both channels
         * Left (0) = full left, zero right
         * Right (127) = zero left, full right */
        int volL, volR;
        if (s->pan <= 64) {
            volL = vol;
            volR = (vol * s->pan) / 64;
        } else {
            volL = (vol * (127 - s->pan)) / 63;
            volR = vol;
        }

        double rate_ratio = (double)s->playback_rate / (double)AE_OUTPUT_RATE;
        double pos = s->position;

        for (int f = 0; f < outFrames; f++) {
            int ipos = (int)pos;

            /* Check bounds and handle looping */
            if (ipos >= s->pcm_frames) {
                s->loops_done++;
                if (s->loop_count != 0 && s->loops_done >= s->loop_count) {
                    s->status = SMP_DONE;
                    break;
                }
                /* Preserve fractional overshoot for seamless loop */
                pos -= (double)s->pcm_frames;
                ipos = (int)pos;
                if (ipos < 0) { ipos = 0; pos = 0.0; }
                /* Start crossfade to prevent click (64 samples ≈ 3ms @ 22050) */
                s->loop_fade = 64;
            }

            /* Linear interpolation resampler (eliminates aliasing vs point sampling) */
            int smpL, smpR;
            double frac = pos - (double)ipos;
            if (s->pcm_channels == 1) {
                int v0 = s->pcm_data[ipos];
                int v1 = (ipos + 1 < s->pcm_frames) ? s->pcm_data[ipos + 1] : v0;
                int v = v0 + (int)((double)(v1 - v0) * frac);
                smpL = (v * volL) / 127;
                smpR = (v * volR) / 127;
            } else {
                int vL0 = s->pcm_data[ipos * 2];
                int vR0 = s->pcm_data[ipos * 2 + 1];
                int idx1 = (ipos + 1 < s->pcm_frames) ? (ipos + 1) : ipos;
                int vL1 = s->pcm_data[idx1 * 2];
                int vR1 = s->pcm_data[idx1 * 2 + 1];
                int vL = vL0 + (int)((double)(vL1 - vL0) * frac);
                int vR = vR0 + (int)((double)(vR1 - vR0) * frac);
                smpL = (vL * volL) / 127;
                smpR = (vR * volR) / 127;
            }

            /* Crossfade at loop boundary to prevent clicks */
            if (s->loop_fade > 0) {
                int t = 64 - s->loop_fade; /* 0..63 */
                smpL = (s->prev_smpL * (64 - t) + smpL * t) / 64;
                smpR = (s->prev_smpR * (64 - t) + smpR * t) / 64;
                s->loop_fade--;
            }
            s->prev_smpL = smpL;
            s->prev_smpR = smpR;

            mixBuf[f * 2]     += smpL;
            mixBuf[f * 2 + 1] += smpR;
            pos += rate_ratio;
        }
        s->position = pos;
    }

    /* Mix each active stream */
    for (int si = 0; si < AE_MAX_STREAMS; si++) {
        AE_Stream *st = &g_aeStreams[si];
        if (!st->in_use || st->status != SMP_PLAYING) continue;
        if (!st->decode_buf || st->decode_buf_frames == 0) continue;

        int vol = (st->volume * g_aeMasterVol) / 127;
        int volL, volR;
        if (st->pan <= 64) {
            volL = vol;
            volR = (vol * st->pan) / 64;
        } else {
            volL = (vol * (127 - st->pan)) / 63;
            volR = vol;
        }

        double rate_ratio = (double)st->sample_rate / (double)AE_OUTPUT_RATE;
        double pos = st->position;

        for (int f = 0; f < outFrames; f++) {
            int ipos = (int)pos;

            /* Need more decoded data? */
            if (ipos >= st->decode_buf_frames) {
                /* Check if all data consumed */
                if (st->data_read >= st->data_size && !st->back_ready) {
                    st->loops_done++;
                    if (st->loop_count != 0 && st->loops_done >= st->loop_count) {
                        st->status = SMP_DONE;
                        break;
                    }
                    /* Loop: reset read position, invalidate pre-decoded back */
                    st->data_read = 0;
                    st->back_ready = 0;
                }
                double overshoot = pos - (double)st->decode_buf_frames;
                if (st->back_ready && st->decode_back) {
                    /* Fast path: swap pre-decoded back buffer (no decode stall) */
                    short *tmp = st->decode_buf;
                    st->decode_buf = st->decode_back;
                    st->decode_back = tmp;
                    st->decode_buf_frames = st->back_frames;
                    st->back_ready = 0;
                } else {
                    /* Fallback: inline decode (first buffer, or back not ready) */
                    AE_StreamFillBuffer(st);
                }
                pos = overshoot > 0.0 ? overshoot : 0.0;
                ipos = (int)pos;
                if (st->decode_buf_frames == 0) {
                    /* No more data available */
                    st->status = SMP_DONE;
                    break;
                }
            }

            int smpL, smpR;
            double frac = pos - (double)ipos;
            if (st->channels == 1) {
                int v0 = st->decode_buf[ipos];
                int v1 = (ipos + 1 < st->decode_buf_frames) ? st->decode_buf[ipos + 1] : v0;
                int v = v0 + (int)((double)(v1 - v0) * frac);
                smpL = (v * volL) / 127;
                smpR = (v * volR) / 127;
            } else {
                int vL0 = st->decode_buf[ipos * 2];
                int vR0 = st->decode_buf[ipos * 2 + 1];
                int idx1 = (ipos + 1 < st->decode_buf_frames) ? (ipos + 1) : ipos;
                int vL1 = st->decode_buf[idx1 * 2];
                int vR1 = st->decode_buf[idx1 * 2 + 1];
                int vL = vL0 + (int)((double)(vL1 - vL0) * frac);
                int vR = vR0 + (int)((double)(vR1 - vR0) * frac);
                smpL = (vL * volL) / 127;
                smpR = (vR * volR) / 127;
            }

            mixBuf[f * 2]     += smpL;
            mixBuf[f * 2 + 1] += smpR;
            pos += rate_ratio;
        }
        st->position = pos;
    }

    /* Clip to 16-bit. Volume is controlled via MasterVolume INI setting. */
    for (int i = 0; i < totalSamples; i++) {
        int v = mixBuf[i];
        if (v >  32767) v =  32767;
        if (v < -32768) v = -32768;
        outBuf[i] = (short)v;
    }
}


/* ==================================================================
 *  WAVEOUT OUTPUT (polling thread - avoids CALLBACK_FUNCTION issues)
 * ================================================================== */

static HANDLE g_aeThread = NULL;

static DWORD WINAPI AE_AudioThread(LPVOID param) {
    (void)param;
    timeBeginPeriod(1); /* ensure 1ms Sleep resolution */
    LogMsg("AE: audio thread started\n");
    int curBuf = 0;
    while (g_aeRunning) {
        /* Wait for current buffer to complete */
        while (!(g_aeWaveHdr[curBuf].dwFlags & WHDR_DONE)) {
            if (!g_aeRunning) goto done;
            Sleep(1);
        }
        /* Fill buffer with mixed audio */
        short *buf = g_aeWaveBuf[curBuf];

        /* Use TryEnterCriticalSection to avoid deadlocks.
         * If the game thread holds the CS (doing open_stream, set_sample_file, etc.),
         * we output silence instead of blocking the audio thread. */
        if (TryEnterCriticalSection(&g_aeCS)) {
            __try {
                AE_MixAudio(buf, g_aeBufFrames);
                /* Pre-decode stream back buffers while we hold the CS.
                 * This ensures the next buffer swap in the mixer is instant. */
                AE_PreDecodeStreams();
            } __except(EXCEPTION_EXECUTE_HANDLER) {
                memset(buf, 0, g_aeBufFrames * AE_OUTPUT_CHANNELS * sizeof(short));
            }
            LeaveCriticalSection(&g_aeCS);
        } else {
            /* CS contention - output silence to avoid glitch/hang */
            memset(buf, 0, g_aeBufFrames * AE_OUTPUT_CHANNELS * sizeof(short));
        }

        /* Resubmit buffer */
        waveOutWrite(g_aeWaveOut, &g_aeWaveHdr[curBuf], sizeof(WAVEHDR));
        curBuf = (curBuf + 1) % AE_NUM_BUFFERS;
    }
done:
    timeEndPeriod(1);
    LogMsg("AE: audio thread exiting\n");
    return 0;
}

static int AE_StartOutput(void) {
    LogMsg("AE: AE_StartOutput() enter\n");
    WAVEFORMATEX wfx = {0};
    wfx.wFormatTag      = WAVE_FORMAT_PCM;
    wfx.nChannels       = AE_OUTPUT_CHANNELS;
    wfx.nSamplesPerSec  = AE_OUTPUT_RATE;
    wfx.wBitsPerSample  = AE_OUTPUT_BITS;
    wfx.nBlockAlign     = (wfx.nChannels * wfx.wBitsPerSample) / 8;
    wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;

    /* Use CALLBACK_NULL - we poll with our own thread */
    MMRESULT mr = waveOutOpen(&g_aeWaveOut, WAVE_MAPPER, &wfx,
        0, 0, CALLBACK_NULL);
    LogMsg("AE: waveOutOpen -> %d (handle=%p)\n", mr, g_aeWaveOut);
    if (mr != MMSYSERR_NOERROR) {
        snprintf(g_aeLastError, sizeof(g_aeLastError), "waveOutOpen failed: %d", mr);
        return 0;
    }

    g_aeBufFrames = (AE_OUTPUT_RATE * AE_BUFFER_MS) / 1000;
    if (g_aeBufFrames < 64) g_aeBufFrames = 64;
    int bufBytes = g_aeBufFrames * AE_OUTPUT_CHANNELS * sizeof(short);

    /* Prepare and submit initial silence buffers */
    for (int i = 0; i < AE_NUM_BUFFERS; i++) {
        g_aeWaveBuf[i] = (short*)calloc(1, bufBytes);
        if (!g_aeWaveBuf[i]) { LogMsg("AE: calloc failed buf %d\n", i); return 0; }
        memset(&g_aeWaveHdr[i], 0, sizeof(WAVEHDR));
        g_aeWaveHdr[i].lpData = (LPSTR)g_aeWaveBuf[i];
        g_aeWaveHdr[i].dwBufferLength = bufBytes;
        waveOutPrepareHeader(g_aeWaveOut, &g_aeWaveHdr[i], sizeof(WAVEHDR));
        waveOutWrite(g_aeWaveOut, &g_aeWaveHdr[i], sizeof(WAVEHDR));
    }

    /* Start the audio polling thread */
    g_aeRunning = 1;
    g_aeThread = CreateThread(NULL, 0, AE_AudioThread, NULL, 0, NULL);
    if (g_aeThread) {
        SetThreadPriority(g_aeThread, THREAD_PRIORITY_TIME_CRITICAL);
    }

    LogMsg("AE: output started (%d Hz, %d-bit, %dch, %dms x%d, %d frames/buf, thread=%p)\n",
        AE_OUTPUT_RATE, AE_OUTPUT_BITS, AE_OUTPUT_CHANNELS,
        AE_BUFFER_MS, AE_NUM_BUFFERS, g_aeBufFrames, g_aeThread);
    return 1;
}

static void AE_StopOutput(void) {
    g_aeRunning = 0;
    if (g_aeThread) {
        WaitForSingleObject(g_aeThread, 2000);
        CloseHandle(g_aeThread);
        g_aeThread = NULL;
    }
    if (g_aeWaveOut) {
        waveOutReset(g_aeWaveOut);
        for (int i = 0; i < AE_NUM_BUFFERS; i++) {
            if (g_aeWaveHdr[i].dwFlags & WHDR_PREPARED)
                waveOutUnprepareHeader(g_aeWaveOut, &g_aeWaveHdr[i], sizeof(WAVEHDR));
            free(g_aeWaveBuf[i]);
            g_aeWaveBuf[i] = NULL;
        }
        waveOutClose(g_aeWaveOut);
        g_aeWaveOut = NULL;
    }
    LogMsg("AE: output stopped\n");
}

/* Lazy start: only open waveOut when first audio actually plays */
static void AE_EnsureOutputStarted(void) {
    if (!g_aeRunning && g_aeDriverOpen) {
        LogMsg("AE: lazy-starting output (first play)\n");
        int ok = AE_StartOutput();
        LogMsg("AE: lazy-start %s\n", ok ? "OK" : "FAILED");
    }
}


/* ==================================================================
 *  STREAM DECODE HELPERS
 * ================================================================== */

/*
 * Decode PCM stream data from file_mem into decode_buf.
 * Returns number of frames decoded.
 */
static int AE_StreamDecodePCM(AE_Stream *st, int maxFrames) {
    if (!st->file_mem || st->data_read >= st->data_size) return 0;

    int bytesPerFrame = st->channels * (st->bits_per_sample / 8);
    if (bytesPerFrame == 0) return 0;
    int available = (st->data_size - st->data_read) / bytesPerFrame;
    int frames = available < maxFrames ? available : maxFrames;

    const unsigned char *src = st->file_mem + st->data_offset + st->data_read;

    if (st->bits_per_sample == 16) {
        /* Already 16-bit PCM - copy directly */
        memcpy(st->decode_buf, src, frames * st->channels * sizeof(short));
    } else if (st->bits_per_sample == 8) {
        /* 8-bit unsigned -> 16-bit signed */
        int total = frames * st->channels;
        for (int i = 0; i < total; i++)
            st->decode_buf[i] = (short)((src[i] - 128) << 8);
    }

    st->data_read += frames * bytesPerFrame;
    return frames;
}

/*
 * Decode IMA ADPCM stream data from file_mem into decode_buf.
 * Decodes one block at a time.
 */
static int AE_StreamDecodeADPCM(AE_Stream *st, int maxFrames) {
    if (!st->file_mem || st->data_read >= st->data_size) return 0;
    if (st->block_align == 0) return 0;

    /* Calculate samples per block to avoid partial block decode */
    int samplesPerBlock = st->samples_per_block;
    if (samplesPerBlock <= 0)
        samplesPerBlock = (st->block_align - 4 * st->channels) * 2 / st->channels + 1;

    int totalFrames = 0;
    short *out = st->decode_buf;

    /* Only decode blocks that FULLY fit in the output buffer */
    while (totalFrames + samplesPerBlock <= maxFrames &&
           st->data_read + st->block_align <= st->data_size) {
        const unsigned char *block = st->file_mem + st->data_offset + st->data_read;

        if (st->channels == 1) {
            int n = ImaAdpcm_DecodeBlockMono(block, st->block_align,
                out + totalFrames, samplesPerBlock);
            totalFrames += n;
        } else {
            short tmpL[8192], tmpR[8192];
            int maxDec = samplesPerBlock < 8192 ? samplesPerBlock : 8192;
            int n = ImaAdpcm_DecodeBlockStereo(block, st->block_align,
                tmpL, tmpR, maxDec);
            for (int i = 0; i < n; i++) {
                out[totalFrames * 2]     = tmpL[i];
                out[totalFrames * 2 + 1] = tmpR[i];
                totalFrames++;
            }
        }
        st->data_read += st->block_align;
    }
    return totalFrames;
}

/*
 * Fill the stream's decode buffer with the next chunk of audio.
 * Called from the mixer when data is needed (fallback path).
 */
static void AE_StreamFillBuffer(AE_Stream *st) {
    int frames;
    if (st->format_tag == 1) { /* PCM */
        frames = AE_StreamDecodePCM(st, st->decode_buf_alloc);
    } else if (st->format_tag == 17) { /* IMA ADPCM */
        frames = AE_StreamDecodeADPCM(st, st->decode_buf_alloc);
    } else {
        frames = 0;
    }
    st->decode_buf_frames = frames;
    st->decode_buf_pos = 0;
    st->position = 0.0;
}

/*
 * Decode into a specified output buffer (used for back-buffer pre-fill).
 * Does NOT reset stream position — only advances data_read.
 */
static int AE_StreamDecodeInto(AE_Stream *st, short *outBuf, int maxFrames) {
    /* Temporarily redirect decode output */
    short *save = st->decode_buf;
    st->decode_buf = outBuf;
    int frames;
    if (st->format_tag == 1)
        frames = AE_StreamDecodePCM(st, maxFrames);
    else if (st->format_tag == 17)
        frames = AE_StreamDecodeADPCM(st, maxFrames);
    else
        frames = 0;
    st->decode_buf = save;
    return frames;
}

/*
 * Pre-decode back buffers for all active streams.
 * Called from audio thread AFTER mix, under g_aeCS.
 * This ensures the next buffer swap in the mixer is instant (no decode stall).
 */
static void AE_PreDecodeStreams(void) {
    for (int si = 0; si < AE_MAX_STREAMS; si++) {
        AE_Stream *st = &g_aeStreams[si];
        if (!st->in_use || st->status != SMP_PLAYING) continue;
        if (st->back_ready) continue;  /* already pre-filled */
        if (!st->decode_back) continue; /* no back buffer allocated */
        if (st->data_read >= st->data_size) continue; /* at end, mixer handles loop */

        int frames = AE_StreamDecodeInto(st, st->decode_back, st->decode_buf_alloc);
        st->back_frames = frames;
        st->back_ready = (frames > 0) ? 1 : 0;
    }
}


/* ==================================================================
 *  MSS API IMPLEMENTATION (Hook functions)
 * ================================================================== */

static MSS_S32 __stdcall Hook_AIL_startup(void) {
    if (g_config.use_custom_audio) {
        /* Do NOT call real_AIL_startup — MSS starts internal mixer thread
         * that crashes with NULL driver (ACCESS_VIOLATION at mss+0x139FB).
         * Timer functions are now implemented natively. */
        if (!g_aeInitialized) {
            InitializeCriticalSection(&g_aeCS);
            memset(g_aeSamples, 0, sizeof(g_aeSamples));
            memset(g_aeStreams, 0, sizeof(g_aeStreams));
            for (int i = 0; i < AE_MAX_SAMPLES; i++)
                g_aeSamples[i].status = SMP_FREE;
            for (int i = 0; i < AE_MAX_STREAMS; i++)
                g_aeStreams[i].status = SMP_FREE;
            g_aeMasterVol = g_config.audio_master_volume;
            g_aeInitialized = 1;
            LogMsg("AE: AIL_startup -> custom engine initialized (no real MSS)\n");
        }
        return 1;
    }
    /* Pass-through */
    MSS_S32 r = real_AIL_startup ? real_AIL_startup() : 0;
    LogMsg("_AIL_startup() -> %d [passthrough]\n", r);
    return r;
}

static MSS_S32 __stdcall Hook_AIL_shutdown(void) {
    if (g_config.use_custom_audio) {
        /* Stop all timers first */
        for (int i = 0; i < AE_MAX_TIMERS; i++) {
            if (g_aeTimers[i].running) {
                g_aeTimers[i].running = 0;
                if (g_aeTimers[i].thread) {
                    WaitForSingleObject(g_aeTimers[i].thread, 2000);
                    CloseHandle(g_aeTimers[i].thread);
                }
            }
            memset(&g_aeTimers[i], 0, sizeof(AE_Timer));
        }
        AE_StopOutput();
        EnterCriticalSection(&g_aeCS);
        for (int i = 0; i < AE_MAX_SAMPLES; i++) {
            if (g_aeSamples[i].pcm_data) free(g_aeSamples[i].pcm_data);
            memset(&g_aeSamples[i], 0, sizeof(AE_Sample));
        }
        for (int i = 0; i < AE_MAX_STREAMS; i++) {
            if (g_aeStreams[i].decode_buf)  free(g_aeStreams[i].decode_buf);
            if (g_aeStreams[i].decode_back) free(g_aeStreams[i].decode_back);
            if (g_aeStreams[i].file_mem)    free((void*)g_aeStreams[i].file_mem);
            if (g_aeStreams[i].file_handle) fclose(g_aeStreams[i].file_handle);
            memset(&g_aeStreams[i], 0, sizeof(AE_Stream));
        }
        LeaveCriticalSection(&g_aeCS);
        DeleteCriticalSection(&g_aeCS);
        g_aeInitialized = 0;
        g_aeDriverOpen = 0;
        LogMsg("AE: AIL_shutdown -> engine destroyed\n");
        return 1;
    }
    return real_AIL_shutdown ? real_AIL_shutdown() : 0;
}

/* Preference names for logging */
static const char* MSS_PrefName(MSS_U32 id) {
    switch(id) {
        case  0: return "DIG_MIXER_CHANNELS";
        case  1: return "DIG_DEFAULT_VOLUME";
        case  2: return "DIG_RESAMPLING_TOLERANCE";
        case  3: return "DIG_MIXER_ADPCM";
        case  4: return "DIG_OUTPUT_BUFFER_SIZE";
        case  5: return "DIG_LATENCY";
        case  6: return "DIG_USE_STEREO";
        case  7: return "DIG_INPUT_LATENCY";
        case  8: return "MDI_SERVICE_RATE";
        case  9: return "MDI_SEQUENCES";
        case 10: return "MDI_DEFAULT_VOLUME";
        case 11: return "MDI_QUANT_ADVANCE";
        case 12: return "MDI_DOUBLE_NOTE_OFF";
        case 13: return "DIG_ENABLE_RESAMPLE_FILTER";
        case 14: return "DIG_DECODE_BUFFER_SIZE";
        case 15: return "DIG_DS_MIX_SPEED";
        case 16: return "DIG_DS_USE_PRIMARY";
        case 17: return "DIG_USE_WAVEOUT";
        default: return "UNKNOWN";
    }
}

static MSS_S32 __stdcall Hook_AIL_set_preference(MSS_U32 number, MSS_S32 value) {
    LogMsg("_AIL_set_preference(%u [%s], %d)%s\n",
        number, MSS_PrefName(number), value,
        g_config.use_custom_audio ? " [custom: ignored]" : "");
    if (g_config.use_custom_audio) {
        /* In custom mode, we ignore MSS preferences - we handle everything ourselves */
        return 0;
    }
    /* Pass-through with crackle fix overrides */
    MSS_S32 origValue = value;
    if (g_config.fix_audio_crackle) {
        switch(number) {
            case 4: value = g_config.audio_buffer_size > 0 ? g_config.audio_buffer_size : (value < 16384 ? 16384 : value); break;
            case 5: value = g_config.audio_latency > 0 ? g_config.audio_latency : (value < 300 ? 300 : value); break;
        }
    }
    if (g_config.audio_use_waveout && number == 17) value = 1;
    if (value != origValue) LogMsg("  -> overridden to %d\n", value);
    return real_AIL_set_preference ? real_AIL_set_preference(number, value) : 0;
}

static MSS_S32 __stdcall Hook_AIL_waveOutOpen(HDIGDRIVER *drvr, void *phwo,
    MSS_S32 wDeviceID, WAVEFORMATEX *lpFormat)
{
    if (g_config.use_custom_audio) {
        /* Don't start waveOut output yet - defer until first play.
         * This avoids the callback thread running during game loading. */
        LogMsg("AE: AIL_waveOutOpen -> deferred (will start on first play)\n");
        g_aeDriverOpen = 1;
        if (drvr) *drvr = AE_FAKE_DRIVER;
        return 0;
    }
    /* Pass-through */
    if (g_config.audio_use_waveout && real_AIL_set_preference)
        real_AIL_set_preference(17, 1);
    if (g_config.fix_audio_crackle && real_AIL_set_preference) {
        int lat = g_config.audio_latency > 0 ? g_config.audio_latency : 300;
        int buf = g_config.audio_buffer_size > 0 ? g_config.audio_buffer_size : 16384;
        real_AIL_set_preference(5, lat);
        real_AIL_set_preference(4, buf);
    }
    LogMsg("_AIL_waveOutOpen [passthrough]\n");
    return real_AIL_waveOutOpen ? real_AIL_waveOutOpen(drvr, phwo, wDeviceID, lpFormat) : -1;
}

static void __stdcall Hook_AIL_waveOutClose(HDIGDRIVER dig) {
    if (g_config.use_custom_audio) {
        LogMsg("AE: AIL_waveOutClose\n");
        AE_StopOutput();
        g_aeDriverOpen = 0;
        return;
    }
    if (real_AIL_waveOutClose) real_AIL_waveOutClose(dig);
}

static MSS_S32 __stdcall Hook_AIL_digital_configuration(HDIGDRIVER dig,
    MSS_S32 *rate, MSS_S32 *format, char *string)
{
    if (g_config.use_custom_audio) {
        if (rate)   *rate = AE_OUTPUT_RATE;
        if (format) *format = 8; /* stereo 16-bit */
        if (string) strcpy(string, "AITD Custom Audio Engine");
        LogMsg("AE: digital_configuration -> rate=%d format=8\n", AE_OUTPUT_RATE);
        return 0;
    }
    MSS_S32 r = real_AIL_digital_configuration ?
        real_AIL_digital_configuration(dig, rate, format, string) : -1;
    if (g_digConfigLogCount < 1) {
        g_digConfigLogCount++;
        LogMsg("_AIL_digital_configuration -> %d\n", r);
    }
    return r;
}

static HSAMPLE __stdcall Hook_AIL_allocate_sample_handle(HDIGDRIVER dig) {
    if (g_config.use_custom_audio) {
        EnterCriticalSection(&g_aeCS);
        for (int i = 0; i < AE_MAX_SAMPLES; i++) {
            if (!g_aeSamples[i].in_use) {
                memset(&g_aeSamples[i], 0, sizeof(AE_Sample));
                g_aeSamples[i].in_use = 1;
                g_aeSamples[i].status = SMP_DONE;  /* DONE = "ready to use" in MSS */
                g_aeSamples[i].magic  = AE_SAMPLE_MAGIC;
                g_aeSamples[i].volume = 127;
                g_aeSamples[i].pan    = 64;
                LeaveCriticalSection(&g_aeCS);
                LogMsg("AE: allocate_sample_handle -> slot %d (%p)\n", i, &g_aeSamples[i]);
                return (HSAMPLE)&g_aeSamples[i];
            }
        }
        LeaveCriticalSection(&g_aeCS);
        LogMsg("AE: allocate_sample_handle -> FAILED (all slots used)\n");
        return NULL;
    }
    HSAMPLE h = real_AIL_allocate_sample_handle ?
        real_AIL_allocate_sample_handle(dig) : NULL;
    LogMsg("_AIL_allocate_sample_handle -> %p [passthrough]\n", h);
    return h;
}

static void __stdcall Hook_AIL_release_sample_handle(HSAMPLE sample) {
    if (g_config.use_custom_audio) {
        AE_Sample *s = (AE_Sample*)sample;
        if (!s || s->magic != AE_SAMPLE_MAGIC) return;
        EnterCriticalSection(&g_aeCS);
        LogMsg("AE: release_sample_handle(%p)\n", s);
        if (s->pcm_data) { free(s->pcm_data); s->pcm_data = NULL; }
        s->in_use = 0;
        s->status = SMP_FREE;
        s->magic  = 0;
        LeaveCriticalSection(&g_aeCS);
        return;
    }
    if (real_AIL_release_sample_handle) real_AIL_release_sample_handle(sample);
}

/*
 * set_sample_file: parse WAV, decode ADPCM if needed, store as PCM.
 * This is where the ADPCM fix happens - we decode correctly here.
 */
static MSS_S32 __stdcall Hook_AIL_set_sample_file(HSAMPLE sample,
    const void *file_image, MSS_S32 block)
{
    if (g_config.use_custom_audio) {
        AE_Sample *s = (AE_Sample*)sample;
        if (!s || s->magic != AE_SAMPLE_MAGIC) {
            LogMsg("AE: set_sample_file -> bad handle %p\n", sample);
            return 0;
        }
        if (!file_image) {
            LogMsg("AE: set_sample_file(slot%d) -> NULL file_image\n", (int)(s - g_aeSamples));
            return 0;
        }

        int slotIdx = (int)(s - g_aeSamples);
        const unsigned char *mem = (const unsigned char*)file_image;

        /* Validate RIFF header before parsing */
        __try {
            if (mem[0]!='R' || mem[1]!='I' || mem[2]!='F' || mem[3]!='F') {
                LogMsg("AE: set_sample_file(slot%d, %p) -> not RIFF (0x%02X%02X%02X%02X)\n",
                    slotIdx, file_image, mem[0], mem[1], mem[2], mem[3]);
                return 0;
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            LogMsg("AE: set_sample_file(slot%d) -> EXCEPTION reading header at %p\n",
                slotIdx, file_image);
            return 0;
        }

        int memSize = 8 + *(int*)(mem + 4);
        if (memSize < 44 || memSize > 64 * 1024 * 1024) { /* sanity: 44B min, 64MB max */
            LogMsg("AE: set_sample_file(slot%d) -> bad RIFF size %d\n", slotIdx, memSize);
            return 0;
        }

        LogMsg("AE: set_sample_file(slot%d, %p, block=%d, riffSize=%d) ENTER\n",
            slotIdx, file_image, block, memSize);

        EnterCriticalSection(&g_aeCS);

        /* Free previous data */
        if (s->pcm_data) { free(s->pcm_data); s->pcm_data = NULL; }
        s->pcm_frames = 0;
        s->position = 0.0;
        s->loops_done = 0;

        __try {
            WavInfo wav;
            if (!ParseWav(mem, memSize, &wav)) {
                LogMsg("AE: set_sample_file(slot%d) -> WAV parse FAILED\n", slotIdx);
                s->status = SMP_DONE;
                LeaveCriticalSection(&g_aeCS);
                return 0;
            }

            s->pcm_channels = wav.channels;
            s->pcm_rate     = wav.sample_rate;
            s->playback_rate = wav.sample_rate;

            if (wav.format_tag == 1) {
                /* PCM */
                int bytesPerSample = wav.bits_per_sample / 8;
                if (bytesPerSample == 0) bytesPerSample = 2;
                s->pcm_frames = wav.data_size / (wav.channels * bytesPerSample);
                if (s->pcm_frames <= 0) {
                    LogMsg("AE: set_sample_file(slot%d) PCM 0 frames\n", slotIdx);
                    s->status = SMP_DONE;
                    LeaveCriticalSection(&g_aeCS);
                    return 0;
                }
                s->pcm_data = (short*)malloc(s->pcm_frames * wav.channels * sizeof(short));
                if (!s->pcm_data) {
                    LogMsg("AE: set_sample_file(slot%d) malloc failed (%d frames)\n",
                        slotIdx, s->pcm_frames);
                    s->pcm_frames = 0;
                    s->status = SMP_DONE;
                    LeaveCriticalSection(&g_aeCS);
                    return 0;
                }

                if (wav.bits_per_sample == 16) {
                    memcpy(s->pcm_data, wav.data, s->pcm_frames * wav.channels * sizeof(short));
                } else if (wav.bits_per_sample == 8) {
                    int total = s->pcm_frames * wav.channels;
                    for (int i = 0; i < total; i++)
                        s->pcm_data[i] = (short)((wav.data[i] - 128) << 8);
                }
                LogMsg("AE: set_sample_file(slot%d) PCM %dch %dHz %d frames OK\n",
                    slotIdx, wav.channels, wav.sample_rate, s->pcm_frames);

            } else if (wav.format_tag == 17) {
                /* IMA ADPCM */
                if (wav.block_align == 0) {
                    LogMsg("AE: set_sample_file(slot%d) ADPCM block_align=0\n", slotIdx);
                    s->status = SMP_DONE;
                    LeaveCriticalSection(&g_aeCS);
                    return 0;
                }
                int numBlocks = wav.data_size / wav.block_align;
                int samplesPerBlock = wav.samples_per_block;
                if (samplesPerBlock == 0)
                    samplesPerBlock = (wav.block_align - 4 * wav.channels) * 2 / wav.channels + 1;
                if (samplesPerBlock <= 0 || numBlocks <= 0) {
                    LogMsg("AE: set_sample_file(slot%d) ADPCM bad params: blocks=%d spb=%d\n",
                        slotIdx, numBlocks, samplesPerBlock);
                    s->status = SMP_DONE;
                    LeaveCriticalSection(&g_aeCS);
                    return 0;
                }
                int totalFrames = numBlocks * samplesPerBlock;

                s->pcm_data = (short*)malloc(totalFrames * wav.channels * sizeof(short));
                if (!s->pcm_data) {
                    LogMsg("AE: set_sample_file(slot%d) ADPCM malloc failed\n", slotIdx);
                    s->pcm_frames = 0;
                    s->status = SMP_DONE;
                    LeaveCriticalSection(&g_aeCS);
                    return 0;
                }
                s->pcm_frames = 0;

                const unsigned char *blockPtr = wav.data;
                int dataRemain = wav.data_size;

                if (wav.channels == 1) {
                    while (dataRemain >= wav.block_align) {
                        int n = ImaAdpcm_DecodeBlockMono(blockPtr, wav.block_align,
                            s->pcm_data + s->pcm_frames, totalFrames - s->pcm_frames);
                        s->pcm_frames += n;
                        blockPtr += wav.block_align;
                        dataRemain -= wav.block_align;
                    }
                } else {
                    short *tmpL = (short*)malloc(samplesPerBlock * sizeof(short));
                    short *tmpR = (short*)malloc(samplesPerBlock * sizeof(short));
                    if (tmpL && tmpR) {
                        while (dataRemain >= wav.block_align) {
                            int n = ImaAdpcm_DecodeBlockStereo(blockPtr, wav.block_align,
                                tmpL, tmpR, samplesPerBlock);
                            for (int i = 0; i < n && (s->pcm_frames + i) < totalFrames; i++) {
                                s->pcm_data[(s->pcm_frames + i) * 2]     = tmpL[i];
                                s->pcm_data[(s->pcm_frames + i) * 2 + 1] = tmpR[i];
                            }
                            s->pcm_frames += n;
                            blockPtr += wav.block_align;
                            dataRemain -= wav.block_align;
                        }
                    }
                    if (tmpL) free(tmpL);
                    if (tmpR) free(tmpR);
                }
                LogMsg("AE: set_sample_file(slot%d) ADPCM %dch %dHz %d frames (%d blocks) OK\n",
                    slotIdx, wav.channels, wav.sample_rate, s->pcm_frames, numBlocks);

            } else {
                LogMsg("AE: set_sample_file(slot%d) unknown format %d\n", slotIdx, wav.format_tag);
                s->status = SMP_DONE;
                LeaveCriticalSection(&g_aeCS);
                return 0;
            }

            s->status = SMP_STOPPED;
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            LogMsg("AE: set_sample_file(slot%d) -> EXCEPTION during decode!\n", slotIdx);
            if (s->pcm_data) { free(s->pcm_data); s->pcm_data = NULL; }
            s->pcm_frames = 0;
            s->status = SMP_DONE;
        }

        LeaveCriticalSection(&g_aeCS);
        return (s->status == SMP_STOPPED) ? 1 : 0;
    }

    /* Pass-through */
    MSS_S32 r = real_AIL_set_sample_file ? real_AIL_set_sample_file(sample, file_image, block) : 0;
    /* Log WAV header */
    const unsigned char *hdr = (const unsigned char*)file_image;
    if (file_image && hdr[0]=='R' && hdr[1]=='I') {
        LogMsg("_AIL_set_sample_file(%p, WAV fmt=%u ch=%u rate=%u bps=%u) -> %d\n",
            sample, *(unsigned short*)(hdr+20), *(unsigned short*)(hdr+22),
            *(unsigned int*)(hdr+24), *(unsigned short*)(hdr+34), r);
    }
    return r;
}

static void __stdcall Hook_AIL_set_sample_address(HSAMPLE sample,
    const void *start, MSS_U32 len)
{
    if (g_config.use_custom_audio) {
        /* set_sample_address is used to set raw PCM data without WAV header.
         * Need set_sample_type to be called first to know format.
         * For now, treat as 16-bit mono PCM at 22050 Hz. */
        AE_Sample *s = (AE_Sample*)sample;
        if (!s || s->magic != AE_SAMPLE_MAGIC || !start) return;
        EnterCriticalSection(&g_aeCS);
        if (s->pcm_data) { free(s->pcm_data); s->pcm_data = NULL; }
        s->pcm_frames = len / 2; /* assume 16-bit mono */
        s->pcm_channels = 1;
        if (!s->pcm_rate) s->pcm_rate = 22050;
        if (!s->playback_rate) s->playback_rate = s->pcm_rate;
        s->pcm_data = (short*)malloc(len);
        memcpy(s->pcm_data, start, len);
        s->position = 0.0;
        s->loops_done = 0;
        s->status = SMP_STOPPED;
        LeaveCriticalSection(&g_aeCS);
        LogMsg("AE: set_sample_address(%p, %u bytes)\n", s, len);
        return;
    }
    if (real_AIL_set_sample_address) real_AIL_set_sample_address(sample, start, len);
}

static void __stdcall Hook_AIL_set_sample_type(HSAMPLE sample,
    MSS_S32 format, MSS_U32 flags)
{
    if (g_config.use_custom_audio) {
        AE_Sample *s = (AE_Sample*)sample;
        if (!s || s->magic != AE_SAMPLE_MAGIC) return;
        /* format: combination of DIG_F_MONO_8=0, MONO_16=1, STEREO_8=2, STEREO_16=3 */
        s->pcm_channels = (format >= 2) ? 2 : 1;
        LogMsg("AE: set_sample_type(%p, fmt=%d, flags=%u) -> %dch\n",
            s, format, flags, s->pcm_channels);
        return;
    }
    if (real_AIL_set_sample_type) real_AIL_set_sample_type(sample, format, flags);
}

static void __stdcall Hook_AIL_set_sample_loop_count(HSAMPLE sample, MSS_S32 loop_count) {
    if (g_config.use_custom_audio) {
        AE_Sample *s = (AE_Sample*)sample;
        if (!s || s->magic != AE_SAMPLE_MAGIC) return;
        s->loop_count = loop_count;
        LogMsg("AE: set_sample_loop_count(%p, %d%s)\n", s, loop_count,
            loop_count == 0 ? " [INFINITE]" : "");
        return;
    }
    LogMsg("_AIL_set_sample_loop_count(%p, %d)\n", sample, loop_count);
    if (real_AIL_set_sample_loop_count) real_AIL_set_sample_loop_count(sample, loop_count);
}

static void __stdcall Hook_AIL_start_sample(HSAMPLE sample) {
    if (g_config.use_custom_audio) {
        AE_Sample *s = (AE_Sample*)sample;
        if (!s || s->magic != AE_SAMPLE_MAGIC) return;
        AE_EnsureOutputStarted();
        EnterCriticalSection(&g_aeCS);
        s->position = 0.0;
        s->loops_done = 0;
        s->status = SMP_PLAYING;
        LeaveCriticalSection(&g_aeCS);
        LogMsg("AE: start_sample(%p) rate=%d vol=%d pan=%d loop=%d\n",
            s, s->playback_rate, s->volume, s->pan, s->loop_count);
        return;
    }
    LogMsg("_AIL_start_sample(%p)\n", sample);
    if (real_AIL_start_sample) real_AIL_start_sample(sample);
}

static void __stdcall Hook_AIL_stop_sample(HSAMPLE sample) {
    if (g_config.use_custom_audio) {
        AE_Sample *s = (AE_Sample*)sample;
        if (!s || s->magic != AE_SAMPLE_MAGIC) return;
        if (s->status == SMP_FREE || s->status == SMP_DONE) return;
        EnterCriticalSection(&g_aeCS);
        s->status = SMP_STOPPED;
        LeaveCriticalSection(&g_aeCS);
        LogMsg("AE: stop_sample(%p)\n", s);
        return;
    }
    if (real_AIL_stop_sample) real_AIL_stop_sample(sample);
}

static void __stdcall Hook_AIL_end_sample(HSAMPLE sample) {
    if (g_config.use_custom_audio) {
        AE_Sample *s = (AE_Sample*)sample;
        if (!s || s->magic != AE_SAMPLE_MAGIC) return;
        /* Real MSS: end_sample on FREE/DONE is a no-op */
        if (s->status == SMP_FREE || s->status == SMP_DONE) return;
        EnterCriticalSection(&g_aeCS);
        s->status = SMP_DONE;
        s->position = 0.0;
        LeaveCriticalSection(&g_aeCS);
        LogMsg("AE: end_sample(%p)\n", s);
        return;
    }
    if (real_AIL_end_sample) real_AIL_end_sample(sample);
}

static const char* AE_StatusName(MSS_S32 s) {
    switch(s) {
        case SMP_FREE:    return "FREE";
        case SMP_DONE:    return "DONE";
        case SMP_PLAYING: return "PLAYING";
        case SMP_STOPPED: return "STOPPED";
        case SMP_PLAYING_RELEASED: return "PLAYING_RELEASED";
        default: return "?";
    }
}

/* Per-handle status tracking to avoid log flooding */
static MSS_S32 g_sampleStatusCache[AE_MAX_SAMPLES];
static int     g_sampleStatusCacheInit = 0;

static MSS_S32 __stdcall Hook_AIL_sample_status(HSAMPLE sample) {
    if (g_config.use_custom_audio) {
        AE_Sample *s = (AE_Sample*)sample;
        if (!s || s->magic != AE_SAMPLE_MAGIC) return SMP_DONE;
        MSS_S32 st = s->status;
        /* Only log on status transitions for this specific handle */
        int idx = (int)(s - g_aeSamples);
        if (idx >= 0 && idx < AE_MAX_SAMPLES) {
            if (!g_sampleStatusCacheInit) {
                memset(g_sampleStatusCache, -1, sizeof(g_sampleStatusCache));
                g_sampleStatusCacheInit = 1;
            }
            if (g_sampleStatusCache[idx] != st) {
                LogMsg("AE: sample_status(slot%d) = %d [%s]\n", idx, st, AE_StatusName(st));
                g_sampleStatusCache[idx] = st;
            }
        }
        return st;
    }
    MSS_S32 st = real_AIL_sample_status ? real_AIL_sample_status(sample) : SMP_DONE;
    return st;
}

static void __stdcall Hook_AIL_set_sample_playback_rate(HSAMPLE sample, MSS_S32 rate) {
    if (g_config.use_custom_audio) {
        AE_Sample *s = (AE_Sample*)sample;
        if (!s || s->magic != AE_SAMPLE_MAGIC) return;
        s->playback_rate = rate;
        LogMsg("AE: set_sample_playback_rate(%p, %d Hz)\n", s, rate);
        return;
    }
    LogMsg("_AIL_set_sample_playback_rate(%p, %d Hz)\n", sample, rate);
    if (real_AIL_set_sample_playback_rate) real_AIL_set_sample_playback_rate(sample, rate);
}

static MSS_S32 __stdcall Hook_AIL_sample_playback_rate(HSAMPLE sample) {
    if (g_config.use_custom_audio) {
        AE_Sample *s = (AE_Sample*)sample;
        if (!s || s->magic != AE_SAMPLE_MAGIC) return 22050;
        return s->playback_rate;
    }
    return real_AIL_sample_playback_rate ? real_AIL_sample_playback_rate(sample) : 22050;
}

static MSS_S32 __stdcall Hook_AIL_sample_loop_count(HSAMPLE sample) {
    if (g_config.use_custom_audio) {
        AE_Sample *s = (AE_Sample*)sample;
        if (!s || s->magic != AE_SAMPLE_MAGIC) return 1;
        if (s->loop_count == 0) return 0; /* infinite */
        int remaining = s->loop_count - s->loops_done;
        return remaining > 0 ? remaining : 0;
    }
    MSS_S32 lc = real_AIL_sample_loop_count ? real_AIL_sample_loop_count(sample) : 1;
    LogMsg("_AIL_sample_loop_count(%p) = %d\n", sample, lc);
    return lc;
}

static void __stdcall Hook_AIL_set_sample_volume(HSAMPLE sample, MSS_S32 volume) {
    if (g_config.use_custom_audio) {
        AE_Sample *s = (AE_Sample*)sample;
        if (!s || s->magic != AE_SAMPLE_MAGIC) return;
        s->volume = volume < 0 ? 0 : (volume > 127 ? 127 : volume);
        LogMsg("AE: set_sample_volume(%p, %d)\n", s, s->volume);
        return;
    }
    LogMsg("_AIL_set_sample_volume(%p, %d)\n", sample, volume);
    if (real_AIL_set_sample_volume) real_AIL_set_sample_volume(sample, volume);
}

static MSS_S32 __stdcall Hook_AIL_sample_volume(HSAMPLE sample) {
    if (g_config.use_custom_audio) {
        AE_Sample *s = (AE_Sample*)sample;
        if (!s || s->magic != AE_SAMPLE_MAGIC) return 127;
        return s->volume;
    }
    return real_AIL_sample_volume ? real_AIL_sample_volume(sample) : 127;
}

static void __stdcall Hook_AIL_set_sample_pan(HSAMPLE sample, MSS_S32 pan) {
    if (g_config.use_custom_audio) {
        AE_Sample *s = (AE_Sample*)sample;
        if (!s || s->magic != AE_SAMPLE_MAGIC) return;
        s->pan = pan < 0 ? 0 : (pan > 127 ? 127 : pan);
        LogMsg("AE: set_sample_pan(%p, %d)\n", s, s->pan);
        return;
    }
    LogMsg("_AIL_set_sample_pan(%p, %d)\n", sample, pan);
    if (real_AIL_set_sample_pan) real_AIL_set_sample_pan(sample, pan);
}

static MSS_S32 __stdcall Hook_AIL_sample_pan(HSAMPLE sample) {
    if (g_config.use_custom_audio) {
        AE_Sample *s = (AE_Sample*)sample;
        if (!s || s->magic != AE_SAMPLE_MAGIC) return 64;
        return s->pan;
    }
    return real_AIL_sample_pan ? real_AIL_sample_pan(sample) : 64;
}

/* ---- Stream functions ---- */

static HSTREAM __stdcall Hook_AIL_open_stream(HDIGDRIVER dig,
    const char *filename, MSS_S32 stream_mem)
{
    if (g_config.use_custom_audio) {
        if (!filename) return NULL;

        /* Build full path if relative */
        char fullpath[MAX_PATH];
        if (filename[0] != '\\' && filename[1] != ':') {
            snprintf(fullpath, MAX_PATH, "%s%s", g_gameDir, filename);
        } else {
            strncpy(fullpath, filename, MAX_PATH);
        }

        EnterCriticalSection(&g_aeCS);

        /* Find free stream slot */
        AE_Stream *st = NULL;
        int idx = -1;
        for (int i = 0; i < AE_MAX_STREAMS; i++) {
            if (!g_aeStreams[i].in_use) { st = &g_aeStreams[i]; idx = i; break; }
        }
        if (!st) {
            LeaveCriticalSection(&g_aeCS);
            LogMsg("AE: open_stream(\"%s\") -> FAILED (all slots used)\n", filename);
            return NULL;
        }

        memset(st, 0, sizeof(AE_Stream));
        st->magic = AE_STREAM_MAGIC;
        st->volume = 127;
        st->pan = 64;
        st->loop_count = 1;
        strncpy(st->filename, fullpath, MAX_PATH);

        /* Load entire file into memory (stream_mem flag suggests it anyway) */
        FILE *f = fopen(fullpath, "rb");
        if (!f) {
            /* Try original path */
            f = fopen(filename, "rb");
        }
        if (!f) {
            LeaveCriticalSection(&g_aeCS);
            LogMsg("AE: open_stream(\"%s\") -> file not found\n", fullpath);
            return NULL;
        }

        fseek(f, 0, SEEK_END);
        int fsize = (int)ftell(f);
        fseek(f, 0, SEEK_SET);
        unsigned char *mem = (unsigned char*)malloc(fsize);
        fread(mem, 1, fsize, f);
        fclose(f);

        st->file_mem = mem;
        st->file_mem_size = fsize;

        /* Parse WAV */
        WavInfo wav;
        if (!ParseWav(mem, fsize, &wav)) {
            free(mem);
            st->file_mem = NULL;
            LeaveCriticalSection(&g_aeCS);
            LogMsg("AE: open_stream(\"%s\") -> WAV parse failed\n", fullpath);
            return NULL;
        }

        st->format_tag = wav.format_tag;
        st->channels = wav.channels;
        st->sample_rate = wav.sample_rate;
        st->bits_per_sample = wav.bits_per_sample;
        st->block_align = wav.block_align;
        st->samples_per_block = wav.samples_per_block;
        st->data_offset = (int)(wav.data - mem);
        st->data_size = wav.data_size;
        st->data_read = 0;

        /* Allocate decode buffers (double-buffered) */
        st->decode_buf_alloc = AE_STREAM_DECODE_FRAMES;
        st->decode_buf = (short*)malloc(st->decode_buf_alloc * wav.channels * sizeof(short));
        st->decode_back = (short*)malloc(st->decode_buf_alloc * wav.channels * sizeof(short));
        st->decode_buf_frames = 0;
        st->back_frames = 0;
        st->back_ready = 0;

        /* Pre-fill front decode buffer */
        AE_StreamFillBuffer(st);

        /* Pre-fill back buffer so first swap is instant */
        if (st->data_read < st->data_size) {
            int bf = AE_StreamDecodeInto(st, st->decode_back, st->decode_buf_alloc);
            st->back_frames = bf;
            st->back_ready = (bf > 0) ? 1 : 0;
        }

        st->in_use = 1;
        st->status = SMP_STOPPED;

        LeaveCriticalSection(&g_aeCS);
        LogMsg("AE: open_stream(\"%s\") -> slot %d, fmt=%d %dch %dHz %dbit blk=%d dataSize=%d\n",
            filename, idx, wav.format_tag, wav.channels, wav.sample_rate,
            wav.bits_per_sample, wav.block_align, wav.data_size);
        return (HSTREAM)st;
    }

    /* Pass-through */
    HSTREAM h = real_AIL_open_stream ? real_AIL_open_stream(dig, filename, stream_mem) : NULL;
    LogMsg("_AIL_open_stream(\"%s\") -> %p\n", filename ? filename : "(null)", h);
    return h;
}

static void __stdcall Hook_AIL_close_stream(HSTREAM stream) {
    if (g_config.use_custom_audio) {
        AE_Stream *st = (AE_Stream*)stream;
        if (!st || st->magic != AE_STREAM_MAGIC) return;
        EnterCriticalSection(&g_aeCS);
        LogMsg("AE: close_stream(%p)\n", st);
        if (st->decode_buf)  { free(st->decode_buf);  st->decode_buf = NULL; }
        if (st->decode_back) { free(st->decode_back); st->decode_back = NULL; }
        if (st->file_mem)    { free((void*)st->file_mem); st->file_mem = NULL; }
        if (st->file_handle) { fclose(st->file_handle); st->file_handle = NULL; }
        st->in_use = 0;
        st->status = SMP_FREE;
        st->magic = 0;
        LeaveCriticalSection(&g_aeCS);
        return;
    }
    if (real_AIL_close_stream) real_AIL_close_stream(stream);
}

static void __stdcall Hook_AIL_start_stream(HSTREAM stream) {
    if (g_config.use_custom_audio) {
        AE_Stream *st = (AE_Stream*)stream;
        if (!st || st->magic != AE_STREAM_MAGIC) return;
        AE_EnsureOutputStarted();
        EnterCriticalSection(&g_aeCS);
        st->status = SMP_PLAYING;
        st->position = 0.0;
        st->loops_done = 0;
        st->data_read = 0;
        st->back_ready = 0;
        AE_StreamFillBuffer(st);
        /* Pre-fill back buffer for instant first swap */
        if (st->decode_back && st->data_read < st->data_size) {
            int bf = AE_StreamDecodeInto(st, st->decode_back, st->decode_buf_alloc);
            st->back_frames = bf;
            st->back_ready = (bf > 0) ? 1 : 0;
        }
        LeaveCriticalSection(&g_aeCS);
        LogMsg("AE: start_stream(%p) fmt=%d %dch %dHz vol=%d decode_frames=%d data=%d/%d\n",
            st, st->format_tag, st->channels, st->sample_rate,
            st->volume, st->decode_buf_frames, st->data_read, st->data_size);
        return;
    }
    if (real_AIL_start_stream) real_AIL_start_stream(stream);
}

static void __stdcall Hook_AIL_pause_stream(HSTREAM stream, MSS_S32 onoff) {
    if (g_config.use_custom_audio) {
        AE_Stream *st = (AE_Stream*)stream;
        if (!st || st->magic != AE_STREAM_MAGIC) return;
        EnterCriticalSection(&g_aeCS);
        if (onoff) {
            if (st->status == SMP_PLAYING) st->status = SMP_STOPPED;
        } else {
            if (st->status == SMP_STOPPED) st->status = SMP_PLAYING;
        }
        LeaveCriticalSection(&g_aeCS);
        LogMsg("AE: pause_stream(%p, %d)\n", st, onoff);
        return;
    }
    if (real_AIL_pause_stream) real_AIL_pause_stream(stream, onoff);
}

/* Per-stream status tracking */
static MSS_S32 g_streamStatusCache[AE_MAX_STREAMS];
static int     g_streamStatusCacheInit = 0;

static MSS_S32 __stdcall Hook_AIL_stream_status(HSTREAM stream) {
    if (g_config.use_custom_audio) {
        AE_Stream *st = (AE_Stream*)stream;
        if (!st || st->magic != AE_STREAM_MAGIC) return SMP_DONE;
        MSS_S32 sts = st->status;
        int idx = (int)(st - g_aeStreams);
        if (idx >= 0 && idx < AE_MAX_STREAMS) {
            if (!g_streamStatusCacheInit) {
                memset(g_streamStatusCache, -1, sizeof(g_streamStatusCache));
                g_streamStatusCacheInit = 1;
            }
            if (g_streamStatusCache[idx] != sts) {
                LogMsg("AE: stream_status(slot%d) = %d [%s]\n", idx, sts, AE_StatusName(sts));
                g_streamStatusCache[idx] = sts;
            }
        }
        return sts;
    }
    MSS_S32 sts = real_AIL_stream_status ? real_AIL_stream_status(stream) : SMP_DONE;
    return sts;
}

static void __stdcall Hook_AIL_set_stream_volume(HSTREAM stream, MSS_S32 volume) {
    if (g_config.use_custom_audio) {
        AE_Stream *st = (AE_Stream*)stream;
        if (!st || st->magic != AE_STREAM_MAGIC) return;
        st->volume = volume < 0 ? 0 : (volume > 127 ? 127 : volume);
        LogMsg("AE: set_stream_volume(%p, %d)\n", st, st->volume);
        return;
    }
    LogMsg("_AIL_set_stream_volume(%p, %d)\n", stream, volume);
    if (real_AIL_set_stream_volume) real_AIL_set_stream_volume(stream, volume);
}

static void __stdcall Hook_AIL_set_stream_pan(HSTREAM stream, MSS_S32 pan) {
    if (g_config.use_custom_audio) {
        AE_Stream *st = (AE_Stream*)stream;
        if (!st || st->magic != AE_STREAM_MAGIC) return;
        st->pan = pan < 0 ? 0 : (pan > 127 ? 127 : pan);
        return;
    }
    if (real_AIL_set_stream_pan) real_AIL_set_stream_pan(stream, pan);
}

static void __stdcall Hook_AIL_set_stream_loop_count(HSTREAM stream, MSS_S32 count) {
    if (g_config.use_custom_audio) {
        AE_Stream *st = (AE_Stream*)stream;
        if (!st || st->magic != AE_STREAM_MAGIC) return;
        st->loop_count = count;
        LogMsg("AE: set_stream_loop_count(%p, %d)\n", st, count);
        return;
    }
    if (real_AIL_set_stream_loop_count) real_AIL_set_stream_loop_count(stream, count);
}

static MSS_S32 __stdcall Hook_AIL_stream_loop_count(HSTREAM stream) {
    if (g_config.use_custom_audio) {
        AE_Stream *st = (AE_Stream*)stream;
        if (!st || st->magic != AE_STREAM_MAGIC) return 1;
        if (st->loop_count == 0) return 0;
        int rem = st->loop_count - st->loops_done;
        return rem > 0 ? rem : 0;
    }
    return real_AIL_stream_loop_count ? real_AIL_stream_loop_count(stream) : 1;
}

static void __stdcall Hook_AIL_set_stream_playback_rate(HSTREAM stream, MSS_S32 rate) {
    if (g_config.use_custom_audio) {
        AE_Stream *st = (AE_Stream*)stream;
        if (!st || st->magic != AE_STREAM_MAGIC) return;
        st->sample_rate = rate; /* override effective playback rate */
        LogMsg("AE: set_stream_playback_rate(%p, %d)\n", st, rate);
        return;
    }
    if (real_AIL_set_stream_playback_rate) real_AIL_set_stream_playback_rate(stream, rate);
}

static MSS_S32 __stdcall Hook_AIL_stream_position(HSTREAM stream) {
    if (g_config.use_custom_audio) {
        AE_Stream *st = (AE_Stream*)stream;
        if (!st || st->magic != AE_STREAM_MAGIC) return 0;
        return st->data_read;
    }
    return real_AIL_stream_position ? real_AIL_stream_position(stream) : 0;
}

static void __stdcall Hook_AIL_set_stream_position(HSTREAM stream, MSS_S32 offset) {
    if (g_config.use_custom_audio) {
        AE_Stream *st = (AE_Stream*)stream;
        if (!st || st->magic != AE_STREAM_MAGIC) return;
        EnterCriticalSection(&g_aeCS);
        st->data_read = offset;
        st->position = 0.0;
        st->back_ready = 0;
        AE_StreamFillBuffer(st);
        LeaveCriticalSection(&g_aeCS);
        return;
    }
    if (real_AIL_set_stream_position) real_AIL_set_stream_position(stream, offset);
}

/* ---- Master volume ---- */
static void __stdcall Hook_AIL_set_digital_master_volume(HDIGDRIVER dig, MSS_S32 vol) {
    if (g_config.use_custom_audio) {
        g_aeMasterVol = vol < 0 ? 0 : (vol > 127 ? 127 : vol);
        LogMsg("AE: set_digital_master_volume(%d)\n", g_aeMasterVol);
        return;
    }
    if (real_AIL_set_digital_master_volume) real_AIL_set_digital_master_volume(dig, vol);
}

static MSS_S32 __stdcall Hook_AIL_digital_master_volume(HDIGDRIVER dig) {
    if (g_config.use_custom_audio) return g_aeMasterVol;
    return real_AIL_digital_master_volume ? real_AIL_digital_master_volume(dig) : 127;
}

/* ---- Error string ---- */
static char * __stdcall Hook_AIL_last_error(void) {
    if (g_config.use_custom_audio) return g_aeLastError;
    return real_AIL_last_error ? real_AIL_last_error() : "Unknown error";
}

/* ---- Newly discovered unhooked functions ---- */

/*
 * AIL_init_sample(HSAMPLE sample) - THIS WAS THE CRASH!
 * Real MSS accesses internal sample fields. With our fake handles → crash.
 * In custom mode: no-op (allocate_sample_handle already initializes).
 */
typedef void (__stdcall *PFN_AIL_init_sample)(HSAMPLE);
static PFN_AIL_init_sample real_AIL_init_sample = NULL;

static void __stdcall Hook_AIL_init_sample(HSAMPLE sample) {
    if (g_config.use_custom_audio) {
        AE_Sample *s = (AE_Sample*)sample;
        if (!s || s->magic != AE_SAMPLE_MAGIC) return;
        /* Reset sample to clean state (already done by allocate, but be safe) */
        EnterCriticalSection(&g_aeCS);
        if (s->pcm_data) { free(s->pcm_data); s->pcm_data = NULL; }
        s->pcm_frames = 0;
        s->pcm_channels = 1;
        s->pcm_rate = 22050;
        s->playback_rate = 22050;
        s->position = 0.0;
        s->volume = 127;
        s->pan = 64;
        s->loop_count = 1;
        s->loops_done = 0;
        s->status = SMP_DONE;
        LeaveCriticalSection(&g_aeCS);
        LogMsg("AE: init_sample(slot%d)\n", (int)(s - g_aeSamples));
        return;
    }
    if (real_AIL_init_sample) real_AIL_init_sample(sample);
}

/* AIL_get_preference(U32 number) - returns preference value */
typedef MSS_S32 (__stdcall *PFN_AIL_get_preference)(MSS_U32);
static PFN_AIL_get_preference real_AIL_get_preference = NULL;

static MSS_S32 __stdcall Hook_AIL_get_preference(MSS_U32 number) {
    if (g_config.use_custom_audio) {
        /* Return sensible defaults */
        switch(number) {
            case  0: return 16;   /* DIG_MIXER_CHANNELS */
            case  1: return 127;  /* DIG_DEFAULT_VOLUME */
            case  4: return 16384;/* DIG_OUTPUT_BUFFER_SIZE */
            case  5: return 100;  /* DIG_LATENCY */
            case  6: return 1;    /* DIG_USE_STEREO */
            case 17: return 0;    /* DIG_USE_WAVEOUT */
            default: return 0;
        }
    }
    return real_AIL_get_preference ? real_AIL_get_preference(number) : 0;
}

/* AIL_set_DirectSound_HWND(HDIGDRIVER dig, HWND hwnd) - no-op in custom */
typedef void (__stdcall *PFN_AIL_set_DirectSound_HWND)(HDIGDRIVER, HWND);
static PFN_AIL_set_DirectSound_HWND real_AIL_set_DirectSound_HWND = NULL;

static void __stdcall Hook_AIL_set_DirectSound_HWND(HDIGDRIVER dig, HWND hwnd) {
    if (g_config.use_custom_audio) {
        LogMsg("AE: set_DirectSound_HWND(%p) -> ignored\n", hwnd);
        return;
    }
    if (real_AIL_set_DirectSound_HWND) real_AIL_set_DirectSound_HWND(dig, hwnd);
}

/* Timer functions - custom implementation (no real MSS needed) */
typedef MSS_U32 (__stdcall *PFN_AIL_register_timer)(void*);
typedef void    (__stdcall *PFN_AIL_set_timer_frequency)(MSS_U32, MSS_U32);
typedef void    (__stdcall *PFN_AIL_set_timer_user)(MSS_U32, void*);
typedef void    (__stdcall *PFN_AIL_start_timer)(MSS_U32);
typedef void    (__stdcall *PFN_AIL_stop_timer)(MSS_U32);

static PFN_AIL_register_timer       real_AIL_register_timer = NULL;
static PFN_AIL_set_timer_frequency  real_AIL_set_timer_frequency = NULL;
static PFN_AIL_set_timer_user       real_AIL_set_timer_user = NULL;
static PFN_AIL_start_timer          real_AIL_start_timer = NULL;
static PFN_AIL_stop_timer           real_AIL_stop_timer = NULL;

static DWORD WINAPI AE_TimerThread(LPVOID param) {
    AE_Timer *t = (AE_Timer*)param;
    timeBeginPeriod(1);
    while (t->running) {
        if (t->callback && t->frequency > 0) {
            __try { t->callback(t->user); }
            __except(EXCEPTION_EXECUTE_HANDLER) { /* ignore */ }
            DWORD ms = 1000 / t->frequency;
            if (ms < 1) ms = 1;
            Sleep(ms);
        } else {
            Sleep(10);
        }
    }
    timeEndPeriod(1);
    return 0;
}

static MSS_U32 __stdcall Hook_AIL_register_timer(void *callback) {
    if (g_config.use_custom_audio) {
        for (int i = 0; i < AE_MAX_TIMERS; i++) {
            if (!g_aeTimers[i].in_use) {
                memset(&g_aeTimers[i], 0, sizeof(AE_Timer));
                g_aeTimers[i].in_use = 1;
                g_aeTimers[i].callback = (AE_TimerCB)callback;
                g_aeTimers[i].frequency = 100; /* default */
                LogMsg("AE: register_timer(%p) -> slot %d\n", callback, i);
                return (MSS_U32)i;
            }
        }
        LogMsg("AE: register_timer -> FAILED (all slots used)\n");
        return (MSS_U32)-1;
    }
    return real_AIL_register_timer ? real_AIL_register_timer(callback) : 0;
}

static void __stdcall Hook_AIL_set_timer_frequency(MSS_U32 timer, MSS_U32 freq) {
    if (g_config.use_custom_audio) {
        if (timer < AE_MAX_TIMERS && g_aeTimers[timer].in_use) {
            g_aeTimers[timer].frequency = freq > 0 ? freq : 1;
            LogMsg("AE: set_timer_frequency(%u, %u Hz)\n", timer, freq);
        }
        return;
    }
    if (real_AIL_set_timer_frequency) real_AIL_set_timer_frequency(timer, freq);
}

static void __stdcall Hook_AIL_set_timer_user(MSS_U32 timer, void *user) {
    if (g_config.use_custom_audio) {
        if (timer < AE_MAX_TIMERS && g_aeTimers[timer].in_use)
            g_aeTimers[timer].user = (MSS_U32)(uintptr_t)user;
        return;
    }
    if (real_AIL_set_timer_user) real_AIL_set_timer_user(timer, user);
}

static void __stdcall Hook_AIL_start_timer(MSS_U32 timer) {
    if (g_config.use_custom_audio) {
        if (timer < AE_MAX_TIMERS && g_aeTimers[timer].in_use && !g_aeTimers[timer].running) {
            g_aeTimers[timer].running = 1;
            g_aeTimers[timer].thread = CreateThread(NULL, 0, AE_TimerThread,
                &g_aeTimers[timer], 0, NULL);
            LogMsg("AE: start_timer(%u) freq=%u\n", timer, g_aeTimers[timer].frequency);
        }
        return;
    }
    if (real_AIL_start_timer) real_AIL_start_timer(timer);
}

static void __stdcall Hook_AIL_stop_timer(MSS_U32 timer) {
    if (g_config.use_custom_audio) {
        if (timer < AE_MAX_TIMERS && g_aeTimers[timer].in_use && g_aeTimers[timer].running) {
            g_aeTimers[timer].running = 0;
            if (g_aeTimers[timer].thread) {
                WaitForSingleObject(g_aeTimers[timer].thread, 2000);
                CloseHandle(g_aeTimers[timer].thread);
                g_aeTimers[timer].thread = NULL;
            }
            LogMsg("AE: stop_timer(%u)\n", timer);
        }
        return;
    }
    if (real_AIL_stop_timer) real_AIL_stop_timer(timer);
}


/* ==================================================================
 *  GL HOOKS (unchanged from v9)
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

static GLint UpgradeFilter(GLint m){
    switch(m){case GL_NEAREST:return GL_LINEAR;
    case GL_NEAREST_MIPMAP_NEAREST:case GL_NEAREST_MIPMAP_LINEAR:
    case GL_LINEAR_MIPMAP_NEAREST:return GL_LINEAR_MIPMAP_LINEAR;
    default:return m;}
}
static void WINAPI my_glTexParameteri(GLenum t,GLenum pn,GLint p){
    if(!real_glTexParameteri)return;
    if(g_config.fix_texture_filter&&(pn==GL_TEXTURE_MAG_FILTER||pn==GL_TEXTURE_MIN_FILTER))
        p=UpgradeFilter(p);
    if(g_config.fix_texture_clamp&&(pn==GL_TEXTURE_WRAP_S||pn==GL_TEXTURE_WRAP_T))
        if(p==GL_REPEAT||p==GL_CLAMP) p=GL_CLAMP_TO_EDGE;
    real_glTexParameteri(t,pn,p);
}
static void WINAPI my_glTexParameterf(GLenum t,GLenum pn,GLfloat p){
    if(!real_glTexParameterf)return;
    if(g_config.fix_texture_filter&&(pn==GL_TEXTURE_MAG_FILTER||pn==GL_TEXTURE_MIN_FILTER))
        p=(GLfloat)UpgradeFilter((GLint)p);
    if(g_config.fix_texture_clamp&&(pn==GL_TEXTURE_WRAP_S||pn==GL_TEXTURE_WRAP_T))
        if((GLint)p==GL_REPEAT||(GLint)p==GL_CLAMP) p=(GLfloat)GL_CLAMP_TO_EDGE;
    real_glTexParameterf(t,pn,p);
}
static void WINAPI my_glTexParameteriv(GLenum t,GLenum pn,const GLint*ps){
    if(!real_glTexParameteriv||!ps)return;
    GLint p=ps[0];int changed=0;
    if(g_config.fix_texture_filter&&(pn==GL_TEXTURE_MAG_FILTER||pn==GL_TEXTURE_MIN_FILTER))
        {p=UpgradeFilter(p);changed=1;}
    if(g_config.fix_texture_clamp&&(pn==GL_TEXTURE_WRAP_S||pn==GL_TEXTURE_WRAP_T))
        if(p==GL_REPEAT||p==GL_CLAMP){p=GL_CLAMP_TO_EDGE;changed=1;}
    if(changed){real_glTexParameteriv(t,pn,&p);return;}
    real_glTexParameteriv(t,pn,ps);
}
static void WINAPI my_glTexParameterfv(GLenum t,GLenum pn,const GLfloat*ps){
    if(!real_glTexParameterfv||!ps)return;
    GLfloat p=ps[0];int changed=0;
    if(g_config.fix_texture_filter&&(pn==GL_TEXTURE_MAG_FILTER||pn==GL_TEXTURE_MIN_FILTER))
        {p=(GLfloat)UpgradeFilter((GLint)p);changed=1;}
    if(g_config.fix_texture_clamp&&(pn==GL_TEXTURE_WRAP_S||pn==GL_TEXTURE_WRAP_T))
        if((GLint)p==GL_REPEAT||(GLint)p==GL_CLAMP){p=(GLfloat)GL_CLAMP_TO_EDGE;changed=1;}
    if(changed){real_glTexParameterfv(t,pn,&p);return;}
    real_glTexParameterfv(t,pn,ps);
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
        if (g_config.msaa_samples > 0 && real_glEnable) {
            real_glEnable(GL_MULTISAMPLE);
            LogMsg("MSAA: glEnable(GL_MULTISAMPLE)\n");
        }
    }
    return ok;
}

/* ---- Windowed mode (unchanged from v9) ---- */
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
        if(StrContainsI(fn,"mss32")){
            g_hMSS32=h;
            LogMsg(">>> mss32.dll loaded: %p%s\n", h,
                g_config.use_custom_audio ? " (custom audio active, MSS unused)" : "");
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
    {"wglMakeCurrent",  (void*)my_wglMakeCurrent,   (void**)&real_wglMakeCurrent},
    {NULL,NULL,NULL}
};

/*
 * MSS hook table - ALL known AIL functions the game might call.
 * Each entry maps one or more name variants to a hook function and a real_* pointer.
 * Both decorated (_AIL_xxx@N) and undecorated names are listed.
 */
static HookEntry g_mssHooks[]={
    /* startup/shutdown */
    {"_AIL_startup@0",                   (void*)Hook_AIL_startup,               (void**)&real_AIL_startup},
    {"_AIL_startup",                     (void*)Hook_AIL_startup,               (void**)&real_AIL_startup},
    {"AIL_startup",                      (void*)Hook_AIL_startup,               (void**)&real_AIL_startup},
    {"_AIL_shutdown@0",                  (void*)Hook_AIL_shutdown,              (void**)&real_AIL_shutdown},
    {"_AIL_shutdown",                    (void*)Hook_AIL_shutdown,              (void**)&real_AIL_shutdown},
    {"AIL_shutdown",                     (void*)Hook_AIL_shutdown,              (void**)&real_AIL_shutdown},
    /* preferences */
    {"_AIL_set_preference@8",            (void*)Hook_AIL_set_preference,        (void**)&real_AIL_set_preference},
    {"_AIL_set_preference",              (void*)Hook_AIL_set_preference,        (void**)&real_AIL_set_preference},
    {"AIL_set_preference",               (void*)Hook_AIL_set_preference,        (void**)&real_AIL_set_preference},
    /* driver */
    {"_AIL_waveOutOpen@16",              (void*)Hook_AIL_waveOutOpen,           (void**)&real_AIL_waveOutOpen},
    {"_AIL_waveOutOpen",                 (void*)Hook_AIL_waveOutOpen,           (void**)&real_AIL_waveOutOpen},
    {"AIL_waveOutOpen",                  (void*)Hook_AIL_waveOutOpen,           (void**)&real_AIL_waveOutOpen},
    {"_AIL_waveOutClose@4",              (void*)Hook_AIL_waveOutClose,          (void**)&real_AIL_waveOutClose},
    {"_AIL_waveOutClose",                (void*)Hook_AIL_waveOutClose,          (void**)&real_AIL_waveOutClose},
    {"AIL_waveOutClose",                 (void*)Hook_AIL_waveOutClose,          (void**)&real_AIL_waveOutClose},
    {"_AIL_digital_configuration@16",    (void*)Hook_AIL_digital_configuration, (void**)&real_AIL_digital_configuration},
    {"_AIL_digital_configuration",       (void*)Hook_AIL_digital_configuration, (void**)&real_AIL_digital_configuration},
    {"AIL_digital_configuration",        (void*)Hook_AIL_digital_configuration, (void**)&real_AIL_digital_configuration},
    /* sample handle management */
    {"_AIL_allocate_sample_handle@4",    (void*)Hook_AIL_allocate_sample_handle,(void**)&real_AIL_allocate_sample_handle},
    {"_AIL_allocate_sample_handle",      (void*)Hook_AIL_allocate_sample_handle,(void**)&real_AIL_allocate_sample_handle},
    {"AIL_allocate_sample_handle",       (void*)Hook_AIL_allocate_sample_handle,(void**)&real_AIL_allocate_sample_handle},
    {"_AIL_release_sample_handle@4",     (void*)Hook_AIL_release_sample_handle, (void**)&real_AIL_release_sample_handle},
    {"_AIL_release_sample_handle",       (void*)Hook_AIL_release_sample_handle, (void**)&real_AIL_release_sample_handle},
    {"AIL_release_sample_handle",        (void*)Hook_AIL_release_sample_handle, (void**)&real_AIL_release_sample_handle},
    /* sample control */
    {"_AIL_set_sample_file@12",          (void*)Hook_AIL_set_sample_file,       (void**)&real_AIL_set_sample_file},
    {"_AIL_set_sample_file",             (void*)Hook_AIL_set_sample_file,       (void**)&real_AIL_set_sample_file},
    {"AIL_set_sample_file",              (void*)Hook_AIL_set_sample_file,       (void**)&real_AIL_set_sample_file},
    {"_AIL_set_sample_address@12",       (void*)Hook_AIL_set_sample_address,    (void**)&real_AIL_set_sample_address},
    {"_AIL_set_sample_address",          (void*)Hook_AIL_set_sample_address,    (void**)&real_AIL_set_sample_address},
    {"AIL_set_sample_address",           (void*)Hook_AIL_set_sample_address,    (void**)&real_AIL_set_sample_address},
    {"_AIL_set_sample_type@12",          (void*)Hook_AIL_set_sample_type,       (void**)&real_AIL_set_sample_type},
    {"_AIL_set_sample_type",             (void*)Hook_AIL_set_sample_type,       (void**)&real_AIL_set_sample_type},
    {"AIL_set_sample_type",              (void*)Hook_AIL_set_sample_type,       (void**)&real_AIL_set_sample_type},
    {"_AIL_set_sample_loop_count@8",     (void*)Hook_AIL_set_sample_loop_count, (void**)&real_AIL_set_sample_loop_count},
    {"_AIL_set_sample_loop_count",       (void*)Hook_AIL_set_sample_loop_count, (void**)&real_AIL_set_sample_loop_count},
    {"AIL_set_sample_loop_count",        (void*)Hook_AIL_set_sample_loop_count, (void**)&real_AIL_set_sample_loop_count},
    {"_AIL_start_sample@4",              (void*)Hook_AIL_start_sample,          (void**)&real_AIL_start_sample},
    {"_AIL_start_sample",                (void*)Hook_AIL_start_sample,          (void**)&real_AIL_start_sample},
    {"AIL_start_sample",                 (void*)Hook_AIL_start_sample,          (void**)&real_AIL_start_sample},
    {"_AIL_stop_sample@4",               (void*)Hook_AIL_stop_sample,           (void**)&real_AIL_stop_sample},
    {"_AIL_stop_sample",                 (void*)Hook_AIL_stop_sample,           (void**)&real_AIL_stop_sample},
    {"AIL_stop_sample",                  (void*)Hook_AIL_stop_sample,           (void**)&real_AIL_stop_sample},
    {"_AIL_end_sample@4",                (void*)Hook_AIL_end_sample,            (void**)&real_AIL_end_sample},
    {"_AIL_end_sample",                  (void*)Hook_AIL_end_sample,            (void**)&real_AIL_end_sample},
    {"AIL_end_sample",                   (void*)Hook_AIL_end_sample,            (void**)&real_AIL_end_sample},
    {"_AIL_sample_status@4",             (void*)Hook_AIL_sample_status,         (void**)&real_AIL_sample_status},
    {"_AIL_sample_status",               (void*)Hook_AIL_sample_status,         (void**)&real_AIL_sample_status},
    {"AIL_sample_status",                (void*)Hook_AIL_sample_status,         (void**)&real_AIL_sample_status},
    {"_AIL_set_sample_playback_rate@8",  (void*)Hook_AIL_set_sample_playback_rate,(void**)&real_AIL_set_sample_playback_rate},
    {"_AIL_set_sample_playback_rate",    (void*)Hook_AIL_set_sample_playback_rate,(void**)&real_AIL_set_sample_playback_rate},
    {"AIL_set_sample_playback_rate",     (void*)Hook_AIL_set_sample_playback_rate,(void**)&real_AIL_set_sample_playback_rate},
    {"_AIL_sample_playback_rate@4",      (void*)Hook_AIL_sample_playback_rate,  (void**)&real_AIL_sample_playback_rate},
    {"_AIL_sample_playback_rate",        (void*)Hook_AIL_sample_playback_rate,  (void**)&real_AIL_sample_playback_rate},
    {"AIL_sample_playback_rate",         (void*)Hook_AIL_sample_playback_rate,  (void**)&real_AIL_sample_playback_rate},
    {"_AIL_sample_loop_count@4",         (void*)Hook_AIL_sample_loop_count,     (void**)&real_AIL_sample_loop_count},
    {"_AIL_sample_loop_count",           (void*)Hook_AIL_sample_loop_count,     (void**)&real_AIL_sample_loop_count},
    {"AIL_sample_loop_count",            (void*)Hook_AIL_sample_loop_count,     (void**)&real_AIL_sample_loop_count},
    {"_AIL_set_sample_volume@8",         (void*)Hook_AIL_set_sample_volume,     (void**)&real_AIL_set_sample_volume},
    {"_AIL_set_sample_volume",           (void*)Hook_AIL_set_sample_volume,     (void**)&real_AIL_set_sample_volume},
    {"AIL_set_sample_volume",            (void*)Hook_AIL_set_sample_volume,     (void**)&real_AIL_set_sample_volume},
    {"_AIL_sample_volume@4",             (void*)Hook_AIL_sample_volume,         (void**)&real_AIL_sample_volume},
    {"_AIL_sample_volume",               (void*)Hook_AIL_sample_volume,         (void**)&real_AIL_sample_volume},
    {"AIL_sample_volume",                (void*)Hook_AIL_sample_volume,         (void**)&real_AIL_sample_volume},
    {"_AIL_set_sample_pan@8",            (void*)Hook_AIL_set_sample_pan,        (void**)&real_AIL_set_sample_pan},
    {"_AIL_set_sample_pan",              (void*)Hook_AIL_set_sample_pan,        (void**)&real_AIL_set_sample_pan},
    {"AIL_set_sample_pan",               (void*)Hook_AIL_set_sample_pan,        (void**)&real_AIL_set_sample_pan},
    {"_AIL_sample_pan@4",                (void*)Hook_AIL_sample_pan,            (void**)&real_AIL_sample_pan},
    {"_AIL_sample_pan",                  (void*)Hook_AIL_sample_pan,            (void**)&real_AIL_sample_pan},
    {"AIL_sample_pan",                   (void*)Hook_AIL_sample_pan,            (void**)&real_AIL_sample_pan},
    /* stream control */
    {"_AIL_open_stream@12",              (void*)Hook_AIL_open_stream,           (void**)&real_AIL_open_stream},
    {"_AIL_open_stream",                 (void*)Hook_AIL_open_stream,           (void**)&real_AIL_open_stream},
    {"AIL_open_stream",                  (void*)Hook_AIL_open_stream,           (void**)&real_AIL_open_stream},
    {"_AIL_close_stream@4",              (void*)Hook_AIL_close_stream,          (void**)&real_AIL_close_stream},
    {"_AIL_close_stream",                (void*)Hook_AIL_close_stream,          (void**)&real_AIL_close_stream},
    {"AIL_close_stream",                 (void*)Hook_AIL_close_stream,          (void**)&real_AIL_close_stream},
    {"_AIL_start_stream@4",              (void*)Hook_AIL_start_stream,          (void**)&real_AIL_start_stream},
    {"_AIL_start_stream",                (void*)Hook_AIL_start_stream,          (void**)&real_AIL_start_stream},
    {"AIL_start_stream",                 (void*)Hook_AIL_start_stream,          (void**)&real_AIL_start_stream},
    {"_AIL_pause_stream@8",              (void*)Hook_AIL_pause_stream,          (void**)&real_AIL_pause_stream},
    {"_AIL_pause_stream",                (void*)Hook_AIL_pause_stream,          (void**)&real_AIL_pause_stream},
    {"AIL_pause_stream",                 (void*)Hook_AIL_pause_stream,          (void**)&real_AIL_pause_stream},
    {"_AIL_stream_status@4",             (void*)Hook_AIL_stream_status,         (void**)&real_AIL_stream_status},
    {"_AIL_stream_status",               (void*)Hook_AIL_stream_status,         (void**)&real_AIL_stream_status},
    {"AIL_stream_status",                (void*)Hook_AIL_stream_status,         (void**)&real_AIL_stream_status},
    {"_AIL_set_stream_volume@8",         (void*)Hook_AIL_set_stream_volume,     (void**)&real_AIL_set_stream_volume},
    {"_AIL_set_stream_volume",           (void*)Hook_AIL_set_stream_volume,     (void**)&real_AIL_set_stream_volume},
    {"AIL_set_stream_volume",            (void*)Hook_AIL_set_stream_volume,     (void**)&real_AIL_set_stream_volume},
    {"_AIL_set_stream_pan@8",            (void*)Hook_AIL_set_stream_pan,        (void**)&real_AIL_set_stream_pan},
    {"_AIL_set_stream_pan",              (void*)Hook_AIL_set_stream_pan,        (void**)&real_AIL_set_stream_pan},
    {"AIL_set_stream_pan",               (void*)Hook_AIL_set_stream_pan,        (void**)&real_AIL_set_stream_pan},
    {"_AIL_set_stream_loop_count@8",     (void*)Hook_AIL_set_stream_loop_count, (void**)&real_AIL_set_stream_loop_count},
    {"_AIL_set_stream_loop_count",       (void*)Hook_AIL_set_stream_loop_count, (void**)&real_AIL_set_stream_loop_count},
    {"AIL_set_stream_loop_count",        (void*)Hook_AIL_set_stream_loop_count, (void**)&real_AIL_set_stream_loop_count},
    {"_AIL_stream_loop_count@4",         (void*)Hook_AIL_stream_loop_count,     (void**)&real_AIL_stream_loop_count},
    {"_AIL_stream_loop_count",           (void*)Hook_AIL_stream_loop_count,     (void**)&real_AIL_stream_loop_count},
    {"AIL_stream_loop_count",            (void*)Hook_AIL_stream_loop_count,     (void**)&real_AIL_stream_loop_count},
    {"_AIL_set_stream_playback_rate@8",  (void*)Hook_AIL_set_stream_playback_rate,(void**)&real_AIL_set_stream_playback_rate},
    {"_AIL_set_stream_playback_rate",    (void*)Hook_AIL_set_stream_playback_rate,(void**)&real_AIL_set_stream_playback_rate},
    {"AIL_set_stream_playback_rate",     (void*)Hook_AIL_set_stream_playback_rate,(void**)&real_AIL_set_stream_playback_rate},
    {"_AIL_stream_position@4",           (void*)Hook_AIL_stream_position,       (void**)&real_AIL_stream_position},
    {"_AIL_stream_position",             (void*)Hook_AIL_stream_position,       (void**)&real_AIL_stream_position},
    {"AIL_stream_position",              (void*)Hook_AIL_stream_position,       (void**)&real_AIL_stream_position},
    {"_AIL_set_stream_position@8",       (void*)Hook_AIL_set_stream_position,   (void**)&real_AIL_set_stream_position},
    {"_AIL_set_stream_position",         (void*)Hook_AIL_set_stream_position,   (void**)&real_AIL_set_stream_position},
    {"AIL_set_stream_position",          (void*)Hook_AIL_set_stream_position,   (void**)&real_AIL_set_stream_position},
    /* master volume */
    {"_AIL_set_digital_master_volume@8", (void*)Hook_AIL_set_digital_master_volume,(void**)&real_AIL_set_digital_master_volume},
    {"_AIL_set_digital_master_volume",   (void*)Hook_AIL_set_digital_master_volume,(void**)&real_AIL_set_digital_master_volume},
    {"AIL_set_digital_master_volume",    (void*)Hook_AIL_set_digital_master_volume,(void**)&real_AIL_set_digital_master_volume},
    {"_AIL_digital_master_volume@4",     (void*)Hook_AIL_digital_master_volume, (void**)&real_AIL_digital_master_volume},
    {"_AIL_digital_master_volume",       (void*)Hook_AIL_digital_master_volume, (void**)&real_AIL_digital_master_volume},
    {"AIL_digital_master_volume",        (void*)Hook_AIL_digital_master_volume, (void**)&real_AIL_digital_master_volume},
    /* error */
    {"_AIL_last_error@0",                (void*)Hook_AIL_last_error,            (void**)&real_AIL_last_error},
    {"_AIL_last_error",                  (void*)Hook_AIL_last_error,            (void**)&real_AIL_last_error},
    {"AIL_last_error",                   (void*)Hook_AIL_last_error,            (void**)&real_AIL_last_error},
    /* init_sample - THE CRASH CAUSE: real MSS accesses internal fields of our fake handle */
    {"_AIL_init_sample@4",               (void*)Hook_AIL_init_sample,           (void**)&real_AIL_init_sample},
    {"_AIL_init_sample",                 (void*)Hook_AIL_init_sample,           (void**)&real_AIL_init_sample},
    {"AIL_init_sample",                  (void*)Hook_AIL_init_sample,           (void**)&real_AIL_init_sample},
    /* get_preference */
    {"_AIL_get_preference@4",            (void*)Hook_AIL_get_preference,        (void**)&real_AIL_get_preference},
    {"_AIL_get_preference",              (void*)Hook_AIL_get_preference,        (void**)&real_AIL_get_preference},
    {"AIL_get_preference",               (void*)Hook_AIL_get_preference,        (void**)&real_AIL_get_preference},
    /* DirectSound HWND */
    {"_AIL_set_DirectSound_HWND@8",      (void*)Hook_AIL_set_DirectSound_HWND,  (void**)&real_AIL_set_DirectSound_HWND},
    {"_AIL_set_DirectSound_HWND",        (void*)Hook_AIL_set_DirectSound_HWND,  (void**)&real_AIL_set_DirectSound_HWND},
    {"AIL_set_DirectSound_HWND",         (void*)Hook_AIL_set_DirectSound_HWND,  (void**)&real_AIL_set_DirectSound_HWND},
    /* Timer functions - pass through to real MSS */
    {"_AIL_register_timer@4",            (void*)Hook_AIL_register_timer,        (void**)&real_AIL_register_timer},
    {"_AIL_register_timer",              (void*)Hook_AIL_register_timer,        (void**)&real_AIL_register_timer},
    {"AIL_register_timer",               (void*)Hook_AIL_register_timer,        (void**)&real_AIL_register_timer},
    {"_AIL_set_timer_frequency@8",       (void*)Hook_AIL_set_timer_frequency,   (void**)&real_AIL_set_timer_frequency},
    {"_AIL_set_timer_frequency",         (void*)Hook_AIL_set_timer_frequency,   (void**)&real_AIL_set_timer_frequency},
    {"AIL_set_timer_frequency",          (void*)Hook_AIL_set_timer_frequency,   (void**)&real_AIL_set_timer_frequency},
    {"_AIL_set_timer_user@8",            (void*)Hook_AIL_set_timer_user,        (void**)&real_AIL_set_timer_user},
    {"_AIL_set_timer_user",              (void*)Hook_AIL_set_timer_user,        (void**)&real_AIL_set_timer_user},
    {"AIL_set_timer_user",               (void*)Hook_AIL_set_timer_user,        (void**)&real_AIL_set_timer_user},
    {"_AIL_start_timer@4",               (void*)Hook_AIL_start_timer,           (void**)&real_AIL_start_timer},
    {"_AIL_start_timer",                 (void*)Hook_AIL_start_timer,           (void**)&real_AIL_start_timer},
    {"AIL_start_timer",                  (void*)Hook_AIL_start_timer,           (void**)&real_AIL_start_timer},
    {"_AIL_stop_timer@4",                (void*)Hook_AIL_stop_timer,            (void**)&real_AIL_stop_timer},
    {"_AIL_stop_timer",                  (void*)Hook_AIL_stop_timer,            (void**)&real_AIL_stop_timer},
    {"AIL_stop_timer",                   (void*)Hook_AIL_stop_timer,            (void**)&real_AIL_stop_timer},
    {NULL,NULL,NULL}
};

static FARPROC WINAPI Hook_GetProcAddress(HMODULE hM,LPCSTR nm){
    if(!nm) return real_GetProcAddress_ptr(hM,nm);

    /* MSS interception */
    if(hM && hM==g_hMSS32){
        for(int i=0;g_mssHooks[i].name;i++){
            if(strcmp(nm,g_mssHooks[i].name)==0){
                FARPROC r=real_GetProcAddress_ptr(hM,nm);
                if(r&&g_mssHooks[i].real&&!(*g_mssHooks[i].real))
                    *g_mssHooks[i].real=(void*)r;
                LogMsg("GPA(MSS,\"%s\") -> hooked%s\n", nm,
                    g_config.use_custom_audio ? " [custom]" : "");
                return(FARPROC)g_mssHooks[i].hook;
            }
        }
        LogMsg("GPA(MSS,\"%s\") -> passthrough\n",nm);
    }

    /* GL interception (unchanged) */
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

/* Enumerate all loaded modules, find who imports mss32, and patch their IAT */
static void PatchMSSAllModules(void){
    HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE,GetCurrentProcessId());
    if(snap==INVALID_HANDLE_VALUE){LogMsg("MSS: module snapshot failed\n");return;}
    MODULEENTRY32 me; me.dwSize=sizeof(me);
    LogMsg("--- MSS: scanning all modules ---\n");
    if(Module32First(snap,&me)){
        do {
            HMODULE hMod=(HMODULE)me.modBaseAddr;
            PIMAGE_DOS_HEADER dos=(PIMAGE_DOS_HEADER)hMod;
            if(dos->e_magic!=IMAGE_DOS_SIGNATURE)continue;
            PIMAGE_NT_HEADERS nt=(PIMAGE_NT_HEADERS)((BYTE*)hMod+dos->e_lfanew);
            if(nt->Signature!=IMAGE_NT_SIGNATURE)continue;
            DWORD rva=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
            if(!rva)continue;
            PIMAGE_IMPORT_DESCRIPTOR imp=(PIMAGE_IMPORT_DESCRIPTOR)((BYTE*)hMod+rva);
            int importsMSS=0;
            for(;imp->Name;imp++){
                const char*name=(const char*)((BYTE*)hMod+imp->Name);
                if(_stricmp(name,"mss32.dll")==0){importsMSS=1;break;}
            }
            if(!importsMSS)continue;

            LogMsg("MSS: found importer -> %s (base=%p)\n", me.szModule, hMod);

            /* Use the full hook table instead of the old limited set */
            /* Build a unique-function patch list from g_mssHooks
             * (g_mssHooks has multiple name variants per function) */
            int patchCount = 0;
            for(int i = 0; g_mssHooks[i].name; i++){
                if(g_mssHooks[i].real && *(g_mssHooks[i].real)) continue; /* already resolved */
                if(PatchIATQuiet(hMod, "mss32.dll", g_mssHooks[i].name,
                    g_mssHooks[i].hook, g_mssHooks[i].real)){
                    LogMsg("  patched: %s\n", g_mssHooks[i].name);
                    patchCount++;
                }
            }
            LogMsg("  %d functions patched\n", patchCount);

            /* ALWAYS dump all mss32 imports for diagnostics (incl. ordinals) */
            {
                PIMAGE_IMPORT_DESCRIPTOR imp2=(PIMAGE_IMPORT_DESCRIPTOR)((BYTE*)hMod+rva);
                for(;imp2->Name;imp2++){
                    if(_stricmp((const char*)((BYTE*)hMod+imp2->Name),"mss32.dll")!=0)continue;
                    PIMAGE_THUNK_DATA ot=(PIMAGE_THUNK_DATA)((BYTE*)hMod+imp2->OriginalFirstThunk);
                    PIMAGE_THUNK_DATA ft=(PIMAGE_THUNK_DATA)((BYTE*)hMod+imp2->FirstThunk);
                    LogMsg("  ALL mss32 imports (%s):\n", me.szModule);
                    for(;ot->u1.AddressOfData;ot++,ft++){
                        if(ot->u1.Ordinal&IMAGE_ORDINAL_FLAG){
                            LogMsg("    ORD #%lu -> %p%s\n",
                                (unsigned long)(ot->u1.Ordinal&0xFFFF),
                                (void*)ft->u1.Function,
                                " *** NOT HOOKED (ordinal) ***");
                        } else {
                            PIMAGE_IMPORT_BY_NAME n=(PIMAGE_IMPORT_BY_NAME)((BYTE*)hMod+ot->u1.AddressOfData);
                            /* Check if this was hooked */
                            int hooked = 0;
                            for(int k=0; g_mssHooks[k].name; k++){
                                if(strcmp((char*)n->Name, g_mssHooks[k].name)==0){ hooked=1; break; }
                            }
                            if(!hooked)
                                LogMsg("    %s -> %p *** NOT HOOKED ***\n",
                                    (char*)n->Name, (void*)ft->u1.Function);
                        }
                    }
                    break;
                }
            }
        } while(Module32Next(snap,&me));
    }
    CloseHandle(snap);
    LogMsg("MSS scan done\n");
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
    g_config.fix_audio_crackle  = GetPrivateProfileIntA("Audio","FixAudioCrackle",1,p);
    g_config.audio_latency      = GetPrivateProfileIntA("Audio","AudioLatency",0,p);
    g_config.audio_buffer_size  = GetPrivateProfileIntA("Audio","AudioBufferSize",0,p);
    g_config.audio_use_waveout  = GetPrivateProfileIntA("Audio","UseWaveOut",0,p);
    g_config.use_custom_audio   = GetPrivateProfileIntA("Audio","UseCustomAudio",1,p);
    g_config.audio_master_volume= GetPrivateProfileIntA("Audio","MasterVolume",96,p);
    g_config.audio_output_rate  = GetPrivateProfileIntA("Audio","AudioOutputRate",22050,p);
    /* Apply output rate to engine global */
    if (g_config.audio_output_rate > 0)
        AE_OUTPUT_RATE = g_config.audio_output_rate;
    g_config.enable_log         = GetPrivateProfileIntA("Debug","EnableLog",1,p);
    g_config.show_diagnostics   = GetPrivateProfileIntA("Debug","ShowDiagnostics",0,p);
    if (g_config.enable_log) {
        char lp[MAX_PATH]; snprintf(lp,MAX_PATH,"%saitd_wrapper.log",g_gameDir);
        g_logFile=fopen(lp,"w");
        if (g_logFile) setvbuf(g_logFile, NULL, _IONBF, 0); /* fully unbuffered */
        LogMsg("=== AITD Wrapper v11 (Custom Audio Engine) ===\n");
        LogMsg("AR=%d CD=%d TF=%d TC=%d Win=%d MSAA=%d Res=%dx%d\n",
            g_config.fix_aspect_ratio, g_config.fix_color_depth,
            g_config.fix_texture_filter, g_config.fix_texture_clamp,
            g_config.windowed, g_config.msaa_samples,
            g_config.force_width, g_config.force_height);
        LogMsg("Audio: crackle=%d latency=%d bufsize=%d waveout=%d custom=%d masterVol=%d outRate=%d\n",
            g_config.fix_audio_crackle, g_config.audio_latency,
            g_config.audio_buffer_size, g_config.audio_use_waveout,
            g_config.use_custom_audio, g_config.audio_master_volume,
            AE_OUTPUT_RATE);
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

    /* MSS (Miles Sound System) - scan all loaded modules */
    LogMsg("--- MSS setup (custom=%d) ---\n", g_config.use_custom_audio);
    if(!g_hMSS32){
        g_hMSS32 = GetModuleHandleA("mss32.dll");
        if(!g_hMSS32) g_hMSS32 = GetModuleHandleA("MSS32.dll");
        if(!g_hMSS32) g_hMSS32 = GetModuleHandleA("Mss32.dll");
    }
    if(g_hMSS32){
        LogMsg("MSS: loaded at %p\n", g_hMSS32);
        PatchMSSAllModules();
    } else {
        LogMsg("MSS: not yet loaded (will intercept via LoadLibraryA+GPA)\n");
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
                "!!! EXCEPTION 0x%08X at %p (exe+0x%X mss+0x%X proxy+0x%X)\n",
                code, addr,
                (unsigned int)((char*)addr - (char*)hExe),
                g_hMSS32 ? (unsigned int)((char*)addr - (char*)g_hMSS32) : 0,
                (unsigned int)((char*)addr - (char*)GetModuleHandleA("dinput8.dll")));
            if (code == 0xC0000005 && ep->ExceptionRecord->NumberParameters >= 2) {
                fprintf(g_logFile, "    Access: %s at %p\n",
                    ep->ExceptionRecord->ExceptionInformation[0] ? "WRITE" : "READ",
                    (void*)ep->ExceptionRecord->ExceptionInformation[1]);
            }
            if (ep->ContextRecord) {
                fprintf(g_logFile, "    EAX=%08X EBX=%08X ECX=%08X EDX=%08X\n",
                    (unsigned)ep->ContextRecord->Eax, (unsigned)ep->ContextRecord->Ebx,
                    (unsigned)ep->ContextRecord->Ecx, (unsigned)ep->ContextRecord->Edx);
                fprintf(g_logFile, "    ESP=%08X EBP=%08X ESI=%08X EDI=%08X\n",
                    (unsigned)ep->ContextRecord->Esp, (unsigned)ep->ContextRecord->Ebp,
                    (unsigned)ep->ContextRecord->Esi, (unsigned)ep->ContextRecord->Edi);
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
        WriteDiag("=== AITD Wrapper v11 ===");
        LoadConfig(); WriteDiag("Config OK");
        /* Register crash handler for diagnostics */
        AddVectoredExceptionHandler(1, AE_CrashHandler);
        if(g_config.show_diagnostics){
            char m[512];
            snprintf(m,sizeof(m),"AITD Wrapper v11\n\nAR=%d CD=%d TF=%d TC=%d\n"
                "Windowed=%d MSAA=%d\nRes=%dx%d %s\nCustom Audio=%d",
                g_config.fix_aspect_ratio,g_config.fix_color_depth,
                g_config.fix_texture_filter,g_config.fix_texture_clamp,
                g_config.windowed,g_config.msaa_samples,
                g_config.force_width,g_config.force_height,
                g_config.force_width>0?"(forced)":"(default)",
                g_config.use_custom_audio);
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

        LogMsg("=== v11 ready ===\n"); WriteDiag("Ready");

        /* Log key module addresses for crash diagnostics */
        {
            HMODULE hExe2 = GetModuleHandleA(NULL);
            LogMsg("Module map: exe=%p opengl=%p mss32=%p dinput8proxy=%p\n",
                hExe2, g_hOpenGL, g_hMSS32, hDll);
        }
    } else if(reason==DLL_PROCESS_DETACH){
        if(g_config.windowed) ChangeDisplaySettingsA(NULL, 0);
        /* Shut down custom audio engine */
        if(g_config.use_custom_audio && g_aeInitialized){
            AE_StopOutput();
            /* Don't DeleteCriticalSection here - might be called from DllMain context */
        }
        LogMsg("Shutdown.\n");
        if(g_logFile){fclose(g_logFile);g_logFile=NULL;}
        if(g_hRealDInput8){FreeLibrary(g_hRealDInput8);g_hRealDInput8=NULL;}
    }
    return TRUE;
}
