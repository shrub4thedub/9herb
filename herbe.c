#include <u.h>
#include <libc.h>
#include <draw.h>
#include <event.h>
#include <keyboard.h>

#include "config.h"

enum {
	STACK = 8192,
	NOTIFID = 1234,
};

typedef struct {
	int x, y;
	int w, h;
	char **lines;
	int nlines;
} Notif;

Font *font, *titlefont;
Image *bg, *borderimg, *textcolor;
Notif notif;
int exitcode = 2;

void
usage(void)
{
	fprint(2, "usage: %s text\n", argv0);
	exits("usage");
}

void
fatal(char *fmt, ...)
{
	va_list arg;
	char buf[1024];

	va_start(arg, fmt);
	vsnprint(buf, sizeof buf, fmt, arg);
	va_end(arg);
	fprint(2, "herbe: %s\n", buf);
	exits("fatal");
}

int
textwidth(Font *f, char *s)
{
	return stringwidth(f, s);
}

int
getmaxlen(char *s, Font *f, int maxwidth)
{
	int len, width, i;

	len = strlen(s);
	width = textwidth(f, s);

	if(width <= maxwidth)
		return len;

	for(i = 0; i < len && s[i] != '\n'; i++) {
		width = textwidth(f, s);
		if(width > maxwidth) {
			while(i > 0 && s[i] != ' ')
				i--;
			if(i == 0)
				return len < maxwidth/f->height ? len : maxwidth/f->height;
			return i + 1;
		}
	}

	if(s[i] == '\n') {
		s[i] = ' ';
		return i + 1;
	}

	return len;
}

void
parsetext(char *text)
{
	int maxwidth, len, size;
	char *p;

	maxwidth = width - 2 * padding;
	notif.nlines = 0;
	size = 5;
	notif.lines = malloc(size * sizeof(char*));
	if(notif.lines == nil)
		fatal("malloc: %r");

	p = text;
	while(*p) {
		len = getmaxlen(p, font, maxwidth);
		if(len == 0)
			break;

		if(notif.nlines >= size) {
			size += 5;
			notif.lines = realloc(notif.lines, size * sizeof(char*));
			if(notif.lines == nil)
				fatal("realloc: %r");
		}

		notif.lines[notif.nlines] = malloc(len + 1);
		if(notif.lines[notif.nlines] == nil)
			fatal("malloc: %r");

		memmove(notif.lines[notif.nlines], p, len);
		notif.lines[notif.nlines][len] = '\0';
		notif.nlines++;
		p += len;
	}
}

Point
getpos(void)
{
	Point p;
	Rectangle r;

	r = screen->r;
	notif.h = notif.nlines * font->height + (notif.nlines - 1) * linespacing + 3 * padding;
	notif.w = width;

	switch(corner) {
	case TOPLEFT:
		p.x = posx;
		p.y = posy;
		break;
	case TOPRIGHT:
		p.x = r.max.x - notif.w - bordersize * 2 - posx;
		p.y = posy;
		break;
	case BOTTOMLEFT:
		p.x = posx;
		p.y = r.max.y - notif.h - bordersize * 2 - posy;
		break;
	case BOTTOMRIGHT:
		p.x = r.max.x - notif.w - bordersize * 2 - posx;
		p.y = r.max.y - notif.h - bordersize * 2 - posy;
		break;
	}

	return p;
}

void
drawnotif(void)
{
	Rectangle r;
	Point p, tp;
	int i;

	p = getpos();
	r = Rect(p.x, p.y, p.x + notif.w, p.y + notif.h);

	draw(screen, insetrect(r, -bordersize), borderimg, nil, ZP);
	draw(screen, r, bg, nil, ZP);

	tp.x = r.min.x + padding;
	tp.y = r.min.y + padding + font->ascent;

	for(i = 0; i < notif.nlines; i++) {
		if(i == 0 && titlefont != nil) {
			string(screen, tp, textcolor, ZP, titlefont, notif.lines[i]);
			tp.y += titlefont->height + linespacing;
		} else {
			string(screen, tp, textcolor, ZP, font, notif.lines[i]);
			tp.y += font->height + linespacing;
		}
	}

	flushimage(display, 1);
}


void
eresized(int new)
{
	if(new && getwindow(display, Refnone) < 0)
		fatal("can't reattach to window");
	drawnotif();
}

void
cleanup(void)
{
	int i;

	if(notif.lines) {
		for(i = 0; i < notif.nlines; i++)
			free(notif.lines[i]);
		free(notif.lines);
	}

	if(bg) freeimage(bg);
	if(borderimg) freeimage(borderimg);
	if(textcolor) freeimage(textcolor);
	if(font && font != display->defaultfont) freefont(font);
	if(titlefont && titlefont != display->defaultfont) freefont(titlefont);
}

void
main(int argc, char *argv[])
{
	char *text;
	int i, timer, etype;
	Event e;

	ARGBEGIN {
	default:
		usage();
	} ARGEND

	if(argc < 1)
		usage();

	text = malloc(1);
	text[0] = '\0';
	for(i = 0; i < argc; i++) {
		text = realloc(text, strlen(text) + strlen(argv[i]) + 2);
		if(text == nil)
			fatal("realloc: %r");
		if(i > 0)
			strcat(text, " ");
		strcat(text, argv[i]);
	}

	if(initdraw(nil, nil, "herbe") < 0)
		fatal("initdraw: %r");

	font = openfont(display, fontname);
	if(font == nil)
		font = display->defaultfont;

	titlefont = openfont(display, titlefontname);
	if(titlefont == nil)
		titlefont = font;

	bg = allocimage(display, Rect(0, 0, 1, 1), CMAP8, 1, bgcolor);
	borderimg = allocimage(display, Rect(0, 0, 1, 1), CMAP8, 1, bordercolor);
	textcolor = allocimage(display, Rect(0, 0, 1, 1), CMAP8, 1, fontcolor);

	if(bg == nil || borderimg == nil || textcolor == nil)
		fatal("allocimage: %r");

	parsetext(text);
	free(text);

	einit(Emouse|Ekeyboard);

	if(duration > 0)
		timer = etimer(0, duration * 1000);

	drawnotif();

	for(;;) {
		etype = event(&e);

		switch(etype) {
		case Emouse:
			if(e.mouse.buttons & 1) {
				exitcode = 2;
				goto done;
			}
			if(e.mouse.buttons & 4) {
				exitcode = 0;
				goto done;
			}
			break;
		case Ekeyboard:
			if(e.kbdc == 'q' || e.kbdc == Kdel) {
				exitcode = 2;
				goto done;
			}
			break;
		default:
			if(duration > 0 && etype == timer) {
				exitcode = 2;
				goto done;
			}
			break;
		}
	}

done:
	cleanup();
	exits(exitcode == 0 ? nil : "dismissed");
}