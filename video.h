#ifndef VIDEO_H
#define VIDEO_H

void video_render(void);

void video_palette_w(uint32_t address, uint32_t val, int sz);
uint32_t video_palette_r(uint32_t address, int sz);

#endif
