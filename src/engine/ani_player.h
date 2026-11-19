/* Plays an ANI on the screen with its sound, using the audio clock as the master.
 *
 * All PCM is queued into an SDL_AudioStream at start. The frame to show is
 * floor(seconds_played * fps), where seconds_played counts the samples the
 * device has consumed minus one device buffer (latency). Every frame's dirty
 * rectangle is applied in order (they are deltas); frames are never dropped
 * from the picture, only presented late if the machine can't keep up.
 */
#ifndef TWTW_ANI_PLAYER_H
#define TWTW_ANI_PLAYER_H

#include <SDL3/SDL.h>

#include "formats/ani.h"
#include "platform/screen.h"

typedef struct AniPlayer {
    Ani ani;
    uint8_t *file;               /* owned file data */
    SDL_AudioStream *stream;     /* bound to the device; NULL -> wall clock */
    SDL_AudioDeviceID device;
    Uint64 start_ticks_ns;
    double latency_s;
    int ox, oy;                  /* placement origin on the screen */
    int next_frame;              /* next frame index to apply */
    int playing;
} AniPlayer;

/* Takes ownership of 'file'. device may be 0 (no sound). Returns 0 on success. */
int ani_player_start(AniPlayer *p, uint8_t *file, size_t size, SDL_AudioDeviceID device, int ox, int oy,
                     char *err, size_t errlen);
/* Applies due frames to the screen. Returns 1 while playing, 0 when finished. */
int ani_player_update(AniPlayer *p, Screen *s);
/* Seconds of content played so far (audio clock). */
double ani_player_time(const AniPlayer *p);
void ani_player_stop(AniPlayer *p);

#endif
