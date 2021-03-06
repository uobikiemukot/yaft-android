/* See LICENSE for licence details. */
void erase_cell(struct terminal *term, int y, int x)
{
	struct cell_t *cellp;

	cellp             = &term->cells[x + y * term->cols];
	cellp->glyphp     = term->glyph_map[DEFAULT_CHAR];
	cellp->color_pair = term->color_pair; /* bce */
	cellp->attribute  = ATTR_RESET;
	cellp->width      = HALF;

	term->line_dirty[y] = true;
}

void copy_cell(struct terminal *term, int dst_y, int dst_x, int src_y, int src_x)
{
	struct cell_t *dst, *src;

	dst = &term->cells[dst_x + dst_y * term->cols];
	src = &term->cells[src_x + src_y * term->cols];

	if (src->width == NEXT_TO_WIDE)
		return;
	else if (src->width == WIDE && dst_x == (term->cols - 1))
		erase_cell(term, dst_y, dst_x);
	else {
		*dst = *src;
		if (src->width == WIDE) {
			*(dst + 1) = *src;
			(dst + 1)->width = NEXT_TO_WIDE;
		}
		term->line_dirty[dst_y] = true;
	}
}

int set_cell(struct terminal *term, int y, int x, const struct glyph_t *glyphp)
{
	struct cell_t cell, *cellp;
	uint8_t color_tmp;

	cell.glyphp = glyphp;

	cell.color_pair.fg = (term->attribute & attr_mask[ATTR_BOLD] && term->color_pair.fg <= 7) ?
		term->color_pair.fg + BRIGHT_INC: term->color_pair.fg;
	cell.color_pair.bg = (term->attribute & attr_mask[ATTR_BLINK] && term->color_pair.bg <= 7) ?
		term->color_pair.bg + BRIGHT_INC: term->color_pair.bg;

	if (term->attribute & attr_mask[ATTR_REVERSE]) {
		color_tmp          = cell.color_pair.fg;
		cell.color_pair.fg = cell.color_pair.bg;
		cell.color_pair.bg = color_tmp;
	}

	cell.attribute  = term->attribute;
	cell.width      = glyphp->width;

	cellp    = &term->cells[x + y * term->cols];
	*cellp   = cell;
	term->line_dirty[y] = true;

	if (cell.width == WIDE && x + 1 < term->cols) {
		cellp        = &term->cells[x + 1 + y * term->cols];
		*cellp       = cell;
		cellp->width = NEXT_TO_WIDE;
		return WIDE;
	}
	return HALF;
}

void scroll(struct terminal *term, int from, int to, int offset)
{
	int i, j, size, abs_offset;
	struct cell_t *dst, *src;

	if (offset == 0 || from >= to)
		return;

	if (DEBUG)
		LOGE("scroll from:%d to:%d offset:%d\n", from, to, offset);

	for (i = from; i <= to; i++)
		term->line_dirty[i] = true;

	abs_offset = abs(offset);
	size = sizeof(struct cell_t) * ((to - from + 1) - abs_offset) * term->cols;

	dst = term->cells + from * term->cols;
	src = term->cells + (from + abs_offset) * term->cols;

	if (offset > 0) {
		memmove(dst, src, size);
		for (i = (to - offset + 1); i <= to; i++)
			for (j = 0; j < term->cols; j++)
				erase_cell(term, i, j);
	}
	else {
		memmove(src, dst, size);
		for (i = from; i < from + abs_offset; i++)
			for (j = 0; j < term->cols; j++)
				erase_cell(term, i, j);
	}
}

/* relative movement: cause scrolling */
void move_cursor(struct terminal *term, int y_offset, int x_offset)
{
	int x, y, top, bottom;

	x = term->cursor.x + x_offset;
	y = term->cursor.y + y_offset;

	top = term->scroll.top;
	bottom = term->scroll.bottom;

	if (x < 0)
		x = 0;
	else if (x >= term->cols) {
		if (term->mode & MODE_AMRIGHT)
			term->wrap_occured = true;
		x = term->cols - 1;
	}
	term->cursor.x = x;

	y = (y < 0) ? 0:
		(y >= term->lines) ? term->lines - 1: y;

	if (term->cursor.y == top && y_offset < 0) {
		y = top;
		scroll(term, top, bottom, y_offset);
	}
	else if (term->cursor.y == bottom && y_offset > 0) {
		y = bottom;
		scroll(term, top, bottom, y_offset);
	}
	term->cursor.y = y;
}

/* absolute movement: never scroll */
void set_cursor(struct terminal *term, int y, int x)
{
	int top, bottom;

	if (term->mode & MODE_ORIGIN) {
		top = term->scroll.top;
		bottom = term->scroll.bottom;
		y += term->scroll.top;
	}
	else {
		top = 0;
		bottom = term->lines - 1;
	}

	x = (x < 0) ? 0: (x >= term->cols) ? term->cols - 1: x;
	y = (y < top) ? top: (y > bottom) ? bottom: y;

	term->cursor.x = x;
	term->cursor.y = y;
}

void addch(struct terminal *term, uint32_t code)
{
	int width;
	const struct glyph_t *glyphp;

	if (DEBUG)
		LOGE("addch: U+%.4X\n", code);

	width = my_wcwidth(code);

	if (width <= 0) /* zero width */
		return;
	else if (code >= UCS2_CHARS /* yaft support only UCS2 */
		|| term->glyph_map[code] == NULL /* missing glyph */
		|| term->glyph_map[code]->width != width) /* width unmatch */
		glyphp = (width == 1) ? term->glyph_map[SUBSTITUTE_HALF]: term->glyph_map[SUBSTITUTE_WIDE];
	else
		glyphp = term->glyph_map[code];

	if ((term->wrap_occured && term->cursor.x == term->cols - 1) /* folding */
		|| (glyphp->width == WIDE && term->cursor.x == term->cols - 1)) {
		set_cursor(term, term->cursor.y, 0);
		move_cursor(term, 1, 0);
	}
	term->wrap_occured = false;

	move_cursor(term, 0, set_cell(term, term->cursor.y, term->cursor.x, glyphp));
}

void reset_esc(struct terminal *term)
{
	if (DEBUG)
		LOGE("*esc reset*\n");

	/*
	if (term->esc.size > MAX_ESC_SIZE) {
		term->esc.buf  = erealloc(term->esc.buf, MAX_ESC_SIZE);
		term->esc.size = MAX_ESC_SIZE;
	}
	*/

	term->esc.bp = term->esc.buf;
	term->esc.state = STATE_RESET;
}

bool push_esc(struct terminal *term, uint8_t ch)
{
	long offset;

	if ((term->esc.bp - term->esc.buf + 1) == term->esc.size) { /* buffer limit */
		if (DEBUG)
			LOGE("escape sequence length >= %d, term.esc.buf reallocated\n", term->esc.size);
		offset = term->esc.bp - term->esc.buf;
		term->esc.buf  = erealloc(term->esc.buf, term->esc.size * 2);
		term->esc.size *= 2;
		term->esc.bp   = term->esc.buf + offset;
	}

	/* ref: http://www.vt100.net/docs/vt102-ug/appendixd.html */
	*term->esc.bp++ = ch;
	if (term->esc.state == STATE_ESC) {
		/* format:
			ESC  I.......I F
				 ' '  '/'  '0'  '~'
			0x1B 0x20-0x2F 0x30-0x7E
		*/
		if ('0' <= ch && ch <= '~')        /* final char */
			return true;
		else if (SPACE <= ch && ch <= '/') /* intermediate char */
			return false;
	}
	else if (term->esc.state == STATE_CSI) {
		/* format:
			CSI       P.......P I.......I F
			ESC  '['  '0'  '?'  ' '  '/'  '@'  '~'
			0x1B 0x5B 0x30-0x3F 0x20-0x2F 0x40-0x7E
		*/
		if ('@' <= ch && ch <= '~')
			return true;
		else if (SPACE <= ch && ch <= '?')
			return false;
	}
	else {
		/* format:
			OSC       I.....I F
			ESC  ']'          BEL  or ESC  '\'
			0x1B 0x5D unknown 0x07 or 0x1B 0x5C
			DCS       I....I  F
			ESC  'P'          BEL  or ESC  '\'
			0x1B 0x50 unknown 0x07 or 0x1B 0x5C
		*/
		if (ch == BEL || (ch == BACKSLASH
			&& (term->esc.bp - term->esc.buf) >= 2 && *(term->esc.bp - 2) == ESC))
			return true;
		else if ((ch == ESC || ch == CR || ch == LF || ch == BS || ch == HT)
			|| (SPACE <= ch && ch <= '~'))
			return false;
	}

 	/* invalid sequence */
	reset_esc(term);
	return false;
}

void reset_charset(struct terminal *term)
{
	term->charset.code = term->charset.count = term->charset.following_byte = 0;
	term->charset.is_valid = true;
}

void reset(struct terminal *term)
{
	int i, j;

	term->mode = MODE_RESET;
	term->mode |= (MODE_CURSOR | MODE_AMRIGHT);
	term->wrap_occured = false;

	term->scroll.top = 0;
	term->scroll.bottom = term->lines - 1;

	term->cursor.x = term->cursor.y = 0;

	term->state.mode = term->mode;
	term->state.cursor = term->cursor;
	term->state.attribute = ATTR_RESET;

	term->color_pair.fg = DEFAULT_FG;
	term->color_pair.bg = DEFAULT_BG;

	term->attribute = ATTR_RESET;

	for (i = 0; i < term->lines; i++) {
		for (j = 0; j < term->cols; j++) {
			erase_cell(term, i, j);
			if ((j % TABSTOP) == 0)
				term->tabstop[j] = true;
			else
				term->tabstop[j] = false;
		}
		term->line_dirty[i] = true;
	}

	reset_esc(term);
	reset_charset(term);
}

void redraw(struct terminal *term)
{
	int i;

	for (i = 0; i < term->lines; i++)
		term->line_dirty[i] = true;
}

void term_init(struct terminal *term, int width, int height)
{
	uint32_t code, gi;

	term->width  = width;
	term->height = height;

	term->cols  = term->width / CELL_WIDTH;
	term->lines = term->height / CELL_HEIGHT;

	if (DEBUG)
		LOGE("width:%d height:%d cols:%d lines:%d\n",
			width, height, term->cols, term->lines);

	term->line_dirty = (bool *) ecalloc(term->lines, sizeof(bool));
	term->tabstop    = (bool *) ecalloc(term->cols, sizeof(bool));
	term->cells      = (struct cell_t *) ecalloc(term->cols * term->lines, sizeof(struct cell_t));

	term->esc.buf  = (char *) ecalloc(1, MAX_ESC_SIZE);
	term->esc.size = MAX_ESC_SIZE;

	/* initialize glyph map */
	for (code = 0; code < UCS2_CHARS; code++)
		term->glyph_map[code] = NULL;

	for (gi = 0; gi < sizeof(glyphs) / sizeof(struct glyph_t); gi++)
		term->glyph_map[glyphs[gi].code] = &glyphs[gi];

	if (term->glyph_map[DEFAULT_CHAR] == NULL
		|| term->glyph_map[SUBSTITUTE_HALF] == NULL
		|| term->glyph_map[SUBSTITUTE_WIDE] == NULL)
		fatal("cannot find DEFAULT_CHAR or SUBSTITUTE_HALF or SUBSTITUTE_HALF\n");

	reset(term);
}

void term_die(struct terminal *term)
{
	free(term->line_dirty);
	free(term->tabstop);
	free(term->cells);
	free(term->esc.buf);
}
