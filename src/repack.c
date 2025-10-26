//  DS Prototyp 2025 D.W.Taylor [senseitg@gmail.com]

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "zi_font.h"

static long file_size(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long s = ftell(f);
    fclose(f);
    return s;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <input.zi> <output.zi>\n", argv[0]);
        return 1;
    }

    const char *in_file  = argv[1];
    const char *out_file = argv[2];

    long in_size = file_size(in_file);
    if (in_size < 0) {
        fprintf(stderr, "Failed to open input file: %s\n", in_file);
        return 1;
    }

    printf("Loading font: %s (%ld bytes)\n", in_file, in_size);
    zi_font_t *font = zi_load(in_file);
    if (!font) {
        fprintf(stderr, "Failed to read input font\n");
        return 1;
    }

    printf("Re-encoding font \"%s\" (%u glyphs, %u px height)\n",
           font->font_name ? font->font_name : "(unnamed)",
           font->glyph_count, font->height);

    zi_make_utf8(out_file, font);

    long out_size = file_size(out_file);
    if (out_size < 0)
        printf("Wrote: %s\n", out_file);
    else
        printf("Wrote: %s (%ld bytes)\n", out_file, out_size);

    printf("---------------------------------------------\n");
    printf("Size change: %+ld bytes (%+.2f%%)\n",
           out_size - in_size,
           100.0 * ((double)out_size - (double)in_size) / (double)in_size);
    printf("---------------------------------------------\n");

    zi_free(font);
    return 0;
}