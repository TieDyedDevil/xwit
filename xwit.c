/* $Id: xwit.c,v 3.4 97/10/20 18:32:49 dd Exp $ */
/*
 * Pop up or iconify the current xterm window (using its WINDOW_ID in the env)
 * or a given window id or a list of window matching names. etc...
 * A miscellany of trivial functions.
 *
 * Copyright 1991 CETIA
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of CETIA not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  CETIA makes no representations about the
 * suitability of this software for any purpose.  It is provided "as is"
 * without express or implied warranty.
 *
 * CETIA DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING ALL
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO EVENT SHALL CETIA
 * BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * Original by Mark M Martin. cetia 93/08/13 r1.6 mmm@cetia.fr
 *
 * This version by David DiGiacomo, david@slack.com.
 */
#include <X11/Xatom.h>
#include <X11/Xos.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xproto.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <sys/time.h>
#include "dsimple.h"
#include "ClientWin.h"

/* note: called by dsimple.c code, must be global */
void
usage()
{
	static char Revision[] = "$Revision: 3.4 $";
	char *rbeg, *rend;

	for (rbeg = Revision; *rbeg; rbeg++) {
		if (*rbeg == ' ') {
			break;
		}
	}
	if (*rbeg) {
		for (rend = ++rbeg; *rend; rend++) {
			if (*rend == ' ') {
				*rend = 0;
				break;
			}
		}
		fprintf(stderr, "%s version %s\n\n",
			program_name, rbeg);
	}

	fprintf(stderr,
	"usage: %s -display <display> -sync\n\
	-pop -focus -iconify -unmap -print \n\
	-raise -lower -opposite -[un]circulate\n\
	-resize w h -rows r -columns c -[r]move x y\n\
	-[r]warp x y -colormap <colormapid> -[no]save\n\
	-name <name> -iconname <name> -property <lookfor>\n\
	-bitmap <file> -mask <file> -[r]iconmove x y\n\
	-[no]backingstore -[no]saveunder\n\
	-[no]keyrepeat keycode ... keycode - keycode\n\
	-id <windowid> -root -current -select -all\n\
	-names <initialsubstrings>... [must be last]\n",
		program_name);
	exit(2);
}

enum functions {
    pop, focus, icon, unmap, colormap,
    print,
    raise, lower, opposite, circulate, uncirculate,
    move, rmove, warp, rwarp,
    resize, save, nosave,
    keyrepeat, nokeyrepeat,
    name, iconname,
    rows, columns,
    iconbitmap, iconmove, riconmove,
    F_winattr,
    lastfunc
};
static long function;
#define	FBIT(func)	(1 << (func))

/* options that don't need a window */
#define	NOWINDOW \
	(FBIT(save) | FBIT(nosave) | \
	FBIT(keyrepeat) | FBIT(nokeyrepeat) | \
	FBIT(rwarp))

static enum winidmode {
	WID_none,
	WID_env,
	WID_num,
	WID_root,
	WID_select,
	WID_curr,
	WID_names,
} Winidmode;

static Window root;
static int tox, toy;
static int Gright, Gbottom;
static int towidth, toheight, warpx, warpy;
static Colormap cmap;
static char **names;	/* list of names to avoid */
static int numnames;
static int keys[256];
static char *wmname;
static char *wmiconname;
static int Giconx, Gicony;
static int nrows;
static int ncolumns;
static int nbuffer;
static char *bitmapname;
static char *maskname;
static int Gbs, Gsu;

/* set if we found a window to act on*/
static int Gwinfound;

/* forward declarations */
static void doit(Window);

/*
 * sleep for given millisecs for those without usleep
 */
static void
mssleep(int ms)
{
    struct timeval tv;
    tv.tv_sec = ms/1000;
    tv.tv_usec = (ms%1000)*1000;
    select(0,NULL,NULL,NULL,&tv);
}

static Atom property = XA_WM_NAME;

static Bool MyFetchName(Display *display, Window w, unsigned char **name)
{
	Atom returnedType;
	int returnedFormat;
	unsigned long number;
	unsigned long bytesAfterReturn;
	unsigned char *data;

	if( Success != XGetWindowProperty(display, w, property,
				0, (long)BUFSIZ, False,
				XA_STRING,
				&returnedType, &returnedFormat,
				&number, &bytesAfterReturn, &data)) {
		*name = NULL;
		return False;
	} else if( returnedType != XA_STRING || returnedFormat != 8 ) {
		if(data)
			XFree(data);
		*name = NULL;
		return False;
	} else {
		*name = data;
		return (data!=NULL)?True:False;
	}
}


/* See if the given atom of the _NET_* family is supported on the
   display.  */

static Bool HasNetAtom(Display *dpy, Atom atom)
{
	static Atom *atoms;
	static unsigned long n_atoms = -1;
	int i;

	if (n_atoms == -1) {
		Atom type;
		int format;
		unsigned long bytes_after;

		n_atoms = 0;
		XGetWindowProperty (dpy, RootWindow (dpy, screen),
				    XInternAtom(dpy, "_NET_SUPPORTED", True),
				    0, LONG_MAX, False, XA_ATOM, &type, &format,
				    &n_atoms, &bytes_after, (void *)&atoms);

		if (type != XA_ATOM)
			return False;
	}

	if (atoms == NULL)
		return False;

	for (i = 0; i < n_atoms; i++)
		if (atoms[i] == atom)
			return True;
	return False;
}

/*
 * find all windows below this and if name matches call doit on it
 */
static void
downtree(Window top)
{
    Window *child, dummy;
    unsigned int children, i;
    char **cpp;
    unsigned char *name;
    if (XQueryTree(dpy, top, &dummy, &dummy, &child, &children)==0)
	Fatal_Error("XQueryTree failed");
    for (i=0; i<children; i++)
    if(MyFetchName (dpy, child[i], &name)){
	for(cpp = names;*cpp!=0;cpp++)
	    if(strncmp(*cpp, (char*)name, strlen(*cpp))==0){
		doit(child[i]);
		break;
	    }
	XFree(name);
    } else
	downtree(child[i]);	/* dont go down if found a name */
    if(child)XFree((char *)child);
}


/*
 * [un]set autorepeat for individual keys
 */
static void
setrepeat(void)
{
    unsigned long value_mask;
    XKeyboardControl values;
    int i;

    value_mask = KBKey|KBAutoRepeatMode;
    values.auto_repeat_mode = (function & FBIT(keyrepeat)) ? 
	AutoRepeatModeOn : AutoRepeatModeOff;

    for(i=0;i<256;i++)
    if(keys[i]){
	values.key = i;
	XChangeKeyboardControl(dpy, value_mask, &values);
    }
}

/*
 * get window position, compensating for decorations
 * (based on xwininfo.c)
 */
static void
getpos(Window window, int *xp, int *yp)
{
	XWindowAttributes attributes;
	int rx, ry;
	Window junkwin;

	if (XGetWindowAttributes(dpy, window, &attributes) == 0)
		Fatal_Error("XGetWindowAttributes(0x%x)", window);

	(void) XTranslateCoordinates(dpy, window, attributes.root,
		-attributes.border_width, -attributes.border_width,
		&rx, &ry, &junkwin);

	*xp = rx - attributes.x;
	*yp = ry - attributes.y;
}

/*
 * get window size
 */
static void
getsize(Window window, int *wp, int *hp)
{
	XWindowAttributes attributes;

	if (XGetWindowAttributes(dpy, window, &attributes) == 0)
		Fatal_Error("XGetWindowAttributes(0x%x)", window);

	*wp = attributes.width;
	*hp = attributes.height;
}

/*
 * set window position
 */
static void
domove(Window window, int x, int y, int right, int bottom)
{
	XWindowChanges values;
	unsigned int value_mask;

	if (right || bottom) {
		XWindowAttributes win_attr, frame_attr;
		Window wmframe;

		if (XGetWindowAttributes(dpy, window, &win_attr) == 0)
			Fatal_Error("XGetWindowAttributes(0x%x)", window);

		/* find our window manager frame, if any */
		for (wmframe = window; ; ) {
			Status status;
			Window wroot, parent;
			Window *childlist;
			unsigned int ujunk;

			status = XQueryTree(dpy, wmframe,
				&wroot, &parent, &childlist, &ujunk);
			if (parent == wroot || !parent || !status)
				break;
			wmframe = parent;
			if (status && childlist)
				XFree((char *) childlist);
		}

#ifndef FVWM2
		/*
		 * Norman R. McBride <norm@city.ac.uk> reports that
		 * this code doesn't work with fvwm2, and I don't have
		 * a fix yet, sorry. - DD
		 */
		if (wmframe != window) {
			if (!XGetWindowAttributes(dpy, wmframe, &frame_attr))
				Fatal_Error("XGetWindowAttributes(0x%x)",
					wmframe);

			win_attr.width = frame_attr.width;
			win_attr.height = frame_attr.height;
			win_attr.border_width +=
				frame_attr.border_width;
		}
#endif /* !FVWM2 */

		if (right)
			x += DisplayWidth(dpy, screen) -
				win_attr.width -
				win_attr.border_width;

		if (bottom)
			y += DisplayHeight(dpy, screen) -
				win_attr.height -
				win_attr.border_width;
	}

	values.x = x;
	values.y = y;
	value_mask = CWX | CWY;

	if (XReconfigureWMWindow(dpy, window, screen,
		value_mask, &values) == 0)
		Fatal_Error("move failed");
}

/*
 * dump some intresting window data
 */
static void
doprint(Window window)
{
	XWindowAttributes attributes;
	unsigned char *name;

	if( MyFetchName(dpy,window,&name) ) {
		if (XGetWindowAttributes(dpy, window, &attributes) == 0)
			Fatal_Error("XGetWindowAttributes(0x%x)", window);

		printf("0x%x: x=%d y=%d w=%d h=%d d=%d ",
			(int)window,
			attributes.x,attributes.y,
			attributes.width,attributes.height,
			attributes.depth);
		putchar('\'');
		while( *name != '\0' ) {
			if( *name >= ' ' && ((*name)&0x80)== 0 ) {
				putchar(*name);
			} else
				printf("\\%03hho",*name);
			name++;
		}
		putchar('\'');
		putchar('\n');
	}
}

/*
 * set window size
 */
static void
doresize(Window window, int w, int h)
{
    XWindowChanges values;
    unsigned int value_mask;
    int try;
    int nw, nh;

    values.width = w;
    values.height = h;
    value_mask = CWWidth | CWHeight;

    for (try = 0; try < 2; try++) {
	if (XReconfigureWMWindow(dpy, window, screen, value_mask, &values) == 0)
	    Fatal_Error("resize: XReconfigureWMWindow");

	getsize(window, &nw, &nh);
	if (values.width == nw && values.height == nh)
	    return;

	/* give window manager a couple of chances to react */
	mssleep(100);
	getsize(window, &nw, &nh);
	if (values.width == nw && values.height == nh)
	    return;

	mssleep(400);
	getsize(window, &nw, &nh);
	if (values.width == nw && values.height == nh)
	    return;
    }

    /* last chance */
    values.width += values.width - nw;
    values.height += values.height - nh;
    if (XReconfigureWMWindow(dpy, window, screen, value_mask, &values) == 0)
	Fatal_Error("resize: XReconfigureWMWindow 2");
}

/*
 * set row/column size
 */
static void
rcresize(enum functions what, Window window)
{
    XSizeHints *hints;
    long supplied;
    int w, h;

    if (!(what & FBIT(rows)) || !(what & FBIT(columns)))
	getsize(window, &w, &h);

    if (!(hints = XAllocSizeHints()))
	Fatal_Error("XAllocSizeHints");

    if (XGetWMNormalHints(dpy, window, hints, &supplied) == 0)
	Fatal_Error("XGetWMNormalHints");

    if (!(supplied & PBaseSize) || !(supplied & PResizeInc))
	Fatal_Error("missing PBaseSize and/or PResizeInc hint");

    if (what & FBIT(columns))
	w = hints->base_width + hints->width_inc * ncolumns;

    if (what & FBIT(rows))
	h = hints->base_height + hints->height_inc * nrows;

    doresize(window, w, h);

    XFree(hints);
}

static void
loadbitmap(Window window, const char *file, Pixmap *pmp)
{
	unsigned int w, h;
	int xhot, yhot;

	if (XReadBitmapFile(dpy, window, file,
		&w, &h, pmp, &xhot, &yhot) != BitmapSuccess)
		Fatal_Error("XReadBitmapFile failed");
}

static void
setbitmap(Window window)
{
	static XWMHints *hints;
	static Pixmap bitmap_pm;
	static Pixmap mask_pm;
	XWMHints *ohints;

	if (!hints) {
		if (!(hints = XAllocWMHints()) ||
			!(ohints = XAllocWMHints()))
			Fatal_Error("XAllocWMHints");

		if (bitmapname) {
			loadbitmap(window, bitmapname, &bitmap_pm);
			hints->flags |= IconPixmapHint;
			hints->icon_pixmap = bitmap_pm;
		}

		if (maskname) {
			loadbitmap(window, maskname, &mask_pm);
			hints->flags |= IconMaskHint;
			hints->icon_mask = mask_pm;
		}

		XSetCloseDownMode(dpy, RetainTemporary);
	}

	if ((ohints = XGetWMHints(dpy, window)) != NULL ) {
		if (ohints->icon_pixmap && hints->icon_pixmap)
			XFreePixmap(dpy, ohints->icon_pixmap);
		if (ohints->icon_mask && hints->icon_mask)
			XFreePixmap(dpy, ohints->icon_mask);
		XFree(ohints);
	}

	XSetWMHints(dpy, window, hints);
}

static void
setwinattr(Window window)
{
	XSetWindowAttributes swa;
	unsigned long valuemask;

	valuemask = 0;

	if (Gbs) {
		valuemask |= CWBackingStore | CWBackingPlanes;
		swa.backing_store = Gbs > 0 ? Always : NotUseful;
		swa.backing_planes = ~0L;
	}
	if (Gsu) {
		valuemask |= CWSaveUnder;
		swa.save_under = Gsu > 0;
	}

	XChangeWindowAttributes(dpy, window, valuemask, &swa);
}

/*
 * iconify the given window, or map and raise it, or whatever
 */
static void
doit(Window window)
{
	XWindowChanges values;
	unsigned int value_mask;
	XWMHints *wmhp;
	enum functions f;
	int i = 0;

	Gwinfound = 1;

	f = function;
	for (i = 0; i < lastfunc; i++) {
		if ((f & FBIT(i)) == 0)
			continue;

		switch (i) {
		case warp:
			XWarpPointer(dpy, None, window, 0, 0, 0, 0,
				warpx, warpy);
			break;
		case rwarp:
			XWarpPointer(dpy, None, None, 0, 0, 0, 0,
				warpx, warpy);
			break;
		case move:
			domove(window, tox, toy, Gright, Gbottom);
			break;
		case rmove:
			getpos(window, &values.x, &values.y);
			values.x += tox;
			values.y += toy;
			value_mask = CWX | CWY;
			if (XReconfigureWMWindow(dpy, window, screen,
					value_mask, &values) == 0)
				Fatal_Error("rmove failed");
			break;
		case resize:
			doresize(window, towidth, toheight);
			break;
		case colormap:
			XSetWindowColormap(dpy, window, cmap);
			break;
		case print:
			doprint(window);
			break;
		case pop:
			XMapRaised(dpy, window);
			break;
		case focus: {
			static XClientMessageEvent event;

			if (event.type == 0) {
				event.type = ClientMessage;
				event.message_type =
					XInternAtom(dpy, "_NET_ACTIVE_WINDOW", True);
				event.format = 32;
				event.data.l[0] = 1;
				event.data.l[1] = CurrentTime;
			}

			/* if no _NET_ACTIVE_WINDOW, the wm is oldschool... */
			if (event.message_type == 0
			    || !HasNetAtom (dpy, event.message_type))
				XSetInputFocus(dpy, window, CurrentTime, RevertToNone);
			else {
				event.window = window;
				event.data.l[2] = root;
				if (XSendEvent(dpy, root, (Bool) False,
					       SubstructureRedirectMask,
					       (XEvent *) & event) == 0)
					Fatal_Error("send event failed");
			}
			break;
		}
		case raise:
			values.stack_mode = Above;
			value_mask = CWStackMode;
			XConfigureWindow(dpy, window, value_mask, &values);
			break;
		case lower:
			values.stack_mode = Below;
			value_mask = CWStackMode;
			XConfigureWindow(dpy, window, value_mask, &values);
			break;
		case opposite:
			values.stack_mode = Opposite;
			value_mask = CWStackMode;
			XConfigureWindow(dpy, window, value_mask, &values);
			break;
		case circulate:
			XCirculateSubwindowsUp(dpy, window);
			break;
		case uncirculate:
			XCirculateSubwindowsDown(dpy, window);
			break;
		case unmap:
			XUnmapWindow(dpy, window);
			break;
		case icon:
#if iconify_by_sending_client_message
			static XClientMessageEvent event;

			if (event.type == 0) {
				event.type = ClientMessage;
#ifdef XA_WM_CHANGE_STATE
				event.message_type = XA_WM_CHANGE_STATE;
#else
				event.message_type =
					XInternAtom(dpy, "WM_CHANGE_STATE", True);
				if (event.message_type == 0)
					Fatal_Error("no WM_CHANGE_STATE atom");
#endif
				event.format = 32;
				event.data.l[0] = IconicState;
			}

			event.window = window;
			if (XSendEvent(dpy, root, (Bool) False,
					SubstructureRedirectMask | SubstructureNotifyMask,
					(XEvent *) & event) == 0)
				Fatal_Error("send event failed");
#else /* iconify_by_sending_client_message */
			if (XIconifyWindow(dpy, window, screen) == 0)
				Fatal_Error("iconify failed");
#endif /* iconify_by_sending_client_message */
			break;
		case save:
			XForceScreenSaver(dpy, ScreenSaverActive);
			break;
		case nosave:
			XForceScreenSaver(dpy, ScreenSaverReset);
			break;
		case keyrepeat:
		case nokeyrepeat:
			setrepeat();
			break;
		case name:
			XStoreName(dpy, window, wmname);
			break;
		case iconname:
			XSetIconName(dpy, window, wmiconname);
			break;
		case rows:
			/* don't do it twice */
			if (f & FBIT(columns))
				break;
			/* fall through */
		case columns:
			rcresize(f, window);
			break;
		case iconbitmap:
			setbitmap(window);
			break;
		case iconmove:
			wmhp = XGetWMHints(dpy, window);
			if (wmhp == 0)
				Fatal_Error("no WM_HINTS");
			wmhp->flags |= IconPositionHint;
			wmhp->icon_x = Giconx;
			wmhp->icon_y = Gicony;
			XSetWMHints(dpy, window, wmhp);
			XFree(wmhp);
			break;
		case riconmove:
			wmhp = XGetWMHints(dpy, window);
			if (wmhp == 0)
				Fatal_Error("no WM_HINTS");
			if (wmhp->flags & IconPositionHint) {
				wmhp->icon_x += Giconx;
				wmhp->icon_y += Gicony;
				XSetWMHints(dpy, window, wmhp);
			}
			else
				Fatal_Error("no current icon position");
			XFree(wmhp);
			break;
		case F_winattr:
			setwinattr(window);
			break;
		}
	}
}

/* based on xwininfo.c */
static Window
xwit_select_window(Display *dpy, int current)
{
	Window window = None;
	Window wroot;
	int dummyi;
	unsigned int dummy;

	if (current) {
		XQueryPointer(dpy, RootWindow(dpy, screen),
			&wroot, &window,
			&dummyi, &dummyi, &dummyi, &dummyi, &dummy);
	}
	else {
		printf("\n");
		printf("%s: select window by clicking the mouse\n",
			program_name);
		(void) fflush(stdout);
		window = Select_Window(dpy);
	}
	if (window) {
		if (XGetGeometry(dpy, window, &wroot, &dummyi, &dummyi,
			&dummy, &dummy, &dummy, &dummy) &&
			window != wroot)
			window = XmuClientWindow(dpy, window);
	}
	return window;
}

static Window
getxid(const char *s)
{
	XID id;

	if (sscanf(s, "0x%lx", &id) == 1)
		return id;
	if (sscanf(s, "%ld", &id) == 1)
		return id;
	Fatal_Error("Invalid ID format: %s", s);
	/* NOTREACHED */
        return -1;
}

static int
matchopt(const char *key, int nargs, int *argc, char **argv)
{
	int enough = 0;
	int match = 1;
	char *ap;
	const char *kp;

	ap = *argv;
	if (*ap == '-')
		ap++;

	for (kp = key; *kp; kp++, ap++) {
		if (*kp == '*') {
			enough = 1;
			ap--;
			continue;
		}
		if (*ap == 0) {
			match = enough;
			break;
		}
		if (*ap != *kp) {
			match = 0;
			break;
		}
	}

	if (match) {
		if (argc[0] <= nargs) {
			char option[32];
			int dash, skip;

			strncpy(option, *argv, sizeof option - 1);
			option[sizeof option - 1] = 0;

			dash = *argv[0] == '-';

			for (ap = option; *ap; ap++)
				/* nothing */ ;
			skip = ap - option - dash;
			enough = 0;
	
			for (kp = key; *kp && skip--; kp++) {
				if (*kp == '*')
					skip++;
			}
			for (; *kp; kp++) {
				if (*kp == '*')
					continue;
				if (enough == 0) {
					*ap++ = '(';
					enough = 1;
				}
				*ap++ = *kp;
			}
			if (enough)
				*ap++ = ')';
			*ap = 0;

			fprintf(stderr,
				"%s: option %s needs %d argument%s\n\n",
				program_name, option,
				nargs, nargs > 1 ? "s" : "");
			usage();
		}
		argc[0] -= nargs;
	}
	return match;
}

static void
FetchBuffer(Display *dpy, int nbuf)
{
        char *buf;
        int size;

        buf = XFetchBuffer(dpy, &size, nbuf);

        if( size == 0 )
                fprintf( stderr, "Could not fetch cutbuffer %d\n", nbuf );
        else
                fwrite( buf, 1, size, stdout );
}

static void
StoreBuffer(Display *dpy, int nbuf)
{
        char *buf = NULL;
        int bufsize, nread, total=0;

        bufsize = 10;
        buf = malloc( bufsize );
        while( (nread=read(0,buf+total,bufsize-total)) > 0 )
        {
                total+=nread;
                bufsize *= 2;
                buf = realloc( buf, bufsize );
        }
        XStoreBuffer(dpy, buf, total, nbuf);
        free(buf);
}

char *allwindows[] = {""};

int
main(int argc, char *argv[])
{
	Window window = 0;
	int *pargc = &argc;

	program_name = argv[0] + strlen(argv[0]);
	while (program_name != argv[0] && program_name[-1] != '/')
		program_name--;

	Setup_Display_And_Screen(pargc, argv);

	Winidmode = WID_env;

	while (argv++, --argc > 0) {
		/* argv[0] = next argument */
		/* argc = # of arguments left */
		if (matchopt("a*ll", 0, pargc, argv)) {
			Winidmode = WID_names;
			names = allwindows;
			numnames = 1;
		}
		else if (matchopt("ba*ckingstore", 0, pargc, argv) ||
			matchopt("bs", 0, pargc, argv)) {
			function |= FBIT(F_winattr);
			Gbs = 1;
		}
		else if (matchopt("b*itmap", 1, pargc, argv)) {
			function |= FBIT(iconbitmap);
			bitmapname = *++argv;
		}
		else if (matchopt("colo*rmap", 1, pargc, argv) ||
			matchopt("cm*ap", 1, pargc, argv)) {
			function |= FBIT(colormap);
			cmap = (Colormap) getxid(*++argv);
		}
		else if (matchopt("co*lumns", 1, pargc, argv)) {
			function |= FBIT(columns);
			ncolumns = atoi(*++argv);
		}
		else if (matchopt("store*buffer", 1, pargc, argv)) {
			nbuffer = atoi(*++argv);
                        StoreBuffer( dpy, nbuffer );
		}
		else if (matchopt("fetch*buffer", 1, pargc, argv)) {
			nbuffer = atoi(*++argv);
                        FetchBuffer( dpy, nbuffer );
		}
		else if (matchopt("c*urrent", 0, pargc, argv)) {
			Winidmode = WID_curr;
		}
		else if (matchopt("iconm*ove", 2, pargc, argv)) {
			function |= FBIT(iconmove);
			Giconx = atoi(argv[1]);
			Gicony = atoi(argv[2]);
			argv += 2;
		}
		else if (matchopt("iconn*ame", 1, pargc, argv) ||
			matchopt("in*ame", 1, pargc, argv)) {
			function |= FBIT(iconname);
			wmiconname = *++argv;
		}
		else if (matchopt("id", 1, pargc, argv)) {
			Winidmode = WID_num;
			window = (Window) getxid(*++argv);
		}
		else if (matchopt("i*conify", 0, pargc, argv)) {
			function |= FBIT(icon);
		}
		else if (matchopt("k*eyrepeat", 1, pargc, argv) ||
			matchopt("nok*eyrepeat", 1, pargc, argv)) {
			int i;

			function |= argv[0][1] == 'n' ?
				FBIT(nokeyrepeat) : FBIT(keyrepeat);

			i = atoi(*++argv);
			if (i < 0)
				usage();

			while (1) {
				keys[i & 0xff] = 1;
				if (argc <= 0)
					break;
				if (strcmp(argv[0], "-") == 0) {
					int from = i;

					argc--, argv++;
					if (argc < 0 || (i = atoi(argv[0])) <= 0)
						usage();
					while (from <= i)
						keys[from++ & 0xff] = 1;
					argc--, argv++;
					if (argc <= 0)
						break;
				}
				if ((i = atoi(argv[0])) <= 0)
					break;
				argc--, argv++;
			}
		}
		else if (matchopt("li*st", 1, pargc, argv) ||
			matchopt("names", 1, pargc, argv)) {
			Winidmode = WID_names;
			/* take rest of arg list */
			names = ++argv;
			numnames = argc;
			argc = 0;
		}
		else if (matchopt("l*abel", 1, pargc, argv)) {
			function |= FBIT(name);
			wmname = *++argv;
		}
		else if (matchopt("ma*sk", 1, pargc, argv)) {
			function |= FBIT(iconbitmap);
			maskname = *++argv;
		}
		else if (matchopt("m*ove", 2, pargc, argv)) {
			function |= FBIT(move);
			Gright = (argv[1][0] == '-');
			Gbottom = (argv[2][0] == '-');
			tox = atoi(argv[1]);
			toy = atoi(argv[2]);
			argv += 2;
		}
		else if (matchopt("noba*ckingstore", 0, pargc, argv) ||
			matchopt("nobs", 0, pargc, argv)) {
			function |= FBIT(F_winattr);
			Gbs = -1;
		}
		else if (matchopt("nosaveu*nder", 0, pargc, argv) ||
			matchopt("nosu", 0, pargc, argv)) {
			function |= FBIT(F_winattr);
			Gsu = -1;
		}
		else if (matchopt("nos*ave", 0, pargc, argv)) {
			function |= FBIT(nosave);
		}
		else if (matchopt("n*ame", 1, pargc, argv)) {
			function |= FBIT(name);
			wmname = *++argv;
		}
		else if (matchopt("o*pen", 0, pargc, argv)) {
			function |= FBIT(pop);
		}
		else if (matchopt("p*op", 0, pargc, argv)) {
			function |= FBIT(pop);
		}
		else if (matchopt("pr*int", 0, pargc, argv)) {
			function |= FBIT(print);
		}
		else if (matchopt("f*ocus", 0, pargc, argv)) {
			function |= FBIT(focus);
		}
		else if (matchopt("ra*ise", 0, pargc, argv)) {
			function |= FBIT(raise);
		}
		else if (matchopt("lo*wer", 0, pargc, argv)) {
			function |= FBIT(lower);
		}
		else if (matchopt("op*posite", 0, pargc, argv)) {
			function |= FBIT(opposite);
		}
		else if (matchopt("cir*culate", 0, pargc, argv)) {
			function |= FBIT(circulate);
		}
		else if (matchopt("uncir*culate", 0, pargc, argv)) {
			function |= FBIT(uncirculate);
		}
		else if (matchopt("ri*conmove", 2, pargc, argv)) {
			function |= FBIT(riconmove);
			Giconx = atoi(argv[1]);
			Gicony = atoi(argv[2]);
			argv += 2;
		}
		else if (matchopt("rm*ove", 2, pargc, argv)) {
			function |= FBIT(rmove);
			tox = atoi(argv[1]);
			toy = atoi(argv[2]);
			argv += 2;
		}
		else if (matchopt("roo*t", 0, pargc, argv)) {
			Winidmode = WID_root;
		}
		else if (matchopt("ro*ws", 1, pargc, argv)) {
			function |= FBIT(rows);
			nrows = atoi(*++argv);
		}
		else if (matchopt("rw*arp", 2, pargc, argv)) {
			function |= FBIT(rwarp);
			warpx = atoi(argv[1]);
			warpy = atoi(argv[2]);
			argv += 2;
		}
		else if (matchopt("r*esize", 2, pargc, argv)) {
			function |= FBIT(resize);
			towidth = atoi(argv[1]);
			toheight = atoi(argv[2]);
			argv += 2;
		}
		else if (matchopt("saveu*nder", 0, pargc, argv) ||
			matchopt("su", 0, pargc, argv)) {
			function |= FBIT(F_winattr);
			Gsu = argv[0][1] == 1;
		}
		else if (matchopt("se*lect", 0, pargc, argv)) {
			Winidmode = WID_select;
		}
		else if (matchopt("sy*nc", 0, pargc, argv)) {
			XSynchronize(dpy, True);
		}
		else if (matchopt("s*ave", 0, pargc, argv)) {
			function |= FBIT(save);
		}
		else if (matchopt("un*map", 0, pargc, argv)) {
			function |= FBIT(unmap);
		}
		else if (matchopt("w*arp", 2, pargc, argv)) {
			function |= FBIT(warp);
			warpx = atoi(argv[1]);
			warpy = atoi(argv[2]);
			argv += 2;
		} else if(matchopt("prop*erty",1, pargc,argv)) {
			property = XInternAtom(dpy,argv[1],False);
			if( None == property ) {
			    Fatal_Error("Unknown atom %s",argv[1]);
			}
			argv++;
		}
		else
			usage();
	}

	/* default function: pop */
	if (function == 0)
		function = FBIT(pop);

	if ((function & ~NOWINDOW) == 0)
		Winidmode = WID_none;

	root = DefaultRootWindow(dpy);

	switch (Winidmode) {
	case WID_env:
		{
			char *s;

			s = getenv("WINDOWID");
			if (s != 0)
				window = (Window) getxid(s);
			else {
				/* no WINDOWID, use window under cursor */
				window = xwit_select_window(dpy, 1);
				if (window == None)
					Fatal_Error("WINDOWID not set");
			}
		}
		break;
	case WID_root:
		window = root;
		break;
	case WID_curr:
		window = xwit_select_window(dpy, 1);
		break;
	case WID_select:
		window = xwit_select_window(dpy, 0);
		break;
	default:
	        break;
	}

	switch (Winidmode) {
	case WID_none:
		doit((Window) 0);
		break;
	case WID_names:
		downtree(root);
		break;
	default:
		if (!window)
			Fatal_Error("no window selected");
		doit(window);
		break;
	}

	XSync(dpy, True);
	(void) XCloseDisplay(dpy);
	return(!Gwinfound);
}

