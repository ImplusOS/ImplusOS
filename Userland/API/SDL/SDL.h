#pragma once

#include <stdint.h>

typedef uint8_t Uint8;
typedef int8_t Sint8;
typedef uint16_t Uint16;
typedef int16_t Sint16;
typedef uint32_t Uint32;
typedef int32_t Sint32;

typedef struct SDL_Rect {
    int x;
    int y;
    int w;
    int h;
} SDL_Rect;

typedef struct SDL_Color {
    Uint8 r;
    Uint8 g;
    Uint8 b;
    Uint8 unused;
} SDL_Color;

typedef struct SDL_PixelFormat {
    Uint8 BitsPerPixel;
    Uint8 BytesPerPixel;
    Uint8 Rshift;
    Uint8 Gshift;
    Uint8 Bshift;
    Uint8 Ashift;
    Uint32 Rmask;
    Uint32 Gmask;
    Uint32 Bmask;
    Uint32 Amask;
} SDL_PixelFormat;

typedef struct SDL_Surface {
    Uint32 flags;
    SDL_PixelFormat *format;
    int w;
    int h;
    int pitch;
    void *pixels;
} SDL_Surface;

typedef int SDLKey;
typedef uint16_t SDLMod;

typedef struct SDL_keysym {
    Uint8 scancode;
    SDLKey sym;
    SDLMod mod;
    Uint16 unicode;
} SDL_keysym;

typedef struct SDL_KeyboardEvent {
    Uint8 type;
    Uint8 which;
    Uint8 state;
    SDL_keysym keysym;
} SDL_KeyboardEvent;

typedef struct SDL_MouseMotionEvent {
    Uint8 type;
    Uint8 which;
    Uint8 state;
    Uint16 x;
    Uint16 y;
    Sint16 xrel;
    Sint16 yrel;
} SDL_MouseMotionEvent;

typedef struct SDL_MouseButtonEvent {
    Uint8 type;
    Uint8 which;
    Uint8 button;
    Uint8 state;
    Uint16 x;
    Uint16 y;
} SDL_MouseButtonEvent;

typedef struct SDL_ResizeEvent {
    Uint8 type;
    int w;
    int h;
} SDL_ResizeEvent;

typedef struct SDL_UserEvent {
    Uint8 type;
    int code;
    void *data1;
    void *data2;
} SDL_UserEvent;

typedef union SDL_Event {
    Uint8 type;
    SDL_KeyboardEvent key;
    SDL_MouseMotionEvent motion;
    SDL_MouseButtonEvent button;
    SDL_ResizeEvent resize;
    SDL_UserEvent user;
} SDL_Event;

typedef Uint32 (*SDL_NewTimerCallback)(Uint32 interval, void *param);
typedef Uint32 SDL_TimerID;

#define SDL_INIT_TIMER 0x00000001u
#define SDL_INIT_VIDEO 0x00000020u

#define SDL_SWSURFACE 0x00000000u
#define SDL_RESIZABLE 0x00000010u

#define SDL_DISABLE 0
#define SDL_ENABLE 1

#define SDL_RELEASED 0
#define SDL_PRESSED 1

#define SDL_KEYDOWN 2
#define SDL_KEYUP 3
#define SDL_MOUSEMOTION 4
#define SDL_MOUSEBUTTONDOWN 5
#define SDL_MOUSEBUTTONUP 6
#define SDL_QUIT 12
#define SDL_VIDEORESIZE 16
#define SDL_USEREVENT 24

#define SDL_BUTTON_LEFT 1
#define SDL_BUTTON_MIDDLE 2
#define SDL_BUTTON_RIGHT 3
#define SDL_BUTTON_WHEELUP 4
#define SDL_BUTTON_WHEELDOWN 5

#define SDLK_BACKSPACE 8
#define SDLK_TAB 9
#define SDLK_CLEAR 12
#define SDLK_RETURN 13
#define SDLK_PAUSE 19
#define SDLK_ESCAPE 27
#define SDLK_SPACE 32
#define SDLK_DELETE 127
#define SDLK_KP0 256
#define SDLK_KP1 257
#define SDLK_KP2 258
#define SDLK_KP3 259
#define SDLK_KP4 260
#define SDLK_KP5 261
#define SDLK_KP6 262
#define SDLK_KP7 263
#define SDLK_KP8 264
#define SDLK_KP9 265
#define SDLK_KP_PERIOD 266
#define SDLK_KP_DIVIDE 267
#define SDLK_KP_MULTIPLY 268
#define SDLK_KP_MINUS 269
#define SDLK_KP_PLUS 270
#define SDLK_KP_ENTER 271
#define SDLK_KP_EQUALS 272
#define SDLK_UP 273
#define SDLK_DOWN 274
#define SDLK_RIGHT 275
#define SDLK_LEFT 276
#define SDLK_INSERT 277
#define SDLK_HOME 278
#define SDLK_END 279
#define SDLK_PAGEUP 280
#define SDLK_PAGEDOWN 281
#define SDLK_F1 282
#define SDLK_F2 283
#define SDLK_F3 284
#define SDLK_F4 285
#define SDLK_F5 286
#define SDLK_F6 287
#define SDLK_F7 288
#define SDLK_F8 289
#define SDLK_F9 290
#define SDLK_F10 291
#define SDLK_F11 292
#define SDLK_F12 293
#define SDLK_F13 294
#define SDLK_F14 295
#define SDLK_F15 296
#define SDLK_NUMLOCK 300
#define SDLK_CAPSLOCK 301
#define SDLK_SCROLLOCK 302
#define SDLK_RSHIFT 303
#define SDLK_LSHIFT 304
#define SDLK_RCTRL 305
#define SDLK_LCTRL 306
#define SDLK_RALT 307
#define SDLK_LALT 308
#define SDLK_RMETA 309
#define SDLK_LMETA 310
#define SDLK_LSUPER 311
#define SDLK_RSUPER 312
#define SDLK_MODE 313
#define SDLK_COMPOSE 314
#define SDLK_HELP 315
#define SDLK_PRINT 316
#define SDLK_SYSREQ 317
#define SDLK_BREAK 318
#define SDLK_MENU 319
#define SDLK_POWER 320
#define SDLK_EURO 321
#define SDLK_UNDO 322

int SDL_Init(Uint32 flags);
void SDL_Quit(void);
SDL_Surface *SDL_SetVideoMode(int width, int height, int bpp, Uint32 flags);
const char *SDL_GetError(void);
int SDL_BlitSurface(SDL_Surface *src, SDL_Rect *srcrect,
                    SDL_Surface *dst, SDL_Rect *dstrect);
void SDL_UpdateRect(SDL_Surface *screen, Sint32 x, Sint32 y,
                    Uint32 w, Uint32 h);
int SDL_SetColors(SDL_Surface *surface, SDL_Color *colors,
                  int firstcolor, int ncolors);
int SDL_ShowCursor(int toggle);
int SDL_EnableKeyRepeat(int delay, int interval);
int SDL_PollEvent(SDL_Event *event);
int SDL_WaitEvent(SDL_Event *event);
SDL_TimerID SDL_AddTimer(Uint32 interval, SDL_NewTimerCallback callback,
                         void *param);
int SDL_RemoveTimer(SDL_TimerID id);
int SDL_PushEvent(SDL_Event *event);

