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
	sleep(3000); /* Wait 3 seconds so we can see the error */
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
	Point tp;
	int i;

	r = screen->r;

	/* Fill background */
	draw(screen, r, bg, nil, ZP);

	/* Draw border */
	draw(screen, Rect(r.min.x, r.min.y, r.max.x, r.min.y + bordersize), borderimg, nil, ZP);
	draw(screen, Rect(r.min.x, r.max.y - bordersize, r.max.x, r.max.y), borderimg, nil, ZP);
	draw(screen, Rect(r.min.x, r.min.y, r.min.x + bordersize, r.max.y), borderimg, nil, ZP);
	draw(screen, Rect(r.max.x - bordersize, r.min.y, r.max.x, r.max.y), borderimg, nil, ZP);

	tp.x = r.min.x + bordersize + padding;
	tp.y = r.min.y + bordersize + padding + font->ascent;

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
	/* Notification window handles its own geometry */
}

void
unhidewindow(void)
{
	int wctl;

	wctl = open("/dev/wctl", OWRITE);
	if(wctl < 0) {
		fprint(2, "herbe: can't open /dev/wctl: %r\n");
		return;
	}

	/* Unhide the window */
	if(write(wctl, "unhide", 6) < 0)
		fprint(2, "herbe: unhide failed: %r\n");

	/* Bring to top without focus */
	if(write(wctl, "top", 3) < 0)
		fprint(2, "herbe: top failed: %r\n");

	close(wctl);
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
spawnnotif(char *text)
{
	char *args[15];
	Point p;
	int pid;

	/* Need minimal display setup to calculate geometry */
	if(initdraw(nil, nil, "herbe") < 0)
		fatal("initdraw: %r");

	font = openfont(display, fontname);
	if(font == nil)
		font = display->defaultfont;

	/* Calculate geometry */
	parsetext(text);
	p = getpos();

	/* Try using individual geometry flags instead of -r */
	pid = fork();
	if(pid == 0) {
		char minx[20], miny[20], maxx[20], maxy[20];

		snprint(minx, sizeof minx, "%d", p.x);
		snprint(miny, sizeof miny, "%d", p.y);
		snprint(maxx, sizeof maxx, "%d", p.x + notif.w + 2*bordersize);
		snprint(maxy, sizeof maxy, "%d", p.y + notif.h + 2*bordersize);

		args[0] = "window";
		args[1] = "-hide"; /* Create window hidden to avoid focus steal */
		args[2] = "-minx";
		args[3] = minx;
		args[4] = "-miny";
		args[5] = miny;
		args[6] = "-maxx";
		args[7] = maxx;
		args[8] = "-maxy";
		args[9] = maxy;
		args[10] = "herbe"; /* Use command name, not argv0 path */
		args[11] = "-d";
		args[12] = text;
		args[13] = nil;

		/* Debug: print what we're trying to execute */
		fprint(2, "herbe: spawning window %s %s %s %s %s %s %s %s %s %s %s %s %s\n",
			args[0], args[1], args[2], args[3], args[4], args[5],
			args[6], args[7], args[8], args[9], args[10], args[11], args[12]);

		exec("/bin/window", args);
		fatal("exec window: %r");
	} else if(pid < 0) {
		fatal("fork: %r");
	}
	/* Parent exits, child window will handle notification */
	exits(nil);
}

void
main(int argc, char *argv[])
{
	char *text;
	int i, timer, etype, indisplay = 0;
	Event e;

	ARGBEGIN {
	case 'd':
		/* -display flag means we're running inside the spawned window */
		indisplay = 1;
		break;
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

	/* If not running in display mode, spawn window and exit */
	if(!indisplay) {
		fprint(2, "herbe: spawning notification window\n");
		spawnnotif(text);
		/* Never reached */
	}

	/* We're running inside the spawned window, display notification */
	fprint(2, "herbe: running in display mode\n");
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

	/* Show the hidden window without stealing focus */
	unhidewindow();

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