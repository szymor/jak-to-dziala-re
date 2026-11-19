#include "engine/ani_player.h"

#include <stdlib.h>
#include <string.h>

int ani_player_start(AniPlayer *p, uint8_t *file, size_t size, SDL_AudioDeviceID device, int ox, int oy,
                     char *err, size_t errlen)
{
    memset(p, 0, sizeof *p);
    if (ani_open(&p->ani, file, size, err, errlen) != 0) {
        free(file);
        return -1;
    }
    p->file = file;
    p->ox = ox;
    p->oy = oy;
    p->device = device;
    if (device) {
        SDL_AudioSpec src = {SDL_AUDIO_U8, p->ani.channels, p->ani.rate};
        p->stream = SDL_CreateAudioStream(&src, NULL);
        if (p->stream && SDL_BindAudioStream(device, p->stream)) {
            uint8_t *pcm = malloc(p->ani.audio_bytes ? p->ani.audio_bytes : 1);
            if (pcm) {
                ani_copy_audio(&p->ani, pcm);
                SDL_PutAudioStreamData(p->stream, pcm, (int)p->ani.audio_bytes);
                SDL_FlushAudioStream(p->stream);
                free(pcm);
            }
            SDL_AudioSpec dev;
            int frames = 0;
            if (SDL_GetAudioDeviceFormat(device, &dev, &frames) && dev.freq > 0)
                p->latency_s = (double)frames / dev.freq;
        } else {
            SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO, "ANI sound unavailable: %s", SDL_GetError());
            SDL_DestroyAudioStream(p->stream);
            p->stream = NULL;
        }
    }
    p->start_ticks_ns = SDL_GetTicksNS();
    p->playing = 1;
    return 0;
}

double ani_player_time(const AniPlayer *p)
{
    if (p->stream && !p->drained_ns) {
        /* Input bytes not yet pulled by the device are still queued in the stream. */
        int queued = SDL_GetAudioStreamQueued(p->stream);
        double played = (double)((long)p->ani.audio_bytes - (queued > 0 ? queued : 0)) / p->ani.rate - p->latency_s;
        return played > 0 ? played : 0;
    }
    if (p->drained_ns)
        return p->drained_at_s + (double)(SDL_GetTicksNS() - p->drained_ns) / 1e9;
    return (double)(SDL_GetTicksNS() - p->start_ticks_ns) / 1e9;
}

int ani_player_update(AniPlayer *p, Screen *s)
{
    if (!p->playing)
        return 0;
    if (p->stream && !p->drained_ns && SDL_GetAudioStreamQueued(p->stream) <= 0) {
        p->drained_at_s = (double)p->ani.audio_bytes / p->ani.rate;   /* the device buffer (latency) still plays */
        p->drained_ns = SDL_GetTicksNS();
        p->drained_at_s -= p->latency_s;
        if (p->drained_at_s < 0)
            p->drained_at_s = 0;
    }
    int due = (int)(ani_player_time(p) * p->ani.fps);     /* frame j is shown at t = j / fps */
    if (due >= p->ani.frames)
        due = p->ani.frames - 1;
    while (p->next_frame <= due) {
        if (ani_apply_frame(&p->ani, p->next_frame, s->pixels, SCREEN_W, SCREEN_H, SCREEN_W, p->ox, p->oy) > 0)
            s->dirty = 1;
        p->next_frame++;
    }
    double end = (double)p->ani.frames / p->ani.fps;
    double audio_end = (double)p->ani.audio_bytes / p->ani.rate;
    if (audio_end > end)
        end = audio_end;                                   /* play the sound to the end too */
    if (p->next_frame >= p->ani.frames && ani_player_time(p) >= end - (p->stream ? p->latency_s : 0))
        p->playing = 0;
    return p->playing;
}

void ani_player_stop(AniPlayer *p)
{
    if (p->stream) {
        SDL_DestroyAudioStream(p->stream);   /* unbinds and drops queued sound */
        p->stream = NULL;
    }
    ani_close(&p->ani);
    free(p->file);
    p->file = NULL;
    p->playing = 0;
}
