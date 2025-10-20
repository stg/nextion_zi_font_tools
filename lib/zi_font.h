#include <stdint.h>

typedef struct {
  uint16_t c;     // unicode codepoint
  uint8_t w;      // width
  uint8_t *data;  // grayscale pixels (height*w)
} zi_glyph_t;

typedef struct {
	char *font_name;      // description string from .zi header
	uint8_t height;
	uint32_t glyph_count;
	zi_glyph_t *glyphs;
} zi_font_t;

zi_font_t * zi_load(const char *path);
void zi_free(zi_font_t *font);
void zi_make_utf8(const char *file_name, const zi_font_t *font);
	