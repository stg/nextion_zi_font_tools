//  DS Prototyp 2025 D.W.Taylor [senseitg@gmail.com]

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <dirent.h>
#include <ctype.h>
#include "zi_font.h"

// TGA loader (uncompressed grayscale)
static uint8_t *load_tga_gray(const char *path, int *w, int *h) {
  FILE *f = fopen(path, "rb");
  if (!f) {
    perror(path);
    return NULL;
  }

  uint8_t hdr[18];
  if (fread(hdr, 1, 18, f) != 18) {
    fclose(f);
    return NULL;
  }

  if (hdr[2] != 3) {  // type 3 = uncompressed grayscale
    fprintf(stderr, "%s: not grayscale type 3 (found %u)\n", path, hdr[2]);
    fclose(f);
    return NULL;
  }

  *w = hdr[12] | (hdr[13] << 8);
  *h = hdr[14] | (hdr[15] << 8);
  uint8_t bpp = hdr[16];
  if (bpp != 8) {
    fprintf(stderr, "%s: expected 8bpp, got %u\n", path, bpp);
    fclose(f);
    return NULL;
  }

  size_t n = (size_t)(*w) * (*h);
  uint8_t *buf = malloc(n);
  if (!buf) {
    fclose(f);
    return NULL;
  }

  if (fread(buf, 1, n, f) != n) {
    fprintf(stderr, "%s: truncated\n", path);
    free(buf);
    fclose(f);
    return NULL;
  }

  fclose(f);
  return buf;
}

// parse "<fontname>_<hex>.tga" filenames
static int parse_glyph_filename(const char *font_name,
                                const char *filename,
                                uint32_t *out_code)
{
    const char *base = strrchr(filename, '/');
    if (!base) base = strrchr(filename, '\\');
    base = base ? base + 1 : filename;

    size_t name_len = strlen(font_name);
    if (strncmp(base, font_name, name_len) != 0)
        return 0; // prefix mismatch

    const char *p = base + name_len;
    if (*p == '_') p++;  // skip underscore after font name if present

    char hex[9] = {0};
    int i = 0;
    while (isxdigit((unsigned char)p[i]) && i < 8) {
        hex[i] = p[i];
				i++;
		}

    if (i == 0)
        return 0;

    *out_code = (uint32_t)strtoul(hex, NULL, 16);
    return 1;
}

static int glyph_compare(const void *a, const void *b) {
  const zi_glyph_t *ga = (const zi_glyph_t *)a;
  const zi_glyph_t *gb = (const zi_glyph_t *)b;
  if (ga->c < gb->c) return -1;
  if (ga->c > gb->c) return 1;
  return 0;
}

int main(int argc, char **argv) {
  if (argc < 4) {
    fprintf(stderr, "Usage: %s <output.zi> <font_name> <height>\n", argv[0]);
    return 1;
  }

  char *out_file = argv[1];
  char *font_name = argv[2];
  uint8_t height = (uint8_t)atoi(argv[3]);

  DIR *dir = opendir(".");
  if (!dir) {
    perror("opendir");
    return 1;
  }

  zi_glyph_t *glyphs = NULL;
  size_t cap = 0, count = 0;
  struct dirent *de;

  while ((de = readdir(dir)) != NULL) {
    if (!strstr(de->d_name, ".tga")) continue;

    uint32_t code;
    if (!parse_glyph_filename(font_name, de->d_name, &code)) continue;

    int w, h;
    uint8_t *img = load_tga_gray(de->d_name, &w, &h);
    if (!img) continue;
    if (h != height) {
      fprintf(stderr, "%s: expected height %u, got %d (skipped)\n", de->d_name, height, h);
      free(img);
      continue;
    }

    if (count == cap) {
      cap = cap ? cap * 2 : 64;
      glyphs = realloc(glyphs, cap * sizeof(zi_glyph_t));
      if (!glyphs) {
        perror("realloc");
        closedir(dir);
        return 1;
      }
    }

    glyphs[count].c = code;
    glyphs[count].w = (uint8_t)w;
    glyphs[count].data = img;
    count++;
  }
  closedir(dir);

  if (count == 0) {
    fprintf(stderr, "No glyph*.tga files found.\n");
    free(glyphs);
    return 1;
  }

  // sort by codepoint (important for predictable charmap)
  qsort(glyphs, count, sizeof(zi_glyph_t), glyph_compare);

  printf("Loaded %zu glyphs, building %s ...\n", count, out_file);

	zi_font_t font = {
		.font_name = font_name,
		.height = height,
		.glyph_count = (uint32_t)count,
		.glyphs = glyphs
	};

	zi_make_utf8(out_file, &font);

  for (size_t i = 0; i < count; ++i) free(glyphs[i].data);
  free(glyphs);

  return 0;
}
