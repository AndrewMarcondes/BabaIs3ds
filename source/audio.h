#pragma once

// Music and sound effects (3DS only). music_init returns -1 if audio is unavailable; the other calls are then no-ops.
int music_init(void);
// Switches to a looping .ogg track (no-op if it's already playing); NULL or "" stops the music.
void music_play(const char *path);
// Plays a short .ogg once (decoded on first use and cached); missing files are silently skipped.
void sfx_play(const char *path);
void music_exit(void);
