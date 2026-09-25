#include <SDL3/SDL.h>
#include <math.h>

#include "beeper.h"

#define SAMPLE_RATE 44100
#define QUEUE_SIZE  256
#define CHUNK       512
#define VOLUME      0.16f
#define SMOOTHING   0.35f

typedef struct {
    float frequency;
    float second_frequency;
    uint32_t samples;
} tone_t;

static tone_t queue[QUEUE_SIZE];
static int head = 0, tail = 0;
static tone_t current;
static uint32_t remaining = 0;
static float phase = 0, second_phase = 0, smoothed = 0;
static SDL_Mutex *lock = NULL;
static SDL_AudioStream *stream = NULL;
static bool sound_on = true;
static bool key_click_on = false;

void beeper_set_sound(bool on) { sound_on = on; }
bool beeper_sound(void) { return sound_on; }
void beeper_set_key_click(bool on) { key_click_on = on; }
bool beeper_key_click(void) { return key_click_on; }

static float next_sample(void) {
    if (remaining == 0) {
        if (tail == head) return 0.0f;
        current = queue[tail];
        tail = (tail + 1) % QUEUE_SIZE;
        remaining = current.samples;
        if (remaining == 0) return 0.0f;
    }
    remaining--;
    float sample = 0.0f;
    if (current.second_frequency > 0) {
        sample = 0.5f * (sinf(phase * 2.0f * (float)M_PI) + sinf(second_phase * 2.0f * (float)M_PI));
    } else if (current.frequency > 0) {
        sample = phase < 0.5f ? 1.0f : -1.0f;
    }
    phase += current.frequency / SAMPLE_RATE;
    second_phase += current.second_frequency / SAMPLE_RATE;
    phase -= floorf(phase);
    second_phase -= floorf(second_phase);
    return sample;
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
            smoothed += (next_sample() - smoothed) * SMOOTHING;
            buffer[i] = smoothed * VOLUME;
        }
        SDL_UnlockMutex(lock);
        SDL_PutAudioStreamData(audio, buffer, count * (int)sizeof(float));
        wanted -= count;
    }
}

bool beeper_init(void) {
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
