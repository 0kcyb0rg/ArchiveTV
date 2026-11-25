/* The Linux build (RG35XX Plus family, TrimUI Smart Pro / Brick, ...): real system, SDL2 and FFmpeg 2.4 headers
   instead of sys.h's hand-made declarations, plus a small SDL 1.2-style layer over SDL2 (plat_sdl2.c), so
   atv.c reads the same on both. */
#pragma once
#define _GNU_SOURCE
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <math.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <dirent.h>
#include <poll.h>
#include <pthread.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
#include <SDL2/SDL.h>

/* FFmpeg fields atv.c reaches through macros (on the Miyoo they are fixed offsets) */
#define FC_NB_STREAMS(fc)  (((AVFormatContext *)(fc))->nb_streams)
#define FC_STREAMS(fc)     (*(void ***)&((AVFormatContext *)(fc))->streams)
#define FC_PB(fc)          (*(void **)&((AVFormatContext *)(fc))->pb)
#define FC_DURATION(fc)    (((AVFormatContext *)(fc))->duration)
#define FC_FLAGS(fc)       (((AVFormatContext *)(fc))->flags)
#define FC_INTERRUPT(fc)   (((AVFormatContext *)(fc))->interrupt_callback)
#define AVIO_BUFFER(pb)    (((AVIOContext *)(pb))->buffer)
#define ST_CODEC(st)       (*(void **)&((AVStream *)(st))->codec)
#define ST_TIMEBASE(st)    (((AVStream *)(st))->time_base)
#define ST_DISPOSITION(st) (((AVStream *)(st))->disposition)
#define ST_METADATA(st)    (*(void **)&((AVStream *)(st))->metadata)
#define CC_TYPE(c)         ((int)((AVCodecContext *)(c))->codec_type)
#define CC_CODEC_ID(c)     ((int)((AVCodecContext *)(c))->codec_id)
#define CC_SAMPLE_RATE(c)  (((AVCodecContext *)(c))->sample_rate)
#define CC_CHANNELS(c)     (((AVCodecContext *)(c))->channels)
#define CC_CH_LAYOUT(c)    (((AVCodecContext *)(c))->channel_layout)
#define FR_SAMPLE_RATE(f)  (av_frame_get_sample_rate(f))

/* SDL 1.2 names atv.c uses, provided by plat_sdl2.c */
#define SDL_SWSURFACE 0
#define SDL_HWSURFACE 1
#define SDL_DOUBLEBUF 0x40000000
SDL_Surface *atv_SetVideoMode(int w, int h, int bpp, Uint32 flags);
int atv_Flip(SDL_Surface *s);
int atv_PollEvent(SDL_Event *e);
int atv_EnableKeyRepeat(int delay, int interval);
#define SDL_SetVideoMode atv_SetVideoMode
#define SDL_Flip atv_Flip
#define SDL_PollEvent atv_PollEvent
#define SDL_EnableKeyRepeat atv_EnableKeyRepeat

/* the buttons, as the key codes atv.c compares against (same numbers as on the Miyoo; plat_sdl2.c makes them) */
#define K_UP 273
#define K_DOWN 274
#define K_RIGHT 275
#define K_LEFT 276
#define K_A 32
#define K_B 306
#define K_X 304
#define K_Y 308
#define K_L1 101
#define K_R1 116
#define K_L2 9
#define K_R2 8
#define K_START 13
#define K_SELECT 305
#define K_MENU 27
#define FBIOGET_VSCREENINFO 0x4600
