#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include "zi_font.h"

// Write 8-bit grayscale TGA (uncompressed)
static int write_tga_gray(const char *path, int w, int h, const uint8_t *gray) {
    uint8_t hdr[18] = {0};
    hdr[2]  = 3;    // uncompressed grayscale image
    hdr[12] = (uint8_t)(w & 0xFF);
    hdr[13] = (uint8_t)((w >> 8) & 0xFF);
    hdr[14] = (uint8_t)(h & 0xFF);
    hdr[15] = (uint8_t)((h >> 8) & 0xFF);
    hdr[16] = 8;     // bits per pixel
    hdr[17] = 0x20;  // top-left origin

    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return -1; }
    fwrite(hdr, 1, 18, f);
    fwrite(gray, 1, (size_t)w*h, f);
    fclose(f);
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <font.zi>\n", argv[0]);
        return 1;
    }

    zi_font_t *font = zi_load(argv[1]);
    if (!font) {
        fprintf(stderr, "Failed to load font file '%s'\n", argv[1]);
        return 1;
    }

    printf("=============================================\n");
    printf(" Font information\n");
    printf("=============================================\n");
    printf(" File:        %s\n", argv[1]);
    printf(" Font name:   %s\n", font->font_name ? font->font_name : "(none)");
    printf(" Height:      %u px\n", font->height);
    printf(" Glyph count: %u\n", font->glyph_count);
    printf("---------------------------------------------\n");

    for (uint32_t i = 0; i < font->glyph_count; i++) {
        const zi_glyph_t *g = &font->glyphs[i];
        char fname[256];
        snprintf(fname, sizeof(fname), "%s_%04X.tga",
                 font->font_name ? font->font_name : "font",
                 g->c);
        write_tga_gray(fname, g->w, font->height, g->data);
        printf(" Glyph U+%04X  width=%u  -> %s\n", g->c, g->w, fname);
    }

    printf("---------------------------------------------\n");
    printf(" Export complete: %u glyphs saved.\n", font->glyph_count);
    printf("=============================================\n");

    zi_free(font);
    return 0;
}