#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <dirent.h>
#include <math.h>
#include <ctype.h>
#include "upng.h"
#include "zi_font.h"

#define HI(w) (((w) >> 8) & 0xFF)
#define LO(w) ((w) & 0xFF)

// Reads a whole file and returns a pointer to the data chunk
// Needs to be free()'d
void *blob(char *filename, size_t *size) {
	void *data = NULL;
	FILE *f;
	f = fopen(filename, "rb");
	if(f) {
		//printf("F\n");
		fseek(f, 0, SEEK_END);
		*size = ftell(f);
		fseek(f, 0, 0);
		data = malloc(*size + 1);
		if(data) fread(data, 1, *size, f);
		fclose(f);
	}
	return data;
}

typedef struct __attribute__((packed)) {
   uint8_t  idlength;
   uint8_t  colourmaptype;
   uint8_t  datatypecode;
   uint16_t colourmaporigin;
   uint16_t colourmaplength;
   uint8_t  colourmapdepth;
   int16_t  x_origin;
   int16_t  y_origin;
   uint16_t width;
   uint16_t height;
   uint8_t  bitsperpixel;
   uint8_t  imagedescriptor;
   uint8_t  data[];
} tga_t;

_Static_assert(sizeof(tga_t) == 18, "Data structure size incorrect, use mingw-w64");

typedef struct {
	uint16_t c; // following char
	int8_t k; // kerning distance
} kern_t;

#define MAX_GLYPHS 1000
#define MAX_KERN 64

typedef struct {
	uint16_t c;         // character
	uint8_t w, h;       // size of character
	int8_t x, y;        // draw at offset
	uint8_t a;          // advance
	uint8_t *data;      // pixel data
	uint16_t data_size; // size of data (currently w*h, prepared for compression)
	uint8_t kern_count; // number of kerning points
	kern_t kern[MAX_KERN];
} glyph_t;

glyph_t glyph[MAX_GLYPHS];
uint16_t glyph_count;

const bool v_out = true;
const bool g_out = false;

uint8_t *sample_rect(uint8_t * data, uint16_t s, uint16_t x, uint16_t y, uint8_t w, uint8_t h, uint16_t * out_size) {
	*out_size = w * h;
	uint8_t * out = malloc(w * h);
	for(uint16_t yy = 0; yy < h; yy++) {
		for(uint16_t xx = 0; xx < w; xx++) {
			uint8_t pixel;
			pixel = data[(y + yy) * s + (x + xx)];
			out[yy * w + xx] = pixel;
		}
	}
	return out;
}

uint8_t *sample_rect_tga(tga_t * tga, uint16_t x, uint16_t y, uint8_t w, uint8_t h, uint16_t * out_size) {
	*out_size = w * h;
	uint8_t * out = malloc(w * h);
	for(uint16_t yy = 0; yy < h; yy++) {
		for(uint16_t xx = 0; xx < w; xx++) {
			uint8_t pixel;
			if(tga->imagedescriptor & 0x20) {
				pixel = tga->data[(y + yy) * tga->width + (x + xx)];
			} else {
				pixel = tga->data[((tga->height - 1) - (y + yy)) * tga->width + (x + xx)];
			}
			if(g_out) printf("%c", (pixel > 128 ? 'X' : ' '));
			out[yy * w + xx] = pixel;
		}
		if(g_out) printf("\n");
	}
	// TODO: RLE?
	return out;
}

static char tga_fn[256];

// Convert PNG to TGA
int png_to_tga() {
	FILE* fh;
	upng_t* upng;
	unsigned width, height, depth;
	
	upng = upng_new_from_file(tga_fn);
	if (upng_get_error(upng) != UPNG_EOK) {
		printf("Unable to read PNG (%d)\n", upng_get_error(upng));
		return 2;
	}
	upng_header(upng);
	if (upng_get_error(upng) != UPNG_EOK) {
		printf("Unable to parse PNG header (%d)\n", upng_get_error(upng));
		return 3;
	}
	upng_decode(upng);
	if (upng_get_error(upng) != UPNG_EOK) {
		printf("Unable to decode PNG (%d)\n", upng_get_error(upng));
		return 4;
	}

	width = upng_get_width(upng);
	height = upng_get_height(upng);
	depth = upng_get_bpp(upng) / 8;

	if(upng_get_format(upng) != UPNG_RGBA8) {
		printf("Only 32-bit RGBA PNG supported\n");
		return 5;
	}
		
	// Make TGA file
	uint8_t * gs = malloc(width * height);
	if(!gs) {
		printf("Out of memory\n");
		return 1;
	};
	if(strlen(tga_fn) >= sizeof(tga_fn) - 4) {
		printf("Filename too long\n");
		return 1;
	};
	strcat(tga_fn, ".tga");
	fh = fopen(tga_fn, "wb");
	fprintf(fh, "%c%c%c", 0, 0, 3);
	fprintf(fh, "%c%c%c%c%c", 0, 0, 0, 0, 0);
	fprintf(fh, "%c%c%c%c%c%c%c%c%c%c", 0, 0, 0, 0, LO(width), HI(width), LO(height), HI(height), 8, 0);
	for (unsigned y = 0; y < height; ++y) {
		for (unsigned x = 0; x < width; ++x) {
			// Convert RGBA to 8-bit grayscale
			uint8_t v = ((uint16_t)(upng_get_buffer(upng)[(height - y - 1) * width * depth + x * depth + (depth - 1 - 1)])
			           + (uint16_t)(upng_get_buffer(upng)[(height - y - 1) * width * depth + x * depth + (depth - 2 - 1)])
			           + (uint16_t)(upng_get_buffer(upng)[(height - y - 1) * width * depth + x * depth + (depth - 3 - 1)]))
								/ 3;
			v = roundf((float)v * (upng_get_buffer(upng)[(height - y - 1) * width * depth + x * depth + (depth - 0 - 1)]) / 255);
			fputc(v, fh);
		}
	}

  fclose(fh);
	free(gs);
	upng_free(upng);
	
	return 0;
}

// true if filename ends in .png (anycase)
bool ends_with_png(const uint8_t *p_block) {
    size_t len = strlen((const char*)p_block);
    if (len < 4) return false;
    const char *ext = (const char*)p_block + len - 4;
    return (tolower(ext[0]) == '.' &&
            tolower(ext[1]) == 'p' &&
            tolower(ext[2]) == 'n' &&
            tolower(ext[3]) == 'g');
}

void check_glyph(glyph_t * g) {
	const uint8_t threshold = 0;
	uint16_t x1 = 0, y1 = 0;
	uint16_t x0 = g->w, y0 = g->h;
	for(uint16_t y = 0; y < g->h; y++) {
		for(uint16_t x = 0; x < g->w; x++) {
			if(g->data[y * g->w + x] > threshold) {
				if(x < x0) x0 = x;
				if(x + 1 > x1) x1 = x + 1;
				if(y < y0) y0 = y;
				if(y + 1 > y1) y1 = y + 1;
			}
		}
	}
	if(x0 > x1) x0 = x1 = 0;
	if(y0 > y1) y0 = y1 = 0;

	uint8_t *data = sample_rect(g->data, g->w, x0, y0, x1 - x0, y1 - y0, &(g->data_size));
	free(g->data);
	g->data = data;
	g->w = x1 - x0;
	g->h = y1 - y0;
	g->x += x0;
	g->y += y0;
}

// Warning: Monsters Be Here
// No buffer checks, no memory freed, no nothing.
// Essentially junk code, but will work until it's fed bad data.
int main(int argc, char *argv[]) {

	if(argc != 2) {
		printf("Usage: %s <font> (omit .fnt)\n", argv[0]);
		return 1;
	}

	char fn[256];
	size_t font_size = 0;

	sprintf(fn, "%s.fnt", argv[1]);
	uint8_t * font = blob(fn, &font_size);
	//uint16_t base_line;

	if(font_size == 0) {
		printf("Font file not accepted\n");
		return 1;
	}

	uint8_t * p_font = font;
	
	tga_t ** tga;
	
	// Check magic
	if(memcmp(p_font, "BMF", 3)) {
		printf("Not a BMF file");
		return 1;
	}
	p_font += 3; // Skip magic
	// Check version
	if(*p_font != 3) {
		printf("Wrong version of BMF file");
		return 1;
	}
	p_font += 1; // Skip version
	
	while((p_font - font) < font_size) {
		uint8_t block_type = *p_font;
		p_font += 1;
		uint32_t block_size = *(uint32_t *)p_font;
		p_font += 4;
		
		uint8_t * p_block = p_font;
		p_font += block_size;
		
		if(v_out) printf("== BLOCK %u ==\n", block_type);
		if(block_type == 1) { // info
			p_block += 14; // Skip to fontName
			if(v_out) printf("Font: %s\n", p_block);
		} else if(block_type == 2) { // common
			printf("%u %u\n", *(uint16_t *)&p_block[0], *(uint16_t *)&p_block[2]);
			//base_line = *(uint16_t *)&p_block[2];
			p_block += 8;
			tga = malloc(*(uint16_t *)p_block * sizeof(tga_t *));
			if(v_out) printf("OK\n");
		} else if(block_type == 3) { // pages
			uint8_t n;
			while(block_size) {
				size_t tga_size = 0;
				if(v_out) printf("Loading %s\n", p_block);
				// Convert PNG (from modern tools) to TGA (what this was designed for)
				if(strlen((const char *)p_block) >= sizeof(tga_fn)) {
						printf("Filename too long\n");
						return 1;
				}
				strcpy(tga_fn, (const char *)p_block);
				if(ends_with_png(p_block)) {
					if(png_to_tga(p_block)) {
						return 1;
					}
				}
				tga[n] = (tga_t *)blob(tga_fn, &tga_size);
				size_t skip = strlen((const char *)p_block) + 1;
				block_size -= skip;
				p_block += skip;
				if(tga_size < sizeof(tga_t)) {
					printf("Input files not accepted\n");
					return 1;
				}
				if(tga[n]->bitsperpixel != 8) {
					printf("Only 8-bit TGA supported (%u)\n", tga[n]->bitsperpixel);
					return 1;
				}
				if(tga[n]->datatypecode != 3) {
					printf("Only uncompressed grayscale TGA supported\n");
					return 1;
				}	
				n++;
			}
		} else if(block_type == 4) { // chars
			
			while(block_size) {

				if(glyph_count >= MAX_GLYPHS) {
					printf("Too many glyphs\n");
					return 1;
				}

				uint32_t id = *(uint32_t *)&p_block[0]; // unicode id
				uint16_t src_x = *(uint16_t *)&p_block[4];
				uint16_t src_y = *(uint16_t *)&p_block[6];
				uint16_t w = *(uint16_t *)&p_block[8];
				uint16_t h = *(uint16_t *)&p_block[10];
				int16_t ox = *(int16_t *)&p_block[12];
				int16_t oy = *(int16_t *)&p_block[14];
				uint16_t a = *(int16_t *)&p_block[16];
				if(id < 1 || id > 65535) {
					printf("Characted ID %u out of range\n", id);
					return 1;
				}
				//printf("%u\n", id);
				if(w > 255 || h > 255) {
					printf("Glyph size out of range\n");
					return 1;
				}
				if(ox < -128 || ox > 127 || oy < -128 || oy > 127) {
					printf("Glyph offset out of range (%d, %d)\n", ox, oy);
					return 1;
				}
				if(a > 255) {
					printf("Glyph advance out of range\n");
					return 1;
				}
				if(v_out) printf("%c", (char)id);
				glyph[glyph_count].c = id;
				glyph[glyph_count].w = w;
				glyph[glyph_count].h = h;
				glyph[glyph_count].x = ox;
				glyph[glyph_count].y = oy;
				glyph[glyph_count].a = a;

				uint8_t tga_id = p_block[18];
				glyph[glyph_count].data = sample_rect_tga(tga[tga_id], src_x, src_y, w, h, &glyph[glyph_count].data_size);
				
				check_glyph(&glyph[glyph_count]);
				
				glyph_count++;
				
				p_block += 20;
				block_size -= 20;
			}
			if(v_out) printf("\n");
		} else if(block_type == 5) { // kerning pairs
			while(block_size) {
				uint32_t first = *(uint32_t *)&p_block[0];
				uint32_t id = *(uint32_t *)&p_block[4]; // second
				int16_t kern = *(uint16_t *)&p_block[8];
				if(id < 1 || id > 65535) {
					printf("Characted ID %u out of range\n", id);
					return 1;
				}
				if(kern < -128 || kern > 127) {
					printf("Kerning for %u:%u out of range (%d)\n", first, id, kern);
				}
				for(uint16_t n = 0; n < glyph_count; n++) {
					if(glyph[n].c == first) {
						if(glyph[n].kern_count >= MAX_KERN) {
							printf("Too many kerning pairs for glyph %u\n", n);
							return 1;							
						}
						glyph[n].kern[glyph[n].kern_count].c = id;
						glyph[n].kern[glyph[n].kern_count].k = kern;
						glyph[n].kern_count++;
						break;
					}
				}
				
				p_block += 10;
				block_size -= 10;
			}
			if(v_out) printf("Kerning processed\n");
		}
	}

	// Phew, made it - we should have all the data we need.
	// Let's write it out in a manner that can be rendered quickly.
	
	// Output glyph data

 printf("Preparing ZI font output\n");

	//Determine maximum height
	int8_t min_h = 0;
	for (uint16_t i = 0; i < glyph_count; i++) {
			if(glyph[i].y < min_h) min_h = glyph[i].y;
	}
	for (uint16_t i = 0; i < glyph_count; i++) {
		glyph[i].y -= min_h;
		glyph[i].x = 0;
	}
	uint8_t max_h = 0;
	for (uint16_t i = 0; i < glyph_count; i++) {
			int bottom = glyph[i].y + glyph[i].h;
			if (bottom > max_h)
					max_h = bottom;
	}
	printf("Detected font height: %u px\n", max_h);

	// Make unified glyph array for ZI
	zi_glyph_t *zi_glyphs = calloc(glyph_count, sizeof(glyph_t));

	for (uint16_t i = 0; i < glyph_count; i++) {
		
			int full_w = glyph[i].x + glyph[i].w;   // include left offset
			int full_h = max_h;
			if(full_w < glyph[i].a) full_w = glyph[i].a;
			
			uint8_t *dst = calloc((size_t)full_w * full_h, 1); // cleared background
			uint8_t *src = glyph[i].data;

			// Copy glyph bitmap into correct X/Y position
			
			for(uint8_t y = 0; y < glyph[i].h; y++) {
					uint8_t *drow = dst + (y + glyph[i].y) * full_w + glyph[i].x;
					uint8_t *srow = src + y * glyph[i].w;
					memcpy(drow, srow, glyph[i].w);
			}

			zi_glyphs[i].c = glyph[i].c;
			zi_glyphs[i].w = full_w;
			zi_glyphs[i].data = dst;

			//free(src); // release original bitmap
	}

	// Build font struct for ZI
	zi_font_t *zi_font = calloc(1, sizeof(zi_font_t));

	size_t name_len = strlen(argv[1]) + 7; // " utf-8" + null
	zi_font->font_name = malloc(name_len);
	snprintf(zi_font->font_name, name_len, "%sutf-8", argv[1]);

	zi_font->height = max_h;
	zi_font->glyph_count = glyph_count;
	zi_font->glyphs = zi_glyphs;

	// Make .zi file
	char out_file[256];
	snprintf(out_file, sizeof(out_file), "%s.zi", argv[1]);
	printf("Writing output file: %s\n", out_file);
	zi_make_utf8(out_file, zi_font);

	zi_free(zi_font);

	printf("ZI font successfully written.\n");
	
	// Done
	return 0;
}