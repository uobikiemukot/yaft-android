/* See LICENSE for licence details. */
#include <linux/fb.h>
#include <linux/vt.h>
#include <linux/kd.h>

/* shell */
const char *shell_cmd = "/system/bin/sh";

enum keystate {
	SHIFT_MASK = 0x01,
	CTRL_MASK  = 0x02,
	ALT_MASK   = 0x04,
};

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
	//ANativeWindow_Buffer buf;
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
	//bool softkeyboard_visible;
};

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

	return (r << vinfo->red.offset)
		+ (g << vinfo->green.offset) + (b << vinfo->blue.offset);
}

void fb_init(struct framebuffer *fb)
{
	extern const uint32_t color_list[COLORS];
	int i;
	int32_t pixel_format;
	struct fb_vinfo_t vinfo;
	ANativeWindow_Buffer buf;

	fb->width  = ANativeWindow_getWidth(fb->app->window);
	fb->height = ANativeWindow_getHeight(fb->app->window);

	//ANativeWindow_setBuffersGeometry(fb->app->window , 0, 0, WINDOW_FORMAT_RGB_565);
	//ANativeWindow_setBuffersGeometry(fb->app->window , 0, 0, WINDOW_FORMAT_RGBA_8888);

	pixel_format = ANativeWindow_getFormat(fb->app->window);
	if (pixel_format == WINDOW_FORMAT_RGBA_8888
		|| pixel_format == WINDOW_FORMAT_RGBX_8888) {
		vinfo.red.offset   = 0;
		vinfo.green.offset = 8;
		vinfo.blue.offset  = 16;

		vinfo.red.length   = 8;
		vinfo.green.length = 8;
		vinfo.blue.length  = 8;

		fb->bytes_per_pixel = 4;
	}
	else if (pixel_format == WINDOW_FORMAT_RGB_565) {
		vinfo.red.offset   = 0;
		vinfo.green.offset = 5;
		vinfo.blue.offset  = 11;

		vinfo.red.length   = 5;
		vinfo.green.length = 6;
		vinfo.blue.length  = 5;

		fb->bytes_per_pixel = 2;
	}
	else
		fatal("unknown framebuffer type");

	if (DEBUG)
		LOGE("format:%d width:%d height:%d bytes perl pixel:%d\n",
			pixel_format, fb->width, fb->height, fb->bytes_per_pixel);

	for (i = 0; i < COLORS; i++) /* init color palette */
		fb->color_palette[i] = color2pixel(&vinfo, color_list[i]);

	if (ANativeWindow_lock(fb->app->window, &buf, NULL) < 0)
		fatal("ANativeWindow_lock() failed");

	//fb->line_length = fb->width * fb->bytes_per_pixel;
	//fb->screen_size = fb->height * fb->line_length;
	fb->line_length = buf.stride * fb->bytes_per_pixel;
	fb->screen_size = buf.height * fb->line_length;

	ANativeWindow_unlockAndPost(fb->app->window);

	fb->buf   = (unsigned char *) ecalloc(1, fb->screen_size);
	//fb->buf.bits = NULL;
	fb->vinfo = vinfo;

	fb->offset.x = 0; // FIXME: hard coding!!
	fb->offset.y = 25; // FIXME: hard coding!!
	fb->width  -= fb->offset.x;
	fb->height -= fb->offset.y;
}

void fb_die(struct framebuffer *fb)
{
	//(void) fb;
	free(fb->buf);
}

static inline void draw_line(struct framebuffer *fb, struct terminal *term, int line)
{
	int pos, bdf_padding, glyph_width, margin_right;
	int col, w, h;
	uint32_t pixel;
	struct color_pair_t color_pair;
	struct cell_t *cellp;
	const struct glyph_t *glyphp;

	for (col = term->cols - 1; col >= 0; col--) {
		margin_right = (term->cols - 1 - col) * CELL_WIDTH;

		/* target cell */
		cellp = &term->cells[col + line * term->cols];

		/* get color and glyph */
		color_pair = cellp->color_pair;
		glyphp     = cellp->glyphp;

		/* check wide character or not */
		glyph_width = (cellp->width == HALF) ? CELL_WIDTH: CELL_WIDTH * 2;
		bdf_padding = my_ceil(glyph_width, BITS_PER_BYTE) * BITS_PER_BYTE - glyph_width;
		if (cellp->width == WIDE)
			bdf_padding += CELL_WIDTH;

		/* check cursor positon */
		if ((term->mode & MODE_CURSOR && line == term->cursor.y)
			&& (col == term->cursor.x
			|| (cellp->width == WIDE && (col + 1) == term->cursor.x)
			|| (cellp->width == NEXT_TO_WIDE && (col - 1) == term->cursor.x))) {
			color_pair.fg = DEFAULT_BG;
			color_pair.bg = ACTIVE_CURSOR_COLOR;
		}

		for (h = 0; h < CELL_HEIGHT; h++) {
			/* if UNDERLINE attribute on, swap bg/fg */
			if ((h == (CELL_HEIGHT - 1)) && (cellp->attribute & attr_mask[ATTR_UNDERLINE]))
				color_pair.bg = color_pair.fg;

			for (w = 0; w < CELL_WIDTH; w++) {
				pos = (term->width - 1 - margin_right - w + fb->offset.x) * fb->bytes_per_pixel
					+ (line * CELL_HEIGHT + h + fb->offset.y) * fb->line_length;

				/* set color palette */
				if (glyphp->bitmap[h] & (0x01 << (bdf_padding + w)))
					pixel = fb->color_palette[color_pair.fg];
				else
					pixel = fb->color_palette[color_pair.bg];

				/* update copy buffer only */
				memcpy(fb->buf + pos, &pixel, fb->bytes_per_pixel);
			}
		}
	}
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

	if (DEBUG)
		LOGE("format:%d stride:%d width:%d height:%d\n",
			dst_buf.format, dst_buf.stride, dst_buf.width, dst_buf.height);

	if (term->mode & MODE_CURSOR)
		term->line_dirty[term->cursor.y] = true;

	for (line = 0; line < term->lines; line++) {
		if (term->line_dirty[line])
			draw_line(fb, term, line);
	}
	//memcpy(dst_buf.bits, fb->buf.bits, fb->screen_size);
	memcpy(dst_buf.bits, fb->buf, fb->screen_size);

	//fb->buf = dst_buf;

	ANativeWindow_unlockAndPost(fb->app->window);
}
