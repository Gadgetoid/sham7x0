#include <SDL3/SDL.h>
#include <math.h>

#include "beeper.h"

#define SAMPLE_RATE 44100
#define QUEUE_SIZE  256
#define CHUNK       512
#define VOLUME      0.20f
#define PIEZO_HZ    4000.0f
#define PIEZO_Q     1.4f
#define ATTACK_MS   1.0f
#define RELEASE_MS  3.0f
#define SILENCE     1e-6f

typedef struct {
    float b0, b1, b2, a1, a2;
    float x1, x2, y1, y2;
} biquad_t;

typedef struct {
    float frequency;
    float second_frequency;
    uint32_t samples;
} tone_t;

static tone_t queue[QUEUE_SIZE];
static int head = 0, tail = 0;
static tone_t current;
static uint32_t remaining = 0;
static float phase = 0, second_phase = 0, envelope = 0;
static float attack = 0, release = 0;
static bool sounding = false;
static bool fading = true;
static biquad_t piezo;
static SDL_Mutex *lock = NULL;
static SDL_AudioStream *stream = NULL;
static bool sound_on = true;

void beeper_set_sound(bool on) { sound_on = on; }
bool beeper_sound(void) { return sound_on; }

static void setup_piezo(void) {
    float w0 = 2.0f * (float)M_PI * PIEZO_HZ / SAMPLE_RATE;
    float alpha = sinf(w0) / (2.0f * PIEZO_Q);
    float a0 = 1.0f + alpha;
    piezo = (biquad_t){ alpha / a0, 0.0f, -alpha / a0, -2.0f * cosf(w0) / a0, (1.0f - alpha) / a0, 0, 0, 0, 0 };
    attack = 1.0f - expf(-1000.0f / (ATTACK_MS * SAMPLE_RATE));
    release = 1.0f - expf(-1000.0f / (RELEASE_MS * SAMPLE_RATE));
}

static float filter(biquad_t *f, float x) {
    float y = f->b0 * x + f->b1 * f->x1 + f->b2 * f->x2 - f->a1 * f->y1 - f->a2 * f->y2;
    f->x2 = f->x1;
    f->x1 = x;
    f->y2 = f->y1;
    f->y1 = y;
    return y;
}

static float oscillate(const tone_t *tone) {
    float sample = 0.0f;
    if (tone->second_frequency > 0) {
        sample = 0.5f * (sinf(phase * 2.0f * (float)M_PI) + sinf(second_phase * 2.0f * (float)M_PI));
    } else if (tone->frequency > 0) {
        sample = phase < 0.5f ? 1.0f : -1.0f;
    }
    phase += tone->frequency / SAMPLE_RATE;
    second_phase += tone->second_frequency / SAMPLE_RATE;
    phase -= floorf(phase);
    second_phase -= floorf(second_phase);
    return sample;
}

static float next_sample(void) {
    sounding = false;
    if (remaining == 0 && tail != head) {
        tone_t next = queue[tail];
        tail = (tail + 1) % QUEUE_SIZE;
        remaining = next.samples;
        fading = !(next.frequency > 0 || next.second_frequency > 0);
        if (!fading) current = next;
    }
    if (remaining == 0) fading = true;
    else {
        remaining--;
        sounding = !fading;
    }
    return oscillate(&current);
}

static void SDLCALL feed(void *user, SDL_AudioStream *audio, int additional, int total) {
    (void)user;
    (void)total;
    float buffer[CHUNK];
    int wanted = additional / (int)sizeof(float);
    while (wanted > 0) {
        int count = wanted < CHUNK ? wanted : CHUNK;
        SDL_LockMutex(lock);
        for (int i = 0; i < count; i++) {
            float oscillator = next_sample();
            envelope += ((sounding ? 1.0f : 0.0f) - envelope) * (sounding ? attack : release);
            if (!sounding && envelope < SILENCE) {
                envelope = 0;
                if (fabsf(piezo.y1) < SILENCE && fabsf(piezo.y2) < SILENCE) piezo.x1 = piezo.x2 = piezo.y1 = piezo.y2 = 0;
            }
            buffer[i] = filter(&piezo, oscillator * envelope) * VOLUME;
        }
        SDL_UnlockMutex(lock);
        SDL_PutAudioStreamData(audio, buffer, count * (int)sizeof(float));
        wanted -= count;
    }
}

bool beeper_init(void) {
    setup_piezo();
    lock = SDL_CreateMutex();
    SDL_AudioSpec spec = { SDL_AUDIO_F32, 1, SAMPLE_RATE };
    stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, feed, NULL);
    if (!stream) {
        SDL_Log("beeper: no audio (%s)", SDL_GetError());
        return false;
    }
    SDL_ResumeAudioStreamDevice(stream);
    return true;
}

void beeper_tone(float frequency, float second_frequency, uint32_t duration_ms) {
    if (!lock) return;
    SDL_LockMutex(lock);
    int next = (head + 1) % QUEUE_SIZE;
    if (next != tail) {
        queue[head] = (tone_t){ frequency, second_frequency, (uint32_t)((uint64_t)duration_ms * SAMPLE_RATE / 1000) };
        head = next;
    }
    SDL_UnlockMutex(lock);
}

void beeper_stop(void) {
    if (!lock) return;
    SDL_LockMutex(lock);
    head = tail = 0;
    remaining = 0;
    SDL_UnlockMutex(lock);
}

bool beeper_busy(void) {
    if (!lock) return false;
    SDL_LockMutex(lock);
    bool busy = remaining > 0 || head != tail;
    SDL_UnlockMutex(lock);
    return busy;
}

void beeper_deinit(void) {
    if (stream) SDL_DestroyAudioStream(stream);
    stream = NULL;
    if (lock) SDL_DestroyMutex(lock);
    lock = NULL;
}
