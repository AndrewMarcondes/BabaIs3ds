#pragma once

// Background music (3DS only). music_init returns -1 if audio is unavailable; the other calls are then no-ops.
int music_init(void);
// Switches to a looping .ogg track (no-op if it's already playing); NULL or "" stops the music.
void music_play(const char *path);
void music_exit(void);
