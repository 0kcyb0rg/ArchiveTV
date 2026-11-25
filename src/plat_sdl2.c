/* The Linux build's screen and buttons, over SDL2: an SDL 1.2-style face for atv.c.
   - SDL_SetVideoMode gives a 640x480 surface; SDL_Flip scales it to the real screen (kept 4:3, black bars).
   - The handheld's buttons (a game controller to SDL2) arrive as the same key codes the Miyoo sends,
     with key repeat done here (SDL2 has no SDL_EnableKeyRepeat). A keyboard works too. */
#include <SDL2/SDL.h>
#include <stdio.h>
#include <string.h>

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

static SDL_Window *win;
static SDL_Renderer *ren;
static SDL_Texture *tex;
static SDL_Surface *surf;

SDL_Surface *atv_SetVideoMode(int w, int h, int bpp, Uint32 flags) {
    (void)bpp; (void)flags;
    if (surf) return surf;
    SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK);
    SDL_DisplayMode dm;
    int dw = 640, dh = 480;
    if (SDL_GetDesktopDisplayMode(0, &dm) == 0 && dm.w > 0 && dm.h > 0) { dw = dm.w; dh = dm.h; }
    win = SDL_CreateWindow("ArchiveTV", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, dw, dh, SDL_WINDOW_FULLSCREEN);
    if (!win) { fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); return 0; }
    /* exactly 640x480: no smoothing; anything else: smooth, so the pixel font doesn't wobble */
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, dw == w && dh == h ? "0" : "1");
    ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!ren) { fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError()); return 0; }
    SDL_RenderSetLogicalSize(ren, w, h);
    tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
    surf = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_ShowCursor(0);
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i)) { SDL_GameController *c = SDL_GameControllerOpen(i); fprintf(stderr, "controller %d: %s\n", i, c ? SDL_GameControllerName(c) : "?"); }
        else { SDL_Joystick *j = SDL_JoystickOpen(i); fprintf(stderr, "joystick %d (no mapping): %s, %d buttons\n", i, j ? SDL_JoystickName(j) : "?", j ? SDL_JoystickNumButtons(j) : 0); }
    }
    int rw = 0, rh = 0; SDL_GetRendererOutputSize(ren, &rw, &rh);
    SDL_RendererInfo ri; SDL_GetRendererInfo(ren, &ri);
    fprintf(stderr, "screen %dx%d, renderer %s\n", rw, rh, ri.name);
    return surf;
}

int atv_Flip(SDL_Surface *s) {
    if (!ren || !s) return -1;
    SDL_UpdateTexture(tex, 0, s->pixels, s->pitch);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);
    SDL_RenderCopy(ren, tex, 0, 0);
    SDL_RenderPresent(ren);
    return 0;
}

/* ---- buttons ---- */
static int rep_delay = 0, rep_int = 0;
static int held = 0; static Uint32 held_next = 0;
static int queue[32], qn = 0, qup[32];
int atv_EnableKeyRepeat(int delay, int interval) { rep_delay = delay; rep_int = interval; return 0; }

static void push(int key, int down) {
    if (!key || qn >= 32) return;
    queue[qn] = key; qup[qn] = !down; qn++;
    if (down) { held = key; held_next = SDL_GetTicks() + (Uint32)rep_delay; }
    else if (held == key) held = 0;
}
/* positions, as on the Miyoo: A right, B bottom, X top, Y left (SDL names them by Xbox position) */
static int pad_key(int b) {
    switch (b) {
    case SDL_CONTROLLER_BUTTON_B: return K_A;
    case SDL_CONTROLLER_BUTTON_A: return K_B;
    case SDL_CONTROLLER_BUTTON_Y: return K_X;
    case SDL_CONTROLLER_BUTTON_X: return K_Y;
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return K_L1;
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return K_R1;
    case SDL_CONTROLLER_BUTTON_BACK: return K_SELECT;
    case SDL_CONTROLLER_BUTTON_START: return K_START;
    case SDL_CONTROLLER_BUTTON_GUIDE: return K_MENU;
    case SDL_CONTROLLER_BUTTON_DPAD_UP: return K_UP;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return K_DOWN;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return K_LEFT;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return K_RIGHT;
    }
    return 0;
}
static int kb_key(SDL_Keycode k) {
    switch (k) {
    case SDLK_UP: return K_UP;
    case SDLK_DOWN: return K_DOWN;
    case SDLK_LEFT: return K_LEFT;
    case SDLK_RIGHT: return K_RIGHT;
    case SDLK_RETURN: case SDLK_SPACE: return K_A;
    case SDLK_BACKSPACE: case SDLK_LCTRL: return K_B;
    case SDLK_LSHIFT: return K_X;
    case SDLK_LALT: return K_Y;
    case SDLK_PAGEUP: case SDLK_e: return K_L1;
    case SDLK_PAGEDOWN: case SDLK_t: return K_R1;
    case SDLK_TAB: return K_L2;
    case SDLK_RCTRL: return K_SELECT;
    case SDLK_ESCAPE: return K_MENU;
    }
    return 0;
}

int atv_PollEvent(SDL_Event *e) {
    static int axdir[2] = { 0, 0 }, trig[2] = { 0, 0 };
    SDL_Event ev;
    while (!qn && SDL_PollEvent(&ev)) {
        switch (ev.type) {
        case SDL_QUIT: *e = ev; return 1;
        case SDL_CONTROLLERBUTTONDOWN: push(pad_key(ev.cbutton.button), 1); break;
        case SDL_CONTROLLERBUTTONUP: push(pad_key(ev.cbutton.button), 0); break;
        case SDL_CONTROLLERAXISMOTION: {
            int v = ev.caxis.value;
            if (ev.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT || ev.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT) {
                int i = ev.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT, on = v > 16000;
                if (on != trig[i]) { trig[i] = on; push(i ? K_R2 : K_L2, on); }
            } else if (ev.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX || ev.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
                int i = ev.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY, d = v > 20000 ? 1 : v < -20000 ? -1 : 0;   /* the stick works as a D-pad */
                if (d != axdir[i]) {
                    if (axdir[i]) push(i ? (axdir[i] > 0 ? K_DOWN : K_UP) : (axdir[i] > 0 ? K_RIGHT : K_LEFT), 0);
                    if (d) push(i ? (d > 0 ? K_DOWN : K_UP) : (d > 0 ? K_RIGHT : K_LEFT), 1);
                    axdir[i] = d;
                }
            }
            break;
        }
        case SDL_JOYBUTTONDOWN:   /* a pad SDL has no mapping for: say which button, so testers can tell us */
            if (!SDL_IsGameController(ev.jbutton.which)) fprintf(stderr, "joystick button %d (no mapping)\n", ev.jbutton.button);
            break;
        case SDL_KEYDOWN: if (!ev.key.repeat) push(kb_key(ev.key.keysym.sym), 1); break;
        case SDL_KEYUP: push(kb_key(ev.key.keysym.sym), 0); break;
        case SDL_CONTROLLERDEVICEADDED: if (!SDL_GameControllerFromInstanceID(ev.cdevice.which)) SDL_GameControllerOpen(ev.cdevice.which); break;
        }
    }
    if (!qn && held && rep_delay > 0 && SDL_TICKS_PASSED(SDL_GetTicks(), held_next)) {   /* held down: repeat */
        held_next = SDL_GetTicks() + (Uint32)(rep_int > 0 ? rep_int : 70);
        queue[qn] = held; qup[qn] = 0; qn++;
    }
    if (!qn) return 0;
    memset(e, 0, sizeof *e);
    e->type = qup[0] ? SDL_KEYUP : SDL_KEYDOWN;
    e->key.keysym.sym = queue[0];
    qn--; memmove(queue, queue + 1, sizeof(int) * (size_t)qn); memmove(qup, qup + 1, sizeof(int) * (size_t)qn);
    return 1;
}
