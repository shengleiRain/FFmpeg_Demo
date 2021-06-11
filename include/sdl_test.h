#ifndef _FFMPEG_DEMO_SDL_TEST_H
#define _FFMPEG_DEMO_SDL_TEST_H

void hello_sdl2();

void play_yuv(const char* src_file, unsigned int  pixel_w, unsigned int pixel_h);

void play_pcm(const char* src_file);

#endif
