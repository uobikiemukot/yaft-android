/* See LICENSE for licence details. */
#include <linux/fb.h>
#include <linux/vt.h>
#include <linux/kd.h>

/* shell */
const char *shell_cmd = "/system/bin/sh";

struct bitfield_t {
	uint8_t offset;
	uint8_t length;
};

struct fb_vinfo_t {
	struct bitfield_t red;
	struct bitfield_t green;
	struct bitfield_t blue;
};

/* struct for android */
struct framebuffer {
	unsigned char *buf;             /* copy of framebuffer */
	int width, height;              /* display resolution */
	int line_length;                /* line length (byte) */
	long screen_size;
	int bytes_per_pixel;            /* BYTES per pixel */
	uint32_t color_palette[COLORS]; /* 256 color palette */
	struct point_t offset;
	struct fb_vinfo_t vinfo;
	struct android_app *app;
};

struct app_state {
	struct terminal *term;
	struct framebuffer *fb;
	int keystate;
	bool focused;
	bool initialized;
};

//ARect visible_rect; /* set by onContentRectChanged() in yaft.c */

/* common functions */
static inline uint32_t color2pixel(struct fb_vinfo_t *vinfo, uint32_t color)
{
	uint32_t r, g, b;

	/* direct color */
	r = bit_mask[8] & (color >> 16);
	g = bit_mask[8] & (color >> 8);
	b = bit_mask[8] & (color >> 0);

	r = r >> (BITS_PER_BYTE - vinfo->red.length);
	g = g >> (BITS_PER_BYTE - vinfo->green.length);
	b = b >> (BITS_PER_BYTE - vinfo->blue.length);

	//return bit_reverse((r << vinfo->red.offset)
		//+ (g << vinfo->green.offset) + (b << vinfo->blue.offset), 16);
	return (r << vinfo->red.offset)
		+ (g << vinfo->green.offset) + (b << vinfo->blue.offset);
}

void fb_init(struct framebuffer *fb)
{
	extern const uint32_t color_list[COLORS];
	int i;
	int32_t pixel_format;
	struct fb_vinfo_t vinfo;

	fb->width  = ANativeWindow_getWidth(fb->app->window);
	fb->height = ANativeWindow_getHeight(fb->app->window);
	/*
	fb->width   = visible_rect.right - visible_rect.left;
	fb->height  = visible_rect.bottom - visible_rect.top;
	fb->offset.y = visible_rect.top;
	fb->offset.x = visible_rect.left;
	*/

	pixel_format = ANativeWindow_getFormat(fb->app->window);
	if (pixel_format == WINDOW_FORMAT_RGBA_8888
		|| pixel_format == WINDOW_FORMAT_RGBX_8888) {
		vinfo.red.offset   = 16;
		vinfo.green.offset = 8;
		vinfo.blue.offset  = 0;

		vinfo.red.length   = 8;
		vinfo.green.length = 8;
		vinfo.blue.length  = 8;

		fb->bytes_per_pixel = 4;
	}
	else if (pixel_format == WINDOW_FORMAT_RGB_565) {
		vinfo.red.offset   = 11;
		vinfo.green.offset = 5;
		vinfo.blue.offset  = 0;

		vinfo.red.length   = 5;
		vinfo.green.length = 6;
		vinfo.blue.length  = 5;

		fb->bytes_per_pixel = 2;
	}
	else
		fatal("unknown framebuffer type");

	if (DEBUG) {
		LOGE("fb_init()\n");
		LOGE("width:%d height:%d\n", fb->width, fb->height);
		LOGE("pixel_format:%d bpp:%d\n", pixel_format, fb->bytes_per_pixel);
	}

	for (i = 0; i < COLORS; i++) /* init color palette */
		fb->color_palette[i] = color2pixel(&vinfo, color_list[i]);

	fb->line_length = ANativeWindow_getWidth(fb->app->window) * fb->bytes_per_pixel;
	fb->screen_size = ANativeWindow_getHeight(fb->app->window) * fb->line_length;

	fb->buf   = (unsigned char *) ecalloc(1, fb->screen_size);
	fb->vinfo = vinfo;

	fb->offset.x = 0; // FIXME: hard coding!!
	fb->offset.y = 25; // FIXME: hard coding!!
	fb->height -= fb->offset.y;
}

void fb_die(struct framebuffer *fb)
{
	//(void) fb;
	/* FIXME: should we release something else? */
	if (DEBUG)
		LOGE("fb_die()\n");
	free(fb->buf);
}

static inline void draw_line(struct framebuffer *fb, struct terminal *term, int line)
{
	int pos, bit_shift, margin_right;
	int col, w, h;
	uint32_t pixel;
	struct color_pair_t color_pair;
	struct cell_t *cellp;
	const struct glyph_t *glyphp;

	/*
	fb->yoffset = fb->app->contentRect.top;
	fb->height -= fb->yoffset;

	LOGE("yoffset:%d\n", fb->yoffset);
	*/

	for (col = term->cols - 1; col >= 0; col--) {
		margin_right = (term->cols - 1 - col) * CELL_WIDTH;

		/* target cell */
		cellp = &term->cells[col + line * term->cols];

		/* get color and glyph */
		color_pair = cellp->color_pair;
		glyphp     = cellp->glyphp;

		/* check wide character or not */
		bit_shift = (cellp->width == WIDE) ? CELL_WIDTH: 0;

		/* check cursor positon */
		if ((term->mode & MODE_CURSOR && line == term->cursor.y)
			&& (col == term->cursor.x
			|| (cellp->width == WIDE && (col + 1) == term->cursor.x)
			|| (cellp->width == NEXT_TO_WIDE && (col - 1) == term->cursor.x))) {
			color_pair.fg = DEFAULT_BG;
			color_pair.bg = (!tty.visible && BACKGROUND_DRAW) ? PASSIVE_CURSOR_COLOR: ACTIVE_CURSOR_COLOR;
		}

		for (h = 0; h < CELL_HEIGHT; h++) {
			/* if UNDERLINE attribute on, swap bg/fg */
			if ((h == (CELL_HEIGHT - 1)) && (cellp->attribute & attr_mask[ATTR_UNDERLINE]))
				color_pair.bg = color_pair.fg;

			for (w = 0; w < CELL_WIDTH; w++) {
				pos = (term->width - 1 - margin_right - w + fb->offset.x) * fb->bytes_per_pixel
					+ (line * CELL_HEIGHT + h + fb->offset.y) * fb->line_length;

				/* set color palette */
				if (glyphp->bitmap[h] & (0x01 << (bit_shift + w)))
					//pixel = color2pixel(&fb->vinfo, color_list[color_pair.fg]);
					pixel = fb->color_palette[color_pair.fg];
				else
					//pixel = color2pixel(&fb->vinfo, color_list[color_pair.bg]);
					pixel = fb->color_palette[color_pair.bg];

				/* update copy buffer only */
				//memcpy(((unsigned char *) fb->buf.bits) + pos, &pixel, fb->bytes_per_pixel);
				//memcpy(fb->fp + pos, &pixel, fb->bytes_per_pixel);
				memcpy(fb->buf + pos, &pixel, fb->bytes_per_pixel);
			}
		}
	}

	/* actual display update (bit blit) */
	/*
	int size;
	pos = (line * CELL_HEIGHT + fb->yoffset) * fb->line_length;
	size = CELL_HEIGHT * fb->line_length;
	memcpy(fb->fp + pos, fb->buf + pos, size);
	*/

	//LOGE("done drawing\n");
	term->line_dirty[line] = ((term->mode & MODE_CURSOR) && term->cursor.y == line) ? true: false;
}

void refresh(struct framebuffer *fb, struct terminal *term)
{
	int line;
	ANativeWindow_Buffer dst_buf;

	if (fb->app->window == NULL)
		return;

	if (ANativeWindow_lock(fb->app->window, &dst_buf, NULL) < 0)
		return;

	//fb->buf = (unsigned char *) fb->native_buf.bits;
	//fb->fp = (unsigned char *) dst_buf.bits;

	if (term->mode & MODE_CURSOR)
		term->line_dirty[term->cursor.y] = true;

	for (line = 0; line < term->lines; line++) {
		if (term->line_dirty[line])
			draw_line(fb, term, line);
	}
	memcpy(dst_buf.bits, fb->buf, fb->screen_size);
	//memcpy(((unsigned char *) dst_buf.bits) + fb->offset.y * fb->line_length,
		//fb->buf + fb->offset.y * fb->line_length, fb->height * fb->line_length);

	ANativeWindow_unlockAndPost(fb->app->window);
}
