/* ArchiveTV for Miyoo Mini Plus (Onion OS)
   Browse Internet Archive video collections, download a video to the SD card, and play it.
   SDL 1.2 for screen, input and audio; FFmpeg 2.4 (bundled in libs/) for decoding;
   Web requests and downloads go through Onion OS's own curl (in /mnt/SDCARD/.tmp_update).
   Usage: archivetv <app_dir> [rotate_180 0|1] */
#include "sys.h"
#include "ui.h"
#include "version.h"

#define WMAX 1024
#define HMAX 768
static int W = 640, H = 480;   /* 640x480 on Mini v1-3 / Plus, 752x560 on Mini v4 / Flip */
#define ROWH 36
#define LISTY 54

static FILE *LOG;
static char appdir[512], viddir[600];
static int flip = 1;
enum { SFX_MOVE, SFX_SELECT, SFX_BACK, SFX_TAB, SFX_TYPE, SFX_TOGGLE, SFX_ERROR, SFX_DONE, SFX_HELLO, SFX_N };
static void sfx(int id);

static void logf_(const char *fmt, ...) {
    if (!LOG) return;
    va_list ap; va_start(ap, fmt); vfprintf(LOG, fmt, ap); va_end(ap);
    fputs("\n", LOG); fflush(LOG);
}
/* seconds since the app started. (Counting from 1970 made the drawing code's (int)(t * 12) overflow
   on the device, which froze every animation: the TV's blink, the static, the stripes, the bouncing.) */
static double now_s(void) {
    static long t0 = 0;
    struct timeval tv; gettimeofday(&tv, 0);
    if (!t0) t0 = tv.tv_sec - 1;
    return (double)(tv.tv_sec - t0) + tv.tv_usec / 1e6;
}

/* ======================= drawing ======================= */
static SDL_Surface *scr;
static uint32_t *back, *vbuf;
#define RGB(r, g, b) ((uint32_t)(((r) << 16) | ((g) << 8) | (b)))
static void rect(uint32_t *buf, int x, int y, int w, int h, uint32_t c) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > W) w = W - x;
    if (y + h > H) h = H - y;
    if (w <= 0 || h <= 0) return;
    for (int j = 0; j < h; j++) { uint32_t *p = buf + (y + j) * W + x; for (int i = 0; i < w; i++) p[i] = c; }
}
/* ---------- screen effects: overlays/<key>.ovl, 640x480 x (keep, r, g, b) bytes ---------- */
static uint8_t *ovl;                  /* NULL = off */
static char fx_keys[12][16], fx_names[12][40]; static int nfx = 0, g_fx = 1;   /* scanlines on by default */
static void fx_list(void) {
    nfx = 0;
    char p[700]; snprintf(p, sizeof p, "%s/overlays/effects.txt", appdir);
    FILE *f = fopen(p, "r"); if (!f) return;
    char line[120];
    while (nfx < 12 && fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        char *bar = strchr(line, '|'); if (!bar) continue;
        *bar = 0; char *nm = bar + 1; while (*nm == ' ') nm++;
        char *k = line; size_t L = strlen(k); while (L && k[L - 1] == ' ') k[--L] = 0;
        snprintf(fx_keys[nfx], sizeof fx_keys[0], "%s", k);
        snprintf(fx_names[nfx], sizeof fx_names[0], "%s", nm);
        nfx++;
    }
    fclose(f);
}
/* fx 0 = off, 1..nfx = effects */
static void fx_load(int fx) {
    free(ovl); ovl = 0;
    if (fx <= 0 || fx > nfx) return;
    char p[700];
    if (W == 640 && H == 480) snprintf(p, sizeof p, "%s/overlays/%s.ovl", appdir, fx_keys[fx - 1]);
    else snprintf(p, sizeof p, "%s/overlays/%s_%dx%d.ovl", appdir, fx_keys[fx - 1], W, H);
    FILE *f = fopen(p, "rb"); if (!f) { logf_("no overlay %s", p); return; }
    ovl = (uint8_t *)malloc(W * H * 4);
    if (ovl && fread(ovl, 1, W * H * 4, f) != (size_t)(W * H * 4)) { free(ovl); ovl = 0; }
    fclose(f);
}
static int g_vsync = 1;   /* hardware double buffer: frames swap on vsync, so no tearing */
static SDL_Surface *video_mode(int vsync) {
    SDL_Surface *s = 0;
    if (vsync) { s = SDL_SetVideoMode(W, H, 32, SDL_HWSURFACE | SDL_DOUBLEBUF); if (s) logf_("video: vsync on (flags %x)", s->flags); }
    if (!s) { s = SDL_SetVideoMode(W, H, 32, SDL_SWSURFACE); if (s) logf_("video: vsync off"); }
    return s;
}
static void present(const uint32_t *src) {
    if (!scr) return;
    SDL_LockSurface(scr);
    uint8_t *px = (uint8_t *)scr->pixels;
    SDL_PixelFormat *f = scr->format;
    int pitch = scr->pitch, sw = scr->w < W ? scr->w : W, sh = scr->h < H ? scr->h : H;
    if (f->BytesPerPixel == 4) {
        int std = f->Rshift == 16 && f->Gshift == 8 && f->Bshift == 0;
        for (int y = 0; y < sh; y++) {
            const uint32_t *s = src + y * W;
            uint32_t *d = (uint32_t *)(px + (flip ? sh - 1 - y : y) * pitch);
            if (ovl && std) {
                const uint8_t *o = ovl + y * W * 4;
                for (int x = 0; x < sw; x++, o += 4) {
                    uint32_t v = s[x], k = o[0];
                    uint32_t r = (((v >> 16) & 255) * k >> 8) + o[1], g = (((v >> 8) & 255) * k >> 8) + o[2], b = ((v & 255) * k >> 8) + o[3];
                    d[flip ? sw - 1 - x : x] = (r << 16) | (g << 8) | b;
                }
                continue;
            }
            if (!flip && std) { memcpy(d, s, (size_t)sw * 4); continue; }
            if (std) { for (int x = 0; x < sw; x++) d[sw - 1 - x] = s[x]; continue; }
            for (int x = 0; x < sw; x++) {
                uint32_t v = s[x];
                v = (((v >> 16) & 255) << f->Rshift) | (((v >> 8) & 255) << f->Gshift) | ((v & 255) << f->Bshift);
                d[flip ? sw - 1 - x : x] = v;
            }
        }
    } else if (f->BytesPerPixel == 2) {
        for (int y = 0; y < sh; y++) {
            const uint32_t *s = src + y * W;
            uint16_t *d = (uint16_t *)(px + (flip ? sh - 1 - y : y) * pitch);
            for (int x = 0; x < sw; x++) {
                uint32_t v = s[x];
                d[flip ? sw - 1 - x : x] = (uint16_t)(((v >> 8) & 0xF800) | ((v >> 5) & 0x07E0) | ((v >> 3) & 0x1F));
            }
        }
    }
    SDL_UnlockSurface(scr);
    SDL_Flip(scr);
}

/* UTF-8 -> Latin-1 for the bitmap font */
static void l1(char *dst, size_t cap, const char *src) {
    size_t k = 0;
    const unsigned char *p = (const unsigned char *)src;
    while (*p && k + 3 < cap) {
        unsigned cp = *p++;
        if (cp >= 0x80) {
            int n = cp >= 0xF0 ? 3 : cp >= 0xE0 ? 2 : cp >= 0xC0 ? 1 : 0;
            cp &= n == 3 ? 7 : n == 2 ? 15 : 31;
            for (int i = 0; i < n && (*p & 0xC0) == 0x80; i++) cp = (cp << 6) | (*p++ & 63);
            if (n == 0) cp = '?';
        }
        if (cp == '\n' || cp == '\r' || cp == '\t') cp = ' ';
        if (cp < 256) { if (cp >= 32) dst[k++] = (char)cp; continue; }
        if (cp == 0x2018 || cp == 0x2019) dst[k++] = '\'';
        else if (cp == 0x201C || cp == 0x201D) dst[k++] = '"';
        else if (cp == 0x2013 || cp == 0x2014) dst[k++] = '-';
        else if (cp == 0x2026) { dst[k++] = '.'; dst[k++] = '.'; dst[k++] = '.'; }
        else dst[k++] = '?';
    }
    dst[k] = 0;
}
static void fmt_size(char *d, size_t cap, long long b) {
    if (b <= 0) snprintf(d, cap, "?");
    else if (b < 1024 * 1024) snprintf(d, cap, "%dK", (int)(b / 1024));
    else if (b < 1024LL * 1024 * 1024) snprintf(d, cap, "%dM", (int)(b / (1024 * 1024)));
    else snprintf(d, cap, "%d.%dG", (int)(b >> 30), (int)((b % (1LL << 30)) * 10 >> 30));
}
static void fmt_time(char *d, size_t cap, double t) {
    if (t < 0) t = 0;
    int s = (int)t, h = s / 3600, m = s / 60 % 60;
    if (h) snprintf(d, cap, "%d:%02d:%02d", h, m, s % 60);
    else snprintf(d, cap, "%02d:%02d", m, s % 60);
}

/* ---------- common screens (look: ui.c) ---------- */
static void message_m(const char *title, const char *l1s, const char *l2s, int mood, const char *hints) {
    ui_message(title, l1s, l2s, mood, hints, now_s());
    present(back);
}
/* the static-y "Tuning in..." screen; it keeps animating while the network call behind it waits */
static struct { int on; double last; char title[64], l1[160], l2[240], hints[64]; } LM;
static void message(const char *title, const char *l1s, const char *l2s, const char *hints) {
    message_m(title, l1s, l2s, MOOD_STATIC, hints);
    snprintf(LM.title, sizeof LM.title, "%s", title ? title : "");
    snprintf(LM.l1, sizeof LM.l1, "%s", l1s ? l1s : "");
    snprintf(LM.l2, sizeof LM.l2, "%s", l2s ? l2s : "");
    snprintf(LM.hints, sizeof LM.hints, "%s", hints ? hints : "");
    LM.on = 1; LM.last = now_s();
}
static void loading_anim(void) {
    double t = now_s();
    if (!LM.on || t - LM.last < 0.08) return;
    LM.last = t;
    message_m(LM.title, LM.l1, LM.l2[0] ? LM.l2 : 0, MOOD_STATIC, LM.hints[0] ? LM.hints : 0);
}
static int wait_key(void) {
    for (;;) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) return K_MENU;
            if (e.type == SDL_KEYDOWN) return e.key.keysym.sym;
        }
        usleep(20000);
    }
}
static void flush_keys(void) { SDL_Event e; while (SDL_PollEvent(&e)) {} }
/* animated message that waits for a button */
static int ask(const char *title, const char *l1s, const char *l2s, int mood, const char *hints) {
    usleep(150000);
    flush_keys();
    for (;;) {
        message_m(title, l1s, l2s, mood, hints);
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) return K_MENU;
            if (e.type == SDL_KEYDOWN) return e.key.keysym.sym;
        }
        usleep(50000);
    }
}
static void error_box(const char *what, const char *detail) { LM.on = 0; sfx(SFX_ERROR); ask("Oops!", what, detail, MOOD_SAD, "A:OK"); }

/* Generic list: label(i) gives the main text, the right-hand text and a tone (0 normal, 1 good, 2 warn, 3 dim, 4 more). */
typedef void (*LabelFn)(int i, char *main, size_t mc, char *side, size_t sc, int *tone);
#define LMAXCH 34
static const char *const TABS[6] = { "Search", "Guide", "Saved", "Lists", "Files", "\x01" };   /* \x01 = gear icon */
static int g_theme = 1;   /* Night is the default */
static void settings_save(int tab);
static void queue_badge(char *b, size_t cap);
static void queue_tick(void);
#define TH_MAXW 176
#define TH_MAXH 132
#define TH_SLOTS 32
typedef struct { char id[600]; int state, w, h; uint32_t px[TH_MAXW * TH_MAXH]; double used; } Thumb;   /* id = the picture's address */   /* state 1 = fetching, 2 = ready, 3 = none */
static void th_poll(void);
static void local_thumb_drop(const char *name);
static pid_t th_pid = 0;   /* the picture being fetched */
static Thumb *thumb_want(const char *url, int *waiting);
static const char *thumb_of_item(const char *id);
static int g_thumbs = 1;   /* Settings > Thumbnails */
typedef const char *(*ThumbFn)(int row);
static ThumbFn g_thumb_fn = 0;   /* set right before a list_loop whose rows have Internet Archive items */
static int g_exit_code = 0;   /* 42 = launch.sh installs a downloaded update and starts the app again */
static void upd_poll(void);
static const char *upd_note(void);
static int g_quit = 0;   /* set once the user says yes to "Turn off ArchiveTV?": every screen backs out */
static void autooff_check(void);
static int quit_ask(void);
static int list_loop_t(const char *title, int n, LabelFn fn, const char *footer, int *sel, const char *empty, const char **status, int tab) {
    LM.on = 0;   /* a list is up: the loading screen is over */
    if (g_quit) return K_MENU;
    autooff_check();
    if (g_quit) return K_MENU;
    int bheld = 0; double xdown = 0, mpause = 0; double moff = 0, tlast = now_s();   /* moff = ms of title scrolling; holding X runs it faster */
    const char *theme_msg = 0; double theme_t = 0, note_t = -100; const char *note = 0;
    if (*sel >= n) *sel = n ? n - 1 : 0;
    if (*sel < 0) *sel = 0;
    int top = *sel - UVIS / 2; if (top < 0) top = 0;
    double tsel = now_s();
    static char mains[16][400], sides[16][48];
    URow rows_v[16];
    ThumbFn thf = g_thumb_fn; g_thumb_fn = 0;   /* this list's preview pictures (screens set it each time) */
    SDL_EnableKeyRepeat(300, 70);
    for (;;) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) { g_quit = 1; return K_MENU; }
            if (e.type == SDL_KEYUP && e.key.keysym.sym == K_X && xdown > 0) {
                /* X: a tap does the screen's X action, holding it scrolls the title fast */
                int tap = now_s() - xdown < 0.3; xdown = 0; bheld = 0;
                if (tap) { sfx(SFX_SELECT); SDL_EnableKeyRepeat(0, 0); return K_X; }
                continue;
            }
            if (e.type != SDL_KEYDOWN) continue;
            int k = e.key.keysym.sym, os = *sel;
            if (k == K_L2) k = K_L1;   /* the back triggers do the same as the shoulders in lists */
            if (k == K_R2) k = K_R1;
            if (k == K_X) { if (xdown == 0) xdown = now_s(); continue; }
            if (k == K_B && tab >= 0) continue;   /* B does nothing on the main tabs (MENU turns the app off) */
            if (k == K_MENU) {
                sfx(SFX_BACK);
                if (quit_ask()) { g_quit = 1; SDL_EnableKeyRepeat(0, 0); return K_MENU; }
                SDL_EnableKeyRepeat(300, 70); flush_keys(); bheld = 0;
                continue;
            }
            switch (k) {
            case K_UP: if (n) *sel = (*sel + n - 1) % n; break;
            case K_DOWN: if (n) *sel = (*sel + 1) % n; break;
            case K_L1: case K_R1:
                if (tab >= 0) { sfx(SFX_TAB); SDL_EnableKeyRepeat(0, 0); return k; }
                if (k == K_L1) { *sel -= UVIS; if (*sel < 0) *sel = 0; } else { *sel += UVIS; if (*sel >= n) *sel = n ? n - 1 : 0; }
                break;
            case K_LEFT: *sel -= UVIS; if (*sel < 0) *sel = 0; break;
            case K_RIGHT: *sel += UVIS; if (*sel >= n) *sel = n ? n - 1 : 0; break;
            case K_SELECT:
                sfx(SFX_TOGGLE);
                g_theme = (g_theme + 1) % ui_theme_count();
                ui_set_theme(g_theme); settings_save(-1);
                { static char tm[40]; snprintf(tm, sizeof tm, "Theme: %s", ui_theme_name(g_theme)); theme_msg = tm; theme_t = now_s(); }
                break;
            case K_A: case K_B: case K_Y: case K_START:
                sfx(k == K_B ? SFX_BACK : SFX_SELECT);
                SDL_EnableKeyRepeat(0, 0);
                return k;
            default: break;
            }
            if (*sel != os) { tsel = now_s(); moff = 0; mpause = 0; sfx(SFX_MOVE); }
        }
        {   /* title scrolling: holding X runs to the end of the title and stops there; let go = a 1 s pause, then normal */
            double tn = now_s(), dt = tn - tlast; tlast = tn;
            int was = bheld;
            bheld = xdown > 0 && tn - xdown >= 0.3;
            if (was && !bheld) mpause = tn + 1.0;
            if (bheld) {
                double endp = 1000.0 + ui_marquee_extra * 200.0;
                if (moff < 1000) moff = 1000;
                if (moff < endp) { moff += dt * 5000.0; if (moff > endp) moff = endp; }
            } else if (tn >= mpause) moff += dt * 1000.0;
        }
        if (*sel < top) top = *sel;
        if (*sel >= top + UVIS) top = *sel - UVIS + 1;
        if (top > n - UVIS) top = n - UVIS > 0 ? n - UVIS : 0;
        double t = now_s();
        for (int i = 0; i < UVIS && top + i < n; i++) {
            int tone = 0;
            mains[i][0] = sides[i][0] = 0;
            fn(top + i, mains[i], sizeof mains[i], sides[i], sizeof sides[i], &tone);
            rows_v[i].main = mains[i]; rows_v[i].side = sides[i]; rows_v[i].tone = tone;
        }
        char cnt[32]; snprintf(cnt, sizeof cnt, "%d/%d", n ? *sel + 1 : 0, n);
        { static double lq = 0; if (t - lq > 0.3) { lq = t; queue_tick(); autooff_check(); upd_poll(); if (g_quit) { SDL_EnableKeyRepeat(0, 0); return K_MENU; } } }
        if (tab >= 0 && !note) { note = upd_note(); if (note) note_t = t; }
        char qb[24]; queue_badge(qb, sizeof qb); ui_footer_badge(qb);
        ui_bg(t);
        if (tab >= 0) ui_header_tabs(TABS, 6, tab, 0, t);
        else ui_header(title, cnt, t, MOOD_HAPPY);
        (void)tsel; ui_list(rows_v, n, *sel, top, (int)moff, t, empty);
        th_poll();
        if (thf && n) {   /* the selected row's picture (static while it comes), then the next rows' ahead of time */
            const char *id = thf(*sel); int wait = 0;
            Thumb *th = id ? thumb_want(id, &wait) : 0;
            if (th && th->state == 2) ui_thumb(th->px, th->w, th->h, *sel - top, t);
            else if (wait || (th && th->state == 1)) ui_thumb(0, 0, 0, *sel - top, t);
            if (th_pid <= 0 && th && th->state >= 2 && t - tsel > 0.15)
                for (int d = 1; d <= 4 && th_pid <= 0; d++) {
                    int r = *sel + (d == 4 ? -1 : d); if (r < 0 || r >= n) continue;
                    const char *pid_ = thf(r); int w2; if (pid_) thumb_want(pid_, &w2);
                }
        }
        if (theme_msg && t - theme_t < 1.5) ui_toast(theme_msg);
        else if (note && t - note_t < 4) ui_toast(note);
        else if (status && *status && **status) ui_toast(*status);
        ui_footer(footer);
        present(back);
        usleep(30000);
    }
}

static int list_loop(const char *title, int n, LabelFn fn, const char *footer, int *sel, const char *empty, const char **status) {
    return list_loop_t(title, n, fn, footer, sel, empty, status, -1);
}
/* popup menu over the current screen; returns the chosen index or -1 */
static int menu_pick(const char *title, const char *const *items, int n) {
    static uint32_t saved[WMAX * HMAX];
    memcpy(saved, back, sizeof(uint32_t) * W * H);
    int sel = 0;
    SDL_EnableKeyRepeat(300, 80);
    flush_keys();
    for (;;) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type != SDL_KEYDOWN) continue;
            switch (e.key.keysym.sym) {
            case K_UP: sel = (sel + n - 1) % n; sfx(SFX_MOVE); break;
            case K_DOWN: sel = (sel + 1) % n; sfx(SFX_MOVE); break;
            case K_A: case K_START: sfx(SFX_SELECT); SDL_EnableKeyRepeat(0, 0); return sel;
            case K_B: case K_MENU: sfx(SFX_BACK); SDL_EnableKeyRepeat(0, 0); return -1;
            default: break;
            }
        }
        memcpy(back, saved, sizeof(uint32_t) * W * H);
        ui_menu(title, items, n, sel, now_s());
        present(back);
        usleep(30000);
    }
}


/* ======================= menu audio: looping music + little sounds ======================= */
static const char *SFX_NAMES[SFX_N] = { "move", "select", "back", "tab", "type", "toggle", "error", "done", "hello" };
static int16_t *sfx_buf[SFX_N]; static int sfx_len[SFX_N];
static int g_music = 1, g_sounds = 1;
static int g_group = 1;     /* fold episodes of one show into a single row */
static int g_autooff = 0;   /* power off when the download queue is done */
static int g_off_armed = 0; /* a queued download finished since the queue was last idle */
#define MRING (1 << 18)      /* 256 KB of music ready to play */
static struct {
    int open;
    uint8_t ring[MRING]; int rd, wr, fill;
    volatile int run; pthread_t th; int have_th;
    long pos, loop_start, loop_end;   /* in stereo frames */
    int fade;                         /* frames of fade-in left */
    struct { const int16_t *d; int n, p; } voice[4];
} MA;
static void sfx_load(void) {
    for (int i = 0; i < SFX_N; i++) {
        char p[700]; snprintf(p, sizeof p, "%s/sfx/%s.raw", appdir, SFX_NAMES[i]);
        FILE *f = fopen(p, "rb"); if (!f) continue;
        fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
        sfx_buf[i] = (int16_t *)malloc((size_t)n);
        if (sfx_buf[i] && fread(sfx_buf[i], 1, (size_t)n, f) == (size_t)n) sfx_len[i] = (int)(n / 2); else { free(sfx_buf[i]); sfx_buf[i] = 0; }
        fclose(f);
    }
    char p[700]; snprintf(p, sizeof p, "%s/music/music.txt", appdir);
    FILE *f = fopen(p, "r");
    if (f) {
        char line[80];
        while (fgets(line, sizeof line, f)) {
            if (!strncmp(line, "loop_start ", 11)) MA.loop_start = strtol(line + 11, 0, 10);
            else if (!strncmp(line, "loop_end ", 9)) MA.loop_end = strtol(line + 9, 0, 10);
        }
        fclose(f);
    }
}
static void sfx(int id) {
    if (!g_sounds || !MA.open || id < 0 || id >= SFX_N || !sfx_buf[id]) return;
    SDL_LockAudio();
    int v = 0;
    for (int i = 0; i < 4; i++) if (MA.voice[i].p >= MA.voice[i].n) { v = i; break; } else if (i == 3) v = 0;
    MA.voice[v].d = sfx_buf[id]; MA.voice[v].n = sfx_len[id]; MA.voice[v].p = 0;
    SDL_UnlockAudio();
}
static void menu_cb(void *u, Uint8 *out, int len) {
    (void)u;
    int16_t *o = (int16_t *)out;
    int frames = len / 4;
    int have = MA.fill / 4; if (have > frames) have = frames;
    for (int i = 0; i < frames; i++) {
        int l = 0, r = 0;
        if (i < have) {
            int16_t *m = (int16_t *)(MA.ring + MA.rd);
            l = m[0]; r = m[1];
            MA.rd = (MA.rd + 4) % MRING;
            /* music sits a bit under the sounds; short fade-in when it (re)starts */
            int g = 115;
            if (MA.fade > 0) { g = g * (44100 - MA.fade) / 44100; MA.fade--; }
            l = l * g >> 8; r = r * g >> 8;
        }
        for (int v = 0; v < 4; v++) if (MA.voice[v].p < MA.voice[v].n) { int s2 = MA.voice[v].d[MA.voice[v].p++] * 140 >> 8; l += s2; r += s2; }
        o[2 * i] = (int16_t)(l > 32767 ? 32767 : l < -32768 ? -32768 : l);
        o[2 * i + 1] = (int16_t)(r > 32767 ? 32767 : r < -32768 ? -32768 : r);
    }
    MA.fill -= have * 4;
}
/* keeps the ring topped up from music/music.raw, jumping from loop_end back to loop_start */
/* The menu music ships compressed (music/music.ogg, Vorbis: decodes to the exact same samples).
   The first run unpacks it once to music/music.raw on the card, so it can loop sample-exactly.
   Runs on the music thread; gives up (and tries again next time) if the music is stopped meanwhile. */
static int music_unpack(const char *ogg, const char *raw) {
    char tmp[720]; snprintf(tmp, sizeof tmp, "%s.tmp", raw);
    double t0 = now_s();
    void *fc = 0, *swr = 0, *dec = 0, *cc = 0;
    AVFrame *fr = 0; FILE *out = 0; int ok = 0, ocap = 0; uint8_t *ob = 0; long long frames = 0;
    if (avformat_open_input(&fc, ogg, 0, 0) < 0) { logf_("music: cannot open %s", ogg); return -1; }
    if (avformat_find_stream_info(fc, 0) < 0) goto done;
    int ai = av_find_best_stream(fc, AVMEDIA_TYPE_AUDIO, -1, -1, &dec, 0);
    if (ai < 0 || !dec) goto done;
    cc = ST_CODEC(FC_STREAMS(fc)[ai]);
    if (avcodec_open2(cc, dec, 0) < 0) { cc = 0; goto done; }
    fr = av_frame_alloc(); out = fopen(tmp, "wb");
    if (!fr || !out) goto done;
    AVPacket pkt;
    int stopped = 0;
    while (av_read_frame(fc, &pkt) >= 0) {
        if (!MA.run) { stopped = 1; av_free_packet(&pkt); break; }
        if (pkt.stream_index == ai) {
            AVPacket t = pkt;
            while (t.size > 0) {
                int got = 0, len = avcodec_decode_audio4(cc, fr, &got, &t);
                if (len < 0) break;
                t.data += len; t.size -= len;
                if (!got) continue;
                int rate = FR_SAMPLE_RATE(fr) ? FR_SAMPLE_RATE(fr) : CC_SAMPLE_RATE(cc);
                if (!swr) {
                    int64_t lay = CC_CH_LAYOUT(cc) ? (int64_t)CC_CH_LAYOUT(cc) : av_get_default_channel_layout(CC_CHANNELS(cc));
                    swr = swr_alloc_set_opts(0, AV_CH_LAYOUT_STEREO, AV_SAMPLE_FMT_S16, 44100, lay, fr->format, rate, 0, 0);
                    if (!swr || swr_init(swr) < 0) { logf_("music: swr init failed"); goto done; }
                }
                int need = fr->nb_samples * 2 + 2048;
                if (need > ocap) { free(ob); ob = (uint8_t *)malloc((size_t)need * 4); ocap = ob ? need : 0; if (!ob) goto done; }
                int ns = swr_convert(swr, &ob, ocap, (const uint8_t **)fr->extended_data, fr->nb_samples);
                if (ns > 0) { if (fwrite(ob, 4, (size_t)ns, out) != (size_t)ns) goto done; frames += ns; }
            }
        }
        av_free_packet(&pkt);
    }
    if (stopped) { logf_("music: unpacking stopped, will finish next time"); goto done; }
    if (swr && ob) { int ns = swr_convert(swr, &ob, ocap, 0, 0); if (ns > 0 && fwrite(ob, 4, (size_t)ns, out) == (size_t)ns) frames += ns; }
    ok = frames > 44100;
done:
    if (out) { fclose(out); out = 0; }
    if (ok && rename(tmp, raw) == 0) { sync(); logf_("music: unpacked %lld frames in %.1f s", frames, now_s() - t0); }
    else { ok = 0; unlink(tmp); }
    free(ob); if (swr) swr_free(&swr);
    if (fr) av_frame_free(&fr);
    if (cc) avcodec_close(cc);
    avformat_close_input(&fc);
    return ok ? 0 : -1;
}
static void *music_thread(void *a) {
    (void)a;
    char p[700]; snprintf(p, sizeof p, "%s/music/music.raw", appdir);
    if (access(p, F_OK) != 0) {
        char ogg[700]; snprintf(ogg, sizeof ogg, "%s/music/music.ogg", appdir);
        if (access(ogg, F_OK) == 0) music_unpack(ogg, p);
    }
    FILE *f = fopen(p, "rb");
    if (!f) { logf_("no music at %s", p); return 0; }
    static uint8_t chunk[16384];
    if (MA.loop_end <= 0) { fseek(f, 0, SEEK_END); MA.loop_end = ftell(f) / 4; }
    fseek(f, MA.pos * 4, SEEK_SET);
    while (MA.run) {
        int space; SDL_LockAudio(); space = MRING - MA.fill; SDL_UnlockAudio();
        if (space < (int)sizeof chunk) { usleep(20000); continue; }
        long want = sizeof chunk / 4;
        if (MA.pos + want > MA.loop_end) want = MA.loop_end - MA.pos;
        size_t got = want > 0 ? fread(chunk, 4, (size_t)want, f) : 0;
        if (got == 0) { MA.pos = MA.loop_start; fseek(f, MA.pos * 4, SEEK_SET); continue; }
        SDL_LockAudio();
        size_t bytes = got * 4, done = 0;
        while (done < bytes) {
            size_t c = (size_t)(MRING - MA.wr); if (c > bytes - done) c = bytes - done;
            memcpy(MA.ring + MA.wr, chunk + done, c);
            MA.wr = (int)((MA.wr + c) % MRING); done += c;
        }
        MA.fill += (int)bytes;
        SDL_UnlockAudio();
        MA.pos += (long)got;
        if (MA.pos >= MA.loop_end) { MA.pos = MA.loop_start; fseek(f, MA.pos * 4, SEEK_SET); }
    }
    fclose(f);
    return 0;
}
static void music_stop_thread(void) {
    if (MA.have_th) { MA.run = 0; pthread_join(MA.th, 0); MA.have_th = 0; }
    SDL_LockAudio(); MA.rd = MA.wr = MA.fill = 0; SDL_UnlockAudio();
}
static void music_start_thread(void) {
    if (!MA.open || !g_music || MA.have_th) return;
    MA.fade = 44100;
    MA.run = 1;
    if (pthread_create(&MA.th, 0, music_thread, 0) == 0) MA.have_th = 1;
}
static void menu_audio_start(void) {
    if (MA.open || (!g_music && !g_sounds)) return;
    SDL_AudioSpec want, got;
    memset(&want, 0, sizeof want); memset(&got, 0, sizeof got);
    want.freq = 44100; want.format = AUDIO_S16SYS; want.channels = 2; want.samples = 1024; want.callback = menu_cb;
    if (SDL_OpenAudio(&want, &got) < 0) { logf_("menu audio: SDL_OpenAudio failed"); return; }
    MA.open = 1;
    for (int i = 0; i < 4; i++) MA.voice[i].n = MA.voice[i].p = 0;
    music_start_thread();
    SDL_PauseAudio(0);
}
static void menu_audio_stop(void) {
    if (!MA.open) return;
    music_stop_thread();
    SDL_PauseAudio(1);
    SDL_CloseAudio();
    MA.open = 0;
}
static void menu_audio_apply(void) {   /* after a settings change */
    if (!g_music && !g_sounds) { menu_audio_stop(); return; }
    if (!MA.open) { menu_audio_start(); return; }
    if (g_music) music_start_thread(); else music_stop_thread();
}

static void urlenc(char *d, size_t cap, const char *s, int keep_slash) {
    static const char *hx = "0123456789ABCDEF";
    size_t k = 0;
    for (const unsigned char *p = (const unsigned char *)s; *p && k + 4 < cap; p++) {
        unsigned c = *p;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~' || (keep_slash && c == '/'))
            d[k++] = (char)c;
        else { d[k++] = '%'; d[k++] = hx[c >> 4]; d[k++] = hx[c & 15]; }
    }
    d[k] = 0;
}

/* ======================= HTTP: Onion's curl ======================= */
/* Onion OS ships curl (with TLS) in .tmp_update; every web request goes through it. */
/* curl: Onion keeps it in .tmp_update; spruceOS ships a static one in spruce/bin. Found once, at first use. */
static const char *curl_bin(void) {
    static const char *found = 0;
    if (found) return found;
    static char mine[720];   /* our own static curl in App/ArchiveTV/bin, for systems without one (sprigUI) */
    snprintf(mine, sizeof mine, "%s/bin/curl", appdir);
    const char *C[] = { "/mnt/SDCARD/.tmp_update/bin/curl", "/mnt/SDCARD/spruce/bin/curl", mine, "/usr/bin/curl", "/bin/curl" };
    for (int i = 0; i < 5; i++) if (access(C[i], F_OK) == 0) { found = C[i]; break; }
    if (!found) found = C[0];
    if (found == mine) chmod(mine, 0755);
    logf_("curl: %s", found);
    return found;
}
#define CURL_BIN curl_bin()
#define CURL_ENV "LD_LIBRARY_PATH=/mnt/SDCARD/.tmp_update/lib:/customer/lib:/mnt/SDCARD/miyoo/lib:$LD_LIBRARY_PATH"
static void shq(char *d, size_t cap, const char *s) {   /* single-quote for sh */
    size_t k = 0;
    if (k + 1 < cap) d[k++] = '\'';
    for (; *s && k + 6 < cap; s++) { if (*s == '\'') { memcpy(d + k, "'\\''", 4); k += 4; } else d[k++] = *s; }
    if (k + 1 < cap) d[k++] = '\'';
    d[k] = 0;
}
static void curl_tls(char *d, size_t cap) {
    const char *v = getenv("VERIFY");
    if (v && !strcmp(v, "0")) { snprintf(d, cap, "-k"); return; }
    char ca[700], q[800]; snprintf(ca, sizeof ca, "%s/cacert.pem", appdir); shq(q, sizeof q, ca);
    snprintf(d, cap, "--cacert %s", q);
}
/* asks the server for the file size; runs curl in its own process so a stop (d->cancel) never has to wait */
/* Waits for data on a socket or pipe in short slices. On the screen's own thread B/MENU gives up (returns -9),
   so a stuck connection never freezes the app; gives up by itself after `secs` with nothing arriving. */
static pthread_t ui_thread; static int ui_thread_set = 0;
static int net_wait(int fd, int secs) {
    double t0 = now_s();
    int ui = ui_thread_set && pthread_self() == ui_thread;
    for (;;) {
        struct pollfd pf; pf.fd = fd; pf.events = POLLIN; pf.revents = 0;
        if (ui) loading_anim();
        int r = poll(&pf, 1, 100);
        if (r > 0) return 1;
        if (r < 0 && errno != 4 /* EINTR */) return -1;
        if (ui) {
            SDL_Event e;
            while (SDL_PollEvent(&e)) if (e.type == SDL_KEYDOWN && (e.key.keysym.sym == K_B || e.key.keysym.sym == K_MENU)) { logf_("network wait cancelled"); return -9; }
        }
        if (now_s() - t0 > secs) { logf_("network: nothing for %d s, giving up", secs); return 0; }
    }
}
/* fetches a URL into memory (API answers, a few hundred KB at most). err: -5 = cancelled with B,
   -3 = server said no / no answer, -4 = out of memory, -6 = curl missing */
static char *http_get(const char *url, int *err) {
    *err = 0;
    if (access(CURL_BIN, F_OK) != 0) { logf_("curl not found at %s", CURL_BIN); *err = -6; LM.on = 0; return 0; }
    static char cmd[4400]; char qu[2600], tls[900];
    shq(qu, sizeof qu, url); curl_tls(tls, sizeof tls);
    snprintf(cmd, sizeof cmd, "%s exec %s -sSL --fail --connect-timeout 20 --max-time 90 -A ArchiveTV-Miyoo %s %s",
             CURL_ENV, CURL_BIN, tls, qu);
    int pfd[2];
    if (pipe(pfd) != 0) { *err = -3; LM.on = 0; return 0; }
    pid_t pid = fork();
    if (pid == 0) { dup2(pfd[1], 1); close(pfd[0]); close(pfd[1]); execl("/bin/sh", "sh", "-c", cmd, (char *)0); _exit(127); }
    close(pfd[1]);
    if (pid < 0) { close(pfd[0]); *err = -3; LM.on = 0; return 0; }
    size_t cap = 65536, len = 0;
    char *b = (char *)malloc(cap);
    int cancelled = 0, stalled = 0;
    while (b) {
        if (len + 32769 > cap) {
            if (cap > (48u << 20)) break;
            char *nb = (char *)realloc(b, cap * 2);
            if (!nb) { free(b); b = 0; break; }
            b = nb; cap *= 2;
        }
        int w = net_wait(pfd[0], 30);
        if (w == -9) { cancelled = 1; break; }
        if (w <= 0) { stalled = 1; break; }
        ssize_t k = read(pfd[0], b + len, 32768);
        if (k <= 0) break;
        len += (size_t)k;
    }
    close(pfd[0]);
    int st = 0;
    if (cancelled || stalled || !b) kill(pid, SIGTERM);
    waitpid(pid, &st, 0);
    int code = (st & 0x7f) ? -1 : ((st >> 8) & 255);
    if (cancelled) { free(b); *err = -5; LM.on = 0; return 0; }
    if (!b) { *err = -4; LM.on = 0; return 0; }
    if (stalled || code != 0) { LM.on = 0; logf_("http: curl exit %d%s for %s", code, stalled ? " (stalled)" : "", url); free(b); *err = code == 127 || code == 126 ? -6 : -3; return 0; }
    b[len] = 0;
    logf_("http: %u bytes from %s", (unsigned)len, url);
    return b;
}

/* ======================= tiny JSON reader ======================= */
static const char *jws(const char *p) { while (p && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) p++; return p; }
static const char *jskip(const char *p) {
    p = jws(p);
    if (!p || !*p) return p;
    if (*p == '"') {
        p++;
        while (*p && *p != '"') { if (*p == '\\' && p[1]) p++; p++; }
        return *p ? p + 1 : p;
    }
    if (*p == '{' || *p == '[') {
        int d = 0;
        while (*p) {
            if (*p == '"') { p = jskip(p); continue; }
            if (*p == '{' || *p == '[') d++;
            else if (*p == '}' || *p == ']') { if (--d == 0) return p + 1; }
            p++;
        }
        return p;
    }
    while (*p && *p != ',' && *p != '}' && *p != ']' && *p != ' ' && *p != '\n' && *p != '\r' && *p != '\t') p++;
    return p;
}
static const char *jget(const char *obj, const char *key) {
    const char *p = jws(obj);
    if (!p || *p != '{') return 0;
    p++;
    size_t kl0 = strlen(key);
    for (;;) {
        p = jws(p);
        if (*p == '}') return 0;
        if (*p != '"') return 0;
        const char *ks = p + 1;
        p = jskip(p);
        size_t kl = (size_t)(p - 1 - ks);
        p = jws(p);
        if (*p != ':') return 0;
        p = jws(p + 1);
        if (kl == kl0 && !memcmp(ks, key, kl)) return p;
        p = jws(jskip(p));
        if (*p == ',') p++; else return 0;
    }
}
static const char *jfirst(const char *arr) {
    const char *p = jws(arr);
    if (!p || *p != '[') return 0;
    p = jws(p + 1);
    return *p == ']' || !*p ? 0 : p;
}
static const char *jnext(const char *el) {
    const char *p = jws(jskip(el));
    if (*p != ',') return 0;
    return jws(p + 1);
}
static void put_utf8(char *d, size_t cap, size_t *k, unsigned cp) {
    if (cp < 0x80) { if (*k + 1 < cap) d[(*k)++] = (char)cp; }
    else if (cp < 0x800) { if (*k + 2 < cap) { d[(*k)++] = (char)(0xC0 | (cp >> 6)); d[(*k)++] = (char)(0x80 | (cp & 63)); } }
    else if (cp < 0x10000) { if (*k + 3 < cap) { d[(*k)++] = (char)(0xE0 | (cp >> 12)); d[(*k)++] = (char)(0x80 | ((cp >> 6) & 63)); d[(*k)++] = (char)(0x80 | (cp & 63)); } }
    else if (*k + 4 < cap) { d[(*k)++] = (char)(0xF0 | (cp >> 18)); d[(*k)++] = (char)(0x80 | ((cp >> 12) & 63)); d[(*k)++] = (char)(0x80 | ((cp >> 6) & 63)); d[(*k)++] = (char)(0x80 | (cp & 63)); }
}
static unsigned hex4(const char *p) {
    unsigned v = 0;
    for (int i = 0; i < 4; i++) {
        char c = p[i]; v <<= 4;
        if (c >= '0' && c <= '9') v |= (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f') v |= (unsigned)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v |= (unsigned)(c - 'A' + 10);
        else return 0xFFFFFFFF;
    }
    return v;
}
/* value -> UTF-8 string (string, number, or the first string of an array) */
static int jstr(const char *v, char *d, size_t cap) {
    d[0] = 0;
    v = jws(v);
    if (!v) return -1;
    if (*v == '[') { v = jfirst(v); if (!v) return -1; }
    size_t k = 0;
    if (*v != '"') {
        const char *e = jskip(v);
        while (v < e && k + 1 < cap) d[k++] = *v++;
        d[k] = 0;
        return 0;
    }
    v++;
    while (*v && *v != '"') {
        if (*v == '\\' && v[1]) {
            v++;
            char c = *v++;
            if (c == 'n' || c == 't' || c == 'r') put_utf8(d, cap, &k, ' ');
            else if (c == 'b' || c == 'f') {}
            else if (c == 'u') {
                unsigned cp = hex4(v);
                if (cp == 0xFFFFFFFF) continue;
                v += 4;
                if (cp >= 0xD800 && cp < 0xDC00 && v[0] == '\\' && v[1] == 'u') {
                    unsigned lo = hex4(v + 2);
                    if (lo >= 0xDC00 && lo < 0xE000) { cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); v += 6; }
                }
                put_utf8(d, cap, &k, cp);
            } else put_utf8(d, cap, &k, (unsigned char)c);
        } else if (k + 1 < cap) d[k++] = *v++;
        else v++;
    }
    d[k] = 0;
    return 0;
}

/* ======================= data ======================= */
enum { CH_COLLECTION, CH_ITEM, CH_SEARCH, CH_CSEARCH };
typedef struct { int type; char arg[256]; char title[100]; } Chan;
static Chan chans[64]; static int nchan = 0;

typedef struct { char id[128]; char title[160]; char title8[200]; int coll; } Item;
static Item *items; static int nitems = 0, items_found = 0, items_page = 0;

typedef struct { char name[400]; char fmt[40]; char disp[200]; long long size; int w, h, rank, have; double len; char thumb[400]; } VFile;
static VFile *files; static int nfiles = 0;
static char item_title[200], item_title8[300];

typedef struct { char name[300]; char disp[200]; long long size; } DFile;
static DFile *dfiles; static int ndf = 0;

static void load_channels(void) {
    char path[700]; snprintf(path, sizeof path, "%s/channels.txt", appdir);
    FILE *f = fopen(path, "r");
    nchan = 0;
    if (!f) { logf_("no channels.txt"); return; }
    char line[600];
    while (nchan < 64 && fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        char *s = line; while (*s == ' ' || *s == '\t') s++;
        if (!*s || *s == '#') continue;
        char *bar = strchr(s, '|'), *title = 0;
        if (bar) { *bar = 0; title = bar + 1; while (*title == ' ') title++; }
        char *sp = s; while (*sp && *sp != ' ' && *sp != '\t') sp++;
        if (!*sp) continue;
        *sp++ = 0; while (*sp == ' ' || *sp == '\t') sp++;
        size_t L = strlen(sp); while (L && (sp[L - 1] == ' ' || sp[L - 1] == '\t')) sp[--L] = 0;
        Chan *c = &chans[nchan];
        if (!strcasecmp(s, "collection")) c->type = CH_COLLECTION;
        else if (!strcasecmp(s, "item")) c->type = CH_ITEM;
        else if (!strcasecmp(s, "search")) c->type = CH_SEARCH;
        else if (!strcasecmp(s, "csearch")) c->type = CH_CSEARCH;
        else continue;
        snprintf(c->arg, sizeof c->arg, "%s", sp);
        l1(c->title, sizeof c->title, title && *title ? title : sp);
        nchan++;
    }
    fclose(f);
    logf_("%d channels", nchan);
}

static void safe_name(char *d, size_t cap, const char *id, const char *name) {
    const char *base = strrchr(name, '/'); base = base ? base + 1 : name;
    snprintf(d, cap, "%s__%s", id, base);
    for (char *p = d; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c < 32 || c >= 127 || strchr("\\/:*?\"<>|", c)) *p = '_';
    }
    if (strlen(d) > 200) { char *dot = strrchr(d, '.'); size_t el = dot ? strlen(dot) : 0; if (el < 10) memmove(d + 200 - el, dot ? dot : "", el + 1); else d[200] = 0; }
}
static long long file_size(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    long long s = -1;
    if (fseek(f, 0, SEEK_END) == 0) s = ftell(f);
    fclose(f);
    return s;
}

static int is_video_name(const char *n) {
    const char *dot = strrchr(n, '.');
    if (!dot) return 0;
    static const char *ex[] = { ".mp4", ".m4v", ".ogv", ".mkv", ".avi", ".webm", ".mpg", ".mpeg", ".mov", ".flv", ".mp3", ".ogg", 0 };
    for (int i = 0; ex[i]; i++) if (!strcasecmp(dot, ex[i])) return 1;
    return 0;
}

/* ======================= resume positions ======================= */
typedef struct { char name[300]; int pos; } Res;
static Res resume_tab[200]; static int nres = 0;
static void resume_load(void) {
    char p[700]; snprintf(p, sizeof p, "%s/.resume", viddir);
    FILE *f = fopen(p, "r"); nres = 0;
    if (!f) return;
    char line[400];
    while (nres < 200 && fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        char *t = strchr(line, '\t'); if (!t) continue;
        *t = 0;
        resume_tab[nres].pos = atoi(line);
        snprintf(resume_tab[nres].name, sizeof resume_tab[nres].name, "%s", t + 1);
        nres++;
    }
    fclose(f);
}
static void resume_save(void) {
    char p[700]; snprintf(p, sizeof p, "%s/.resume", viddir);
    FILE *f = fopen(p, "w");
    if (!f) return;
    for (int i = 0; i < nres; i++) fprintf(f, "%d\t%s\n", resume_tab[i].pos, resume_tab[i].name);
    fclose(f);
}
static int resume_get(const char *name) { for (int i = 0; i < nres; i++) if (!strcmp(resume_tab[i].name, name)) return resume_tab[i].pos; return 0; }
static void resume_set(const char *name, int pos) {
    int i;
    for (i = 0; i < nres; i++) if (!strcmp(resume_tab[i].name, name)) break;
    if (pos <= 0) { if (i < nres) { resume_tab[i] = resume_tab[--nres]; resume_save(); } return; }
    if (i == nres) { if (nres == 200) { memmove(resume_tab, resume_tab + 1, sizeof(Res) * 199); nres--; i = nres; } nres++; snprintf(resume_tab[i].name, sizeof resume_tab[i].name, "%s", name); }
    resume_tab[i].pos = pos;
    resume_save();
}

/* ======================= download ======================= */
/* Two download slots: DLW for "watch now" (streams while it downloads), DLQ for the background queue. */
typedef struct Dl {
    volatile int state;   /* 0 idle, 1 running, 2 done (thread ending), 3 failed (thread ending), 4 done + joined */
    volatile int cancel;
    volatile long long got, total;
    char url[3000], dst[900], err[96], disp[200];
    pthread_t th;
    unsigned char buf[65536];
} Dl;
static Dl DLW, DLQ;
static void queue_tick(void);

/* ---- downloads through Onion's curl: they survive Wi-Fi drops and resume from the partial file ---- */
static long long curl_size(Dl *d) {
    static char cmd[4400]; char qu[2600], tls[900], out[64], qo[80];
    snprintf(out, sizeof out, "/tmp/atv_head_%s.txt", d == &DLW ? "w" : "q");
    shq(qu, sizeof qu, d->url); curl_tls(tls, sizeof tls); shq(qo, sizeof qo, out);
    snprintf(cmd, sizeof cmd, "%s exec %s -sIL --max-time 25 %s %s > %s 2>/dev/null", CURL_ENV, CURL_BIN, tls, qu, qo);
    unlink(out);
    pid_t pid = fork();
    if (pid == 0) { execl("/bin/sh", "sh", "-c", cmd, (char *)0); _exit(127); }
    if (pid < 0) return -1;
    int st = 0;
    for (;;) {
        if (waitpid(pid, &st, WNOHANG) == pid) break;
        if (d->cancel) { kill(pid, SIGTERM); waitpid(pid, &st, 0); unlink(out); return -1; }
        usleep(100000);
    }
    long long n = -1; char line[512];
    FILE *p = fopen(out, "r");
    if (p) { while (fgets(line, sizeof line, p)) if (!strncasecmp(line, "content-length:", 15)) n = strtoll(line + 15, 0, 10); fclose(p); }
    unlink(out);
    return n;
}
static void part_info_write(Dl *d) {
    char info[960]; snprintf(info, sizeof info, "%s.part.txt", d->dst);
    FILE *f = fopen(info, "w"); if (!f) return;
    fprintf(f, "%s\n%s\n%lld\n", d->url, d->disp, d->total);
    fclose(f);
}
static void *dl_thread_curl(void *a) {
    Dl *d = (Dl *)a;
    char part[920], info[960]; snprintf(part, sizeof part, "%s.part", d->dst); snprintf(info, sizeof info, "%s.part.txt", d->dst);
    long long have = file_size(part); if (have < 0) have = 0;
    d->got = have;
    d->total = curl_size(d);
    if (d->cancel) { snprintf(d->err, sizeof d->err, "Stopped"); logf_("download stopped before it began"); d->state = 3; return 0; }
    part_info_write(d);
    logf_("curl: %s, %lld of %lld bytes already here", d->url, have, d->total);
    int code = 0;
    if (!(d->total > 0 && have == d->total)) {
        if (d->total > 0 && have > d->total) { unlink(part); d->got = 0; }
        static char cmd[4400]; char qu[2600], qp[1000], tls[900];
        shq(qu, sizeof qu, d->url); shq(qp, sizeof qp, part); curl_tls(tls, sizeof tls);
        snprintf(cmd, sizeof cmd, "%s exec %s -L -sS --fail -C - --retry 5 --retry-delay 3 --connect-timeout 20 --speed-limit 1 --speed-time 60 %s -o %s %s",
                 CURL_ENV, CURL_BIN, tls, qp, qu);
        pid_t pid = fork();
        if (pid == 0) { execl("/bin/sh", "sh", "-c", cmd, (char *)0); _exit(127); }
        if (pid < 0) { snprintf(d->err, sizeof d->err, "Could not start curl"); d->state = 3; return 0; }
        for (;;) {
            int st = 0;
            pid_t r = waitpid(pid, &st, WNOHANG);
            long long sz = file_size(part); if (sz >= 0) d->got = sz;
            if (r == pid) { code = (st & 0x7f) ? -1 : ((st >> 8) & 255); break; }
            if (d->cancel) { kill(pid, SIGTERM); waitpid(pid, &st, 0); code = -2; break; }
            usleep(200000);
        }
        long long sz = file_size(part); d->got = sz > 0 ? sz : 0;
    }
    if (code == 127 || code == 126) { snprintf(d->err, sizeof d->err, "Onion's curl didn't run"); logf_("curl failed to start (exit %d)", code); d->state = 3; return 0; }
    if (code == -2) { snprintf(d->err, sizeof d->err, "Stopped"); logf_("download stopped at %lld bytes (kept to resume)", d->got); d->state = 3; return 0; }
    int complete = d->total > 0 ? d->got == d->total : (code == 0 && d->got > 0);
    if (!complete) {
        snprintf(d->err, sizeof d->err, code == 22 ? "Server said no" : "Connection dropped");
        logf_("download incomplete: curl exit %d, %lld of %lld bytes", code, d->got, d->total);
        d->state = 3; return 0;
    }
    if (rename(part, d->dst) != 0) { snprintf(d->err, sizeof d->err, "Could not save file"); d->state = 3; return 0; }
    unlink(info);
    sync();
    logf_("download done: %s (%lld bytes)", d->dst, d->got);
    d->state = 2;
    return 0;
}
static int dl_begin(Dl *d, const char *url, const char *dst, const char *disp) {
    memset((void *)d, 0, offsetof(Dl, buf));
    snprintf(d->url, sizeof d->url, "%s", url);
    snprintf(d->dst, sizeof d->dst, "%s", dst);
    snprintf(d->disp, sizeof d->disp, "%s", disp ? disp : "");
    d->state = 1;
    logf_("download %s -> %s", url, dst);
    if (access(CURL_BIN, F_OK) != 0) { d->state = 0; logf_("curl not found at %s", CURL_BIN); return -1; }
    if (pthread_create(&d->th, 0, dl_thread_curl, d)) { d->state = 0; return -1; }
    return 0;
}
/* joins a finished download thread; safe to call any time */
static void dl_reap(Dl *d) {
    if (d->state == 2 || d->state == 3) { pthread_join(d->th, 0); d->state = d->state == 2 ? 4 : 0; }
}
static int dl_busy(Dl *d) { return d->state == 1 || d->state == 2 || d->state == 3; }
/* Shows progress until enough of the file is on the card to start watching (min_bytes, or 4% of it),
   or until it is complete when min_bytes is 0. own = B cancels the download itself.
   Returns 1 = ready, 0 = cancelled / failed / B pressed. */
/* returns 1 ready, 0 cancelled/failed, 2 = Y pressed: "put it in the queue instead" (only for a watch-now download) */
static int dl_wait(Dl *d, long long min_bytes, const char *disp, int own) {
    double t0 = now_s(), tl = 0; long long g0 = 0; double rate = 0;
    flush_keys();
    while (d->state == 1) {
        long long need = min_bytes;
        if (need > 0 && d->total > 0) { long long n2 = d->total / 25; if (n2 > need) need = n2; if (need > d->total) need = d->total; }
        if (need > 0 && d->got >= need) { ui_play_at = 0; return 1; }
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type != SDL_KEYDOWN) continue;
            int k = e.key.keysym.sym;
            if (k == K_B || k == K_MENU) { if (own) d->cancel = 1; else { ui_play_at = 0; return 0; } }
            if (k == K_Y && own && d == &DLW && min_bytes > 0) { ui_play_at = 0; return 2; }
        }
        double t = now_s();
        if (t - tl > 0.25) {
            if (tl > 0) { double inst = (d->got - g0) / (t - tl); rate = rate > 0 ? rate * 0.8 + inst * 0.2 : inst; }
            g0 = d->got; tl = t;
            if (d != &DLQ) queue_tick();
        }
        char eta[24] = "";
        if (rate > 0 && d->total > 0) fmt_time(eta, sizeof eta, (d->total - d->got) / rate);
        { long long nd = min_bytes; if (nd > 0 && d->total > 0) { long long n2 = d->total / 25; if (n2 > nd) nd = n2; } ui_play_at = nd > 0 && d->total > 0 ? (double)nd / d->total : 0; }
        ui_download(disp, d->got, d->total, t - t0 > 1 ? rate : 0, eta, d->cancel, t,
                    own && d == &DLW && min_bytes > 0 ? "B:Cancel  Y:Queue it instead" : "B:Cancel");
        present(back);
        usleep(30000);
    }
    ui_play_at = 0;
    int ok = d->state == 2 || d->state == 4;
    if (own) { const char *err = d->err; int cancelled = d->cancel; dl_reap(d); if (!ok && !cancelled) error_box("Download failed", err); }
    return ok;
}

/* ---- streaming: FFmpeg reads the file while it is still being downloaded ---- */
static struct { FILE *f; volatile int abort; int in_player; double last_draw; char title[120]; Dl *dl; char path[1000]; long long total; int far, probing; } SR;
/* is the download we stream from still writing our file? (the queue slot moves on to other files) */
static int sr_live(void) {
    if (!SR.dl || strcmp(SR.dl->dst, SR.path)) return 0;
    if (SR.dl->total > 0) SR.total = SR.dl->total;
    return SR.dl->state == 1;
}
/* ======================= player ======================= */
#define RING_SZ (1 << 20)
#define VQMAX 96
static struct { uint8_t *ring; int rd, wr, fill, rate, open; double end_pts, lat; int valid; } A;

static void audio_cb(void *u, Uint8 *out, int len) {
    (void)u;
    int n = A.fill < len ? A.fill : len, done = 0;
    while (done < n) {
        int chunk = RING_SZ - A.rd; if (chunk > n - done) chunk = n - done;
        memcpy(out + done, A.ring + A.rd, (size_t)chunk);
        A.rd = (A.rd + chunk) % RING_SZ; done += chunk;
    }
    if (n < len) memset(out + n, 0, (size_t)(len - n));
    A.fill -= n;
}
static int ring_fill(void) { SDL_LockAudio(); int f = A.fill; SDL_UnlockAudio(); return f; }

typedef struct {
    void *fc, *vcc, *acc, *swr;
    int vi, ai;
    double vtb, atb, dur;
    AVFrame *vf, *af;
    int swr_rate, swr_fmt; int64_t swr_lay;
    uint8_t *obuf; int ocap;
    AVPacket vq[VQMAX]; int vqh, vqn;
    int demux_eof, vflushed, pending, shown, use_wall, paused, skipmode, black, drops;
    double ppts, pos, wall0, pause_t, last_show, osd_until, drop_t, last_drop;
    int fmt_logged;
    int seek_pending; double seek_target, seek_at, last_target, last_seek_t;
    int dur_guess; double dur_est, avail_t, stream_t;
} Player;
static Player PL;

/* ---- YUV -> RGB ---- */
static int ytab[2][256], rvt[2][256], gut[2][256], gvt[2][256], but_[2][256];
static uint8_t clp[1280];
static int conv_key_w = -1, conv_key_h, conv_key_cx, conv_key_cy, conv_key_sar;
static int vdx, vdy, vdw, vdh, xm[WMAX], xmc[WMAX], ym[HMAX];
static void yuv_tables(void) {
    for (int i = 0; i < 1280; i++) { int v = i - 512; clp[i] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v); }
    for (int full = 0; full < 2; full++)
        for (int i = 0; i < 256; i++) {
            double ys = full ? 1.0 : 255.0 / 219.0, cs = full ? 1.0 : 255.0 / 224.0;
            double y = full ? i : (i - 16) * ys, c = (i - 128) * cs;
            ytab[full][i] = (int)(y * 256);
            rvt[full][i] = (int)(1.402 * c * 256);
            gut[full][i] = (int)(-0.344136 * c * 256);
            gvt[full][i] = (int)(-0.714136 * c * 256);
            but_[full][i] = (int)(1.772 * c * 256);
        }
}
static void show_frame(AVFrame *f) {
    int fmt = f->format, cx, cy, full = 0, gray = 0;
    switch (fmt) {
    case 0: cx = 1; cy = 1; break;            /* YUV420P */
    case 12: cx = 1; cy = 1; full = 1; break; /* YUVJ420P */
    case 4: cx = 1; cy = 0; break;            /* YUV422P */
    case 13: cx = 1; cy = 0; full = 1; break;
    case 5: cx = 0; cy = 0; break;            /* YUV444P */
    case 14: cx = 0; cy = 0; full = 1; break;
    case 8: cx = cy = 0; gray = 1; full = 1; break;
    default:
        if (!PL.fmt_logged) { logf_("unsupported pixel format %d", fmt); PL.fmt_logged = 1; }
        return;
    }
    int w = f->width, h = f->height;
    if (w <= 0 || h <= 0) return;
    AVRational sar = av_guess_sample_aspect_ratio(PL.fc, FC_STREAMS(PL.fc)[PL.vi], f);
    int sark = sar.num > 0 && sar.den > 0 ? sar.num * 1000 / sar.den : 1000;
    if (w != conv_key_w || h != conv_key_h || cx != conv_key_cx || cy != conv_key_cy || sark != conv_key_sar) {
        conv_key_w = w; conv_key_h = h; conv_key_cx = cx; conv_key_cy = cy; conv_key_sar = sark;
        double ar = (double)w / h * (sark / 1000.0);
        if (!(ar > 0.2 && ar < 5)) ar = (double)w / h;
        vdw = W; vdh = (int)(W / ar + 0.5);
        if (vdh > H) { vdh = H; vdw = (int)(H * ar + 0.5); }
        vdw &= ~1; vdh &= ~1;
        vdx = (W - vdw) / 2; vdy = (H - vdh) / 2;
        for (int i = 0; i < vdw; i++) { xm[i] = i * w / vdw; xmc[i] = xm[i] >> cx; }
        for (int j = 0; j < vdh; j++) ym[j] = j * h / vdh;
        rect(vbuf, 0, 0, W, H, 0);
        logf_("video %dx%d fmt %d sar %d/%d -> %dx%d at %d,%d", w, h, fmt, sar.num, sar.den, vdw, vdh, vdx, vdy);
    }
    const int *yt = ytab[full], *rv = rvt[full], *gu = gut[full], *gv = gvt[full], *bu = but_[full];
    const uint8_t *cl = clp + 512;
    for (int j = 0; j < vdh; j++) {
        int sy = ym[j];
        const uint8_t *Y = f->data[0] + sy * f->linesize[0];
        uint32_t *o = vbuf + (vdy + j) * W + vdx;
        if (gray) {
            for (int i = 0; i < vdw; i++) { int g = cl[yt[Y[xm[i]]] >> 8]; o[i] = RGB(g, g, g); }
            continue;
        }
        const uint8_t *U = f->data[1] + (sy >> cy) * f->linesize[1], *V = f->data[2] + (sy >> cy) * f->linesize[2];
        for (int i = 0; i < vdw; i++) {
            int yy = yt[Y[xm[i]]], c = xmc[i], u = U[c], v = V[c];
            o[i] = ((uint32_t)cl[(yy + rv[v]) >> 8] << 16) | ((uint32_t)cl[(yy + gu[u] + gv[v]) >> 8] << 8) | cl[(yy + bu[u]) >> 8];
        }
    }
}

static double master_clock(void) {
    double t = now_s();
    if (PL.ai >= 0 && !PL.use_wall) {
        SDL_LockAudio();
        double c = A.valid ? A.end_pts - A.fill / (A.rate * 4.0) - A.lat : -1e9;
        int valid = A.valid;
        SDL_UnlockAudio();
        if (valid) return c;
    }
    return t - PL.wall0;
}

static void decode_audio(AVPacket *pkt) {
    AVPacket tmp = *pkt;
    while (tmp.size > 0) {
        int got = 0;
        int len = avcodec_decode_audio4(PL.acc, PL.af, &got, &tmp);
        if (len < 0) break;
        tmp.data += len; tmp.size -= len;
        if (!got) continue;
        AVFrame *fr = PL.af;
        int inrate = FR_SAMPLE_RATE(fr) ? FR_SAMPLE_RATE(fr) : CC_SAMPLE_RATE(PL.acc);
        if (inrate <= 0) continue;
        int64_t inlay = CC_CH_LAYOUT(PL.acc) ? (int64_t)CC_CH_LAYOUT(PL.acc) : av_get_default_channel_layout(CC_CHANNELS(PL.acc));
        if (!PL.swr || inrate != PL.swr_rate || fr->format != PL.swr_fmt || inlay != PL.swr_lay) {
            swr_free(&PL.swr);
            PL.swr = swr_alloc_set_opts(0, AV_CH_LAYOUT_STEREO, AV_SAMPLE_FMT_S16, A.rate, inlay, fr->format, inrate, 0, 0);
            if (!PL.swr || swr_init(PL.swr) < 0) { logf_("swr init failed"); swr_free(&PL.swr); PL.ai = -1; return; }
            PL.swr_rate = inrate; PL.swr_fmt = fr->format; PL.swr_lay = inlay;
            logf_("audio %d Hz fmt %d ch %d -> %d Hz", inrate, fr->format, CC_CHANNELS(PL.acc), A.rate);
        }
        int need = (int)((double)fr->nb_samples * A.rate / inrate) + 1024;
        if (need > PL.ocap) { free(PL.obuf); PL.obuf = (uint8_t *)malloc((size_t)need * 4); PL.ocap = PL.obuf ? need : 0; if (!PL.obuf) return; }
        int ns = swr_convert(PL.swr, &PL.obuf, PL.ocap, (const uint8_t **)fr->extended_data, fr->nb_samples);
        if (ns <= 0) continue;
        int64_t ts = av_frame_get_best_effort_timestamp(fr);
        double end;
        if (ts != AV_NOPTS_VALUE) end = ts * PL.atb + (double)fr->nb_samples / inrate;
        else end = (A.valid ? A.end_pts : PL.pos) + (double)ns / A.rate;
        int bytes = ns * 4;
        for (int tries = 0; tries < 400; tries++) {
            SDL_LockAudio();
            if (RING_SZ - A.fill >= bytes) {
                int done = 0;
                while (done < bytes) {
                    int chunk = RING_SZ - A.wr; if (chunk > bytes - done) chunk = bytes - done;
                    memcpy(A.ring + A.wr, PL.obuf + done, (size_t)chunk);
                    A.wr = (A.wr + chunk) % RING_SZ; done += chunk;
                }
                A.fill += bytes; A.end_pts = end; A.valid = 1;
                SDL_UnlockAudio();
                break;
            }
            SDL_UnlockAudio();
            if (PL.paused) break;
            usleep(5000);
        }
    }
}

static void vq_clear(void) {
    while (PL.vqn > 0) { av_free_packet(&PL.vq[PL.vqh]); PL.vqh = (PL.vqh + 1) % VQMAX; PL.vqn--; }
    PL.vqh = 0;
}
/* forward = land on the first keyframe at or after t (so a skip ahead never lands behind where you were) */
static void do_seek_dir(double t, int forward) {
    if (PL.dur > 0 && t > PL.dur - 3) t = PL.dur - 3;
    if (t < 0) t = 0;
    int64_t ts = (int64_t)(t * 1000000.0);
    int r = forward ? av_seek_frame(PL.fc, -1, ts, 0) : -1;
    if (r < 0) r = av_seek_frame(PL.fc, -1, ts, AVSEEK_FLAG_BACKWARD);
    logf_("seek to %.1f (%s) -> %d", t, forward ? "fwd" : "back", r);
    if (r < 0) return;
    if (PL.vcc) avcodec_flush_buffers(PL.vcc);
    if (PL.acc) avcodec_flush_buffers(PL.acc);
    vq_clear();
    PL.pending = 0; PL.demux_eof = 0; PL.vflushed = 0; PL.shown = 0; PL.use_wall = 0;
    SDL_LockAudio(); A.rd = A.wr = A.fill = 0; A.valid = 0; SDL_UnlockAudio();
    PL.pos = t;
    PL.wall0 = now_s() - t;
}
static void do_seek(double t) { do_seek_dir(t, 0); }

static void refresh(const char *title) {
    double t = now_s();
    int osd = PL.paused || t < PL.osd_until || PL.vi < 0;
    if (PL.black) return;
    if (!osd) { present(vbuf); return; }
    if (PL.vi < 0) ui_message("ArchiveTV", "Audio only", 0, MOOD_HAPPY, 0, t);
    else memcpy(back, vbuf, sizeof(uint32_t) * W * H);
    time_t tt = time(0); struct tm tmv; localtime_r(&tt, &tmv);
    char hh[8]; snprintf(hh, sizeof hh, "%02d:%02d", tmv.tm_hour, tmv.tm_min);
    ui_osd(back, title, PL.seek_pending ? PL.seek_target : PL.pos, PL.dur, PL.paused, hh, t);
    present(back);
}

static const char *BL = "/sys/class/pwm/pwmchip0/pwm0/duty_cycle";
static long bl_saved = -1;
static void backlight(int on) {
    if (!on) {
        FILE *f = fopen(BL, "r");
        if (!f) return;
        char b[32] = ""; fgets(b, sizeof b, f); fclose(f);
        long v = strtol(b, 0, 10);
        if (v <= 0) return;
        bl_saved = v;
        f = fopen(BL, "w"); if (f) { fputs("0\n", f); fclose(f); }
    } else if (bl_saved > 0) {
        FILE *f = fopen(BL, "w"); if (f) { fprintf(f, "%ld\n", bl_saved); fclose(f); }
        bl_saved = -1;
    }
}

static int audio_open(void) {
    SDL_AudioSpec want, got;
    memset(&want, 0, sizeof want); memset(&got, 0, sizeof got);
    want.freq = 44100; want.format = AUDIO_S16SYS; want.channels = 2; want.samples = 1024; want.callback = audio_cb;
    if (!A.ring) A.ring = (uint8_t *)malloc(RING_SZ);
    if (!A.ring) return -1;
    A.rd = A.wr = A.fill = 0; A.valid = 0;
    if (SDL_OpenAudio(&want, &got) < 0) { logf_("SDL_OpenAudio failed"); return -1; }
    A.rate = got.freq > 0 ? got.freq : 44100;
    int samples = got.samples ? got.samples : 1024;
    A.lat = (double)samples / A.rate + 0.03;
    A.open = 1;
    logf_("SDL audio %d Hz %d ch fmt 0x%x samples %d", got.freq, got.channels, got.format, got.samples);
    return 0;
}


static void sr_wait_ui(void) {
    double t = now_s();
    SDL_Event e;
    while (SDL_PollEvent(&e)) if (e.type == SDL_KEYDOWN && (e.key.keysym.sym == K_B || e.key.keysym.sym == K_MENU)) SR.abort = 1;
    if (t - SR.last_draw < 0.12) return;
    SR.last_draw = t;
    sr_live();
    long long got = SR.dl && !strcmp(SR.dl->dst, SR.path) ? SR.dl->got : SR.total;
    int pct = SR.total > 0 ? (int)(100.0 * got / SR.total) : -1;
    if (SR.in_player && PL.shown && !PL.black) {
        memcpy(back, vbuf, sizeof(uint32_t) * W * H);
        time_t tt = time(0); struct tm tmv; localtime_r(&tt, &tmv);
        char hh[8]; snprintf(hh, sizeof hh, "%02d:%02d", tmv.tm_hour, tmv.tm_min);
        ui_osd(back, SR.title, PL.pos, PL.dur, 0, hh, t);
        char m[48];
        if (pct >= 0) snprintf(m, sizeof m, "Buffering... %d%%", pct); else snprintf(m, sizeof m, "Buffering...");
        ui_badge(m, t);
        present(back);
    } else if (!PL.black) {
        ui_download(SR.title, got, SR.total, 0, "", SR.abort, t, "B:Stop");
        present(back);
    }
}
static int sr_read(void *op, uint8_t *buf, int size) {
    (void)op;
    for (;;) {
        size_t n = fread(buf, 1, (size_t)size, SR.f);
        if (n > 0) return (int)n;
        clearerr(SR.f);
        if (!sr_live()) {   /* download over: one more try for the last bytes, then real end of file */
            n = fread(buf, 1, (size_t)size, SR.f);
            if (n > 0) return (int)n;
            clearerr(SR.f);
            return AVERROR_EOF;
        }
        if (SR.abort) return AVERROR_EXIT;
        /* before the first picture, a read far past the downloaded part means this format keeps its index
           at the end (some mp4s) or wants the length (ogv): say "end of file" so it can carry on or give up */
        if (SR.probing && SR.dl && ftell(SR.f) > SR.dl->got + (8LL << 20)) { SR.far = 1; return AVERROR_EOF; }
        sr_wait_ui();
        usleep(40000);
    }
}
static int64_t sr_seek(void *op, int64_t off, int whence) {
    (void)op;
    sr_live();
    if (whence & AVSEEK_SIZE) return SR.total > 0 ? SR.total : -1;
    whence &= ~AVSEEK_FORCE;
    long cur = ftell(SR.f);
    int64_t target = whence == SEEK_SET ? off : whence == 1 ? cur + off : whence == SEEK_END && SR.total > 0 ? SR.total + off : -1;
    if (target < 0) return -1;
    if (fseek(SR.f, (long)target, SEEK_SET) != 0) return -1;
    clearerr(SR.f);
    return target;
}

/* While streaming: how much is downloaded (for the time bar and to keep skips inside it), and a length
   estimate for files whose header can't say yet (from how many bytes the first minutes took). */
static void stream_update(double t) {
    if (t - PL.stream_t < 0.5) return;
    PL.stream_t = t;
    int live = sr_live();
    long long got = SR.dl && !strcmp(SR.dl->dst, SR.path) ? SR.dl->got : SR.total;
    double frac = !live || SR.total <= 0 ? 1.0 : (double)got / SR.total;
    if (SR.f && SR.total > 0 && PL.pos > 20) {
        long bp = ftell(SR.f);
        if (bp > 0) {
            double est = PL.pos * (double)SR.total / bp;
            PL.dur_est = PL.dur_est > 0 ? PL.dur_est * 0.9 + est * 0.1 : est;
            if (PL.dur <= 0 || PL.dur_guess || (PL.dur - PL.dur_est) / PL.dur_est > 0.5 || (PL.dur_est - PL.dur) / PL.dur_est > 0.5) {
                if (!PL.dur_guess) logf_("length from the header (%.0f s) looks off; estimating %.0f s", PL.dur, PL.dur_est);
                PL.dur = PL.dur_est; PL.dur_guess = 1;
            }
        }
    }
    PL.avail_t = frac >= 1 ? -1 : frac * PL.dur;
    ui_osd_avail = frac >= 1 || PL.dur <= 0 ? -1 : frac;
}
/* Plays a local file (streaming = it is still downloading). Returns when it ends or the user stops it. */
static int play_file_inner(const char *path, const char *key, const char *title_utf8, Dl *stream) {
    int streaming = stream != 0;
    char title[120]; l1(title, sizeof title, title_utf8);
    message_m("ArchiveTV", "Warming up...", title, MOOD_WOW, 0);
    memset(&PL, 0, sizeof PL);
    ui_osd_avail = -1;
    PL.vi = PL.ai = -1;
    conv_key_w = -1;
    rect(vbuf, 0, 0, W, H, 0);
    void *fc = 0, *pb = 0;
    int ended = -1;
    memset(&SR, 0, sizeof SR);
    SR.dl = stream;
    snprintf(SR.path, sizeof SR.path, "%s", path);
    if (stream) SR.total = stream->total;
    snprintf(SR.title, sizeof SR.title, "%s", title);
    if (streaming) {
        char part[1100]; snprintf(part, sizeof part, "%s.part", path);
        SR.f = fopen(part, "rb");
        if (!SR.f) SR.f = fopen(path, "rb");
        unsigned char *iob = (unsigned char *)av_malloc(65536);
        fc = avformat_alloc_context();
        if (SR.f && iob && fc) pb = avio_alloc_context(iob, 65536, 0, 0, sr_read, 0, sr_seek);
        if (!pb) { if (SR.f) fclose(SR.f); SR.f = 0; error_box("Could not start streaming", 0); return -1; }
        FC_PB(fc) = pb;
        FC_FLAGS(fc) |= AVFMT_FLAG_CUSTOM_IO;
        logf_("streaming %s (%lld of %lld bytes so far)", path, stream->got, stream->total);
    }
    SR.probing = streaming;
    int r = avformat_open_input(&fc, streaming ? "stream" : path, 0, 0);
    if (r < 0) {
        char eb[128]; av_strerror(r, eb, sizeof eb); logf_("open %s: %s", path, eb);
        if (pb) { av_free(AVIO_BUFFER(pb)); av_free(pb); }
        if (SR.f) { fclose(SR.f); SR.f = 0; }
        if (!SR.abort && !streaming) error_box("Could not open the file", eb);
        return -1;
    }
    PL.fc = fc;
    int fsi = avformat_find_stream_info(fc, 0);
    SR.probing = 0;
    if (fsi < 0) { if (!SR.abort && !streaming) error_box("Unrecognized file", 0); goto close_out; }
    int64_t d = FC_DURATION(fc);
    PL.dur = d > 0 ? d / 1000000.0 : 0;
    void *vdec = 0, *adec = 0;
    int vi = av_find_best_stream(fc, AVMEDIA_TYPE_VIDEO, -1, -1, &vdec, 0);
    int ai = av_find_best_stream(fc, AVMEDIA_TYPE_AUDIO, -1, vi >= 0 ? vi : -1, &adec, 0);
    logf_("open %s: %u streams, duration %.1f s, video %d, audio %d", path, FC_NB_STREAMS(fc), PL.dur, vi, ai);
    if (vi >= 0 && vdec) {
        void *st = FC_STREAMS(fc)[vi];
        AVRational tb = ST_TIMEBASE(st);
        void *cc = ST_CODEC(st);
        av_opt_set_int(cc, "threads", 2, 0);
        av_opt_set_int(cc, "skip_loop_filter", AVDISCARD_ALL, 0);
        av_opt_set(cc, "flags2", "+fast", 0);
        if (tb.num > 0 && tb.den > 0 && avcodec_open2(cc, vdec, 0) >= 0) { PL.vi = vi; PL.vcc = cc; PL.vtb = (double)tb.num / tb.den; }
        else logf_("video stream not usable (tb %d/%d)", tb.num, tb.den);
    }
    if (ai >= 0 && adec) {
        void *st = FC_STREAMS(fc)[ai];
        AVRational tb = ST_TIMEBASE(st);
        void *cc = ST_CODEC(st);
        if (tb.num > 0 && tb.den > 0 && avcodec_open2(cc, adec, 0) >= 0) { PL.ai = ai; PL.acc = cc; PL.atb = (double)tb.num / tb.den; }
        else logf_("audio stream not usable (tb %d/%d)", tb.num, tb.den);
    }
    if (PL.vi < 0 && PL.ai < 0) { if (!streaming) error_box("No playable video or audio", "(see log.txt)"); if (PL.vcc) avcodec_close(PL.vcc); goto close_out; }
    if (PL.ai >= 0 && audio_open() < 0) { if (PL.acc) avcodec_close(PL.acc); PL.acc = 0; PL.ai = -1; }
    PL.vf = av_frame_alloc(); PL.af = av_frame_alloc();
    SR.in_player = 1;

    int start = resume_get(key);
    if (streaming && start > 0 && PL.dur > 0) {   /* the spot we left off at isn't downloaded yet: start from the top */
        sr_live();
        long long got = SR.dl ? SR.dl->got : 0;
        if (SR.total <= 0 || (double)start / PL.dur > (double)got / SR.total - 0.03) { logf_("resume point %d s not downloaded yet", start); start = 0; }
    }
    PL.wall0 = now_s();
    if (start > 10 && (PL.dur <= 0 || start < PL.dur - 30)) do_seek(start);
    PL.osd_until = now_s() + 4;
    if (A.open) SDL_PauseAudio(0);
    SDL_EnableKeyRepeat(0, 0);
    flush_keys();
    int stop = 0;
    ended = 0;
    double last_osd_draw = 0;
    while (!stop) {
        double t = now_s();
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) { stop = 1; break; }
            if (e.type != SDL_KEYDOWN) continue;
            int k = e.key.keysym.sym;
            if (PL.black && k != K_X) continue;
            double step = 0;
            switch (k) {
            case K_B: case K_MENU: stop = 1; break;
            case K_A: case K_START:
                PL.paused = !PL.paused;
                if (PL.paused) PL.pause_t = t; else PL.wall0 += t - PL.pause_t;
                if (A.open) SDL_PauseAudio(PL.paused);
                PL.osd_until = t + 3;
                break;
            case K_Y: PL.osd_until = PL.osd_until > t ? 0 : t + 5; break;
            case K_X: PL.black = !PL.black; backlight(!PL.black); if (PL.black) { rect(back, 0, 0, W, H, 0); present(back); } break;
            case K_SELECT: do_seek(0); PL.osd_until = t + 3; break;
            case K_LEFT: step = -10; break;
            case K_RIGHT: step = 10; break;
            case K_L1: case K_DOWN: step = -60; break;
            case K_R1: case K_UP: step = 60; break;
            case K_L2: step = -300; break;
            case K_R2: step = 300; break;
            default: break;
            }
            if (step != 0) {
                /* gather quick presses into one jump, counting from where you were aiming */
                double now = now_s();
                double base = PL.seek_pending ? PL.seek_target : (now - PL.last_seek_t < 4 ? (step > 0 ? (PL.last_target > PL.pos ? PL.last_target : PL.pos) : PL.last_target) : PL.pos);
                double tgt = base + step;
                if (tgt < 0) tgt = 0;
                if (PL.dur > 0 && tgt > PL.dur - 3) tgt = PL.dur - 3;
                if (streaming && step > 0 && PL.avail_t > 0 && tgt > PL.avail_t - 4) tgt = PL.avail_t - 4 > PL.pos ? PL.avail_t - 4 : PL.pos;   /* stay inside what's downloaded */
                PL.seek_target = tgt; PL.seek_pending = 1; PL.seek_at = now + 0.45;
                PL.osd_until = now + 3;
            }
            if (!PL.black) refresh(title);
        }
        if (SR.abort) stop = 1;
        if (stop) break;
        if (streaming) stream_update(t);
        if (PL.seek_pending && t >= PL.seek_at) {
            double tgt = PL.seek_target;
            PL.seek_pending = 0;
            do_seek_dir(tgt, tgt > PL.pos);
            PL.last_target = tgt; PL.last_seek_t = now_s();
            PL.osd_until = now_s() + 3;
            if (!PL.black) refresh(title);
        }
        { static double lastq = 0; if (t - lastq > 0.5) { lastq = t; queue_tick(); } }
        if (PL.paused && PL.shown) { usleep(30000); continue; }

        int work = 0;
        /* 1. demux: keep ~2 s of audio and a queue of video packets */
        for (int it = 0; it < 12 && !PL.demux_eof; it++) {
            if (PL.vi >= 0 && PL.vqn >= VQMAX) break;
            if (PL.ai >= 0) { int rf = ring_fill(); if (rf > RING_SZ - 160000) break; if (rf > RING_SZ / 3 && (PL.vi < 0 || PL.vqn > 4)) break; }
            AVPacket pkt; av_init_packet(&pkt); pkt.data = 0; pkt.size = 0;
            if (av_read_frame(PL.fc, &pkt) < 0) { PL.demux_eof = 1; break; }
            work = 1;
            if (pkt.stream_index == PL.vi) {
                if (av_dup_packet(&pkt) < 0) { av_free_packet(&pkt); continue; }
                PL.vq[(PL.vqh + PL.vqn) % VQMAX] = pkt; PL.vqn++;
            } else {
                if (pkt.stream_index == PL.ai) decode_audio(&pkt);
                av_free_packet(&pkt);
            }
        }
        /* 2. video */
        double wait = 0.01;
        if (PL.vi >= 0) {
            if (!PL.pending && (PL.vqn > 0 || (PL.demux_eof && !PL.vflushed))) {
                int got = 0;
                AVPacket p;
                if (PL.vqn > 0) {
                    p = PL.vq[PL.vqh]; PL.vqh = (PL.vqh + 1) % VQMAX; PL.vqn--;
                    if (avcodec_decode_video2(PL.vcc, PL.vf, &got, &p) < 0) got = 0;
                    av_free_packet(&p);
                } else {
                    av_init_packet(&p); p.data = 0; p.size = 0;
                    if (avcodec_decode_video2(PL.vcc, PL.vf, &got, &p) < 0 || !got) PL.vflushed = 1;
                }
                work = 1;
                if (got) {
                    int64_t ts = av_frame_get_best_effort_timestamp(PL.vf);
                    PL.ppts = ts != AV_NOPTS_VALUE ? ts * PL.vtb : PL.ppts + 1.0 / 30;
                    PL.pending = 1;
                }
            }
            if (PL.pending) {
                double clk = master_clock();
                if (!PL.shown) {
                    show_frame(PL.vf); PL.shown = 1; PL.pending = 0; PL.pos = PL.ppts;
                    PL.last_show = t;
                    if (PL.ai < 0 || PL.use_wall) PL.wall0 = t - PL.ppts;
                    refresh(title); last_osd_draw = t;
                } else if (PL.ppts <= clk + 0.008) {
                    PL.pending = 0;
                    if (PL.ppts < clk - 0.12 && t - PL.last_show < 0.4) {
                        PL.drops++; PL.last_drop = t;
                        if (!PL.skipmode && PL.drops > 10 && t - PL.drop_t < 3) { av_opt_set_int(PL.vcc, "skip_frame", AVDISCARD_NONREF, 0); PL.skipmode = 1; logf_("video too slow: skipping non-reference frames"); }
                        if (t - PL.drop_t >= 3) { PL.drop_t = t; PL.drops = 0; }
                    } else {
                        show_frame(PL.vf); PL.pos = PL.ppts; PL.last_show = t;
                        refresh(title); last_osd_draw = t;
                    }
                    work = 1;
                } else wait = PL.ppts - clk;
            }
            if (PL.skipmode && t - PL.last_drop > 15) { av_opt_set_int(PL.vcc, "skip_frame", AVDISCARD_DEFAULT, 0); PL.skipmode = 0; PL.drops = 0; logf_("back to full decoding"); }
        } else {
            double clk = master_clock();
            if (clk > -1e8) PL.pos = clk;
            if (t - last_osd_draw > 0.5) { refresh(title); last_osd_draw = t; }
        }
        if (PL.vi >= 0 && t - last_osd_draw > 0.5 && (t < PL.osd_until || PL.paused)) { refresh(title); last_osd_draw = t; }

        /* audio ran out before the video did */
        if (PL.ai >= 0 && !PL.use_wall && PL.demux_eof && ring_fill() == 0) { PL.use_wall = 1; PL.wall0 = t - PL.pos; }
        /* 3. end */
        if (PL.demux_eof && (PL.vi < 0 || (PL.vflushed && !PL.pending && PL.vqn == 0)) && (PL.ai < 0 || ring_fill() == 0)) { ended = 1; break; }

        if (!work) { double s = wait > 0.01 ? 0.01 : wait; usleep(s > 0.001 ? (unsigned)(s * 1e6) : 1000); }
    }
    if (PL.black) backlight(1);
    if (A.open) { SDL_PauseAudio(1); SDL_CloseAudio(); A.open = 0; }
    resume_set(key, ended || PL.pos < 30 || (PL.dur > 0 && PL.pos > PL.dur - 60) ? 0 : (int)PL.pos);
    vq_clear();
    av_frame_free(&PL.vf); av_frame_free(&PL.af);
    swr_free(&PL.swr); free(PL.obuf);
    if (PL.vcc) avcodec_close(PL.vcc);
    if (PL.acc) avcodec_close(PL.acc);
    logf_("playback finished at %.1f (%s)", PL.pos, ended ? "end" : "stopped");
close_out:
    ui_osd_avail = -1;
    avformat_close_input(&fc);
    if (pb) { av_free(AVIO_BUFFER(pb)); av_free(pb); }
    if (SR.f) { fclose(SR.f); SR.f = 0; }
    SR.in_player = 0;
    flush_keys();
    return ended;
}


/* ======================= device ======================= */
static int has_wifi = 1;
static void detect_device(void) {
    /* screen: Onion switches the framebuffer to 752x560 on a Mini v4 / Flip when the app folder has "full_resolution" */
    int fd = open("/dev/fb0", O_RDONLY);
    if (fd >= 0) {
        unsigned v[40]; memset(v, 0, sizeof v);
        if (ioctl(fd, FBIOGET_VSCREENINFO, v) == 0 && v[0] >= 320 && v[0] <= WMAX && v[1] >= 240 && v[1] <= HMAX) { W = (int)v[0] & ~1; H = (int)v[1] & ~1; }
        close(fd);
    }
    const char *ov = getenv("ATV_RES");    /* e.g. 752x560, for testing */
    if (ov && atoi(ov) >= 320) { const char *x = strchr(ov, 'x'); if (x) { W = atoi(ov) & ~1; H = atoi(x + 1) & ~1; } }
    char model[16] = "";
    FILE *f = fopen("/tmp/deviceModel", "r"); if (f) { fgets(model, sizeof model, f); fclose(f); }
    has_wifi = access("/sys/class/net/wlan0", F_OK) == 0 || !strncmp(model, "354", 3);
    logf_("device model %s, screen %dx%d, wifi %s", model[0] ? model : "?", W, H, has_wifi ? "yes" : "no");
}

static void menu_audio_start(void);
static void menu_audio_stop(void);
static int play_file(const char *path, const char *key, const char *title_utf8, Dl *stream) {
    menu_audio_stop();
    int r = play_file_inner(path, key, title_utf8, stream);
    menu_audio_start();
    return r;
}

/* ======================= settings ======================= */
static int g_tab = 1;
static void settings_save(int tab) {
    if (tab >= 0) g_tab = tab;
    char p[700]; snprintf(p, sizeof p, "%s/settings.txt", appdir);
    FILE *f = fopen(p, "w"); if (!f) return;
    fprintf(f, "theme %d\ntab %d\nscreen %d\nmusic %d\nsounds %d\nvsync %d\ngroup %d\nautooff %d\nthumbs %d\n", g_theme, g_tab, g_fx, g_music, g_sounds, g_vsync, g_group, g_autooff, g_thumbs);
    fclose(f);
}
/* reads one setting before everything else is up (the screen mode is picked before settings_load) */
static int settings_peek(const char *key, int def) {
    char p[700]; snprintf(p, sizeof p, "%s/settings.txt", appdir);
    FILE *f = fopen(p, "r"); if (!f) return def;
    char line[80]; size_t L = strlen(key); int v = def;
    while (fgets(line, sizeof line, f)) if (!strncmp(line, key, L)) v = atoi(line + L);
    fclose(f);
    return v;
}
static void settings_load(void) {
    char p[700]; snprintf(p, sizeof p, "%s/settings.txt", appdir);
    FILE *f = fopen(p, "r");
    if (!f) { if (!has_wifi) g_tab = 4; return; }   /* no Wi-Fi: start on Files */
    char line[80];
    while (fgets(line, sizeof line, f)) {
        if (!strncmp(line, "theme ", 6)) g_theme = atoi(line + 6);
        else if (!strncmp(line, "tab ", 4)) g_tab = atoi(line + 4);
        else if (!strncmp(line, "screen ", 7)) g_fx = atoi(line + 7);
        else if (!strncmp(line, "music ", 6)) g_music = atoi(line + 6) != 0;
        else if (!strncmp(line, "sounds ", 7)) g_sounds = atoi(line + 7) != 0;
        else if (!strncmp(line, "group ", 6)) g_group = atoi(line + 6) != 0;
        else if (!strncmp(line, "autooff ", 8)) g_autooff = atoi(line + 8) != 0;
        else if (!strncmp(line, "thumbs ", 7)) g_thumbs = atoi(line + 7) != 0;
    }
    fclose(f);
    if (g_theme < 0 || g_theme >= ui_theme_count()) g_theme = 1;
    if (g_tab < 0 || g_tab > 5) g_tab = 1;
}

/* ======================= download queue ======================= */
typedef struct { char url[2200]; char dst[900]; char disp[200]; int failed; } QE;
static QE *Q; static int nq = 0, q_active = -1;
static void q_save(void) {
    char p[700]; snprintf(p, sizeof p, "%s/queue.txt", appdir);
    FILE *f = fopen(p, "w"); if (!f) return;
    for (int i = 0; i < nq; i++) fprintf(f, "%s\t%s\t%s\t%d\n", Q[i].url, Q[i].dst, Q[i].disp, Q[i].failed);
    fclose(f);
}
static void q_load(void) {
    if (!Q) Q = (QE *)calloc(100, sizeof(QE));
    nq = 0;
    char p[700]; snprintf(p, sizeof p, "%s/queue.txt", appdir);
    FILE *f = fopen(p, "r"); if (!f) return;
    static char line[3600];
    while (nq < 100 && fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        char *a = line, *b = strchr(a, '\t'); if (!b) continue; *b++ = 0;
        char *c = strchr(b, '\t'); if (!c) continue; *c++ = 0;
        char *d = strchr(c, '\t'); if (d) *d++ = 0;
        snprintf(Q[nq].url, sizeof Q[nq].url, "%s", a);
        snprintf(Q[nq].dst, sizeof Q[nq].dst, "%s", b);
        snprintf(Q[nq].disp, sizeof Q[nq].disp, "%s", c);
        Q[nq].failed = d ? atoi(d) : 0;
        nq++;
    }
    fclose(f);
}
static int q_find(const char *dst) { for (int i = 0; i < nq; i++) if (!strcmp(Q[i].dst, dst)) return i; return -1; }
static void part_delete(const char *dst) {
    char p[960]; snprintf(p, sizeof p, "%s.part", dst); unlink(p);
    snprintf(p, sizeof p, "%s.part.txt", dst); unlink(p);
}
/* del = also throw away a half-finished download */
static void q_remove(int i, int del) {
    if (i < 0 || i >= nq) return;
    if (i == q_active) { DLQ.cancel = 1; while (DLQ.state == 1) usleep(20000); dl_reap(&DLQ); q_active = -1; }
    if (del) part_delete(Q[i].dst);
    else if (q_active > i) q_active--;
    memmove(&Q[i], &Q[i + 1], sizeof(QE) * (size_t)(nq - i - 1));
    nq--;
    q_save();
}
static int q_add(const char *url, const char *dst, const char *disp) {
    if (q_find(dst) >= 0) return 1;
    if (nq >= 100) return -1;
    QE *e = &Q[nq++];
    memset(e, 0, sizeof *e);
    snprintf(e->url, sizeof e->url, "%s", url);
    snprintf(e->dst, sizeof e->dst, "%s", dst);
    snprintf(e->disp, sizeof e->disp, "%s", disp);
    q_save();
    return 0;
}
/* puts a download next in line (right after the one running now) */
static int q_add_next(const char *url, const char *dst, const char *disp) {
    int qi = q_find(dst);
    if (qi >= 0) { Q[qi].failed = 0; q_save(); return 1; }
    if (nq >= 100) return -1;
    int at = (q_active >= 0 && q_active < nq && DLQ.state == 1) ? q_active + 1 : 0;
    if (at == 0 && q_active >= 0) q_active++;
    memmove(&Q[at + 1], &Q[at], sizeof(QE) * (size_t)(nq - at));
    nq++;
    QE *e = &Q[at]; memset(e, 0, sizeof *e);
    snprintf(e->url, sizeof e->url, "%s", url);
    snprintf(e->dst, sizeof e->dst, "%s", dst);
    snprintf(e->disp, sizeof e->disp, "%s", disp);
    q_save();
    return 0;
}
/* failed: 0 = waiting, 1 = failed, 2 = paused. Pausing keeps the half-finished file to resume later. */
static void q_pause(int i, int pause) {
    if (i < 0 || i >= nq) return;
    Q[i].failed = pause ? 2 : 0;
    if (pause && i == q_active) {
        DLQ.cancel = 1; while (DLQ.state == 1) usleep(20000);
        if (DLQ.state == 2 || DLQ.state == 3) pthread_join(DLQ.th, 0);
        DLQ.state = 0; q_active = -1;
    }
    q_save();
}
/* called often from every screen: finishes and starts queued downloads */
static void queue_tick(void) {
    if (DLW.state == 2 && !SR.in_player) sfx(SFX_DONE);
    dl_reap(&DLW);
    if (DLQ.state == 2 || DLQ.state == 3) {
        int ok = DLQ.state == 2;
        dl_reap(&DLQ);
        sfx(ok ? SFX_DONE : SFX_ERROR);
        if (q_active >= 0 && q_active < nq) {
            if (ok) { int i = q_active; q_active = -1; q_remove(i, 0); if (g_autooff) g_off_armed = 1; }
            else if (!Q[q_active].failed) { Q[q_active].failed = 1; q_save(); }
        }
        q_active = -1;
        DLQ.state = 0;
    }
    if (DLQ.state == 0 || DLQ.state == 4) {
        for (int i = 0; i < nq; i++) {
            if (Q[i].failed) continue;
            if (access(Q[i].dst, F_OK) == 0) { q_remove(i, 0); i--; continue; }
            if (DLW.state == 1 && !strcmp(DLW.dst, Q[i].dst)) continue;
            if (dl_begin(&DLQ, Q[i].url, Q[i].dst, Q[i].disp) == 0) q_active = i;
            break;
        }
    }
}
/* "Turn off after downloads": once a queued download has finished and nothing is left to fetch,
   count down 30 s (B keeps it on), then save and power the handheld off. */
static void autooff_check(void) {
    if (!g_autooff || !g_off_armed || SR.in_player) return;
    if (DLQ.state == 1 || DLW.state == 1) return;
    for (int i = 0; i < nq; i++) if (!Q[i].failed && access(Q[i].dst, F_OK) != 0) return;   /* still work to do */
    g_off_armed = 0;
    logf_("downloads finished: turning off in 30 s unless cancelled");
    flush_keys();
    double t0 = now_s(), last = -1;
    for (;;) {
        double t = now_s();
        int left = 30 - (int)(t - t0);
        if (left <= 0) break;
        if ((int)last != left) {
            char l1s[64]; snprintf(l1s, sizeof l1s, "Turning off in %d s", left);
            message_m("Downloads done!", l1s, "Sleep tight, little TV", MOOD_SLEEP, "A:Turn off now  B:Keep on");
            last = left;
        }
        SDL_Event e;
        while (SDL_PollEvent(&e)) if (e.type == SDL_KEYDOWN) {
            int k = e.key.keysym.sym;
            if (k == K_B || k == K_MENU) { logf_("power off cancelled"); sfx(SFX_BACK); flush_keys(); return; }
            if (k == K_A || k == K_START) { t0 = t - 31; break; }
        }
        usleep(50000);
    }
    logf_("powering off");
    settings_save(-1); q_save();
    message_m("Good night!", "Turning off...", 0, MOOD_SLEEP, 0);
    menu_audio_stop();
    sync();
    usleep(400000);
    /* Onion and spruce both run as root on the Mini; busybox poweroff does a clean shutdown */
    if (system("sync; poweroff") != 0) system("sync; reboot -p");
    g_quit = 1;
}
static int quit_ask(void) {
    int k2 = ask("ArchiveTV", "Turn off ArchiveTV?", nq ? "Downloads resume next time" : 0, MOOD_SLEEP, "A:Yes  B:No");
    return k2 == K_A;
}
static void queue_badge(char *b, size_t cap) {
    b[0] = 0;
    Dl *d = DLQ.state == 1 ? &DLQ : DLW.state == 1 ? &DLW : 0;
    if (!d) return;
    int pct = d->total > 0 ? (int)(100.0 * d->got / d->total) : 0;
    int left = 0;
    for (int i = 0; i < nq; i++) if (!Q[i].failed) left++;
    /* \x02 = little down arrow */
    if (left > 1) snprintf(b, cap, "\x02%d%%+%d", pct, left - 1);
    else snprintf(b, cap, "\x02%d%%", pct);
}

/* ======================= screens ======================= */
static void net_error(int err) {
    if (err == -5) return;   /* the user pressed B while it was loading */
    if (!has_wifi) { error_box("No Wi-Fi on this Miyoo.", "Copy videos into App/ArchiveTV/videos"); return; }
    if (err == -6) error_box("curl is missing.", "(App/ArchiveTV/bin/curl)");
    else error_box("Network error. Is Wi-Fi on?", "(details in log.txt)");
}
typedef struct { char dst[900]; char url[2200]; char disp[200]; long long have, total; } PartF;
static PartF *parts; static int npart = 0;
static void scan_parts(void) {
    if (!parts) parts = (PartF *)calloc(50, sizeof(PartF));
    npart = 0;
    DIR *d = opendir(viddir); if (!d) return;
    struct dirent *de;
    while ((de = readdir(d)) && npart < 50) {
        size_t L = strlen(de->d_name);
        if (de->d_name[0] == '.' || L < 6 || strcmp(de->d_name + L - 5, ".part")) continue;
        PartF *pf = &parts[npart];
        snprintf(pf->dst, sizeof pf->dst, "%s/%.*s", viddir, (int)(L - 5), de->d_name);
        if (q_find(pf->dst) >= 0) continue;                                  /* shown as a queue entry */
        if ((DLW.state == 1 && !strcmp(DLW.dst, pf->dst)) || (DLQ.state == 1 && !strcmp(DLQ.dst, pf->dst))) continue;
        char p[1000]; snprintf(p, sizeof p, "%s/%s", viddir, de->d_name);
        pf->have = file_size(p); pf->total = -1; pf->url[0] = 0;
        const char *u = strstr(de->d_name, "__");
        char nm[300]; snprintf(nm, sizeof nm, "%.*s", (int)(L - 5), u ? u + 2 : de->d_name);
        l1(pf->disp, sizeof pf->disp, nm);
        char info[960]; snprintf(info, sizeof info, "%s.part.txt", pf->dst);
        FILE *f = fopen(info, "r");
        if (f) {
            char line[2300];
            if (fgets(line, sizeof line, f)) { line[strcspn(line, "\r\n")] = 0; snprintf(pf->url, sizeof pf->url, "%s", line); }
            if (fgets(line, sizeof line, f)) { line[strcspn(line, "\r\n")] = 0; if (line[0]) snprintf(pf->disp, sizeof pf->disp, "%s", line); }
            if (fgets(line, sizeof line, f)) pf->total = strtoll(line, 0, 10);
            fclose(f);
        }
        npart++;
    }
    closedir(d);
}
static int cmp_df(const void *a, const void *b) { return strcasecmp(((const DFile *)a)->disp, ((const DFile *)b)->disp); }
static void scan_downloads(void) {
    scan_parts();
    if (!dfiles) dfiles = (DFile *)calloc(500, sizeof(DFile));
    ndf = 0;
    DIR *d = opendir(viddir);
    if (!d) return;
    struct dirent *de;
    while ((de = readdir(d)) && ndf < 500) {
        const char *n = de->d_name;
        if (n[0] == '.') continue;
        size_t L = strlen(n);
        if (L > 5 && !strcmp(n + L - 5, ".part")) continue;
        if (!is_video_name(n)) continue;
        DFile *f = &dfiles[ndf];
        snprintf(f->name, sizeof f->name, "%s", n);
        const char *u = strstr(n, "__");
        l1(f->disp, sizeof f->disp, u ? u + 2 : n);
        char p[1000]; snprintf(p, sizeof p, "%s/%s", viddir, n);
        f->size = file_size(p);
        ndf++;
    }
    closedir(d);
    qsort(dfiles, (size_t)ndf, sizeof(DFile), cmp_df);
}

/* ---------- home list editing (channels.txt) ---------- */
static void clean_line(char *d, size_t cap, const char *s) {
    size_t k = 0;
    for (; *s && k + 1 < cap; s++) d[k++] = (*s == '|' || *s == '\n' || *s == '\r' || *s == '\t') ? ' ' : *s;
    d[k] = 0;
}
static int is_chan_line(const char *s);
static int chan_add(const char *type, const char *arg, const char *title8) {
    for (int i = 0; i < nchan; i++) if (!strcmp(chans[i].arg, arg)) return 1;
    char path[700], tmp[720]; snprintf(path, sizeof path, "%s/channels.txt", appdir); snprintf(tmp, sizeof tmp, "%s.new", path);
    char a[300], t[300]; clean_line(a, sizeof a, arg); clean_line(t, sizeof t, title8);
    FILE *in = fopen(path, "r"), *out = fopen(tmp, "w");
    if (!out) { if (in) fclose(in); return -1; }
    int put = 0; static char line[1200];
    while (in && fgets(line, sizeof line, in)) {   /* the new one goes above the first saved line: newest on top */
        const char *q = line; while (*q == ' ' || *q == '\t') q++;
        if (!put && is_chan_line(q)) { fprintf(out, "%s %s | %s\n", type, a, t); put = 1; }
        fputs(line, out);
        size_t L = strlen(line); if (L && line[L - 1] != '\n') fputs("\n", out);
    }
    if (!put) fprintf(out, "%s %s | %s\n", type, a, t);
    if (in) fclose(in);
    fclose(out);
    if (rename(tmp, path) != 0) { unlink(tmp); return -1; }
    sync();
    load_channels();
    return 0;
}
static const char *save_msg(int r) { return r == 1 ? "Already saved" : r == 0 ? "Saved! Find it in the Saved tab" : "Could not save"; }
static int is_chan_line(const char *s) {
    if (s[0] == '#') return 0;
    const char *ty[] = { "collection", "item", "search", "csearch", 0 };
    for (int i = 0; ty[i]; i++) {
        size_t L = strlen(ty[i]);
        if (!strncasecmp(s, ty[i], L) && (s[L] == ' ' || s[L] == '\t')) {
            const char *p = s + L; while (*p == ' ' || *p == '\t') p++;
            return *p && *p != '\n' && *p != '\r' && *p != '|';
        }
    }
    return 0;
}
static void chan_remove(int idx) {
    char path[700], tmp[720]; snprintf(path, sizeof path, "%s/channels.txt", appdir); snprintf(tmp, sizeof tmp, "%s.new", path);
    FILE *f = fopen(path, "r"), *o = fopen(tmp, "w");
    if (!f || !o) { if (f) fclose(f); if (o) fclose(o); return; }
    char line[700]; int n = 0;
    while (fgets(line, sizeof line, f)) {
        char *s = line; while (*s == ' ' || *s == '\t') s++;
        if (is_chan_line(s)) { if (n++ == idx) continue; }
        fputs(line, o);
    }
    fclose(f); fclose(o);
    rename(tmp, path); sync();
    load_channels();
}

/* ---------- recent searches ("v words" = videos, "c words" = collections) ---------- */
static char hist[12][120]; static int nhist = 0;
static void hist_load(void) {
    char p[700]; snprintf(p, sizeof p, "%s/searches.txt", appdir);
    FILE *f = fopen(p, "r"); nhist = 0;
    if (!f) return;
    while (nhist < 12 && fgets(hist[nhist], sizeof hist[0], f)) {
        hist[nhist][strcspn(hist[nhist], "\r\n")] = 0;
        if (!hist[nhist][0]) continue;
        if (!(hist[nhist][0] == 'v' || hist[nhist][0] == 'c') || hist[nhist][1] != ' ') { char t[120]; snprintf(t, sizeof t, "v %s", hist[nhist]); snprintf(hist[nhist], sizeof hist[0], "%s", t); }
        nhist++;
    }
    fclose(f);
}
static void hist_save(void) {
    char p[700]; snprintf(p, sizeof p, "%s/searches.txt", appdir);
    FILE *f = fopen(p, "w"); if (!f) return;
    for (int i = 0; i < nhist; i++) fprintf(f, "%s\n", hist[i]);
    fclose(f);
}
static void hist_add(const char *q) {
    int i;
    for (i = 0; i < nhist; i++) if (!strcmp(hist[i], q)) break;
    if (i == nhist) { if (nhist < 12) nhist++; i = nhist - 1; }
    for (; i > 0; i--) memcpy(hist[i], hist[i - 1], sizeof hist[0]);
    snprintf(hist[0], sizeof hist[0], "%s", q);
    hist_save();
}
static void hist_del(int i) { if (i < 0 || i >= nhist) return; for (; i < nhist - 1; i++) memcpy(hist[i], hist[i + 1], sizeof hist[0]); nhist--; hist_save(); }

/* returns 1 with text filled, 0 if cancelled */
static int keyboard_act(const char *title, char *text, size_t cap, const char *action) {
    int kx = 0, ky = 0, caps = 0;
    static const int SPX[4] = { 0, 2, 6, 8 };   /* a column inside each bottom-row key */
    SDL_EnableKeyRepeat(300, 80);
    flush_keys();
    for (;;) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) return 0;
            if (e.type != SDL_KEYDOWN) continue;
            size_t n = strlen(text);
            switch (e.key.keysym.sym) {
            case K_UP: ky = (ky + 4) % 5; sfx(SFX_MOVE); break;
            case K_DOWN: ky = (ky + 1) % 5; sfx(SFX_MOVE); break;
            case K_LEFT:
                sfx(SFX_MOVE);
                if (ky == 4) kx = SPX[(ui_kb_special(kx) + 3) % 4];
                else kx = (kx + 9) % 10;
                break;
            case K_RIGHT:
                sfx(SFX_MOVE);
                if (ky == 4) kx = SPX[(ui_kb_special(kx) + 1) % 4];
                else kx = (kx + 1) % 10;
                break;
            case K_A:
                if (ky < 4) {
                    if (n + 1 < cap && n < 60) {
                        char ch = (char)ui_kb_char(kx, ky);
                        if (caps && ch >= 'a' && ch <= 'z') ch = (char)(ch - 32);
                        text[n] = ch; text[n + 1] = 0; sfx(SFX_TYPE);
                    }
                } else {
                    int sp = ui_kb_special(kx);
                    if (sp == 0) { caps = !caps; sfx(SFX_TOGGLE); }
                    else if (sp == 1) { if (n && n + 1 < cap && text[n - 1] != ' ') { text[n] = ' '; text[n + 1] = 0; sfx(SFX_TYPE); } }
                    else if (sp == 2) { if (n) { text[n - 1] = 0; sfx(SFX_BACK); } }
                    else if (n) { sfx(SFX_SELECT); SDL_EnableKeyRepeat(0, 0); return 1; }
                }
                break;
            case K_SELECT: caps = !caps; sfx(SFX_TOGGLE); break;
            case K_Y: if (n + 1 < cap && n && text[n - 1] != ' ') { text[n] = ' '; text[n + 1] = 0; sfx(SFX_TYPE); } break;
            case K_B: sfx(SFX_BACK); if (n) text[n - 1] = 0; else { SDL_EnableKeyRepeat(0, 0); return 0; } break;
            case K_START: if (n) { sfx(SFX_SELECT); SDL_EnableKeyRepeat(0, 0); return 1; } break;
            case K_MENU: sfx(SFX_BACK); SDL_EnableKeyRepeat(0, 0); return 0;
            default: break;
            }
        }
        ui_keyboard(title, text, kx, ky, now_s(), action, caps);
        present(back);
        usleep(30000);
    }
}
static int keyboard(const char *title, char *text, size_t cap) { return keyboard_act(title, text, cap, "SEARCH"); }

/* ---------- playlists: playlists/<name>.txt, one "id<TAB>file<TAB>title" per line ---------- */
typedef struct { char id[128]; char name[400]; char disp[200]; } PE;
static char pldir[600];
static char pls[60][64]; static int npl = 0;
static PE *ple; static int nple = 0;
static int cmp_str(const void *a, const void *b) { return strcasecmp((const char *)a, (const char *)b); }
static void pl_scan(void) {
    npl = 0;
    DIR *d = opendir(pldir); if (!d) return;
    struct dirent *de;
    while ((de = readdir(d)) && npl < 60) {
        size_t L = strlen(de->d_name);
        if (de->d_name[0] == '.' || L < 5 || L > 60 || strcasecmp(de->d_name + L - 4, ".txt")) continue;
        memcpy(pls[npl], de->d_name, L - 4); pls[npl][L - 4] = 0;
        npl++;
    }
    closedir(d);
    qsort(pls, (size_t)npl, sizeof pls[0], cmp_str);
}
static void pl_path(char *p, size_t cap, const char *name) { snprintf(p, cap, "%s/%s.txt", pldir, name); }
static int pl_load(const char *name) {
    if (!ple) ple = (PE *)calloc(300, sizeof(PE));
    nple = 0;
    char p[800]; pl_path(p, sizeof p, name);
    FILE *f = fopen(p, "r"); if (!f) return -1;
    char line[900];
    while (nple < 300 && fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        char *a = line, *b = strchr(a, '\t'); if (!b) continue; *b++ = 0;
        char *c = strchr(b, '\t'); if (c) *c++ = 0;
        snprintf(ple[nple].id, sizeof ple[nple].id, "%s", a);
        snprintf(ple[nple].name, sizeof ple[nple].name, "%s", b);
        snprintf(ple[nple].disp, sizeof ple[nple].disp, "%s", c && *c ? c : b);
        nple++;
    }
    fclose(f);
    return 0;
}
static void pl_save(const char *name) {
    char p[800]; pl_path(p, sizeof p, name);
    FILE *f = fopen(p, "w"); if (!f) return;
    for (int i = 0; i < nple; i++) fprintf(f, "%s\t%s\t%s\n", ple[i].id, ple[i].name, ple[i].disp);
    fclose(f); sync();
}
static int pl_append(const char *name, const char *id, const char *file, const char *disp) {
    char p[800]; pl_path(p, sizeof p, name);
    FILE *f = fopen(p, "a"); if (!f) return -1;
    char a[200], b[450], c[220]; clean_line(a, sizeof a, id); clean_line(b, sizeof b, file); clean_line(c, sizeof c, disp);
    fprintf(f, "%s\t%s\t%s\n", a, b, c);
    fclose(f); sync();
    return 0;
}
static int pl_new(char *out, size_t cap) {
    char name[64] = "";
    if (!keyboard_act("New playlist", name, 40, "CREATE")) return 0;
    for (char *p = name; *p; p++) if (strchr("\\/:*?\"<>|.", *p)) *p = '_';
    size_t L = strlen(name); while (L && name[L - 1] == ' ') name[--L] = 0;
    if (!L) return 0;
    char p[800]; pl_path(p, sizeof p, name);
    FILE *f = fopen(p, "a"); if (f) fclose(f);
    snprintf(out, cap, "%s", name);
    return 1;
}
/* asks which playlist; returns 1 with the name */
static int pl_choose(char *out, size_t cap) {
    pl_scan();
    const char *items[62]; int n = 0;
    items[n++] = "+ New playlist...";
    for (int i = 0; i < npl; i++) items[n++] = pls[i];
    int k = menu_pick("Add to playlist", items, n);
    if (k < 0) return 0;
    if (k == 0) return pl_new(out, cap);
    snprintf(out, cap, "%s", pls[k - 1]);
    return 1;
}

/* ---------- watching ---------- */
static void dl_url(char *url, size_t cap, const char *id, const char *file) {
    char ide[400], ne[1600];
    urlenc(ide, sizeof ide, id, 0);
    urlenc(ne, sizeof ne, file, 1);
    snprintf(url, cap, "https://archive.org/download/%s/%s", ide, ne);
}
static void local_path(char *p, size_t cap, char *sn, size_t sncap, const char *id, const char *file) {
    safe_name(sn, sncap, id, file);
    snprintf(p, cap, "%s/%s", viddir, sn);
}
/* Watch a file now: plays it from the card, or streams it while it downloads.
   Returns 1 if it played to the end, 0 if stopped, -1 if it could not play. */
static int watch_path(const char *url_in, const char *p, const char *disp);
static int watch(const char *id, const char *file, const char *disp) {
    char sn[300], p[1000], url[2200];
    if (!strcmp(id, "local")) {   /* a video copied onto the card by hand */
        snprintf(sn, sizeof sn, "%s", file); snprintf(p, sizeof p, "%s/%s", viddir, file);
        if (access(p, F_OK) != 0) { error_box("That video is not on the card anymore.", disp); return -1; }
        return play_file(p, sn, disp, 0);
    }
    local_path(p, sizeof p, sn, sizeof sn, id, file);
    dl_url(url, sizeof url, id, file);
    return watch_path(url, p, disp);
}
/* stops a watch-now download and lets the queue carry on from the same partial file (shows in Files) */
static int dl_to_queue(Dl *d, const char *p, const char *disp) {
    char u2[3000]; snprintf(u2, sizeof u2, "%s", d->url);
    if (d->state == 1) { d->cancel = 1; while (d->state == 1) usleep(20000); }
    dl_reap(d);
    if (access(p, F_OK) == 0) return 1;   /* it finished meanwhile */
    int r = q_add_next(u2, p, disp);
    queue_tick();
    return r;
}
/* watch a video by its download address and card path (plays, or streams while downloading) */
static int watch_path(const char *url_in, const char *p, const char *disp) {
    char sn[300], url[2200];
    const char *base = strrchr(p, '/'); snprintf(sn, sizeof sn, "%s", base ? base + 1 : p);
    snprintf(url, sizeof url, "%s", url_in);
    if (access(p, F_OK) == 0) return play_file(p, sn, disp, 0);
    Dl *d = 0; int own = 1;
    if (DLQ.state == 1 && !strcmp(DLQ.dst, p)) { d = &DLQ; own = 0; }        /* already downloading in the queue */
    else if (DLW.state == 1 && !strcmp(DLW.dst, p)) { d = &DLW; }           /* kept downloading earlier */
    else {
        if (DLW.state == 1) {
            int k = ask("Busy", "Another video is downloading.", DLW.disp, MOOD_WOW, "A:Stop it  B:Back");
            if (k != K_A) return -1;
            DLW.cancel = 1; while (DLW.state == 1) usleep(20000);
            dl_reap(&DLW);
        }
        int qi = q_find(p);
        if (qi >= 0) q_remove(qi, 0);
        if (dl_begin(&DLW, url, p, disp) < 0) { error_box("Could not start download", 0); return -1; }
        d = &DLW;
    }
    int w = dl_wait(d, 2LL << 20, disp, own);
    if (w == 2) {   /* switch to the background queue; curl keeps what's already here and resumes */
        int r = dl_to_queue(d, p, disp);
        sfx(SFX_TOGGLE);
        message_m("Download queue", r == 2 || r < 0 ? "The queue is full." : "Added to the download queue!", "Find it in the Files tab", MOOD_HAPPY, 0);
        usleep(1200000);
        return -1;
    }
    if (!w) return -1;
    int streaming = d->state == 1 || d->state == 2;
    int r = play_file(p, sn, disp, streaming ? d : 0);
    int never = streaming && !PL.shown && (PL.vcc || !PL.acc);   /* audio-only files never show a picture */   /* not one picture: this file can't play until it's all here */
    if (never) logf_("could not stream %s%s", p, SR.far ? " (needs the end of the file)" : "");
    if (streaming && d->state == 1) {
        if (never) {
            int k = ask("Can't stream this one", "It plays once it's downloaded.", disp, MOOD_SAD, own ? "A:Queue it  B:Cancel" : "A:OK");
            if (own) {
                if (k == K_A) { dl_to_queue(d, p, disp); message_m("Download queue", "Downloading in the queue", "Find it in the Files tab", MOOD_HAPPY, 0); usleep(1100000); }
                else { d->cancel = 1; while (d->state == 1) usleep(20000); dl_reap(d); }
            }
        } else if (own) {
            int k = ask("Still downloading", "Add it to the download queue?", disp, MOOD_WOW, "A:Queue it  B:Stop");
            if (k == K_A) dl_to_queue(d, p, disp);
            else { d->cancel = 1; while (d->state == 1) usleep(20000); dl_reap(d); }
        }
    } else if (never && d->state != 1 && access(p, F_OK) != 0) error_box("Could not play this video", "(see log.txt)");
    queue_tick();
    return r;
}
static void play_list_from(int start, const char *plname) {
    for (int i = start; i < nple; i++) {
        int r = watch(ple[i].id, ple[i].name, ple[i].disp);
        if (r != 1 || i + 1 >= nple) break;
        /* up next, B to stop */
        double t0 = now_s(); int stop = 0;
        flush_keys();
        while (now_s() - t0 < 3.0 && !stop) {
            char l2[220]; snprintf(l2, sizeof l2, "%s", ple[i + 1].disp);
            message_m(plname, "Up next:", l2, MOOD_HAPPY, "A:Play now  B:Stop");
            SDL_Event e;
            while (SDL_PollEvent(&e)) if (e.type == SDL_KEYDOWN) { if (e.key.keysym.sym == K_A) t0 = -10; else if (e.key.keysym.sym == K_B || e.key.keysym.sym == K_MENU) stop = 1; }
            usleep(40000);
        }
        if (stop) break;
    }
}

/* ---------- an item (a show / upload): its video files ---------- */
static void lbl_file(int i, char *m, size_t mc, char *s, size_t sc, int *tone) {
    VFile *f = &files[i];
    snprintf(m, mc, "%s%s", f->have ? "* " : "", f->disp);
    char sz[16]; fmt_size(sz, sizeof sz, f->size);
    if (f->h > 0) snprintf(s, sc, "%dp %s", f->h, sz); else snprintf(s, sc, "%s", sz);
    *tone = f->rank == 0 ? 1 : f->rank >= 3 ? 3 : 0;
    if (f->h > 480) *tone = 2;
}

/* ---------- episodes of one show folded into a single row ---------- */
/* compares names the way people count: "Ep 2" before "Ep 10" */
static int natcmp(const char *a, const char *b) {
    for (;;) {
        if (!*a || !*b) return (unsigned char)*a - (unsigned char)*b;
        if (*a >= '0' && *a <= '9' && *b >= '0' && *b <= '9') {
            while (*a == '0') a++;
            while (*b == '0') b++;
            const char *ea = a, *eb = b;
            while (*ea >= '0' && *ea <= '9') ea++;
            while (*eb >= '0' && *eb <= '9') eb++;
            if (ea - a != eb - b) return (int)(ea - a) - (int)(eb - b);
            for (; a < ea; a++, b++) if (*a != *b) return *a - *b;
            continue;
        }
        int ca = tolower((unsigned char)*a), cb = tolower((unsigned char)*b);
        if (ca != cb) return ca - cb;
        a++; b++;
    }
}
static int is_num(const char *t) { if (!*t) return 0; for (; *t; t++) if (*t < '0' || *t > '9') return 0; return 1; }
/* a word that says "an episode number comes here": 12, ep, ep12, e05, s01e02, #3, part, vol, disc... */
static int is_ep_word(const char *w) {
    char t[40]; size_t n = 0;
    for (; *w && n < sizeof t - 1; w++) t[n++] = (char)tolower((unsigned char)*w);
    t[n] = 0;
    if (is_num(t)) return n <= 4;
    static const char *W[] = { "ep", "eps", "episode", "episodes", "e", "ch", "chapter", "part", "pt", "vol", "volume", "disc", "disk", "no", "act", "reel", 0 };
    for (int i = 0; W[i]; i++) if (!strcmp(t, W[i])) return 1;
    const char *d = t;
    if (*d == 's' && d[1] >= '0' && d[1] <= '9') { d++; while (*d >= '0' && *d <= '9') d++; if (*d == 'e' || !*d) return 1; }
    if ((t[0] == 'e' && is_num(t + 1)) || (!strncmp(t, "ep", 2) && is_num(t + 2)) || (t[0] == '#' && is_num(t + 1))) return 1;
    return 0;
}
/* "Sailor Moon - 012 [x264].mp4" -> key "sailor moon", show "Sailor Moon". 0 = doesn't look like an episode. */
static int series_key(const char *in, char *key, size_t kc, char *show, size_t sc) {
    char b[260]; size_t n = 0; int depth = 0;
    const char *dot = strrchr(in, '.');
    if (dot && strlen(dot) <= 5 && strlen(dot) >= 3 && isalpha((unsigned char)dot[1])) {} else dot = 0;
    for (const char *p = in; *p && (!dot || p < dot) && n < sizeof b - 1; p++) {   /* drop [tags] (notes) {crc} */
        if (*p == '[' || *p == '(' || *p == '{') { depth++; continue; }
        if (*p == ']' || *p == ')' || *p == '}') { if (depth) depth--; continue; }
        if (depth) continue;
        char c = *p;
        if (c == '_' || c == '.' || c == '-' || c == '~' || c == ',' || c == ':' || c == '|') c = ' ';
        b[n++] = c;
    }
    b[n] = 0;
    key[0] = show[0] = 0;
    size_t kl = 0, sl = 0; int words = 0, found = 0;
    for (char *w = strtok(b, " \t"); w; w = strtok(0, " \t")) {
        char *h = w; if (*h == '#' && h[1]) { if (is_num(h + 1) && words) { found = 1; break; } }
        if (words && is_ep_word(w)) { found = 1; break; }
        if (!words && is_num(w)) return 0;   /* starts with a number: probably a year or a list position */
        size_t L = strlen(w);
        if (kl + L + 2 >= kc || sl + L + 2 >= sc) break;
        if (words) { key[kl++] = ' '; show[sl++] = ' '; }
        for (size_t i = 0; i < L; i++) { key[kl++] = (char)tolower((unsigned char)w[i]); show[sl++] = w[i]; }
        key[kl] = show[sl] = 0;
        words++;
    }
    if (found && kl >= 3) return 1;
    /* "Show Name - Episode Title" with no number: the part before " - " names the show */
    const char *dash = strstr(in, " - ");
    if (dash && dash > in && (size_t)(dash - in) < 200) {
        char tmp[220]; size_t L = (size_t)(dash - in);
        memcpy(tmp, in, L); memcpy(tmp + L, " 1", 3);
        return series_key(tmp, key, kc, show, sc);
    }
    return 0;
}
/* Shared folding: for n names, gid[i] = the series row it belongs to (-1 = stands alone).
   A show needs 3+ episodes to get its own row. */
#define GMAX 2000
typedef struct { char key[96]; char show[120]; int n, first; } Series;
static Series *g_ser; static int g_nser;
static int g_gid[GMAX];
static int *g_sord; static char (*g_keys)[96];
static int cmp_sord(const void *a, const void *b) { int x = *(const int *)a, y = *(const int *)b; int c = strcmp(g_keys[x], g_keys[y]); return c ? c : x - y; }
static void series_build(int n, const char *(*name)(int)) {
    if (n > GMAX) n = GMAX;
    if (!g_ser) { g_ser = (Series *)calloc(GMAX, sizeof(Series)); g_sord = (int *)calloc(GMAX, sizeof(int)); g_keys = calloc(GMAX, 96); }
    g_nser = 0;
    if (!g_ser || !g_sord || !g_keys) { for (int i = 0; i < n; i++) g_gid[i] = -1; return; }
    static char shows[GMAX][120];
    for (int i = 0; i < n; i++) {
        g_gid[i] = -1;
        if (!g_group || !series_key(name(i), g_keys[i], 96, shows[i], 120)) g_keys[i][0] = 0;
        g_sord[i] = i;
    }
    qsort(g_sord, (size_t)n, sizeof(int), cmp_sord);
    for (int a = 0; a < n;) {
        int b = a + 1;
        while (b < n && !strcmp(g_keys[g_sord[a]], g_keys[g_sord[b]])) b++;
        if (g_keys[g_sord[a]][0] && b - a >= 3) {
            Series *sr = &g_ser[g_nser];
            snprintf(sr->key, sizeof sr->key, "%s", g_keys[g_sord[a]]);
            snprintf(sr->show, sizeof sr->show, "%s", shows[g_sord[a]]);
            sr->n = b - a; sr->first = g_sord[a];   /* sorted by key then index: the first one in list order */
            for (int j = a; j < b; j++) g_gid[g_sord[j]] = g_nser;
            g_nser++;
        }
        a = b;
    }
}
/* the rows shown: a series sits where its first episode was. rows[] >= 0 = entry, < 0 = -(series+1) */
static int series_rows(int n, int *rows) {
    int r = 0;
    for (int i = 0; i < n && i < GMAX; i++) {
        if (g_gid[i] < 0) rows[r++] = i;
        else if (g_ser[g_gid[i]].first == i) rows[r++] = -(g_gid[i] + 1);
    }
    return r;
}
/* members of one series, in episode order */
static const char *(*g_mname)(int);
static int cmp_members(const void *a, const void *b) { return natcmp(g_mname(*(const int *)a), g_mname(*(const int *)b)); }
static int series_members(int n, int sid, int *out, const char *(*name)(int)) {
    int m = 0;
    for (int i = 0; i < n && i < GMAX; i++) if (g_gid[i] == sid) out[m++] = i;
    g_mname = name;
    qsort(out, (size_t)m, sizeof(int), cmp_members);
    return m;
}
static int cmp_file(const void *a, const void *b) {
    const VFile *x = (const VFile *)a, *y = (const VFile *)b;
    if (x->rank != y->rank) return x->rank - y->rank;
    if (x->size != y->size) return x->size < y->size ? -1 : 1;
    return strcasecmp(x->name, y->name);
}
/* Internet Archive makes a few stills of each video, "<item>.thumbs/<file name without extension>_000057.jpg".
   Each video file gets one from near its start (the third, past any title card). */
static char (*th_names)[400]; static int th_n;
static void thumb_note(const char *name) {
    if (!strstr(name, ".thumbs/") || !strstr(name, ".jpg") || th_n >= 1500) return;
    if (!th_names) { th_names = calloc(1500, 400); if (!th_names) return; }
    snprintf(th_names[th_n++], 400, "%s", name);
}
static void thumb_base(char *b, size_t cap, const char *name) {   /* "dir/Ep 1_512kb.mp4" -> "Ep 1" */
    const char *sl = strrchr(name, '/');
    snprintf(b, cap, "%s", sl ? sl + 1 : name);
    char *dot = strrchr(b, '.');
    if (dot) *dot = 0;
    size_t n = strlen(b);
    if (n > 6 && !strcmp(b + n - 6, "_512kb")) b[n - 6] = 0;
    else if (n > 3 && !strcmp(b + n - 3, ".ia")) b[n - 3] = 0;
}
static int cmp_str400(const void *a, const void *b) { return strcmp((const char *)a, (const char *)b); }
static void thumb_match(void) {
    if (th_n) qsort(th_names, (size_t)th_n, 400, cmp_str400);
    for (int i = 0; i < nfiles; i++) {
        char b[400], want[420]; thumb_base(b, sizeof b, files[i].name);
        snprintf(want, sizeof want, "%s_", b);
        size_t wl = strlen(want); int hit = -1, k = 0;
        for (int j = 0; j < th_n; j++) {
            const char *f = strstr(th_names[j], ".thumbs/") + 8;
            if (strncmp(f, want, wl) || strchr(f + wl, '_') || !(f[wl] >= '0' && f[wl] <= '9')) continue;   /* <base>_<digits>.jpg */
            hit = j; if (++k == 3) break;
        }
        files[i].thumb[0] = 0;
        if (hit >= 0) snprintf(files[i].thumb, sizeof files[i].thumb, "%s", th_names[hit]);
    }
    th_n = 0;
}
static char cur_item_id[128];
static const char *thumb_file(int fi) {   /* a still from this video, or else the item's picture */
    static char u[4][900]; static int k = 0;
    if (fi < 0 || fi >= nfiles) return 0;
    if (!files[fi].thumb[0]) return thumb_of_item(cur_item_id);
    char ide[300], ne[800]; urlenc(ide, sizeof ide, cur_item_id, 0); urlenc(ne, sizeof ne, files[fi].thumb, 1);
    k = (k + 1) & 3; snprintf(u[k], sizeof u[k], "https://archive.org/download/%s/%s", ide, ne);
    return u[k];
}
static int load_item(const char *id) {
    char url[600], ide[400];
    urlenc(ide, sizeof ide, id, 0);
    snprintf(url, sizeof url, "https://archive.org/metadata/%s", ide);
    int err = 0;
    char *js = http_get(url, &err);
    if (!js) return err ? err : -1;
    if (!files) files = (VFile *)calloc(400, sizeof(VFile));
    nfiles = 0; item_title[0] = 0; item_title8[0] = 0; th_n = 0;
    snprintf(cur_item_id, sizeof cur_item_id, "%s", id);
    const char *md = jget(js, "metadata");
    if (md) { char t[400]; const char *tv = jget(md, "title"); if (tv) { jstr(tv, t, sizeof t); l1(item_title, sizeof item_title, t); snprintf(item_title8, sizeof item_title8, "%s", t); } }
    if (!item_title[0]) l1(item_title, sizeof item_title, id);
    if (!item_title8[0]) snprintf(item_title8, sizeof item_title8, "%s", id);
    const char *fa = jget(js, "files");
    for (const char *el = fa ? jfirst(fa) : 0; el && nfiles < 400; el = jnext(el)) {
        loading_anim();
        char name[400], fmt[64], tmp[64];
        const char *v = jget(el, "name"); if (!v) continue;
        jstr(v, name, sizeof name);
        if (!is_video_name(name)) { thumb_note(name); continue; }
        VFile *f = &files[nfiles];
        memset(f, 0, sizeof *f);
        snprintf(f->name, sizeof f->name, "%s", name);
        fmt[0] = 0; if ((v = jget(el, "format"))) jstr(v, fmt, sizeof fmt);
        snprintf(f->fmt, sizeof f->fmt, "%s", fmt);
        if ((v = jget(el, "size"))) { jstr(v, tmp, sizeof tmp); f->size = strtoll(tmp, 0, 10); }
        if ((v = jget(el, "height"))) { jstr(v, tmp, sizeof tmp); f->h = atoi(tmp); }
        if ((v = jget(el, "width"))) { jstr(v, tmp, sizeof tmp); f->w = atoi(tmp); }
        if ((v = jget(el, "length"))) { jstr(v, tmp, sizeof tmp); f->len = strtod(tmp, 0); }
        const char *dot = strrchr(name, '.');
        int mp4 = dot && (!strcasecmp(dot, ".mp4") || !strcasecmp(dot, ".m4v"));
        int audio = dot && (!strcasecmp(dot, ".mp3") || !strcasecmp(dot, ".ogg"));
        if (mp4 && (strstr(fmt, "512Kb") || (f->h > 0 && f->h <= 480))) f->rank = 0;
        else if (dot && !strcasecmp(dot, ".ogv")) f->rank = 1;
        else if (mp4) f->rank = f->h > 480 ? 2 : 1;
        else if (audio) f->rank = 4;
        else f->rank = 3;
        if (f->size > 2000000000LL) f->rank = 5;
        const char *base = strrchr(name, '/');
        l1(f->disp, sizeof f->disp, base ? base + 1 : name);
        char sn[300], p[1000]; local_path(p, sizeof p, sn, sizeof sn, id, name);
        f->have = access(p, F_OK) == 0;
        nfiles++;
    }
    free(js);
    thumb_match();
    qsort(files, (size_t)nfiles, sizeof(VFile), cmp_file);
    logf_("item %s: %d video files", id, nfiles);
    return 0;
}
/* the actions on one file of an item: A = menu, START = watch right away */
static void file_act(const char *id, int fi, int k, const char **status) {
    VFile *f = &files[fi];
    char sn[300], p[1000], url[2200];
    local_path(p, sizeof p, sn, sizeof sn, id, f->name);
    if (k == K_START && f->size <= 2000000000LL) { watch(id, f->name, f->disp); f->have = access(p, F_OK) == 0; return; }
    if (k != K_A && k != K_START) return;
    if (f->size > 2000000000LL) { *status = "Too big (over 2 GB) - pick another file"; return; }
    f->have = access(p, F_OK) == 0;
    int queued = q_find(p) >= 0;
    const char *opts_new[] = { "Watch now", queued ? "Already in queue" : "Add to download queue", "Add to playlist...", "Back" };
    const char *opts_have[] = { "Watch", "Add to playlist...", "Delete from card", "Back" };
    int c = menu_pick(f->disp, f->have ? opts_have : opts_new, 4);
    if (c < 0) return;
    const char *act = f->have ? opts_have[c] : opts_new[c];
    if (!strcmp(act, "Watch now") || !strcmp(act, "Watch")) { watch(id, f->name, f->disp); f->have = access(p, F_OK) == 0; }
    else if (!strcmp(act, "Add to download queue")) {
        dl_url(url, sizeof url, id, f->name);
        int r = q_add(url, p, f->disp);
        *status = r == 0 ? "Added to the queue (see Files tab)" : r == 1 ? "Already in the queue" : "Queue is full";
        queue_tick();
    } else if (!strcmp(act, "Add to playlist...")) {
        char pl[64];
        if (pl_choose(pl, sizeof pl)) { *status = pl_append(pl, id, f->name, f->disp) == 0 ? "Added to playlist!" : "Could not save"; }
    } else if (!strcmp(act, "Delete from card")) {
        unlink(p); resume_set(sn, 0); local_thumb_drop(sn); sync(); f->have = 0; *status = "Deleted.";
    }
}
static int g_file_sort = 0;   /* 0 = best quality first, 1 = A to Z (episode order) */
static int cmp_file_az(const void *a, const void *b) {
    const VFile *x = (const VFile *)a, *y = (const VFile *)b;
    int c = natcmp(x->disp, y->disp);
    return c ? c : x->rank - y->rank;
}
static const char *fname_i(int i) { return files[i].disp; }
static int irow[GMAX], nirow, gmem[GMAX], ngmem;
static void lbl_irow(int r, char *m, size_t mc, char *s, size_t sc, int *tone) {
    int v = irow[r];
    if (v >= 0) { lbl_file(v, m, mc, s, sc, tone); return; }
    Series *sr = &g_ser[-v - 1];
    snprintf(m, mc, "%s", sr->show); snprintf(s, sc, "%d eps", sr->n); *tone = 4;
}
static void lbl_gmem(int r, char *m, size_t mc, char *s, size_t sc, int *tone) { lbl_file(gmem[r], m, mc, s, sc, tone); }
static const char *thumb_gmem(int r) { return r < ngmem ? thumb_file(gmem[r]) : 0; }
static const char *thumb_irow(int r) { if (r >= nirow) return 0; int v = irow[r]; return thumb_file(v >= 0 ? v : g_ser[-v - 1].first); }
/* the episodes of one show inside an item, in episode order */
static void screen_series_files(const char *id, int sid) {
    char title[120]; snprintf(title, sizeof title, "%s", g_ser[sid].show);
    ngmem = series_members(nfiles, sid, gmem, fname_i);
    int sel = 0; const char *status = "";
    for (;;) {
        g_thumb_fn = thumb_gmem;
        int k = list_loop(title, ngmem, lbl_gmem, "A:Menu  START:Watch  B:Back", &sel, "Nothing here.", &status);
        if (k == K_B || k == K_MENU) return;
        status = "";
        if (ngmem) file_act(id, gmem[sel], k, &status);
    }
}
static void screen_item(const char *id, const char *fallback_title) {
    char t[120]; l1(t, sizeof t, fallback_title);
    message("Tuning in...", "Tuning in...", t, "B:Cancel");
    int err = load_item(id);
    if (err) { net_error(err); return; }
    char my_title[200], my_title8[300];
    snprintf(my_title, sizeof my_title, "%s", item_title); snprintf(my_title8, sizeof my_title8, "%s", item_title8);
    if (g_file_sort) qsort(files, (size_t)nfiles, sizeof(VFile), cmp_file_az);
    int sel = 0;
    const char *status = "";
    for (;;) {
        series_build(nfiles, fname_i);
        nirow = series_rows(nfiles, irow);
        if (sel >= nirow) sel = nirow ? nirow - 1 : 0;
        g_thumb_fn = thumb_irow;
        int k = list_loop(my_title, nirow, lbl_irow, "A:Menu  X:Sort  Y:Save", &sel, "No video files in this item.", &status);
        if (k == K_B || k == K_MENU) return;
        if (k == K_Y) { status = save_msg(chan_add("item", id, my_title8)); continue; }
        if (k == K_X) {
            g_file_sort = !g_file_sort;
            qsort(files, (size_t)nfiles, sizeof(VFile), g_file_sort ? cmp_file_az : cmp_file);
            sel = 0; status = g_file_sort ? "Sorted: A to Z (episode order)" : "Sorted: best quality first";
            continue;
        }
        status = "";
        if ((k != K_A && k != K_START) || !nirow) continue;
        int v = irow[sel];
        if (v < 0) { screen_series_files(id, -v - 1); continue; }
        file_act(id, v, k, &status);
    }
}

/* ---------- lists of items (collections and searches) ---------- */
static const char *SORTS[4] = { "downloads+desc", "addeddate+desc", "addeddate+asc", "titleSorter+asc" };
static const char *SORTN[4] = { "Sorted: most popular", "Sorted: newest", "Sorted: oldest", "Sorted: A to Z" };
static void lbl_item(int i, char *m, size_t mc, char *s, size_t sc, int *tone) {
    if (i == nitems) { snprintf(m, mc, "More... (%d of %d)", nitems, items_found); *tone = 4; return; }
    snprintf(m, mc, "%s", items[i].title);
    if (items[i].coll) { snprintf(s, sc, "coll."); *tone = 4; }
}
static int load_items(const Chan *c, int page, int sort) {
    char q[900], qe[1800], url[2400];
    if (c->type == CH_COLLECTION) snprintf(q, sizeof q, "collection:(%s) AND mediatype:(movies OR collection)", c->arg);
    else if (c->type == CH_CSEARCH) snprintf(q, sizeof q, "(%s) AND mediatype:(collection)", c->arg);
    else snprintf(q, sizeof q, "(%s) AND mediatype:(movies)", c->arg);
    urlenc(qe, sizeof qe, q, 0);
    snprintf(url, sizeof url, "https://archive.org/advancedsearch.php?q=%s&fl%%5B%%5D=identifier&fl%%5B%%5D=title&fl%%5B%%5D=mediatype&rows=100&page=%d&sort%%5B%%5D=%s&output=json", qe, page, SORTS[sort % 4]);
    int err = 0;
    char *js = http_get(url, &err);
    if (!js) return err ? err : -1;
    if (!items) items = (Item *)calloc(2000, sizeof(Item));
    if (page == 1) nitems = 0;
    const char *resp = jget(js, "response");
    if (!resp) { logf_("search: unexpected answer: %.200s", js); free(js); return -5; }
    char tmp[64];
    const char *nf = jget(resp, "numFound");
    if (nf) { jstr(nf, tmp, sizeof tmp); items_found = atoi(tmp); }
    const char *docs = jget(resp, "docs");
    for (const char *el = docs ? jfirst(docs) : 0; el && nitems < 2000; el = jnext(el)) {
        loading_anim();
        Item *it = &items[nitems];
        const char *v = jget(el, "identifier"); if (!v) continue;
        jstr(v, it->id, sizeof it->id);
        char t[400] = "";
        if ((v = jget(el, "title"))) jstr(v, t, sizeof t);
        l1(it->title, sizeof it->title, t[0] ? t : it->id);
        snprintf(it->title8, sizeof it->title8, "%s", t[0] ? t : it->id);
        it->coll = 0;
        if ((v = jget(el, "mediatype"))) { jstr(v, tmp, sizeof tmp); it->coll = !strcmp(tmp, "collection"); }
        nitems++;
    }
    items_page = page;
    free(js);
    logf_("search page %d: %d of %d", page, nitems, items_found);
    return 0;
}
/* nested collections need their own copy of the list */
typedef struct { Item *v; int n, found, page; } ItemsSave;
static void items_push(ItemsSave *s) {
    s->n = nitems; s->found = items_found; s->page = items_page;
    s->v = (Item *)malloc(sizeof(Item) * (size_t)(nitems ? nitems : 1));
    if (s->v) memcpy(s->v, items, sizeof(Item) * (size_t)nitems);
}
static void items_pop(ItemsSave *s) {
    if (s->v) { memcpy(items, s->v, sizeof(Item) * (size_t)s->n); free(s->v); }
    nitems = s->n; items_found = s->found; items_page = s->page;
}
static void screen_collection(const Chan *c);
static const char *iname_i(int i) { return items[i].title; }
static int crow[GMAX], ncrow;
static void lbl_crow(int r, char *m, size_t mc, char *s, size_t sc, int *tone) {
    if (r >= ncrow) { lbl_item(nitems, m, mc, s, sc, tone); return; }   /* "More..." */
    int v = crow[r];
    if (v >= 0) { lbl_item(v, m, mc, s, sc, tone); return; }
    Series *sr = &g_ser[-v - 1];
    snprintf(m, mc, "%s", sr->show); snprintf(s, sc, "%d", sr->n); *tone = 4;
}
/* opens one search result: a video item, or a collection inside this one */
static void open_item_idx(int idx) {
    Item it = items[idx];
    ItemsSave sv; items_push(&sv);
    if (it.coll) {
        Chan sub; memset(&sub, 0, sizeof sub);
        sub.type = CH_COLLECTION;
        snprintf(sub.arg, sizeof sub.arg, "%s", it.id);
        snprintf(sub.title, sizeof sub.title, "%s", it.title);
        screen_collection(&sub);
    } else screen_item(it.id, it.title8);
    items_pop(&sv);
}
static const char *thumb_crow(int r) {
    if (r >= ncrow) return 0;
    int v = crow[r];
    return thumb_of_item(v >= 0 ? items[v].id : items[g_ser[-v - 1].first].id);   /* a show: its first episode's picture */
}
static int *smem_cur; static int nsmem_cur;
static const char *thumb_smem(int r) { return r < nsmem_cur ? thumb_of_item(items[smem_cur[r]].id) : 0; }
static void lbl_smem(int r, char *m, size_t mc, char *s, size_t sc, int *tone) { lbl_item(smem_cur[r], m, mc, s, sc, tone); }
/* results that look like episodes of one show, in episode order */
static void screen_series_items(int sid) {
    char title[120]; snprintf(title, sizeof title, "%s", g_ser[sid].show);
    int *mem = (int *)malloc(sizeof(int) * GMAX); if (!mem) return;
    int nm = series_members(nitems, sid, mem, iname_i);
    int sel = 0; const char *status = "";
    for (;;) {
        smem_cur = mem; nsmem_cur = nm;
        g_thumb_fn = thumb_smem;
        int k = list_loop(title, nm, lbl_smem, "A:Open  Y:Save  B:Back", &sel, "Nothing here.", &status);
        if (k == K_START) k = K_A;
        if (k == K_B || k == K_MENU) break;
        if (k == K_Y && nm) { status = save_msg(chan_add(items[mem[sel]].coll ? "collection" : "item", items[mem[sel]].id, items[mem[sel]].title8)); continue; }
        status = "";
        if (k == K_A && nm) open_item_idx(mem[sel]);
    }
    free(mem);
}
static void screen_collection(const Chan *c) {
    int sort = 0;
    message("Tuning in...", "Tuning in...", c->title, "B:Cancel");
    int err = load_items(c, 1, sort);
    if (err) { net_error(err); return; }
    int sel = 0;
    const char *status = "";
    const char *empty = c->type == CH_COLLECTION ? "No videos in this collection." : "Nothing found. Try other words?";
    for (;;) {
        int more = nitems < items_found && nitems < 2000;
        series_build(nitems, iname_i);
        ncrow = series_rows(nitems, crow);
        if (sel > ncrow + more - 1) sel = ncrow + more ? ncrow + more - 1 : 0;
        g_thumb_fn = thumb_crow;
        int k = list_loop(c->title, ncrow + more, lbl_crow, "A:Open  Y:Save  X:Sort  B:Back", &sel, empty, &status);
        if (k == K_START) k = K_A;
        if (k == K_B || k == K_MENU) return;
        int v = sel < ncrow ? crow[sel] : -100000;
        if (k == K_Y && sel < ncrow && v >= 0) { status = save_msg(chan_add(items[v].coll ? "collection" : "item", items[v].id, items[v].title8)); continue; }
        if (k == K_X) {
            sort = (sort + 1) % 4;
            message("Tuning in...", "Re-sorting...", c->title, "B:Cancel");
            int e2 = load_items(c, 1, sort);
            if (e2) { net_error(e2); if (e2 != -5) return; sort = (sort + 3) % 4; continue; }
            sel = 0; status = SORTN[sort];
            continue;
        }
        status = "";
        if (k != K_A) continue;
        if (more && sel == ncrow) {
            message("Tuning in...", "Loading more...", c->title, "B:Cancel");
            int e2 = load_items(c, items_page + 1, sort);
            if (e2) net_error(e2);
            continue;
        }
        if (sel >= ncrow) continue;
        if (v < 0) screen_series_items(-v - 1);
        else open_item_idx(v);
    }
}
static void open_chan(const Chan *c) {
    if (c->type == CH_ITEM) screen_item(c->arg, c->title);
    else screen_collection(c);
}

/* ---------- guide: guide.txt, "[Category]" then "collection_id | Name" lines ---------- */
typedef struct { char id[96]; char title[64]; int cat, search; } GEnt;
#define GCMAX 48
static char gcats[GCMAX][40], gpar[GCMAX][40]; static int ngcat = 0;   /* "[Parent > Child]" makes a sub-shelf */
static GEnt gents[600]; static int ngent = 0;
/* top level of the Guide tab: plain categories and parent folders */
typedef struct { int cat; char folder[40]; } GTop;
static GTop gtop[GCMAX]; static int ngtop = 0;
static void guide_load(void) {
    ngcat = ngent = 0;
    char p[700]; snprintf(p, sizeof p, "%s/guide.txt", appdir);
    FILE *f = fopen(p, "r"); if (!f) return;
    char line[300];
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        char *s = line; while (*s == ' ' || *s == '\t') s++;
        if (!*s || *s == '#') continue;
        if (*s == '[') {
            char *e = strchr(s, ']'); if (e) *e = 0;
            if (ngcat < GCMAX) {
                char *name = s + 1, *gt = strchr(name, '>');
                gpar[ngcat][0] = 0;
                if (gt) {
                    char par[80]; size_t pl = (size_t)(gt - name); if (pl > 79) pl = 79;
                    memcpy(par, name, pl); par[pl] = 0;
                    while (pl && par[pl - 1] == ' ') par[--pl] = 0;
                    l1(gpar[ngcat], sizeof gpar[0], par);
                    name = gt + 1; while (*name == ' ') name++;
                }
                l1(gcats[ngcat], sizeof gcats[0], name);
                ngcat++;
            }
            continue;
        }
        if (!ngcat || ngent >= 600) continue;
        char *bar = strchr(s, '|'), *t = 0;
        if (bar) { *bar = 0; t = bar + 1; while (*t == ' ') t++; }
        size_t L = strlen(s); while (L && (s[L - 1] == ' ' || s[L - 1] == '\t')) s[--L] = 0;
        if (!L) continue;
        int srch = !strncasecmp(s, "search ", 7);
        if (srch) { s += 7; while (*s == ' ') s++; }
        gents[ngent].search = srch;
        snprintf(gents[ngent].id, sizeof gents[0].id, "%s", s);
        l1(gents[ngent].title, sizeof gents[0].title, t && *t ? t : s);
        gents[ngent].cat = ngcat - 1;
        ngent++;
    }
    fclose(f);
    ngtop = 0;
    for (int c = 0; c < ngcat; c++) {
        if (!gpar[c][0]) { gtop[ngtop].cat = c; gtop[ngtop].folder[0] = 0; ngtop++; continue; }
        int seen = 0;
        for (int i = 0; i < ngtop; i++) if (gtop[i].folder[0] && !strcmp(gtop[i].folder, gpar[c])) seen = 1;
        if (!seen) { gtop[ngtop].cat = c; snprintf(gtop[ngtop].folder, sizeof gtop[0].folder, "%s", gpar[c]); ngtop++; }
    }
}
static int gf_n(const char *folder) { int n = 0; for (int c = 0; c < ngcat; c++) if (!strcmp(gpar[c], folder)) n++; return n; }
static int gf_idx(const char *folder, int k) { for (int c = 0; c < ngcat; c++) if (!strcmp(gpar[c], folder) && k-- == 0) return c; return -1; }
static int gc_n(int c) { int n = 0; for (int i = 0; i < ngent; i++) if (gents[i].cat == c) n++; return n; }
static int gc_idx(int c, int k) { for (int i = 0; i < ngent; i++) if (gents[i].cat == c && k-- == 0) return i; return -1; }
static int g_cur_cat = 0;
static void lbl_gcoll(int i, char *m, size_t mc, char *s, size_t sc, int *tone) {
    (void)tone;
    int g = gc_idx(g_cur_cat, i);
    snprintf(m, mc, "%s", g >= 0 ? gents[g].title : "?");
    if (g >= 0 && gents[g].search) snprintf(s, sc, "show");
}
static const char *thumb_gcoll(int i) { int g = gc_idx(g_cur_cat, i); return g >= 0 && !gents[g].search ? thumb_of_item(gents[g].id) : 0; }
static void screen_guide_cat(int cat) {
    int sel = 0;
    const char *status = "";
    for (;;) {
        g_cur_cat = cat;
        int n = gc_n(cat);
        g_thumb_fn = thumb_gcoll;
        int k = list_loop(gcats[cat], n, lbl_gcoll, "A:Open  Y:Save  B:Back", &sel, "No collections here.", &status);
        if (k == K_START) k = K_A;
        if (k == K_B || k == K_MENU) return;
        int g = gc_idx(cat, sel);
        if (g < 0) continue;
        if (k == K_Y) { status = save_msg(chan_add(gents[g].search ? "search" : "collection", gents[g].id, gents[g].title)); continue; }
        status = "";
        if (k != K_A) continue;
        Chan c; memset(&c, 0, sizeof c);
        c.type = gents[g].search ? CH_SEARCH : CH_COLLECTION;
        snprintf(c.arg, sizeof c.arg, "%s", gents[g].id);
        snprintf(c.title, sizeof c.title, "%s", gents[g].title);
        screen_collection(&c);
    }
}

static const char *g_cur_folder = "";
static void lbl_gfolder(int i, char *m, size_t mc, char *s, size_t sc, int *tone) {
    (void)tone;
    int c = gf_idx(g_cur_folder, i);
    snprintf(m, mc, "%s", c >= 0 ? gcats[c] : "?");
    if (c >= 0) snprintf(s, sc, "%d", gc_n(c));
}
static void screen_guide_folder(const char *folder) {
    int sel = 0;
    char title[40]; snprintf(title, sizeof title, "%s", folder);
    for (;;) {
        g_cur_folder = title;
        int k = list_loop(title, gf_n(title), lbl_gfolder, "A:Open  B:Back", &sel, "Empty.", 0);
        if (k == K_START) k = K_A;
        if (k == K_B || k == K_MENU) return;
        if (k != K_A) continue;
        int c = gf_idx(title, sel);
        if (c >= 0) screen_guide_cat(c);
    }
}

/* ---------- home tabs ---------- */
static void lbl_tab_search(int i, char *m, size_t mc, char *s, size_t sc, int *tone) {
    if (i == 0) { snprintf(m, mc, "Search videos..."); *tone = 4; return; }
    if (i == 1) { snprintf(m, mc, "Search collections..."); *tone = 4; return; }
    l1(m, mc, hist[i - 2] + 2);
    snprintf(s, sc, "%s", hist[i - 2][0] == 'c' ? "colls" : "videos");
}
static void lbl_tab_guide(int i, char *m, size_t mc, char *s, size_t sc, int *tone) {
    (void)tone;
    if (i >= ngtop) return;
    if (gtop[i].folder[0]) { snprintf(m, mc, "%s", gtop[i].folder); snprintf(s, sc, "%d >", gf_n(gtop[i].folder)); return; }
    snprintf(m, mc, "%s", gcats[gtop[i].cat]);
    snprintf(s, sc, "%d", gc_n(gtop[i].cat));
}
static void lbl_tab_saved(int i, char *m, size_t mc, char *s, size_t sc, int *tone) {
    Chan *c = &chans[i];
    snprintf(m, mc, "%s", c->title);
    snprintf(s, sc, "%s", c->type == CH_ITEM ? "show" : c->type == CH_COLLECTION ? "coll." : "search");
    if (c->type == CH_SEARCH || c->type == CH_CSEARCH) *tone = 3;
}
static const char *thumb_dfile(int i) {   /* Files tab: downloaded videos only */
    static char u[2][320]; static int k = 0;
    i -= nq + npart;
    if (i < 0 || i >= ndf) return 0;
    k ^= 1; snprintf(u[k], sizeof u[k], "local:%s", dfiles[i].name);
    return u[k];
}
static const char *thumb_saved(int i) { return i < nchan && (chans[i].type == CH_ITEM || chans[i].type == CH_COLLECTION) ? thumb_of_item(chans[i].arg) : 0; }
static void lbl_tab_lists(int i, char *m, size_t mc, char *s, size_t sc, int *tone) {
    (void)s; (void)sc;
    if (i == 0) { snprintf(m, mc, "+ New playlist..."); *tone = 4; return; }
    l1(m, mc, pls[i - 1]);
}
/* Files tab: queue entries first, then what's on the card */
static void lbl_tab_files(int i, char *m, size_t mc, char *s, size_t sc, int *tone) {
    if (i < nq) {
        snprintf(m, mc, "%s", Q[i].disp);
        if (i == q_active && DLQ.state == 1) { snprintf(s, sc, "%d%%", DLQ.total > 0 ? (int)(100.0 * DLQ.got / DLQ.total) : 0); *tone = 2; }
        else if (Q[i].failed == 2) {
            char part[960]; snprintf(part, sizeof part, "%s.part", Q[i].dst);
            long long h = file_size(part); char sz[16]; fmt_size(sz, sizeof sz, h > 0 ? h : 0);
            snprintf(s, sc, "paused %s", sz); *tone = 3;
        }
        else if (Q[i].failed) { snprintf(s, sc, "failed"); *tone = 2; }
        else { snprintf(s, sc, "queued"); *tone = 3; }
        return;
    }
    i -= nq;
    if (i < npart) {
        PartF *pf = &parts[i];
        snprintf(m, mc, "%s", pf->disp);
        if (pf->total > 0) snprintf(s, sc, "paused %d%%", (int)(100.0 * pf->have / pf->total));
        else { char sz[16]; fmt_size(sz, sizeof sz, pf->have); snprintf(s, sc, "paused %s", sz); }
        *tone = 3;
        return;
    }
    i -= npart;
    if (i < 0 || i >= ndf) { snprintf(m, mc, "..."); *tone = 3; return; }
    snprintf(m, mc, "%s", dfiles[i].disp);
    fmt_size(s, sc, dfiles[i].size);
    if (resume_get(dfiles[i].name) > 0) *tone = 1;
}

static void lbl_plentry(int i, char *m, size_t mc, char *s, size_t sc, int *tone) {
    char sn[300], p[1000];
    snprintf(m, mc, "%d. %s", i + 1, ple[i].disp);
    if (!strcmp(ple[i].id, "local")) snprintf(p, sizeof p, "%s/%s", viddir, ple[i].name);
    else local_path(p, sizeof p, sn, sizeof sn, ple[i].id, ple[i].name);
    if (access(p, F_OK) == 0) { snprintf(s, sc, "on card"); *tone = 1; }
    else if (q_find(p) >= 0) { snprintf(s, sc, "queued"); *tone = 3; }
}
static void screen_playlist(const char *name) {
    char title[80]; l1(title, sizeof title, name);
    int sel = 0, armed = -1;
    const char *status = "";
    for (;;) {
        pl_load(name);
        int k = list_loop(title, nple, lbl_plentry, "A:Play  Y:Queue all  X:Remove  B:Back", &sel, "Empty playlist.\nAdd videos from any show.", &status);
        if (k == K_START) k = K_A;
        if (k == K_B || k == K_MENU) return;
        if (k == K_X && nple) {
            if (armed == sel) { memmove(&ple[sel], &ple[sel + 1], sizeof(PE) * (size_t)(nple - sel - 1)); nple--; pl_save(name); armed = -1; status = "Removed."; }
            else { armed = sel; status = "Press X again to remove it"; }
            continue;
        }
        armed = -1; status = "";
        if (k == K_Y) {
            int added = 0;
            for (int i = 0; i < nple; i++) {
                char sn[300], p[1000], url[2200];
                if (!strcmp(ple[i].id, "local")) continue;
                local_path(p, sizeof p, sn, sizeof sn, ple[i].id, ple[i].name);
                if (access(p, F_OK) == 0) continue;
                dl_url(url, sizeof url, ple[i].id, ple[i].name);
                if (q_add(url, p, ple[i].disp) == 0) added++;
            }
            queue_tick();
            static char msg[48]; snprintf(msg, sizeof msg, added ? "%d added to the download queue" : "Everything is already queued", added);
            status = msg;
            continue;
        }
        if (k == K_A && nple) play_list_from(sel, title);
    }
}

/* ---------------- updating the app itself ----------------
   update.txt on the miyoo-builds branch names the newest version and, for every file of the app, its CRC-32 and
   size, plus one fixed commit of that branch to take the files from:
       archivetv-update 1
       version 1.5
       base https://raw.githubusercontent.com/mroxfordyazuka/ArchiveTV/<commit>/App/ArchiveTV/
       file 1a2b3c4d 160628 archivetv
   Only the files that differ from the ones on the card are downloaded, into update/files/. Each is checked, and
   only when all are good are update/list and update/ready written. launch.sh moves them into place after the app
   has closed (a running program can't replace itself); exit code 42 makes it start the app again right away.
   Videos, playlists, saved shows, the queue and settings are never part of an update. */
#define UPD_URL  "https://raw.githubusercontent.com/mroxfordyazuka/ArchiveTV/miyoo-builds/update.txt"
#define UPD_BASE "https://raw.githubusercontent.com/mroxfordyazuka/ArchiveTV/"
#define UPD_MAX 80
typedef struct { unsigned crc; long long size; char path[120]; int need; } UFile;
static char upd_new[16];          /* a newer version we know is out, or "" */
static int upd_ready = 0;         /* an update is downloaded and waits for a restart */
static pid_t upd_pid = 0;         /* the quiet check started at launch */
static int upd_noted = 0;
static const char *upd_url(void) { const char *u = getenv("ATV_UPDATE_URL"); return u && u[0] ? u : UPD_URL; }   /* for testing */
static void upd_path(char *d, size_t cap, const char *rel) { snprintf(d, cap, "%s/update%s%s", appdir, rel[0] ? "/" : "", rel); }
static const char *app_version(void) {
    static char v[16] = "";
    if (v[0]) return v;
    snprintf(v, sizeof v, "%s", APP_VERSION);
    char p[760]; upd_path(p, sizeof p, "pretend-version");   /* testing: act like an older version */
    FILE *f = fopen(p, "r");
    if (f) { char b[16]; if (fgets(b, sizeof b, f)) { b[strcspn(b, "\r\n ")] = 0; if (b[0]) snprintf(v, sizeof v, "%s", b); } fclose(f); }
    return v;
}
static int ver_cmp(const char *a, const char *b) {
    for (int i = 0; i < 4; i++) {
        long x = strtol(a, (char **)&a, 10), y = strtol(b, (char **)&b, 10);
        if (x != y) return x < y ? -1 : 1;
        if (*a == '.') a++;
        if (*b == '.') b++;
    }
    return 0;
}
static unsigned crc_tab[256];
static unsigned crc32_buf(unsigned c, const unsigned char *p, size_t n) {
    if (!crc_tab[1]) for (unsigned i = 0; i < 256; i++) { unsigned r = i; for (int k = 0; k < 8; k++) r = r & 1 ? 0xEDB88320u ^ (r >> 1) : r >> 1; crc_tab[i] = r; }
    c = ~c;
    while (n--) c = crc_tab[(c ^ *p++) & 255] ^ (c >> 8);
    return ~c;
}
static int crc_file(const char *path, unsigned *crc, long long *size) {
    FILE *f = fopen(path, "rb"); if (!f) return -1;
    static unsigned char buf[65536]; unsigned c = 0; long long n = 0; size_t k;
    while ((k = fread(buf, 1, sizeof buf, f)) > 0) { c = crc32_buf(c, buf, k); n += (long long)k; }
    fclose(f); *crc = c; *size = n;
    return 0;
}
/* files of the user's own that an update may never write, whatever the list says */
static int upd_path_ok(const char *p) {
    if (!p[0] || p[0] == '/' || strstr(p, "..")) return 0;
    for (const char *c = p; *c; c++) if (!((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') || *c == '.' || *c == '_' || *c == '-' || *c == '/')) return 0;
    static const char *const MINE[] = { "settings.txt", "queue.txt", "searches.txt", "channels.txt", "log.txt", "log.txt.old", "music/music.raw", "update-failed.txt", 0 };
    for (int i = 0; MINE[i]; i++) if (!strcmp(p, MINE[i])) return 0;
    if (!strncmp(p, "videos/", 7) || !strncmp(p, "playlists/", 10) || !strncmp(p, "update/", 7)) return 0;
    return 1;
}
/* reads update.txt; returns the number of files, or -1 when it isn't a list we trust */
static int upd_parse(char *txt, char *ver, size_t vc, char *base, size_t bc, UFile *F) {
    int n = 0, head = 0; ver[0] = base[0] = 0;
    int test = getenv("ATV_UPDATE_URL") && getenv("ATV_UPDATE_URL")[0];
    for (char *line = txt; line && *line; ) {
        char *nl = strchr(line, '\n'); if (nl) *nl = 0;
        line[strcspn(line, "\r")] = 0;
        if (!strcmp(line, "archivetv-update 1")) head = 1;
        else if (!strncmp(line, "version ", 8)) snprintf(ver, vc, "%s", line + 8);
        else if (!strncmp(line, "base ", 5)) snprintf(base, bc, "%s", line + 5);
        else if (!strncmp(line, "file ", 5) && n < UPD_MAX) {
            char *e; unsigned crc = (unsigned)strtoll(line + 5, &e, 16); long long sz = strtoll(e, &e, 10);
            while (*e == ' ') e++;
            if (sz < 0 || !upd_path_ok(e) || strlen(e) >= sizeof F[0].path) { logf_("update: refusing file line '%s'", line); return -1; }
            F[n].crc = crc; F[n].size = sz; F[n].need = 0; snprintf(F[n].path, sizeof F[n].path, "%s", e); n++;
        }
        line = nl ? nl + 1 : 0;
    }
    if (!head || !ver[0] || !base[0] || !n) { logf_("update: list is incomplete"); return -1; }
    if (!test && strncmp(base, UPD_BASE, strlen(UPD_BASE))) { logf_("update: base %s is not ours", base); return -1; }
    return n;
}
static void mkdirs(const char *path) {   /* makes the folders a file path needs */
    char d[900]; snprintf(d, sizeof d, "%s", path);
    for (char *c = d + 1; *c; c++) if (*c == '/') { *c = 0; mkdir(d, 0755); *c = '/'; }
}
static void rm_rf(const char *path) {
    pid_t p = fork();
    if (p == 0) { execl("/bin/rm", "rm", "-rf", path, (char *)0); _exit(127); }
    if (p > 0) waitpid(p, 0, 0);
}
/* at launch: fetch update.txt quietly in the background; upd_poll looks at it later */
static char upd_bad[16];          /* a version that was installed but didn't start, and was put back */
static void upd_start_check(void) {
    char p[760];
    /* launch.sh just installed an update, and here we are: it works. Keep it (drop the backup of the old files).
       Every version must do this, or launch.sh puts the old one back when it closes. */
    upd_path(p, sizeof p, "trial");
    if (access(p, F_OK) == 0) {
        upd_path(p, sizeof p, ""); rm_rf(p);
        snprintf(p, sizeof p, "%s/update-failed.txt", appdir); unlink(p);
        logf_("update: now running %s, keeping it", app_version());
    }
    snprintf(p, sizeof p, "%s/update-failed.txt", appdir);
    { FILE *f = fopen(p, "r"); if (f) { if (fgets(upd_bad, sizeof upd_bad, f)) upd_bad[strcspn(upd_bad, "\r\n ")] = 0; fclose(f); } }
    upd_path(p, sizeof p, "ready"); upd_ready = access(p, F_OK) == 0;
    const char *v = getenv("VERIFY");
    if (!has_wifi || (v && !strcmp(v, "0")) || access(CURL_BIN, F_OK) != 0) return;
    upd_path(p, sizeof p, ""); mkdir(p, 0755);
    static char cmd[3000]; char out[800], qo[900], qu[600], tls[900];
    upd_path(out, sizeof out, "latest.txt"); unlink(out);
    shq(qo, sizeof qo, out); shq(qu, sizeof qu, upd_url()); curl_tls(tls, sizeof tls);
    snprintf(cmd, sizeof cmd, "%s exec %s -sSL --fail --connect-timeout 15 --max-time 40 -A ArchiveTV-Miyoo %s -o %s %s",
             CURL_ENV, CURL_BIN, tls, qo, qu);
    pid_t pid = fork();
    if (pid == 0) { int nul = open("/dev/null", 1); if (nul >= 0) { dup2(nul, 1); dup2(nul, 2); } execl("/bin/sh", "sh", "-c", cmd, (char *)0); _exit(127); }
    if (pid > 0) upd_pid = pid;
}
static void upd_poll(void) {
    if (upd_pid <= 0) return;
    int st = 0;
    if (waitpid(upd_pid, &st, WNOHANG) != upd_pid) return;
    upd_pid = 0;
    if ((st & 0x7f) || ((st >> 8) & 255)) { logf_("update check: no answer (curl %d)", (st >> 8) & 255); return; }
    char p[760]; upd_path(p, sizeof p, "latest.txt");
    FILE *f = fopen(p, "r"); if (!f) return;
    char line[200], v[16] = ""; int head = 0;
    for (int i = 0; i < 4 && fgets(line, sizeof line, f); i++) {
        line[strcspn(line, "\r\n")] = 0;
        if (!strcmp(line, "archivetv-update 1")) head = 1;
        else if (!strncmp(line, "version ", 8)) snprintf(v, sizeof v, "%s", line + 8);
    }
    fclose(f); unlink(p);
    if (head && v[0] && !strcmp(v, upd_bad)) logf_("update check: %s is out, but it didn't start here before", v);
    else if (head && v[0] && ver_cmp(v, app_version()) > 0) { snprintf(upd_new, sizeof upd_new, "%s", v); logf_("update check: %s is out (have %s)", v, app_version()); }
    else logf_("update check: up to date (%s)", app_version());
}
static const char *upd_note(void) {   /* once per run: the toast that tells about a new version */
    if (upd_noted || !upd_new[0] || upd_ready) return 0;
    upd_noted = 1;
    static char m[64]; snprintf(m, sizeof m, "Version %s is out: see Settings", upd_new);
    return m;
}
/* downloads one file of the update, resuming a half-finished one. 1 = good, 0 = failed, -1 = cancelled */
static int upd_fetch(const char *base, UFile *u, long long done, long long total, int idx, int count, const char *ver) {
    char dst[900], rel[200], url[600]; snprintf(rel, sizeof rel, "files/%s", u->path); upd_path(dst, sizeof dst, rel);
    snprintf(url, sizeof url, "%s%s", base, u->path);
    mkdirs(dst);
    if (u->size == 0) { FILE *z = fopen(dst, "wb"); if (!z) return 0; fclose(z); return 1; }   /* an empty marker file */
    unsigned c; long long sz;
    if (crc_file(dst, &c, &sz) == 0) {
        if (sz == u->size && c == u->crc) return 1;    /* already here from an earlier try */
        if (sz >= u->size) unlink(dst);                  /* damaged: start that one again */
    }
    static char cmd[3200]; char qd[1000], qu[700], tls[900];
    shq(qd, sizeof qd, dst); shq(qu, sizeof qu, url); curl_tls(tls, sizeof tls);
    snprintf(cmd, sizeof cmd, "%s exec %s -sSL --fail -C - --retry 3 --retry-delay 2 --connect-timeout 20 --speed-limit 1 --speed-time 60 -A ArchiveTV-Miyoo %s -o %s %s",
             CURL_ENV, CURL_BIN, tls, qd, qu);
    pid_t pid = fork();
    if (pid == 0) { execl("/bin/sh", "sh", "-c", cmd, (char *)0); _exit(127); }
    if (pid < 0) return 0;
    char name[64]; snprintf(name, sizeof name, "ArchiveTV %s (%d of %d)", ver, idx + 1, count);
    double t0 = now_s(), tl = 0, rate = 0; long long g0 = 0; int cancel = 0, st = 0;
    for (;;) {
        pid_t r = waitpid(pid, &st, WNOHANG);
        long long have = file_size(dst); if (have < 0) have = 0;
        long long got = done + have;
        double t = now_s();
        if (t - tl > 0.25) { if (tl > 0) { double inst = (got - g0) / (t - tl); rate = rate > 0 ? rate * 0.8 + inst * 0.2 : inst; } g0 = got; tl = t; }
        if (r == pid) break;
        SDL_Event e;
        while (SDL_PollEvent(&e)) if (e.type == SDL_KEYDOWN && (e.key.keysym.sym == K_B || e.key.keysym.sym == K_MENU)) cancel = 1;
        if (cancel) { kill(pid, SIGTERM); waitpid(pid, &st, 0); return -1; }   /* what's here so far is kept, to resume */
        char eta[24] = ""; if (rate > 0) fmt_time(eta, sizeof eta, (total - got) / rate);
        ui_download(name, got, total, t - t0 > 1 ? rate : 0, eta, 0, t, "B:Stop");
        present(back);
        usleep(40000);
    }
    int code = (st & 0x7f) ? -1 : ((st >> 8) & 255);
    if (crc_file(dst, &c, &sz) != 0 || sz != u->size || c != u->crc) {
        logf_("update: %s came down wrong (curl %d, %lld of %lld bytes, crc %08x want %08x)", u->path, code, sz, u->size, c, u->crc);
        if (sz >= u->size) unlink(dst);
        return 0;
    }
    return 1;
}
static void app_update(void) {
    if (upd_ready) {
        int k = ask("Update ready", "Restart to finish updating.", "Videos and settings are kept.", MOOD_HAPPY, "A:Restart now  B:Later");
        if (k == K_A) { g_exit_code = 42; g_quit = 1; }
        return;
    }
    if (!has_wifi) { error_box("No Wi-Fi on this Miyoo.", "Updating needs the internet."); return; }
    const char *vf = getenv("VERIFY");
    if (vf && !strcmp(vf, "0")) { error_box("The clock is wrong.", "Set the date, then try again."); return; }
    message("Updates", "Looking for a new version...", 0, "B:Cancel");
    int err = 0; char *txt = http_get(upd_url(), &err);
    LM.on = 0;
    if (!txt) { if (err != -5) error_box("Could not check for updates.", err == -6 ? "curl is missing." : "Is Wi-Fi on?"); return; }
    static UFile F[UPD_MAX]; char ver[16], base[400];
    int n = upd_parse(txt, ver, sizeof ver, base, sizeof base, F);
    free(txt);
    if (n < 0) { error_box("The update list looks wrong.", "Try again later."); return; }
    if (ver_cmp(ver, app_version()) <= 0) {
        upd_new[0] = 0;
        char l2[48]; snprintf(l2, sizeof l2, "ArchiveTV %s", app_version());
        sfx(SFX_SELECT); ask("Up to date", "You have the newest version.", l2, MOOD_HAPPY, "A:OK");
        return;
    }
    if (!strcmp(ver, upd_bad)) {
        char l1[48]; snprintf(l1, sizeof l1, "Version %s didn't start here,", ver);
        if (ask("Update", l1, "so it was undone. Try again?", MOOD_SAD, "A:Try again  B:Not now") != K_A) { sfx(SFX_BACK); return; }
    }
    snprintf(upd_new, sizeof upd_new, "%s", ver);
    message_m("Updates", "Comparing files...", 0, MOOD_STATIC, 0);
    int need = 0; long long total = 0;
    for (int i = 0; i < n; i++) {
        char p[900]; snprintf(p, sizeof p, "%s/%s", appdir, F[i].path);
        unsigned c; long long sz;
        F[i].need = !(crc_file(p, &c, &sz) == 0 && sz == F[i].size && c == F[i].crc);
        if (F[i].need) { need++; total += F[i].size; }
    }
    char l1[80], l2[80], szs[16]; fmt_size(szs, sizeof szs, total);
    snprintf(l1, sizeof l1, "ArchiveTV %s is out!", ver);
    snprintf(l2, sizeof l2, "Download %d file%s (%s)?", need, need == 1 ? "" : "s", szs);
    sfx(SFX_SELECT);
    if (ask("Update", l1, l2, MOOD_WOW, "A:Update  B:Not now") != K_A) { sfx(SFX_BACK); return; }
    /* files left from a download of some other version must not get mixed in */
    char dir[800], mark[800]; upd_path(dir, sizeof dir, "files"); upd_path(mark, sizeof mark, "for");
    { char was[16] = ""; FILE *m = fopen(mark, "r"); if (m) { if (!fgets(was, sizeof was, m)) was[0] = 0; fclose(m); was[strcspn(was, "\r\n")] = 0; }
      if (strcmp(was, ver)) { rm_rf(dir); m = fopen(mark, "w"); if (m) { fprintf(m, "%s\n", ver); fclose(m); } } }
    mkdir(dir, 0755);
    long long done = 0; int idx = 0;
    for (int i = 0; i < n; i++) {
        if (!F[i].need) continue;
        int r = upd_fetch(base, &F[i], done, total, idx, need, ver);
        if (r < 0) { sfx(SFX_BACK); ask("Update paused", "It picks up where it left off", "next time you choose Update.", MOOD_SLEEP, "A:OK"); return; }
        if (r == 0) { error_box("A file didn't download right.", "Try again to resume it."); return; }
        done += F[i].size; idx++;
    }
    char lp[800]; upd_path(lp, sizeof lp, "list.tmp");
    FILE *L = fopen(lp, "w");
    if (!L) { error_box("Could not write to the SD card.", 0); return; }
    for (int i = 0; i < n; i++) if (F[i].need) fprintf(L, "%s\n", F[i].path);
    fclose(L);
    char lf[800]; upd_path(lf, sizeof lf, "list"); rename(lp, lf);
    char rp[800]; upd_path(rp, sizeof rp, "ready");
    FILE *R = fopen(rp, "w"); if (R) { fprintf(R, "%s\n", ver); fclose(R); }
    sync();
    upd_ready = 1;
    logf_("update: %s downloaded (%d files, %lld bytes), waiting for launch.sh", ver, need, total);
    sfx(SFX_HELLO);
    int k = ask("Update ready", "Restart to finish updating.", "Videos and settings are kept.", MOOD_HAPPY, "A:Restart now  B:Later");
    if (k == K_A) { g_exit_code = 42; g_quit = 1; }
}

/* ---- preview pictures for the lists (Settings > Thumbnails) ----
   archive.org/services/img/<id> is a small JPEG for most items (collections often have a PNG or GIF logo; those
   are skipped). One is fetched at a time, only after the cursor has rested on a row, and kept for this run. */
static Thumb *TC;
static int th_slot = -1; static double th_started;
static void *th_cc; static AVFrame *th_fr;
static int thumb_ok_id(const char *id) {
    if (!id || !id[0] || strlen(id) >= 120) return 0;
    for (const char *c = id; *c; c++) if (!((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') || *c == '.' || *c == '_' || *c == '-')) return 0;
    return 1;
}
static void thumb_path(char *d, size_t cap) { snprintf(d, cap, "/tmp/archivetv-thumb.jpg"); }
static int thumb_from_frame(Thumb *th, AVFrame *fr);
/* the fetched file -> TC[slot].px */
static int thumb_decode(Thumb *th, const char *path) {
    FILE *f = fopen(path, "rb"); if (!f) return -1;
    static uint8_t buf[256 * 1024 + 64];
    size_t n = fread(buf, 1, sizeof buf - 64, f); fclose(f);
    if (n < 4 || buf[0] != 0xFF || buf[1] != 0xD8) return -1;    /* not a JPEG */
    memset(buf + n, 0, 64);
    if (!th_cc) {
        const void *codec = avcodec_find_decoder_by_name("mjpeg");
        if (!codec) return -1;
        th_cc = avcodec_alloc_context3(codec);
        if (!th_cc || avcodec_open2(th_cc, codec, 0) < 0) { th_cc = 0; logf_("thumbs: no JPEG decoder"); return -1; }
        th_fr = av_frame_alloc();
    }
    AVPacket pk; av_init_packet(&pk); pk.data = buf; pk.size = (int)n;
    int got = 0;
    if (avcodec_decode_video2(th_cc, th_fr, &got, &pk) < 0 || !got) return -1;
    return thumb_from_frame(th, th_fr);
}
/* a decoded picture -> th->px, scaled down to fit TH_MAXW x TH_MAXH (shape kept, square pixels assumed) */
static int thumb_from_frame(Thumb *th, AVFrame *fr) {
    int cx, cy, gray = 0, full = 1;
    switch (fr->format) {
    case 0: cx = 1; cy = 1; full = 0; break;  case 12: cx = 1; cy = 1; break;
    case 4: cx = 1; cy = 0; full = 0; break;  case 13: cx = 1; cy = 0; break;
    case 5: cx = 0; cy = 0; full = 0; break;  case 14: cx = 0; cy = 0; break;
    case 8: cx = cy = 0; gray = 1; break;
    default: return -1;
    }
    int sw = fr->width, sh = fr->height;
    if (sw <= 0 || sh <= 0) return -1;
    int dw = TH_MAXW, dh = sh * TH_MAXW / sw;
    if (dh > TH_MAXH) { dh = TH_MAXH; dw = sw * TH_MAXH / sh; }
    if (dw < 1) dw = 1;
    if (dh < 1) dh = 1;
    const int *yt = ytab[full], *rv = rvt[full], *gu = gut[full], *gv = gvt[full], *bu = but_[full];
    const uint8_t *cl = clp + 512;
    for (int j = 0; j < dh; j++) {
        int sy = j * sh / dh;
        const uint8_t *Y = fr->data[0] + sy * fr->linesize[0];
        const uint8_t *U = gray ? 0 : fr->data[1] + (sy >> cy) * fr->linesize[1], *V = gray ? 0 : fr->data[2] + (sy >> cy) * fr->linesize[2];
        for (int i = 0; i < dw; i++) {
            int sx = i * sw / dw, yy = yt[Y[sx]];
            if (gray) { int g = cl[yy >> 8]; th->px[j * dw + i] = RGB(g, g, g); continue; }
            int u = U[sx >> cx], v = V[sx >> cx];
            th->px[j * dw + i] = ((uint32_t)cl[(yy + rv[v]) >> 8] << 16) | ((uint32_t)cl[(yy + gu[u] + gv[v]) >> 8] << 8) | cl[(yy + bu[u]) >> 8];
        }
    }
    th->w = dw; th->h = dh;
    return 0;
}
/* downloaded videos: a frame from 10% in (at most 2 minutes), grabbed by a child process so the menus keep
   moving, and kept as videos/.thumbs/<file>.rgb for next time */
static void local_thumb_path(char *d, size_t cap, const char *name) { snprintf(d, cap, "%s/.thumbs/%s.rgb", viddir, name); }
static int local_thumb_load(Thumb *th, const char *name) {
    char p[1000]; local_thumb_path(p, sizeof p, name);
    FILE *f = fopen(p, "rb"); if (!f) return -1;
    int hd[3] = { 0 }, ok = fread(hd, sizeof hd, 1, f) == 1 && hd[0] == 0x54565441 && hd[1] > 0 && hd[2] > 0 && hd[1] <= TH_MAXW && hd[2] <= TH_MAXH
                 && fread(th->px, 4, (size_t)(hd[1] * hd[2]), f) == (size_t)(hd[1] * hd[2]);
    fclose(f);
    if (!ok) { unlink(p); return -1; }
    th->w = hd[1]; th->h = hd[2];
    return 0;
}
static void local_thumb_grab(const char *name) {   /* runs in the child; its exit code says how it went */
    static Thumb tmp;
    char v[1000]; snprintf(v, sizeof v, "%s/%s", viddir, name);
    void *fc = 0;
    if (avformat_open_input(&fc, v, 0, 0) < 0) _exit(2);
    if (avformat_find_stream_info(fc, 0) < 0) _exit(3);
    void *dec = 0;
    int vi = av_find_best_stream(fc, AVMEDIA_TYPE_VIDEO, -1, -1, &dec, 0);
    if (vi < 0 || !dec) _exit(4);
    void *cc = ST_CODEC(FC_STREAMS(fc)[vi]);
    av_opt_set_int(cc, "threads", 1, 0);
    if (avcodec_open2(cc, dec, 0) < 0) _exit(5);
    int64_t d = FC_DURATION(fc), at = d > 0 ? d / 10 : 0;
    if (at > 120LL * 1000000) at = 120LL * 1000000;
    if (at > 0) av_seek_frame(fc, -1, at, AVSEEK_FLAG_BACKWARD);
    AVFrame *fr = av_frame_alloc(); AVPacket pk; int got = 0;
    for (int i = 0; i < 600 && !got; i++) {
        if (av_read_frame(fc, &pk) < 0) break;
        if (pk.stream_index == vi) avcodec_decode_video2(cc, fr, &got, &pk);
        av_free_packet(&pk);
    }
    if (!got || thumb_from_frame(&tmp, fr) != 0) _exit(6);
    char p[1000], q[1010]; snprintf(p, sizeof p, "%s/.thumbs", viddir); mkdir(p, 0755);
    local_thumb_path(p, sizeof p, name); snprintf(q, sizeof q, "%s.tmp", p);
    FILE *f = fopen(q, "wb"); if (!f) _exit(7);
    int hd[3] = { 0x54565441, tmp.w, tmp.h };
    int ok = fwrite(hd, sizeof hd, 1, f) == 1 && fwrite(tmp.px, 4, (size_t)(tmp.w * tmp.h), f) == (size_t)(tmp.w * tmp.h);
    if (fclose(f) != 0 || !ok || rename(q, p) != 0) { unlink(q); _exit(8); }
    _exit(0);
}
static void local_thumb_drop(const char *name) { char p[1000]; local_thumb_path(p, sizeof p, name); unlink(p); }
static void th_poll(void) {
    if (th_pid <= 0) return;
    int st = 0;
    if (waitpid(th_pid, &st, WNOHANG) != th_pid) {
        if (now_s() - th_started < 25) return;
        kill(th_pid, SIGKILL); waitpid(th_pid, &st, 0);   /* stuck: give up on this one */
        logf_("thumbs: gave up on %s", TC[th_slot].id);
    }
    th_pid = 0;
    Thumb *th = &TC[th_slot];
    int ok = !(st & 0x7f) && !((st >> 8) & 255);
    if (!strncmp(th->id, "local:", 6)) {
        ok = ok && local_thumb_load(th, th->id + 6) == 0;
        if (!ok) logf_("thumbs: no frame from %s (%d)", th->id + 6, (st & 0x7f) ? 128 + (st & 0x7f) : (st >> 8) & 255);
    } else {
        char p[64]; thumb_path(p, sizeof p);
        ok = ok && thumb_decode(th, p) == 0;
        unlink(p);
    }
    th->state = ok ? 2 : 3;
}
/* the picture for id, once it's here; asks for it if it isn't */
/* the Internet Archive picture of an item or collection */
static const char *thumb_of_item(const char *id) {
    static char u[8][200]; static int k = 0;
    if (!thumb_ok_id(id)) return 0;
    k = (k + 1) & 7; snprintf(u[k], sizeof u[k], "https://archive.org/services/img/%s", id);
    return u[k];
}
/* the picture at this address (https://archive.org/...): its slot once asked for (state 1 = on its way,
   2 = here, 3 = there's none), else 0 with *waiting = 1 while another fetch runs. Starts a fetch if none is running. */
static Thumb *thumb_want(const char *id, int *waiting) {
    *waiting = 0;
    int local = id && !strncmp(id, "local:", 6);
    if (!g_thumbs || !id || strlen(id) >= sizeof TC[0].id) return 0;
    if (!local && (!has_wifi || strncmp(id, "https://archive.org/", 20) || strchr(id, '\''))) return 0;
    if (!TC) { TC = (Thumb *)calloc(TH_SLOTS, sizeof(Thumb)); if (!TC) return 0; }
    double t = now_s();
    for (int i = 0; i < TH_SLOTS; i++)
        if (TC[i].state && !strcmp(TC[i].id, id)) { TC[i].used = t; return &TC[i]; }
    if (!local && access(CURL_BIN, F_OK) != 0) return 0;
    if (th_pid > 0) { *waiting = 1; return 0; }   /* one at a time */
    int s = 0;
    for (int i = 0; i < TH_SLOTS; i++) { if (!TC[i].state) { s = i; break; } if (TC[i].used < TC[s].used) s = i; }
    Thumb *th = &TC[s];
    snprintf(th->id, sizeof th->id, "%s", id); th->state = 1; th->used = t; th->w = th->h = 0;
    if (local) {
        if (local_thumb_load(th, id + 6) == 0) { th->state = 2; return th; }
        pid_t pid = fork();
        if (pid == 0) { int nul = open("/dev/null", 1); if (nul >= 0) { dup2(nul, 1); dup2(nul, 2); } local_thumb_grab(id + 6); }
        if (pid > 0) { th_pid = pid; th_slot = s; th_started = t; } else th->state = 3;
        return th;
    }
    static char cmd[2600]; char out[64], qo[100], tls[900];
    thumb_path(out, sizeof out); unlink(out); shq(qo, sizeof qo, out); curl_tls(tls, sizeof tls);
    snprintf(cmd, sizeof cmd, "%s exec %s -sSL --fail --connect-timeout 10 --max-time 20 --max-filesize 262144 -A ArchiveTV-Miyoo %s -o %s '%s'",
             CURL_ENV, CURL_BIN, tls, qo, id);
    pid_t pid = fork();
    if (pid == 0) { int nul = open("/dev/null", 1); if (nul >= 0) { dup2(nul, 1); dup2(nul, 2); } execl("/bin/sh", "sh", "-c", cmd, (char *)0); _exit(127); }
    if (pid > 0) { th_pid = pid; th_slot = s; th_started = t; } else th->state = 3;
    return th;
}

/* ---------- settings + about ---------- */
static void lbl_tab_settings(int i, char *m, size_t mc, char *s, size_t sc, int *tone) {
    switch (i) {
    case 0: snprintf(m, mc, "Music"); snprintf(s, sc, "%s", g_music ? "On" : "Off"); break;
    case 1: snprintf(m, mc, "Menu sounds"); snprintf(s, sc, "%s", g_sounds ? "On" : "Off"); break;
    case 2: snprintf(m, mc, "Scanlines"); snprintf(s, sc, "%s", g_fx ? "On" : "Off"); break;
    case 3: snprintf(m, mc, "Theme"); snprintf(s, sc, "%s", ui_theme_name(g_theme)); break;
    case 4: snprintf(m, mc, "Vsync (no tearing)"); snprintf(s, sc, "%s", g_vsync ? "On" : "Off"); break;
    case 5: snprintf(m, mc, "Group episodes of a show"); snprintf(s, sc, "%s", g_group ? "On" : "Off"); break;
    case 6: snprintf(m, mc, "Turn off after downloads"); snprintf(s, sc, "%s", g_autooff ? "On" : "Off"); break;
    case 7: snprintf(m, mc, "Thumbnails"); snprintf(s, sc, "%s", g_thumbs ? "On" : "Off"); break;
    case 8: upd_poll(); snprintf(m, mc, "Update ArchiveTV...");
            if (upd_ready) { snprintf(s, sc, "Restart to finish"); *tone = 1; }
            else if (upd_new[0]) { snprintf(s, sc, "%s is out!", upd_new); *tone = 1; }
            else snprintf(s, sc, "v%s", app_version()); break;
    default: snprintf(m, mc, "About ArchiveTV"); *tone = 4; break;
    }
}
static void screen_about(void) {
    flush_keys();
    for (;;) {
        ui_about(now_s());
        present(back);
        SDL_Event e; int done = 0;
        while (SDL_PollEvent(&e)) if (e.type == SDL_KEYDOWN) done = 1;
        if (done) { sfx(SFX_BACK); return; }
        usleep(40000);
    }
}
static void settings_action(int i) {
    switch (i) {
    case 0: g_music = !g_music; menu_audio_apply(); break;
    case 1: g_sounds = !g_sounds; menu_audio_apply(); break;
    case 2: g_fx = g_fx ? 0 : 1; fx_load(g_fx); break;
    case 3: g_theme = (g_theme + 1) % ui_theme_count(); ui_set_theme(g_theme); break;
    case 4: {
        SDL_Surface *ns = video_mode(!g_vsync);
        if (ns) { scr = ns; g_vsync = !g_vsync; }
        else scr = video_mode(g_vsync);   /* keep what worked */
        break;
    }
    case 5: g_group = !g_group; break;
    case 6: g_autooff = !g_autooff; g_off_armed = 0; break;
    case 7: g_thumbs = !g_thumbs; break;
    case 8: app_update(); return;
    default: screen_about(); return;
    }
    sfx(SFX_TOGGLE);
    settings_save(-1);
}

static int tab_count(int tab) {
    switch (tab) {
    case 0: return nhist + 2;
    case 1: return ngtop;
    case 2: return nchan;
    case 3: return npl + 1;
    case 4: return nq + npart + ndf;
    default: return 10;
    }
}
static const char *FOOT[6] = {
    "A:Menu  START:Go  L/R:Tab",
    "A:Open  L/R:Tab",
    "A:Menu  START:Open  L/R:Tab",
    "A:Open  X:Delete  L/R:Tab",
    "A:Menu  START:Play  L/R:Tab",
    "A:Change  L/R:Tab",
};
static const char *EMPTY[6] = { 0, "No guide.txt found.", "Nothing saved yet.\nPress Y on a show to save it.", 0, "Nothing yet.\nDownloads show up here.", 0 };
static LabelFn TABFN[6] = { lbl_tab_search, lbl_tab_guide, lbl_tab_saved, lbl_tab_lists, lbl_tab_files, lbl_tab_settings };

static void home(void) {
    int tab = g_tab, sels[6] = { 0, 0, 0, 0, 0, 0 }, armed = -1;
    const char *status = "";
    for (;;) {
        hist_load(); pl_scan(); scan_downloads();
        int n = tab_count(tab);
        g_thumb_fn = tab == 2 ? thumb_saved : tab == 4 ? thumb_dfile : 0;
        int k = list_loop_t("ArchiveTV", n, TABFN[tab], FOOT[tab], &sels[tab], EMPTY[tab], &status, tab);
        int sel = sels[tab];
        if (k == K_L1 || k == K_R1) { tab = (tab + (k == K_R1 ? 1 : 5)) % 6; armed = -1; status = ""; settings_save(tab); continue; }
        if (k == K_MENU || g_quit) return;
        /* X = delete/remove with a second press to confirm */
        if (k == K_X) {
            int can = (tab == 0 && sel >= 2) || (tab == 2 && sel < nchan) || (tab == 3 && sel >= 1) || (tab == 4 && sel < n);
            if (tab == 4 && armed != sel && sel >= nq && sel < nq + npart) { armed = sel; status = "Press X again to delete the partial download"; continue; }
            if (!can) continue;
            if (armed != sel) { armed = sel; status = tab == 4 && sel < nq ? "Press X again to take it out of the queue" : "Press X again to confirm"; continue; }
            armed = -1; status = "Done.";
            if (tab == 0) hist_del(sel - 2);
            else if (tab == 2) chan_remove(sel);
            else if (tab == 3) { char p[800]; pl_path(p, sizeof p, pls[sel - 1]); unlink(p); sync(); }
            else if (tab == 4) {
                if (sel < nq) q_remove(sel, 1);
                else if (sel - nq < npart) part_delete(parts[sel - nq].dst);
                else if (sel - nq - npart < ndf) { int i = sel - nq - npart; char p[1000]; snprintf(p, sizeof p, "%s/%s", viddir, dfiles[i].name); unlink(p); resume_set(dfiles[i].name, 0); local_thumb_drop(dfiles[i].name); sync(); }
            }
            continue;
        }
        armed = -1; status = "";
        if (k == K_Y && tab == 0 && sel >= 2) {
            const char *h = hist[sel - 2];
            char t[160]; snprintf(t, sizeof t, "%s %s", h[0] == 'c' ? "Collections:" : "Search:", h + 2);
            status = save_msg(chan_add(h[0] == 'c' ? "csearch" : "search", h + 2, t));
            continue;
        }
        int quick = k == K_START;            /* START = do the obvious thing right away */
        if ((k != K_A && !quick) || !n) continue;
        if (tab == 5) { settings_action(sel); continue; }
        if (tab == 0) {
            char q[120] = "", typ = 'v';
            if (sel <= 1) {
                typ = sel == 0 ? 'v' : 'c';
                if (!keyboard(sel == 0 ? "Search videos" : "Search collections", q, sizeof q)) continue;
            } else {
                const char *h = hist[sel - 2];
                if (!quick) {
                    char title[130]; l1(title, sizeof title, h + 2);
                    const char *o[] = { "Search", "Save to Saved", "Forget", "Back" };
                    int c = menu_pick(title, o, 4);
                    if (c == 1) {
                        char t[160]; snprintf(t, sizeof t, "%s %s", h[0] == 'c' ? "Collections:" : "Search:", h + 2);
                        status = save_msg(chan_add(h[0] == 'c' ? "csearch" : "search", h + 2, t));
                        continue;
                    }
                    if (c == 2) { hist_del(sel - 2); status = "Forgotten."; continue; }
                    if (c != 0) continue;
                }
                typ = h[0]; snprintf(q, sizeof q, "%s", h + 2);
            }
            size_t L = strlen(q); while (L && q[L - 1] == ' ') q[--L] = 0;
            if (!L) continue;
            char hq[124]; snprintf(hq, sizeof hq, "%c %s", typ, q); hist_add(hq);
            sels[0] = 2;
            Chan c; memset(&c, 0, sizeof c);
            c.type = typ == 'c' ? CH_CSEARCH : CH_SEARCH;
            snprintf(c.arg, sizeof c.arg, "%s", q);
            char t[140]; snprintf(t, sizeof t, "%s %s", typ == 'c' ? "Collections:" : "Search:", q);
            l1(c.title, sizeof c.title, t);
            screen_collection(&c);
        } else if (tab == 1) { if (sel < ngtop) { if (gtop[sel].folder[0]) screen_guide_folder(gtop[sel].folder); else screen_guide_cat(gtop[sel].cat); } }
        else if (tab == 2) {
            if (sel >= nchan) continue;
            if (!quick) {
                const char *o[] = { "Open", "Remove from Saved", "Back" };
                int c = menu_pick(chans[sel].title, o, 3);
                if (c == 1) { chan_remove(sel); status = "Removed."; continue; }
                if (c != 0) continue;
            }
            Chan c = chans[sel]; open_chan(&c);
        } else if (tab == 3) {
            if (sel == 0) { char nm[64]; if (pl_new(nm, sizeof nm)) { pl_scan(); status = "Playlist made! Add videos from any show."; } }
            else { char nm[64]; snprintf(nm, sizeof nm, "%s", pls[sel - 1]); screen_playlist(nm); }
        } else if (tab == 4) {
            if (sel < nq) {
                QE e = Q[sel];
                int c = 0;   /* START = watch now */
                if (!quick) {
                    const char *o[] = { "Watch now", e.failed == 1 ? "Try again" : e.failed == 2 ? "Resume download" : "Pause download", "Delete download", "Back" };
                    c = menu_pick(e.disp, o, 4);
                }
                int qi = q_find(e.dst);
                if (c == 0) { watch_path(e.url, e.dst, e.disp); continue; }
                if (c == 1 && qi >= 0) {
                    int pause = e.failed == 0;
                    q_pause(qi, pause); queue_tick();
                    sfx(SFX_TOGGLE);
                    status = pause ? "Paused. Resume it any time." : e.failed == 1 ? "Trying again..." : "Resuming...";
                }
                if (c == 2 && qi >= 0) {
                    char msg[120]; snprintf(msg, sizeof msg, "%.60s", e.disp);
                    int k2 = ask("Delete?", "Stop and delete this download?", msg, MOOD_SAD, "A:Delete  B:Keep");
                    if (k2 == K_A) { qi = q_find(e.dst); if (qi >= 0) q_remove(qi, 1); status = "Download deleted."; }
                }
                continue;
            }
            if (sel - nq < npart) {   /* a paused, half-finished download */
                PartF pf = parts[sel - nq];
                int c = 0;
                if (!quick) { const char *o[] = { "Watch now", "Resume download", "Delete download", "Back" }; c = menu_pick(pf.disp, o, 4); }
                else c = -9;   /* START = watch now */
                if (c == 0 || c == -9) { if (pf.url[0]) watch_path(pf.url, pf.dst, pf.disp); else status = "Can't resume this one - delete it instead"; continue; }
                c -= 1;
                if (c == 0) {
                    if (!pf.url[0]) { status = "Can't resume this one - delete it instead"; continue; }
                    int r = q_add_next(pf.url, pf.dst, pf.disp); queue_tick();
                    status = r == 0 || r == 1 ? "Resuming in the queue" : r == 1 ? "Already in the queue" : "Queue is full";
                } else if (c == 1) { part_delete(pf.dst); status = "Deleted."; }
                continue;
            }
            int i = sel - nq - npart;
            if (i >= ndf) continue;
            char p[1000]; snprintf(p, sizeof p, "%s/%s", viddir, dfiles[i].name);
            char nm[300]; snprintf(nm, sizeof nm, "%s", dfiles[i].name);
            const char *u = strstr(nm, "__");
            int c = 0;
            if (!quick) {
                const char *o[] = { "Play", "Add to playlist...", "Delete", "Back" };
                c = menu_pick(dfiles[i].disp, o, 4);
            }
            if (c == 0) play_file(p, nm, u ? u + 2 : nm, 0);
            else if (c == 1) {
                char pl[64];
                if (pl_choose(pl, sizeof pl)) {
                    char id[160] = "";
                    if (u) { size_t il = (size_t)(u - nm); if (il >= sizeof id) il = sizeof id - 1; memcpy(id, nm, il); id[il] = 0; }
                    status = pl_append(pl, id[0] ? id : "local", u ? u + 2 : nm, dfiles[i].disp) == 0 ? "Added to playlist!" : "Could not save";
                }
            } else if (c == 2) {
                char msg[120]; snprintf(msg, sizeof msg, "%.60s", dfiles[i].disp);
                int k2 = ask("Delete?", "Delete this video from the card?", msg, MOOD_SAD, "A:Delete  B:Keep");
                if (k2 == K_A) { unlink(p); resume_set(dfiles[i].name, 0); local_thumb_drop(dfiles[i].name); sync(); status = "Deleted."; }
            }
        }
    }
}

int main(int argc, char **argv) {
    LOG = fdopen(2, "w");
    if (LOG) setvbuf(LOG, 0, _IOLBF, 0);
    signal(SIGPIPE, SIG_IGN);
    if (argc < 2) { logf_("usage: archivetv <app_dir> [rotate]"); return 1; }
    snprintf(appdir, sizeof appdir, "%s", argv[1]);
    flip = argc > 2 ? atoi(argv[2]) == 1 : 1;
    snprintf(viddir, sizeof viddir, "%s/videos", appdir);
    snprintf(pldir, sizeof pldir, "%s/playlists", appdir);
    mkdir(viddir, 0755); mkdir(pldir, 0755);
    logf_("ArchiveTV starting, app dir %s, rotate %d", appdir, flip);

    detect_device();
    ui_thread = pthread_self(); ui_thread_set = 1;
    if (SDL_Init(SDL_INIT_VIDEO) < 0) { logf_("SDL_Init failed"); return 1; }
    /* hardware double buffer: SDL_Flip pans on vsync, so frames don't tear */
    g_vsync = settings_peek("vsync ", 1);
    scr = video_mode(g_vsync);
    if (scr && (scr->w != W || scr->h != H)) { logf_("asked for %dx%d, got %dx%d", W, H, scr->w, scr->h); W = scr->w & ~1; H = scr->h & ~1; if (W > WMAX || H > HMAX) { W = 640; H = 480; } }
    if (!scr && (W != 640 || H != 480)) { W = 640; H = 480; scr = SDL_SetVideoMode(W, H, 32, SDL_SWSURFACE); }
    if (!scr) { logf_("SDL_SetVideoMode failed"); SDL_Quit(); return 1; }
    back = (uint32_t *)malloc((size_t)W * H * 4); vbuf = (uint32_t *)malloc((size_t)W * H * 4);
    if (!back || !vbuf) return 1;
    SDL_ShowCursor(0);
    logf_("screen %dx%d %d bpp, R/G/B shift %d/%d/%d", scr->w, scr->h, scr->format->BitsPerPixel, scr->format->Rshift, scr->format->Gshift, scr->format->Bshift);
    yuv_tables();
    ui_init(back, W, H);
    settings_load();
    ui_set_theme(g_theme);
    fx_list();
    if (g_fx > 1 || g_fx > nfx) g_fx = nfx ? 1 : 0;
    fx_load(g_fx);
    av_log_set_level(16);
    av_register_all();
    ui_app_version = app_version();
    upd_start_check();

    static const char *SAY[] = {
        "Let's watch old TV!", "Old TV was better, right?", "Hey, let's go back in time!",
        "I hate new anime, blehhh", "Let's go back to the 90s!", "Eat my shorts!",
    };
    struct timeval stv; gettimeofday(&stv, 0);   /* a different line each launch */
    const char *say = SAY[(unsigned)(stv.tv_sec ^ stv.tv_usec) % (sizeof SAY / sizeof SAY[0])];
    ui_hello(say, 0, 0, now_s()); present(back);
    sfx_load();
    load_channels();
    guide_load();
    resume_load();
    q_load();
    queue_tick();
    menu_audio_start();
    sfx(SFX_HELLO);
    flush_keys();
    double t_say = now_s() + 0.6;   /* starts talking after the hello jingle */
    int last_shown = 0, say_len = (int)strlen(say);
    for (int go = 0; !go; ) {
        double t = now_s();
        int shown = t < t_say ? 0 : (int)((t - t_say) * 22);
        if (shown > say_len) shown = say_len;
        if (shown > last_shown) { if (say[shown - 1] != ' ' && shown % 2 == 0) sfx(SFX_TYPE); last_shown = shown; }
        ui_hello(say, shown, "A:Let's go!", t); present(back);
        SDL_Event e;
        while (SDL_PollEvent(&e)) if (e.type == SDL_KEYDOWN) { if (shown < say_len) { t_say = -100; } else go = 1; }
        usleep(30000);
    }
    sfx(SFX_SELECT);

    home();

    if (DLQ.state == 1) { DLQ.cancel = 1; while (DLQ.state == 1) usleep(20000); }
    if (DLW.state == 1) { DLW.cancel = 1; while (DLW.state == 1) usleep(20000); }
    dl_reap(&DLQ); dl_reap(&DLW);
    menu_audio_stop();
    SDL_Quit();
    logf_("bye%s", g_exit_code == 42 ? " (restarting to update)" : "");
    return g_exit_code;
}
