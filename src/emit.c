#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include "zi_font.h"

void emit_zi_font(zi_font_t *font, const char *varname, uint8_t lightness) {
    printf("// Auto-generated compact font data for \"%s\"\n", font->font_name);
    printf("// Each glyph row packed 8 pixels per byte (MSB left)\n\n");

    // Emit packed glyph data
    printf("static const uint8_t %s_data[] = {\n", varname);
    size_t offset = 0;
    for (uint32_t gi = 0; gi < font->glyph_count; gi++) {
        zi_glyph_t *g = &font->glyphs[gi];
        //uint16_t bytes_per_row = (g->w + 7) / 8;
        for (uint8_t y = 0; y < font->height; y++) {
            uint8_t acc = 0, bit = 0;
            for (uint8_t x = 0; x < g->w; x++) {
                uint8_t pix = g->data[y * g->w + x];
                acc = (acc << 1) | (pix > lightness);
                bit++;
                if (bit == 8) {
                    if (offset % 16 == 0) printf("  ");
                    printf("0x%02X,", acc);
                    offset++;
                    bit = 0;
                    if (offset % 16 == 0) printf("\n");
                    acc = 0;
                }
            }
            if (bit) {
                acc <<= (8 - bit);
                if (offset % 16 == 0) printf("  ");
                printf("0x%02X,", acc);
                offset++;
                if (offset % 16 == 0) printf("\n");
            }
        }
    }
    if (offset % 16) printf("\n");
    printf("};\n\n");

    // Emit glyph table
    printf("static const zi_glyph_t %s_glyphs[] = {\n", varname);
    size_t pos = 0;
    for (uint32_t gi = 0; gi < font->glyph_count; gi++) {
        zi_glyph_t *g = &font->glyphs[gi];
        uint16_t bytes_per_row = (g->w + 7) / 8;
        size_t bytes_total = bytes_per_row * font->height;
        printf("  { %u, %u, (uint8_t*)&%s_data[%zu] },\n",
               g->c, g->w, varname, pos);
        pos += bytes_total;
    }
    printf("};\n\n");

    // Emit font structure
    printf("const zi_font_t %s = {\n", varname);
    printf("  %u,\n", font->height);
    printf("  %u,\n", font->glyph_count);
    printf("  (zi_glyph_t*)%s_glyphs\n", varname);
    printf("};\n");
}

int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <font.zi> <name> <lightness>\n", argv[0]);
        return 1;
    }

    const char *filename = argv[1];
    const char *name = argv[2];
    uint8_t lightness = (uint8_t)atoi(argv[3]);

    zi_font_t *font = zi_load(filename);
    if (!font) {
        fprintf(stderr, "Failed to load font file '%s'\n", filename);
        return 1;
    }

    emit_zi_font(font, name, lightness);

    zi_free(font);
    return 0;
}