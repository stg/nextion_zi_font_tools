#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "zi_font.h"

// HELPERS

static inline uint8_t q3(uint8_t v8) {
  return (uint8_t)((v8 * 7 + 127) / 255);
}

typedef struct {
  uint8_t *buf;
  uint32_t len, cap;
} obuf_t;

static int o_put(obuf_t *o, uint8_t b) {
  if (o->len == o->cap) {
    uint32_t ncap = o->cap ? (o->cap << 1) : 256;
    uint8_t *nb = (uint8_t *)realloc(o->buf, ncap);
    if (!nb) return -1;
    o->buf = nb;
    o->cap = ncap;
  }
  o->buf[o->len++] = b;
  return 0;
}

// 4-BIT ENCODER

typedef struct {
  uint32_t cost;
  uint8_t tag;
  uint8_t p0;
  uint8_t p1;
} step_t;

static uint32_t runlen_of(const uint32_t n, const uint8_t *flag, uint32_t start, uint32_t maxlen) {
  uint32_t j = start, lim = start + maxlen;
  while (j < n && j < lim && flag[j]) j++;
  return j - start;
}

static int encode_glyph_AA_dp(const uint8_t *src8, uint8_t w, uint8_t h, uint8_t **out, uint32_t *out_len) {
  const uint32_t n = (uint32_t)w * h;
  uint8_t *a = (uint8_t *)malloc(n);
  if (!a) return -1;
  for (uint32_t i = 0; i < n; i++) a[i] = q3(src8[i]);

  // DP arrays
  step_t *step = (step_t *)malloc((n + 1) * sizeof(step_t));
  if (!step) {
    free(a);
    return -1;
  }
  for (uint32_t i = 0; i <= n; i++) step[i].cost = UINT32_MAX;
  step[n].cost = 0;
  step[n].tag = 0xFF;

  // Precompute run lengths of 0 and 7 for fast 00/01/10 checks
  uint8_t is_zero[n ? n : 1], is_opaque[n ? n : 1];
  for (uint32_t i = 0; i < n; i++) {
    is_zero[i] = (a[i] == 0);
    is_opaque[i] = (a[i] == 7);
  }

  // DP from end to start
  for (int32_t i = (int32_t)n - 1; i >= 0; --i) {
    uint32_t best = UINT32_MAX;
    uint8_t btag = 0, p0 = 0, p1 = 0;

    // 00: run of 0 or 7, len 1..31
    if (a[i] == 0 || a[i] == 7) {
      uint8_t v = a[i];
      const uint32_t rl = runlen_of(n, v == 0 ? is_zero : is_opaque, (uint32_t)i, 31);
      for (uint32_t L = 1; L <= rl; ++L) {
        uint32_t cand = 1 + step[i + L].cost;
        if (cand < best) {
          best = cand;
          btag = 0;
          p0 = (v == 7);
          p1 = (uint8_t)L;
        }
      }
    }

    // 01: trans run (1..31) then 1 or 2 opaque 7s
    if (a[i] == 0) {
      uint32_t t = runlen_of(n, is_zero, (uint32_t)i, 31);
      if (t >= 1) {
        uint32_t j = (uint32_t)i + t;
        if (j < n && a[j] == 7) {
          // one opaque
          uint32_t cand1 = 1 + step[j + 1].cost;
          if (cand1 < best) {
            best = cand1;
            btag = 1;
            p0 = 0;
            p1 = (uint8_t)t;
          }
          // two opaque if available
          if (j + 1 < n && a[j + 1] == 7) {
            uint32_t cand2 = 1 + step[j + 2].cost;
            if (cand2 < best) {
              best = cand2;
              btag = 1;
              p0 = 1;
              p1 = (uint8_t)t;
            }
          }
        }
      }
    }

    // 10: short (0..7) trans then one mid-tone (1..6)
    {
      uint32_t t = 0;
      while (t < 8) {
        uint32_t j = (uint32_t)i + t;
        if (j >= n || a[j] != 0) break;
        t++;
      }
      if (t <= 7) {
        uint32_t j = (uint32_t)i + t;
        if (j < n && a[j] != 0 && a[j] != 7) {
          uint32_t cand = 1 + step[j + 1].cost;
          if (cand < best) {
            best = cand;
            btag = 2;
            p0 = (uint8_t)t;
            p1 = (uint8_t)(a[j] & 7);
          }
        }
      }
    }

    // 11: two alphas (any)
    {
      uint8_t c = a[i] & 7;
      uint8_t d = ((uint32_t)(i + 1) < n) ? (a[i + 1] & 7) : 0;
      uint32_t adv = (i + 1 < (int32_t)n) ? 2 : 2; // allow trailing pad
      uint32_t next = (uint32_t)i + adv;
      if (next > n) next = n;
      uint32_t cand = 1 + step[next].cost;
      if (cand < best) {
        best = cand;
        btag = 3;
        p0 = c;
        p1 = d;
      }
    }

    step[i].cost = best;
    step[i].tag = btag;
    step[i].p0 = p0;
    step[i].p1 = p1;
  }

  // Rebuild
  obuf_t o = { 0 };
  if (o_put(&o, 0x03)) {
    free(a);
    free(step);
    return -1;
  }

  uint32_t i = 0;
  while (i < n) {
    uint8_t tag = step[i].tag;
    uint8_t A = step[i].p0, B = step[i].p1;
    if (tag == 0) { // 00 b xxxxx
      // A: b (0=trans,1=opaque), B: len
      if (o_put(&o, (uint8_t)((0u << 6) | ((A & 1) << 5) | (B & 31)))) {
        free(a);
        free(step);
        free(o.buf);
        return -1;
      }
      i += B;
    } else if (tag == 1) { // 01 b xxxxx
      // A: two? (0 or 1), B: trans run
      if (o_put(&o, (uint8_t)((1u << 6) | ((A & 1) << 5) | (B & 31)))) {
        free(a);
        free(step);
        free(o.buf);
        return -1;
      }
      i += B + 1 + (A ? 1 : 0);
    } else if (tag == 2) { // 10 xxx ccc
      // A: trans (0..7), B: alpha (1..6)
      if (o_put(&o, (uint8_t)((2u << 6) | ((A & 7) << 3) | (B & 7)))) {
        free(a);
        free(step);
        free(o.buf);
        return -1;
      }
      i += A + 1;
    } else { // 11 ccc ddd
      if (o_put(&o, (uint8_t)((3u << 6) | ((A & 7) << 3) | (B & 7)))) {
        free(a);
        free(step);
        free(o.buf);
        return -1;
      }
      i += 2;
      if (i > n) i = n; // stay safe
    }
  }

  free(step);
  free(a);
  *out = o.buf;
  *out_len = o.len;
  return 0;
}

// Decide if glyph is binary (only near 0 or near 255)
static int is_binary_glyph(const uint8_t *src, uint32_t count) {
  for (uint32_t i = 0; i < count; i++) {
    uint8_t v = src[i];
    if (!(v <= 3 || v >= 252)) return 0;
  }
  return 1;
}

// MONO ENCODER

typedef struct {
  uint32_t cost;
  uint8_t tag;
  uint8_t p0;
  uint8_t p1;
} bwstep_t;

static uint32_t runlen_bin(const uint32_t n, const uint8_t *arr, uint32_t start, uint32_t maxlen, uint8_t val) {
  uint32_t j = start, lim = start + maxlen;
  while (j < n && j < lim && arr[j] == val) j++;
  return j - start;
}

static int encode_glyph_BW_dp(const uint8_t *src8, uint8_t w, uint8_t h, uint8_t **out, uint32_t *out_len) {
  const uint32_t n = (uint32_t)w * h;
  uint8_t *b = (uint8_t *)malloc(n);
  if (!b) return -1;
  for (uint32_t i = 0; i < n; i++) b[i] = (src8[i] >= 128) ? 1 : 0;
  bwstep_t *step = (bwstep_t *)malloc((n + 1) * sizeof(bwstep_t));
  if (!step) {
    free(b);
    return -1;
  }
  for (uint32_t i = 0; i <= n; i++) step[i].cost = UINT32_MAX;
  step[n].cost = 0;
  step[n].tag = 0xFF;
  for (int32_t i = (int32_t)n - 1; i >= 0; --i) {
    uint32_t best = UINT32_MAX;
    uint8_t tag = 0, p0 = 0, p1 = 0;

    // 00 b xxxxx: run of val (0 or 1), len 1..31
    {
      uint8_t val = b[i];
      uint32_t rl = runlen_bin(n, b, (uint32_t)i, 31, val);
      for (uint32_t L = 1; L <= rl; ++L) {
        uint32_t cand = 1 + step[i + L].cost;
        if (cand < best) {
          best = cand;
          tag = 0;
          p0 = val;
          p1 = (uint8_t)L;
        }
      }
    }

    // 01 b xxxxx: t trans (1..31) then 1 or 2 opaque
    if (b[i] == 0) {
      uint32_t t = runlen_bin(n, b, (uint32_t)i, 31, 0);
      if (t >= 1) {
        uint32_t j = (uint32_t)i + t;
        if (j < n && b[j] == 1) {
          uint32_t cand1 = 1 + step[j + 1].cost; // one opaque
          if (cand1 < best) {
            best = cand1;
            tag = 1;
            p0 = 0;
            p1 = (uint8_t)t;
          }
          if (j + 1 < n && b[j + 1] == 1) {
            uint32_t cand2 = 1 + step[j + 2].cost; // two opaque
            if (cand2 < best) {
              best = cand2;
              tag = 1;
              p0 = 1;
              p1 = (uint8_t)t;
            }
          }
        }
      }
    }

    // 10 b xxxxx: t trans then 3 (b=0) or 4 (b=1) opaque
    if (b[i] == 0) {
      uint32_t t = runlen_bin(n, b, (uint32_t)i, 31, 0);
      if (t <= 31) {
        uint32_t j = (uint32_t)i + t;
        // require 3 or 4 opaques available
        if (j + 2 < n && b[j] == 1 && b[j + 1] == 1 && b[j + 2] == 1) {
          uint32_t cand3 = 1 + step[j + 3].cost; // +3 opaque
          if (cand3 < best) {
            best = cand3;
            tag = 2;
            p0 = 0;
            p1 = (uint8_t)t;
          }
          if (j + 3 < n && b[j + 3] == 1) {
            uint32_t cand4 = 1 + step[j + 4].cost; // +4 opaque
            if (cand4 < best) {
              best = cand4;
              tag = 2;
              p0 = 1;
              p1 = (uint8_t)t;
            }
          }
        }
      }
    }

    // 11 www bbb: www trans (0..7), then bbb opaque (0..7)
    {
      uint32_t t = runlen_bin(n, b, (uint32_t)i, 7, 0);
      uint32_t o = runlen_bin(n, b, (uint32_t)i + t, 7, 1);
      uint32_t adv = t + o;
      if (adv > 0) {
        uint32_t cand = 1 + step[i + adv].cost;
        if (cand < best) {
          best = cand;
          tag = 3;
          p0 = (uint8_t)t;
          p1 = (uint8_t)o;
        }
      }
    }

    step[i].cost = best;
    step[i].tag = tag;
    step[i].p0 = p0;
    step[i].p1 = p1;
  }

  obuf_t o = { 0 };
  if (o_put(&o, 0x01)) {
    free(b);
    free(step);
    return -1;
  }

  uint32_t i = 0;
  while (i < n) {
    uint8_t tag = step[i].tag, A = step[i].p0, B = step[i].p1;
    if (tag == 0) { // 00 b xxxxx
      if (o_put(&o, (uint8_t)((0u << 6) | ((A & 1) << 5) | (B & 31)))) {
        free(b);
        free(step);
        free(o.buf);
        return -1;
      }
      i += B;
    } else if (tag == 1) { // 01 b xxxxx
      if (o_put(&o, (uint8_t)((1u << 6) | ((A & 1) << 5) | (B & 31)))) {
        free(b);
        free(step);
        free(o.buf);
        return -1;
      }
      i += B + 1 + (A ? 1 : 0);
    } else if (tag == 2) { // 10 b xxxxx
      if (o_put(&o, (uint8_t)((2u << 6) | ((A & 1) << 5) | (B & 31)))) {
        free(b);
        free(step);
        free(o.buf);
        return -1;
      }
      i += B + (A ? 4 : 3);
    } else { // 11 www bbb
      if (o_put(&o, (uint8_t)((3u << 6) | ((A & 7) << 3) | (B & 7)))) {
        free(b);
        free(step);
        free(o.buf);
        return -1;
      }
      i += A + B;
    }
  }

  free(step);
  free(b);
  *out = o.buf;
  *out_len = o.len;
  return 0;
}

// ZI font loader


/* Map 3-bit alpha (0..7) to 0..255 */
static inline uint8_t a3_to_a8(uint8_t v3) {
    return (uint8_t)((v3 * 255 + 3) / 7);
}

/* Decode glyph data -> grayscale buffer (alpha as brightness) */
static int decode_glyph(const uint8_t *data, uint32_t len, uint8_t mode,
                        int width, int height, uint8_t *out)
{
    memset(out, 0, (size_t)width * height);
    uint32_t total = (uint32_t)width * height;
    uint32_t wrote = 0;

    for (uint32_t i = 0; i < len && wrote < total; ++i) {
        uint8_t b = data[i];
        uint8_t yz = (b >> 6) & 3;
        uint8_t d  =  b & 0x3F;

        if (mode == 0x03) { /* Anti-aliased */
						if (yz == 0x00) {
                uint8_t opq = (d >> 5) & 1;
                uint8_t cnt = d & 0x1F;
                uint8_t val = opq ? 255 : 0;
                for (uint8_t k=0; k<cnt && wrote<total; ++k) out[wrote++] = val;
            } else if (yz == 0x01) {
                uint8_t two = (d >> 5) & 1;
                uint8_t cnt = d & 0x1F;
                for (uint8_t k=0; k<cnt && wrote<total; ++k) out[wrote++] = 0;
                for (uint8_t k=0; k<(two?2:1) && wrote<total; ++k) out[wrote++] = 255;
            } else if (yz == 0x02) {
                uint8_t trans = (d >> 3) & 7;
                uint8_t ccc   =  d & 7;
                for (uint8_t k=0; k<trans && wrote<total; ++k) out[wrote++] = 0;
                if (wrote<total) out[wrote++] = a3_to_a8(ccc);
            } else { /* 11 */
                uint8_t ccc = (d >> 3) & 7;
                uint8_t ddd =  d & 7;
                if (wrote<total) out[wrote++] = a3_to_a8(ccc);
                if (wrote<total) out[wrote++] = a3_to_a8(ddd);
            }
        } else if (mode == 0x01) { /* Black & White */
            if (yz == 0x00) {
                uint8_t opq = (d >> 5) & 1;
                uint8_t cnt = d & 0x1F;
                uint8_t val = opq ? 255 : 0;
                for (uint8_t k=0; k<cnt && wrote<total; ++k) out[wrote++] = val;
            } else if (yz == 0x01) {
                uint8_t two = (d >> 5) & 1;
                uint8_t cnt = d & 0x1F;
                for (uint8_t k=0; k<cnt && wrote<total; ++k) out[wrote++] = 0;
                for (uint8_t k=0; k<(two?2:1) && wrote<total; ++k) out[wrote++] = 255;
            } else if (yz == 0x02) {
                uint8_t bflag = (d >> 5) & 1;
                uint8_t cnt   = d & 0x1F;
                for (uint8_t k=0; k<cnt && wrote<total; ++k) out[wrote++] = 0;
                for (uint8_t k=0; k<(bflag?4:3) && wrote<total; ++k) out[wrote++] = 255;
            } else { /* 11 */
                uint8_t www = (d >> 3) & 7;
                uint8_t bbb =  d & 7;
                for (uint8_t k=0; k<www && wrote<total; ++k) out[wrote++] = 0;
                for (uint8_t k=0; k<bbb && wrote<total; ++k) out[wrote++] = 255;
            }
        } else {
            fprintf(stderr, "Unknown glyph mode 0x%02X\n", mode);
            return -1;
        }
    }

    if (wrote < total)
        fprintf(stderr, "Warning: decoded %u/%u pixels\n", wrote, total);
    return 0;
}

zi_font_t * zi_load(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return NULL; }
    fseek(f, 0, SEEK_END);
    long szL = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (szL <= 0) { fclose(f); return NULL; }

    uint8_t *buf = malloc(szL);
    fread(buf, 1, szL, f);
    fclose(f);

    uint8_t height = buf[0x07];
    uint32_t glyph_count = (uint32_t)(buf[0x0C] | (buf[0x0D]<<8) |
                                      (buf[0x0E]<<16) | (buf[0x0F]<<24));
    uint8_t desc_len = buf[0x11];
    uint32_t data_addr = (uint32_t)(buf[0x18] | (buf[0x19]<<8) |
                                    (buf[0x1A]<<16) | (buf[0x1B]<<24));
    uint8_t flag_align8 = buf[0x21] & 1;
    uint32_t cmap_off = data_addr + desc_len;

    zi_glyph_t *glyphs = calloc(glyph_count, sizeof(zi_glyph_t));
    const uint8_t *cmap = buf + cmap_off;
    size_t file_size = (size_t)szL;

    // --- read font name string
    char *font_name = malloc(desc_len + 1);
    memcpy(font_name, buf + data_addr, desc_len);
    font_name[desc_len] = '\0';

    for (uint32_t gi = 0; gi < glyph_count; gi++) {
        const uint8_t *e = cmap + gi*10;
        uint16_t code = (uint16_t)(e[0] | (e[1]<<8));
        uint8_t width = e[2];
        uint32_t start_rel = (uint32_t)(e[5] | (e[6]<<8) | (e[7]<<16));
        uint16_t data_len = (uint16_t)(e[8] | (e[9]<<8));

        uint32_t start = flag_align8 ? start_rel * 8 : start_rel;
        uint32_t glyph_off = cmap_off + start;
        if ((uint64_t)glyph_off + data_len > file_size) continue;

        const uint8_t *gdat = buf + glyph_off;
        uint8_t mode = gdat[0];
        const uint8_t *payload = gdat + 1;
        uint32_t plen = data_len - 1;

        uint8_t *gray = malloc((size_t)width * height);
        decode_glyph(payload, plen, mode, width, height, gray);

        glyphs[gi].c = code;
        glyphs[gi].w = width;
        glyphs[gi].data = gray;
    }

    zi_font_t *font = malloc(sizeof(zi_font_t));
    font->font_name = font_name;
    font->height = height;
    font->glyph_count = glyph_count;
    font->glyphs = glyphs;

    free(buf);
    return font;
}

void zi_free(zi_font_t *font) {
    if (!font) return;
    for (uint32_t i = 0; i < font->glyph_count; i++)
        free(font->glyphs[i].data);
    free(font->glyphs);
    free(font->font_name);
    free(font);
}

// ZI font producer

static inline uint32_t align_up(uint32_t v, uint32_t a) {
  return (v + (a - 1)) & ~(a - 1);
}

void zi_make_utf8(const char *file_name, const zi_font_t *font) {
	const char *font_name = font->font_name;
	uint8_t height = font->height;
	uint32_t glyph_count = font->glyph_count;
	zi_glyph_t *glyphs = font->glyphs;
		
  typedef struct {
    uint32_t code;
    uint8_t width;
    uint32_t start_div8; // start offset from START OF CHARMAP (in bytes) divided by 8 if big file
    uint16_t len;        // glyph data length in bytes
    uint8_t *bytes;      // encoded glyph stream (starts with 0x03)
  } GI;

  FILE *f = fopen(file_name, "wb");
  if (!f) {
    perror(file_name);
    return;
  }

  // Encode glyphs
  GI *gi = (GI *)calloc(glyph_count, sizeof(GI));
  if (!gi) {
    fclose(f);
    return;
  }
  uint32_t total_glyph_bytes = 0;
  for (uint32_t i = 0; i < glyph_count; i++) {
    uint8_t *src = glyphs[i].data;
    uint8_t w = glyphs[i].w;
    uint8_t h = height;

    uint8_t *enc = NULL;
    uint32_t elen = 0;
    uint32_t n = (uint32_t)w * h;

    if (n == 0) {
      enc = malloc(1);
      enc[0] = 0x01;
      elen = 1;  // empty glyph
    } else if (is_binary_glyph(src, n)) {
      encode_glyph_BW_dp(src, w, h, &enc, &elen);
    } else {
      encode_glyph_AA_dp(src, w, h, &enc, &elen);
    }

    gi[i].code = glyphs[i].c;
    gi[i].width = w;
    gi[i].bytes = enc;
    gi[i].len = (uint16_t)elen;

    total_glyph_bytes += gi[i].len;
  }

  bool align8 = (total_glyph_bytes > 0xFFFFFFu);

  // Char map takes exactly 10*glyph_count bytes.
  // Offsets in map are measured from start of charmap.
  // With align8 flag set, we store (start/8) in the 24-bit field.
  // First glyph starts at align_up(10*glyph_count, 8).
  
	uint8_t desc_len = (uint8_t)strlen(font_name);
  uint32_t cmap_off = 0x2C + desc_len;                       // file offset where charmap begins
  uint32_t base_from_cmap = align_up(10u * glyph_count, 8u); // first glyph start (bytes from charmap start)

  // Compute start for each glyph
  uint32_t cur_from_cmap = base_from_cmap;
  for (uint32_t i = 0; i < glyph_count; i++) {
    gi[i].start_div8 = cur_from_cmap / (align8 ? 8u : 1u);
    cur_from_cmap += gi[i].len;
    cur_from_cmap = align_up(cur_from_cmap, align8 ? 8u : 1u);
  }
  uint32_t glyph_bytes_total = cur_from_cmap - base_from_cmap;

  // Make the header
  uint32_t total_len = (uint32_t)desc_len + 10u * glyph_count + glyph_bytes_total;

  uint8_t H[0x2C] = { 0 };
  H[0x00] = 0x04; // signature
  H[0x01] = 0xFF; // ? 
  H[0x03] = 0x0A; // orientation (typical)
  H[0x04] = 0x18; // UTF-8 codepage id
  H[0x05] = 0x02; // multibyte subset mode
  H[0x07] = height;
  H[0x08] = 0xFF; // ?
  H[0x09] = 0xFF; // ?
  H[0x0B] = 0xFF; // ?
  H[0x0C] = (uint8_t)(glyph_count & 0xFF);
  H[0x0D] = (uint8_t)((glyph_count >> 8) & 0xFF);
  H[0x0E] = (uint8_t)((glyph_count >> 16) & 0xFF);
  H[0x0F] = (uint8_t)((glyph_count >> 24) & 0xFF);
  H[0x10] = 6;        // version
  H[0x11] = desc_len; // description length (no NUL)
  H[0x14] = (uint8_t)(total_len & 0xFF);
  H[0x15] = (uint8_t)((total_len >> 8) & 0xFF);
  H[0x16] = (uint8_t)((total_len >> 16) & 0xFF);
  H[0x17] = (uint8_t)((total_len >> 24) & 0xFF);
  H[0x18] = 0x2C; // data_addr -> start of description
  H[0x1C] = 0xFF; // ?
  H[0x1E] = 0x01; // ?
  H[0x1F] = 1;    // variable width
	if(desc_len > 5 && !strcmp(&font_name[desc_len - 5], "utf-8")) {
		H[0x20] = desc_len - 5; // description shown
	} else {
		H[0x20] = desc_len; // description shown
	}
  H[0x21] = align8 ? 0x01 : 0x00;          // offsets are divided by 8 or not
  H[0x24] = (uint8_t)(glyph_count & 0xFF); // subset_actual
  H[0x25] = (uint8_t)((glyph_count >> 8) & 0xFF);
  H[0x26] = (uint8_t)((glyph_count >> 16) & 0xFF);
  H[0x27] = (uint8_t)((glyph_count >> 24) & 0xFF);

  fwrite(H, 1, sizeof H, f);
  fwrite(font_name, 1, desc_len, f);

  // Char map entries (10 bytes each)
  for (uint32_t i = 0; i < glyph_count; i++) {
    uint8_t E[10] = { 0 };
    // codepoint is 16-bit in map
    E[0] = (uint8_t)(gi[i].code & 0xFF);
    E[1] = (uint8_t)((gi[i].code >> 8) & 0xFF);
    E[2] = gi[i].width; // width
    // E[3]=kernL=0, E[4]=kernR=0 never figured these out
    // start (24-bit) = from charmap start (may be divided by 8)
    uint32_t S = gi[i].start_div8;
    E[5] = (uint8_t)(S & 0xFF);
    E[6] = (uint8_t)((S >> 8) & 0xFF);
    E[7] = (uint8_t)((S >> 16) & 0xFF);
    // length
    E[8] = (uint8_t)(gi[i].len & 0xFF);
    E[9] = (uint8_t)((gi[i].len >> 8) & 0xFF);
    fwrite(E, 1, 10, f);
  }

  // Pad from end-of-charmap up to the first glyph start (if needed)
  long cur = ftell(f);
  long want = (long)cmap_off + (long)base_from_cmap;
  while (cur < want) {
    static const uint8_t z[8] = { 0 };
    size_t chunk = (size_t)((want - cur) > 8 ? 8 : (want - cur));
    fwrite(z, 1, chunk, f);
    cur += (long)chunk;
  }

  // Write glyph streams with 8-byte padding between them (to match the stored starts)
  for (uint32_t i = 0; i < glyph_count; i++) {
    // ensure current file pos == cmap_off + (gi[i].start_div8 * 8)
    long need = (long)cmap_off + (long)(gi[i].start_div8 * (align8 ? 8u : 1u));
    cur = ftell(f);
    while (cur < need) {
      uint8_t z = 0;
      fwrite(&z, 1, 1, f);
      cur++;
    }
    fwrite(gi[i].bytes, 1, gi[i].len, f);
  }

  fclose(f);
  for (uint32_t i = 0; i < glyph_count; i++) free(gi[i].bytes);
  free(gi);
}
