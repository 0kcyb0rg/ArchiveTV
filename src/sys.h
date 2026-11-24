/* Minimal declarations for glibc (armhf), SDL 1.2 and FFmpeg 2.4 as found on the Miyoo Mini.
   We build without a sysroot, so only what ArchiveTV uses is declared here.
   Struct offsets for FFmpeg were checked against the Radio app's binary (built with the real headers). */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

/* ---------------- libc ---------------- */
typedef int ssize_t;
typedef int pid_t;
typedef long time_t;
typedef unsigned int socklen_t;
typedef struct FILE FILE;
#define EOF (-1)
#define SEEK_SET 0
#define SEEK_END 2
FILE *fopen(const char *, const char *);
FILE *fdopen(int, const char *);
int fclose(FILE *);
size_t fread(void *, size_t, size_t, FILE *);
size_t fwrite(const void *, size_t, size_t, FILE *);
int fseek(FILE *, long, int);
long ftell(FILE *);
char *fgets(char *, int, FILE *);
int fputs(const char *, FILE *);
int fprintf(FILE *, const char *, ...);
int vfprintf(FILE *, const char *, va_list);
int snprintf(char *, size_t, const char *, ...);
int vsnprintf(char *, size_t, const char *, va_list);
int fflush(FILE *);
void clearerr(FILE *);
int setvbuf(FILE *, char *, int, size_t);
#define _IOLBF 1
FILE *popen(const char *, const char *);
int pclose(FILE *);
void *malloc(size_t);
void *calloc(size_t, size_t);
void *realloc(void *, size_t);
void free(void *);
void *memcpy(void *, const void *, size_t);
void *memmove(void *, const void *, size_t);
void *memset(void *, int, size_t);
int memcmp(const void *, const void *, size_t);
size_t strlen(const char *);
int strcmp(const char *, const char *);
int strncmp(const char *, const char *, size_t);
int strcasecmp(const char *, const char *);
int strncasecmp(const char *, const char *, size_t);
char *strchr(const char *, int);
char *strrchr(const char *, int);
char *strstr(const char *, const char *);
char *strcasestr(const char *, const char *);
size_t strcspn(const char *, const char *);
long strtol(const char *, char **, int);
long long strtoll(const char *, char **, int);
double strtod(const char *, char **);
int atoi(const char *);
char *getenv(const char *);
void qsort(void *, size_t, size_t, int (*)(const void *, const void *));
int *__errno_location(void);
#define errno (*__errno_location())
pid_t fork(void);
int pipe(int *);
int system(const char *);
int chmod(const char *, unsigned);
char *strtok(char *, const char *);
int dup2(int, int);
int execv(const char *, char *const[]);
pid_t setsid(void);
void _exit(int) __attribute__((noreturn));
pid_t waitpid(pid_t, int *, int);
#define WNOHANG 1
int execl(const char *, const char *, ...);
int kill(pid_t, int);
int usleep(unsigned);
int access(const char *, int);
#define F_OK 0
#define X_OK 1
#define R_OK 4
int unlink(const char *);
int rename(const char *, const char *);
int mkdir(const char *, unsigned);
int close(int);
int open(const char *, int, ...);
int ioctl(int, unsigned long, ...);
#define O_RDONLY 0
#define FBIOGET_VSCREENINFO 0x4600
ssize_t read(int, void *, size_t);
ssize_t write(int, const void *, size_t);
void sync(void);
typedef void (*sighandler_t)(int);
sighandler_t signal(int, sighandler_t);
#define SIGPIPE 13
#define SIGTERM 15
#define SIG_IGN ((sighandler_t)1)
struct timeval { long tv_sec; long tv_usec; };
struct timespec { long tv_sec; long tv_nsec; };
int gettimeofday(struct timeval *, void *);
time_t time(time_t *);
struct tm { int tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year, tm_wday, tm_yday, tm_isdst; long tm_gmtoff; const char *tm_zone; int pad[4]; };
struct tm *localtime_r(const time_t *, struct tm *);

struct sockaddr { unsigned short sa_family; char sa_data[14]; };
struct sockaddr_in { unsigned short sin_family; unsigned short sin_port; uint32_t sin_addr; unsigned char sin_zero[8]; };
int socket(int, int, int);
int connect(int, const struct sockaddr *, socklen_t);
ssize_t send(int, const void *, size_t, int);
ssize_t recv(int, void *, size_t, int);
int setsockopt(int, int, int, const void *, socklen_t);
#define AF_INET 2
#define SOCK_STREAM 1
#define SOL_SOCKET 1
#define SO_RCVTIMEO 20
#define SO_SNDTIMEO 21
#define MSG_NOSIGNAL 0x4000

typedef struct DIR DIR;
struct dirent { unsigned long d_ino; long d_off; unsigned short d_reclen; unsigned char d_type; char d_name[256]; };
DIR *opendir(const char *);
struct dirent *readdir(DIR *);
int closedir(DIR *);

typedef unsigned long pthread_t;
int pthread_create(pthread_t *, const void *, void *(*)(void *), void *);
int pthread_join(pthread_t, void **);
pthread_t pthread_self(void);
int pthread_equal(pthread_t, pthread_t);
struct pollfd { int fd; short events, revents; };
#define POLLIN 1
int poll(struct pollfd *, unsigned long, int);

/* ---------------- SDL 1.2 ---------------- */
typedef uint8_t Uint8; typedef uint16_t Uint16; typedef uint32_t Uint32; typedef int16_t Sint16;
typedef struct SDL_PixelFormat {
    void *palette;
    Uint8 BitsPerPixel, BytesPerPixel, Rloss, Gloss, Bloss, Aloss, Rshift, Gshift, Bshift, Ashift;
    Uint32 Rmask, Gmask, Bmask, Amask, colorkey; Uint8 alpha;
} SDL_PixelFormat;
typedef struct SDL_Surface {
    Uint32 flags; SDL_PixelFormat *format; int w, h; Uint16 pitch; void *pixels;
    int offset; void *hwdata; Sint16 cx, cy; Uint16 cw, ch; Uint32 unused1, locked; void *map; unsigned fv; int refcount;
} SDL_Surface;
typedef union SDL_Event {
    Uint8 type;
    struct { Uint8 type, which, state; struct { Uint8 scancode; int sym; int mod; Uint16 unicode; } keysym; } key;
    char pad[64];
} SDL_Event;
typedef struct SDL_AudioSpec {
    int freq; Uint16 format; Uint8 channels; Uint8 silence; Uint16 samples; Uint16 padding; Uint32 size;
    void (*callback)(void *, Uint8 *, int); void *userdata;
} SDL_AudioSpec;
_Static_assert(sizeof(SDL_AudioSpec) == 24, "audiospec");
#define SDL_INIT_AUDIO 0x10
#define SDL_INIT_VIDEO 0x20
#define SDL_SWSURFACE 0
#define SDL_HWSURFACE 0x00000001
#define SDL_DOUBLEBUF 0x40000000
#define SDL_KEYDOWN 2
#define SDL_KEYUP 3
#define SDL_QUIT 12
#define AUDIO_S16SYS 0x8010
int SDL_Init(Uint32);
int SDL_InitSubSystem(Uint32);
void SDL_Quit(void);
SDL_Surface *SDL_SetVideoMode(int, int, int, Uint32);
int SDL_Flip(SDL_Surface *);
int SDL_LockSurface(SDL_Surface *);
void SDL_UnlockSurface(SDL_Surface *);
int SDL_PollEvent(SDL_Event *);
int SDL_EnableKeyRepeat(int, int);
int SDL_ShowCursor(int);
int SDL_OpenAudio(SDL_AudioSpec *, SDL_AudioSpec *);
void SDL_PauseAudio(int);
void SDL_CloseAudio(void);
void SDL_LockAudio(void);
void SDL_UnlockAudio(void);

/* Miyoo Mini buttons as SDL keys */
#define K_UP 273
#define K_DOWN 274
#define K_RIGHT 275
#define K_LEFT 276
#define K_A 32        /* SDLK_SPACE */
#define K_B 306       /* SDLK_LCTRL */
#define K_X 304       /* SDLK_LSHIFT */
#define K_Y 308       /* SDLK_LALT */
#define K_L1 101      /* SDLK_e */
#define K_R1 116      /* SDLK_t */
#define K_L2 9        /* SDLK_TAB */
#define K_R2 8        /* SDLK_BACKSPACE */
#define K_START 13    /* SDLK_RETURN */
#define K_SELECT 305  /* SDLK_RCTRL */
#define K_MENU 27     /* SDLK_ESCAPE */

/* ---------------- FFmpeg 2.4 ---------------- */
typedef struct AVRational { int num, den; } AVRational;
typedef struct AVPacket {
    void *buf; int64_t pts, dts; uint8_t *data; int size; int stream_index; int flags;
    void *side_data; int side_data_elems; int duration; void *destruct; void *priv; int64_t pos; int64_t convergence_duration;
} AVPacket;
_Static_assert(sizeof(AVPacket) == 80, "AVPacket");
_Static_assert(offsetof(AVPacket, stream_index) == 32, "AVPacket.stream_index");
typedef struct AVFrame { uint8_t *data[8]; int linesize[8]; uint8_t **extended_data; int width, height, nb_samples, format; } AVFrame;
_Static_assert(offsetof(AVFrame, format) == 80, "AVFrame.format");
#define FLD(p, off, T) (*(T *)((char *)(p) + (off)))
#define FC_NB_STREAMS(fc) FLD(fc, 24, unsigned)
#define FC_STREAMS(fc)    FLD(fc, 28, void **)
#define FC_PB(fc)         FLD(fc, 16, void *)
#define FC_DURATION(fc)   FLD(fc, 1064, int64_t)
#define FC_FLAGS(fc)      FLD(fc, 1084, int)
#define AVFMT_FLAG_CUSTOM_IO 0x0080
#define AVIO_BUFFER(pb)   FLD(pb, 4, unsigned char *)
#define AVSEEK_SIZE 0x10000
#define AVSEEK_FORCE 0x20000
#define AVERROR_EOF (-0x20464F45)
#define AVERROR_EXIT (-0x54495845)
#define FC_INTERRUPT(fc)  FLD(fc, 1160, void *)
#define ST_CODEC(st)      FLD(st, 8, void *)
#define ST_TIMEBASE(st)   FLD(st, 40, AVRational)
#define CC_SAMPLE_RATE(c) FLD(c, 412, int)
#define CC_CHANNELS(c)    FLD(c, 416, int)
#define CC_CH_LAYOUT(c)   FLD(c, 448, uint64_t)
#define FR_SAMPLE_RATE(f) FLD(f, 344, int)
#define AV_NOPTS_VALUE ((int64_t)UINT64_C(0x8000000000000000))
#define AVMEDIA_TYPE_VIDEO 0
#define AVMEDIA_TYPE_AUDIO 1
#define AV_SAMPLE_FMT_S16 1
#define AV_CH_LAYOUT_STEREO 3
#define AVSEEK_FLAG_BACKWARD 1
#define AVDISCARD_DEFAULT 0
#define AVDISCARD_NONREF 8
#define AVDISCARD_ALL 48
void av_register_all(void);
int avformat_network_init(void);
void *avformat_alloc_context(void);
int avformat_open_input(void **, const char *, void *, void **);
int avformat_find_stream_info(void *, void **);
int av_find_best_stream(void *, int, int, int, void **, int);
void avformat_close_input(void **);
int av_read_frame(void *, AVPacket *);
int av_seek_frame(void *, int, int64_t, int);
void av_init_packet(AVPacket *);
int av_dup_packet(AVPacket *);
void av_free_packet(AVPacket *);
int avcodec_open2(void *, const void *, void **);
const void *avcodec_find_decoder_by_name(const char *);
void *avcodec_alloc_context3(const void *);
int avcodec_close(void *);
void avcodec_flush_buffers(void *);
int avcodec_decode_audio4(void *, AVFrame *, int *, const AVPacket *);
int avcodec_decode_video2(void *, AVFrame *, int *, const AVPacket *);
AVFrame *av_frame_alloc(void);
void av_frame_free(AVFrame **);
int64_t av_frame_get_best_effort_timestamp(const AVFrame *);
AVRational av_guess_sample_aspect_ratio(void *, void *, AVFrame *);
int av_opt_set_int(void *, const char *, int64_t, int);
int av_opt_set(void *, const char *, const char *, int);
int av_strerror(int, char *, size_t);
int64_t av_get_default_channel_layout(int);
void av_log_set_level(int);
void *av_malloc(size_t);
void av_free(void *);
void *avio_alloc_context(unsigned char *, int, int, void *, int (*)(void *, uint8_t *, int), int (*)(void *, uint8_t *, int), int64_t (*)(void *, int64_t, int));
int av_dict_set(void **, const char *, const char *, int);
void av_dict_free(void **);
void *swr_alloc_set_opts(void *, int64_t, int, int, int64_t, int, int, int, void *);
int swr_init(void *);
void swr_free(void **);
int swr_convert(void *, uint8_t **, int, const uint8_t **, int);
/* ASCII ctype (glibc's needs locale tables) */
static inline int tolower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
static inline int isalpha(int c) { return (c | 32) >= 'a' && (c | 32) <= 'z'; }
