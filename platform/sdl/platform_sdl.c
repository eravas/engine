/*
 * platform_sdl.c - desktop backend on SDL3. The only file that includes SDL.
 *
 *   <app> [--frames N] [--scale N] [--storage-dir DIR]
 *
 *   --frames N         raise PLATFORM_BTN_QUIT after N presented frames
 *   --scale N          initial window size as a multiple of 320x240 (default 3)
 *   --storage-dir DIR  where slot files live (default ./storage)
 *
 * Backend-specific behaviour:
 *   present  Converts through the palette into a streaming texture and shows
 *            it at the largest integer scale that fits the window. Blocks so
 *            that frames are at least one 30 Hz period apart. Vsync is off,
 *            so tearing is possible.
 *   input    Arrow keys = d-pad; Z X A S = A B X Y; Return = Start;
 *            Tab = Select; Escape or closing the window = Quit.
 *   time     SDL's monotonic counter.
 *   audio    Default playback device. The callback runs on SDL's audio
 *            thread. set_callback returns false if no device could be opened.
 *   storage  One file per slot, <dir>/slotN.bin, written to a temporary file
 *            and renamed over the old one.
 */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kernel/platform.h"

#define FRAME_PERIOD_NS (1000000000ull / 30ull)
#define AUDIO_CHUNK_FRAMES 512

static struct {
    SDL_Window   *window;
    SDL_Renderer *renderer;
    SDL_Texture  *texture;
    uint64_t      next_present_ns;
    uint64_t      frames_presented;
    uint64_t      frame_limit; /* 0 = none */
    bool          quit;

    SDL_AudioStream  *audio;
    platform_audio_fn audio_fn; /* guarded by the audio stream lock */
    void             *audio_user;

    const char *storage_dir;
} g;

/* ---- display ----------------------------------------------------------- */

void platform_present(const uint8_t *pixels, const uint32_t *palette)
{
    void *dst;
    int pitch;
    if (SDL_LockTexture(g.texture, NULL, &dst, &pitch)) {
        for (int y = 0; y < PLATFORM_FB_HEIGHT; y++) {
            const uint8_t *src = pixels + (size_t)y * PLATFORM_FB_WIDTH;
            uint32_t *row = (uint32_t *)(void *)((uint8_t *)dst +
                                                 (size_t)y * (size_t)pitch);
            for (int x = 0; x < PLATFORM_FB_WIDTH; x++) {
                row[x] = palette[src[x]] & 0x00FFFFFFu;
            }
        }
        SDL_UnlockTexture(g.texture);
    }

    SDL_SetRenderDrawColor(g.renderer, 0, 0, 0, 255);
    SDL_RenderClear(g.renderer);
    SDL_RenderTexture(g.renderer, g.texture, NULL, NULL);
    SDL_RenderPresent(g.renderer);
    g.frames_presented++;

    /* Frame limiter: hold a steady 30 Hz cadence; resynchronise if late. */
    uint64_t now = SDL_GetTicksNS();
    if (now < g.next_present_ns) {
        SDL_DelayPrecise(g.next_present_ns - now);
        g.next_present_ns += FRAME_PERIOD_NS;
    } else {
        g.next_present_ns = now + FRAME_PERIOD_NS;
    }
}

/* ---- input ------------------------------------------------------------- */

void platform_poll_input(platform_input_t *out)
{
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_EVENT_QUIT) {
            g.quit = true;
        }
    }

    static const struct {
        SDL_Scancode key;
        uint32_t     button;
    } map[] = {
        {SDL_SCANCODE_UP, PLATFORM_BTN_UP},
        {SDL_SCANCODE_DOWN, PLATFORM_BTN_DOWN},
        {SDL_SCANCODE_LEFT, PLATFORM_BTN_LEFT},
        {SDL_SCANCODE_RIGHT, PLATFORM_BTN_RIGHT},
        {SDL_SCANCODE_Z, PLATFORM_BTN_A},
        {SDL_SCANCODE_X, PLATFORM_BTN_B},
        {SDL_SCANCODE_A, PLATFORM_BTN_X},
        {SDL_SCANCODE_S, PLATFORM_BTN_Y},
        {SDL_SCANCODE_RETURN, PLATFORM_BTN_START},
        {SDL_SCANCODE_TAB, PLATFORM_BTN_SELECT},
    };

    const bool *keys = SDL_GetKeyboardState(NULL);
    uint32_t buttons = 0;
    for (size_t i = 0; i < sizeof map / sizeof map[0]; i++) {
        if (keys[map[i].key]) {
            buttons |= map[i].button;
        }
    }
    if (keys[SDL_SCANCODE_ESCAPE]) {
        g.quit = true;
    }
    if (g.frame_limit != 0 && g.frames_presented >= g.frame_limit) {
        g.quit = true;
    }
    if (g.quit) {
        buttons |= PLATFORM_BTN_QUIT;
    }
    out->buttons = buttons;
}

/* ---- time -------------------------------------------------------------- */

uint64_t platform_time_us(void) { return SDL_GetTicksNS() / 1000u; }

/* ---- audio ------------------------------------------------------------- */

/* Runs on SDL's audio thread with the stream lock held. */
static void SDLCALL audio_feed(void *userdata, SDL_AudioStream *stream,
                               int additional_amount, int total_amount)
{
    (void)userdata;
    (void)total_amount;
    int16_t chunk[AUDIO_CHUNK_FRAMES];
    int frames_left = additional_amount / (int)sizeof(int16_t);
    while (frames_left > 0) {
        int n = frames_left < AUDIO_CHUNK_FRAMES ? frames_left
                                                 : AUDIO_CHUNK_FRAMES;
        if (g.audio_fn != NULL) {
            g.audio_fn(g.audio_user, chunk, (uint32_t)n);
        } else {
            memset(chunk, 0, (size_t)n * sizeof(int16_t));
        }
        SDL_PutAudioStreamData(stream, chunk, n * (int)sizeof(int16_t));
        frames_left -= n;
    }
}

static void audio_open(void)
{
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        return;
    }
    const SDL_AudioSpec spec = {SDL_AUDIO_S16, 1, PLATFORM_AUDIO_RATE_HZ};
    g.audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                        &spec, audio_feed, NULL);
    if (g.audio != NULL) {
        SDL_ResumeAudioStreamDevice(g.audio);
    }
}

bool platform_audio_set_callback(platform_audio_fn fn, void *user)
{
    if (g.audio == NULL) {
        return fn == NULL;
    }
    /* Holding the lock means audio_feed is not running, so on return the
     * old callback can never be entered again. */
    SDL_LockAudioStream(g.audio);
    g.audio_fn = fn;
    g.audio_user = fn != NULL ? user : NULL;
    SDL_UnlockAudioStream(g.audio);
    return true;
}

/* ---- storage ----------------------------------------------------------- */

static void slot_path(char *out, size_t cap, uint32_t slot, const char *ext)
{
    snprintf(out, cap, "%s/slot%u.%s", g.storage_dir, (unsigned)slot, ext);
}

int32_t platform_storage_read(uint32_t slot, void *buf, uint32_t capacity)
{
    if (slot >= PLATFORM_STORAGE_SLOTS) {
        return PLATFORM_STORAGE_ERROR;
    }
    char path[1024];
    slot_path(path, sizeof path, slot, "bin");

    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return PLATFORM_STORAGE_EMPTY;
    }
    /* Read into a slot-sized scratch buffer so that an oversized blob is
     * detected without writing past the caller's buffer. */
    static uint8_t scratch[PLATFORM_STORAGE_SLOT_MAX + 1];
    size_t n = fread(scratch, 1, sizeof scratch, f);
    bool failed = ferror(f) != 0;
    fclose(f);

    if (failed || n > PLATFORM_STORAGE_SLOT_MAX || n > capacity) {
        return PLATFORM_STORAGE_ERROR;
    }
    if (n != 0) {
        memcpy(buf, scratch, n);
    }
    return (int32_t)n;
}

bool platform_storage_write(uint32_t slot, const void *data, uint32_t size)
{
    if (slot >= PLATFORM_STORAGE_SLOTS || size > PLATFORM_STORAGE_SLOT_MAX) {
        return false;
    }
    char path[1024];
    char tmp[1024];
    slot_path(path, sizeof path, slot, "bin");
    slot_path(tmp, sizeof tmp, slot, "tmp");

    SDL_CreateDirectory(g.storage_dir);
    FILE *f = fopen(tmp, "wb");
    if (f == NULL) {
        return false;
    }
    bool ok = size == 0 || fwrite(data, 1, size, f) == size;
    if (fclose(f) != 0) {
        ok = false;
    }
    if (ok) {
        ok = SDL_RenamePath(tmp, path);
    }
    if (!ok) {
        SDL_RemovePath(tmp);
    }
    return ok;
}

/* ---- entry point ------------------------------------------------------- */

static int usage(const char *argv0)
{
    fprintf(stderr, "usage: %s [--frames N] [--scale N] [--storage-dir DIR]\n",
            argv0);
    return 2;
}

int main(int argc, char **argv)
{
    int scale = 3;
    g.storage_dir = "storage";

    /* Every option takes exactly one value. */
    for (int i = 1; i < argc; i += 2) {
        const char *opt = argv[i];
        const char *val = i + 1 < argc ? argv[i + 1] : NULL;
        if (val == NULL) {
            return usage(argv[0]);
        }
        if (strcmp(opt, "--frames") == 0) {
            g.frame_limit = strtoull(val, NULL, 0);
        } else if (strcmp(opt, "--scale") == 0) {
            scale = atoi(val);
            if (scale < 1 || scale > 16) {
                return usage(argv[0]);
            }
        } else if (strcmp(opt, "--storage-dir") == 0) {
            g.storage_dir = val;
        } else {
            return usage(argv[0]);
        }
    }

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    if (!SDL_CreateWindowAndRenderer("engine", PLATFORM_FB_WIDTH * scale,
                                     PLATFORM_FB_HEIGHT * scale,
                                     SDL_WINDOW_RESIZABLE, &g.window,
                                     &g.renderer)) {
        fprintf(stderr, "SDL_CreateWindowAndRenderer: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_SetRenderVSync(g.renderer, 0);
    SDL_SetRenderLogicalPresentation(g.renderer, PLATFORM_FB_WIDTH,
                                     PLATFORM_FB_HEIGHT,
                                     SDL_LOGICAL_PRESENTATION_INTEGER_SCALE);
    g.texture = SDL_CreateTexture(g.renderer, SDL_PIXELFORMAT_XRGB8888,
                                  SDL_TEXTUREACCESS_STREAMING,
                                  PLATFORM_FB_WIDTH, PLATFORM_FB_HEIGHT);
    if (g.texture == NULL) {
        fprintf(stderr, "SDL_CreateTexture: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_SetTextureScaleMode(g.texture, SDL_SCALEMODE_NEAREST);
    audio_open();

    int status = app_main();

    if (g.audio != NULL) {
        SDL_DestroyAudioStream(g.audio);
    }
    SDL_DestroyTexture(g.texture);
    SDL_DestroyRenderer(g.renderer);
    SDL_DestroyWindow(g.window);
    SDL_Quit();
    return status;
}
