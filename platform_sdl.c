#include <SDL.h>
#include <assert.h>
#include <time.h>

#define HW_AUDIO_NUMBUFFERS 3

static SDL_Window *screen;
static SDL_Renderer *renderer;
static SDL_Texture *frame;
static uint8_t framebuf[320*224*4];

static int16_t *AUDIO_BUF[HW_AUDIO_NUMBUFFERS];
static int audio_buf_index_w=1, audio_buf_index_r=0;
const uint8_t *keystate;
uint8_t keypressed[256];
uint8_t keyreleased[256];
static uint8_t keyoldstate[256];
static int samples_per_frame;
static int framecounter;
static int audiocounter;
static clock_t fpsclock;
static int fpscounter;
static int g_audioenable;
static int g_videoenable;

#define WINDOW_WIDTH 900

static void fill_audio(void *userdata, uint8_t* stream, int len);

void plat_init(int audiofreq, int fps)
{
    if ( SDL_Init(SDL_INIT_AUDIO|SDL_INIT_VIDEO) < 0 )
    {
        printf("Unable to init SDL: %s\n", SDL_GetError());
        exit(1);
    }
    atexit(SDL_Quit);

    keystate = SDL_GetKeyboardState(NULL);

    samples_per_frame = audiofreq / fps;
    fprintf(stderr, "Music set to %d FPS\n", fps);

    /* Initialize audio */
    SDL_AudioSpec wanted;

    wanted.freq = audiofreq;
    wanted.format = AUDIO_S16;
    wanted.channels = 2;
    wanted.samples = samples_per_frame;
    wanted.callback = fill_audio;
    wanted.userdata = NULL;
    if (SDL_OpenAudio(&wanted, NULL) < 0) {
        fprintf(stderr, "Couldn't open audio: %s\n", SDL_GetError());
        exit(1);
    }

    for (int i=0;i<HW_AUDIO_NUMBUFFERS;++i)
        AUDIO_BUF[i] = calloc(samples_per_frame*2*2, 1);
}

int plat_poll(void)
{
    SDL_Event event;

    memcpy(keyoldstate, keystate, 256);

    while ( SDL_PollEvent(&event) )
    {
        if (event.type == SDL_QUIT)
            return 0;

        if ( event.type == SDL_KEYDOWN )
        {
            if ( event.key.keysym.sym == SDLK_ESCAPE )
                return 0;
        }
    }

    for (int i=0;i<256;++i)
    {
        if (keystate[i] ^ keyoldstate[i])
        {
            keypressed[i] = keystate[i];
            keyreleased[i] = keyoldstate[i];
        }
        else
            keypressed[i] = keyreleased[i] = 0;
    }

    return 1;
}

void plat_enable_audio(int enable)
{
    SDL_PauseAudio(!enable);
    g_audioenable = enable;
}

void plat_enable_video(int enable)
{
    if (enable && !g_videoenable)
    {
        screen = SDL_CreateWindow("MVS64 - NeoGeo Emulator",
            SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
            WINDOW_WIDTH, WINDOW_WIDTH*3/4, SDL_WINDOW_RESIZABLE);
        renderer = SDL_CreateRenderer(screen, -1, SDL_RENDERER_PRESENTVSYNC);

        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");  // make the scaled rendering look smoother.
        SDL_RenderSetLogicalSize(renderer, 320, 224);

        frame = SDL_CreateTexture(renderer,
                                  SDL_PIXELFORMAT_ARGB8888,
                                  SDL_TEXTUREACCESS_STREAMING,
                                  320, 224);
    }
    else if (!enable && !g_videoenable)
    {
        SDL_DestroyTexture(frame); frame=NULL;
        SDL_DestroyRenderer(renderer); renderer = NULL;
        SDL_DestroyWindow(screen); screen = NULL;
    }

    g_videoenable = enable;
}

void plat_beginframe(uint8_t **screen, int *pitch)
{
    *screen = framebuf;
    *pitch = 320*4;
}

void plat_endframe(void)
{
    if (g_videoenable)
    {
        if (audiocounter < framecounter)
        {
            SDL_UpdateTexture(frame, NULL, framebuf, 320*4);

            SDL_RenderClear(renderer);
            SDL_RenderCopy(renderer, frame, NULL, NULL);
            SDL_RenderPresent(renderer);
            fpscounter += 1;

            if (g_audioenable)
                while (audiocounter < framecounter)
                    SDL_Delay(1);
        }

        if (fpsclock+1000 < SDL_GetTicks())
        {
            char title[256];
            sprintf(title, "MVS64 - NeoGeo Emulator - %d FPS", fpscounter);
            SDL_SetWindowTitle(screen, title);
            fpscounter = 0;
            fpsclock += 1000;
        }
    }

    framecounter += 1;
}

void plat_save_screenshot(const char *fn)
{
    SDL_Surface* saveSurface = SDL_CreateRGBSurfaceFrom(
        framebuf, 320, 224, 32, 320*4,
        0x00FF0000, 0x0000FF00, 0x00000FF, 0);
    assert(saveSurface);
    SDL_SaveBMP(saveSurface, fn);
    SDL_FreeSurface(saveSurface);
}

void plat_beginaudio(int16_t **buf, int *nsamples)
{
    extern int framecounter;
#if 1
    if (audio_buf_index_w >= audio_buf_index_r + HW_AUDIO_NUMBUFFERS)
        printf("[AUDIO](FC=%04d/R=%04d/W%04d) Warning: overflow audio buffer (producing too fast)\n", framecounter, audio_buf_index_r, audio_buf_index_w);
#endif
    *buf = AUDIO_BUF[audio_buf_index_w % HW_AUDIO_NUMBUFFERS];
    *nsamples = samples_per_frame;
}

void plat_endaudio(void)
{
    audio_buf_index_w += 1;
}

void fill_audio(void *userdata, uint8_t *stream, int len)
{
    if (audio_buf_index_r == audio_buf_index_w)
    {
        #if 1
        printf("[AUDIO](FC=%04d/AC=%04d/W=%04d) Warning: no audio generated, silencing...\n", framecounter, audiocounter, audio_buf_index_w);
        #endif
        memset(stream, 0, len);
        return;
    }

    assert(samples_per_frame*2*2 == len);   // 2 channels, 2 bytes
    memcpy(stream, AUDIO_BUF[audio_buf_index_r++ % HW_AUDIO_NUMBUFFERS], len);
    ++audiocounter;
}
