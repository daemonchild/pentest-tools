/*
 *
 * XWatchwin Version 1.1 -- 22 Nov 1995
 *
 * Copyright (C) 1992 - 95		Q. Alex Zhao, azhao@cc.gatech.edu
 *
 *			All Rights Reserved
 *
 * Permission to use, copy, modify, and distribute this software and
 * its documentation for any purpose and without fee is hereby
 * granted, provided that the above copyright notice appear in all
 * copies and that both that copyright notice and this permission
 * notice appear in supporting documentation, and that the name of the
 * author not be used in advertising or publicity pertaining to
 * distribution of the software without specific, written prior
 * permission.
 *
 * This program is distributed in the hope that it will be "useful",
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 */

#include	<stdio.h>
#include	<math.h>
#include	<ctype.h>
#include	<errno.h>

#include	<X11/Xlib.h>
#include	<X11/Xutil.h>
#include	<X11/cursorfont.h>
#include	<X11/keysym.h>
#include	<X11/Xos.h>
#include	<X11/Xfuncs.h>
#include	<X11/Xfuncproto.h>

#ifndef	X_NOT_STDC_ENV
#include	<stdlib.h>
#endif

#ifdef	_AIX
#include	<sys/select.h>
#endif

#ifdef	__sgi
#include	<getopt.h>
#endif

#include	"patchlevel.h"

/* --------------------------------------------------------------------- */

#define	NAMELENGTH	64
#define	STRINGLENGTH	1024
#define	DEFSLEEPTIME	5
#define	MILLION		1000000


/* --------------------------------------------------------------------- */

char            displayName[64], xWatchName[STRINGLENGTH];
char          **Argv;
int             Argc;

Display        *dpy, *dpy2;
XImage         *image;

int             dpy2depth;

static struct timezone zone = {0, 0};

void            ConvertImage(XImage *);
void            WatchWindow(Window, int);
Window          GetWindowByName(Window, char *);
void            realTime(struct timeval *tv);
void            quit();

/* --------------------------------------------------------------------- */

void
main(int argc, char **argv)
{
    Window          watchWin;
    int             i, strPos, optIndex;
    extern char    *optarg;
    extern int      optind;
    int             windowID, updateTime = DEFSLEEPTIME;
    Bool            windowIDSet = False, updateTimeSet = False;
    int             optionsNeeded = 3, numoferrs = 0;

    /* get arguments */
    while ((optIndex = getopt(argc, argv, "u:w:v")) >= 0) {
	switch (optIndex) {
	case 'u':
	    updateTime = atoi(optarg);
	    if (updateTime < 0) {
		updateTime = 0;
	    }
	    if (!updateTimeSet) {
		updateTimeSet = True;
		optionsNeeded--;
	    }
	    break;

	case 'w':
	    sscanf(optarg, "%lx", &windowID);
	    if (!windowIDSet) {
		windowIDSet = True;
		optionsNeeded--;
	    }
	    break;

	case 'v':
	    fprintf(stderr, "%s\n", XWATCHWIN_VERSION_STRING);
	    exit(0);
	    break;

	default:
	    numoferrs ++;
	    break;
	}
    }

    if (numoferrs || (argc < optionsNeeded)) {
	fprintf(stderr, "Usage:\n");
	fprintf(stderr,
	"\t%s [-v] [-u UpdateTime] DisplayName { -w windowID | WindowName }\n",
		argv[0]);
	exit(1);
    }

    Argv = argv;
    Argc = argc;

    /* if no ':' in display name, tack default ':0.0' onto end */
    strncpy(displayName, argv[optind], NAMELENGTH);
    displayName[NAMELENGTH - 1] = '\0';
    if ((strchr(argv[optind], ':') == NULL) &&
    	    (strlen(displayName) <= NAMELENGTH - 3)) {
	strcat(displayName, ":0");
    }

    if (!windowIDSet) {
	xWatchName[0] = '\0';
	for (i = optind + 1; i < argc; i++) {
	    /* Get current length of xWatchName string */
	    strPos = strlen(xWatchName);
	    /* Copy another argument to the end of the string */
	    strcpy(&xWatchName[strPos], argv[i]);
	}
    }

    /* Attempt to open a connection with the remote X server */
    if ((dpy = XOpenDisplay(displayName)) == NULL) {
	/* Couldn't open the display for some reason, so... */
	fprintf(stderr, "%s: Could not open remote display %s\n",
		argv[0], displayName);
	exit(1);
    }

    if (!windowIDSet) {
	watchWin = GetWindowByName(XDefaultRootWindow(dpy), xWatchName);
    } else {
	watchWin = windowID;
    }

    if (watchWin) {
	WatchWindow(watchWin, updateTime);
    } else {
	fprintf(stderr, "Could not find the window you specified.\n");
	exit(1);
    }
}

/* --------------------------------------------------------------------- */

/* Takes two strings, removes spaces from the second,... */
/* ...and compares them..  Returns 1 if equal, 0 if not. */
WinNamesEqual(char *str1, char *str2)
{
    char            tempStr[STRINGLENGTH], *tempStrPtr;

    memset(tempStr, '\0', STRINGLENGTH);

    /* Go through each character in the second string. */
    for (tempStrPtr = tempStr; *str2; str2++) {
	if (!isspace(*str2)) {	/* Is this character a space?  */
	    /* No, copy this character to a temp * string. */
	    *tempStrPtr++ = *str2;
	}
    }

    /* Are the two resulting strings equal? */
    if (!strcasecmp(str1, tempStr)) {
	return (1);		/* Yes, return 1 */
    } else {
	return (0);		/* No, return 0 */
    }
}

/* --------------------------------------------------------------------- */

void
WatchWindow(Window win, int updateTime)
{
    XWindowAttributes copyWinInfo;
    XSetWindowAttributes copyWinAttrs;
    char            buffer[STRINGLENGTH * 2];
    char           *pointer;
    XWMHints       *wmHints = XAllocWMHints();
    XClassHint     *classHints = XAllocClassHint();
    XSizeHints     *sizeHints = XAllocSizeHints();
    XTextProperty   windowName, iconName;
    Window          copyWin;
    GC              gc;
    struct timeval  currentTime, oldTime;
    Bool            srcWinVisible = True, dstWinVisible = False;
    int             imageform, imagewidth, imageheight, maxfdnum, tmp;
    fd_set          writefd, exceptfd, readfd, savedreadfd;
    Atom            delw;
    Cursor          theCursorWatch, theCursorDot;

    /* Get the window attributes of the window we're watching */
    XGetWindowAttributes(dpy, win, &copyWinInfo);

    /* Is the original window in a state to be watched?  */
    if (copyWinInfo.map_state != IsViewable) {
	fprintf(stderr,
		"The window you wish to look at is not in a viewable state.\n");
	fprintf(stderr,
		"Perhaps it is iconified or not mapped.\n");
	exit(1);
    }

    /* Attempt to open a connection with the local X server */
    if ((dpy2 = XOpenDisplay(NULL)) == NULL) {
	fprintf(stderr, "%s: Could not open local display.\n", Argv[0]);
	exit(1);
    }

    /* Create the cursor */
    theCursorWatch = XCreateFontCursor(dpy2, XC_watch);
    theCursorDot = XCreateFontCursor(dpy2, XC_dotbox);

    /* Set a couple more attributes */
    copyWinAttrs.colormap = XDefaultColormap(dpy2, XDefaultScreen(dpy2));
    copyWinAttrs.bit_gravity = copyWinInfo.bit_gravity;

    /* Only interested in unmaps, exposures and button presses */
    copyWinAttrs.event_mask =
	    VisibilityChangeMask | StructureNotifyMask | KeyReleaseMask;
    copyWinAttrs.do_not_propagate_mask = KeyReleaseMask |
	    ButtonPressMask | ButtonReleaseMask | PointerMotionMask |
	    ButtonMotionMask | Button1MotionMask | Button2MotionMask |
	    Button3MotionMask | Button4MotionMask | Button5MotionMask;
    copyWinAttrs.cursor = theCursorWatch;

    /* Check for different depths b/w source & dest displays */
    dpy2depth = XDefaultDepth(dpy2, XDefaultScreen(dpy2));

    if ((copyWinInfo.depth == dpy2depth) ||
	    (copyWinInfo.depth == 1 && dpy2depth == 8) ||
	    (copyWinInfo.depth == 8 && dpy2depth == 1)) {
	imageform = ZPixmap;
    } else {
	imageform = XYPixmap;
    }

    /* Create a copy of the window we're watching */
    copyWin = XCreateWindow(dpy2, XDefaultRootWindow(dpy2),
	    copyWinInfo.x, copyWinInfo.y,
	    imagewidth = copyWinInfo.width, imageheight = copyWinInfo.height,
	    copyWinInfo.border_width,
	    dpy2depth, CopyFromParent,
	    XDefaultVisual(dpy2, XDefaultScreen(dpy2)),
	    (CWColormap | CWBitGravity | CWEventMask |
		    CWDontPropagate | CWCursor),
	    &copyWinAttrs);

    /* Set window properties for my window */
    sprintf(buffer, "XWatchWin [%s] (%s)", displayName, xWatchName);
    pointer = buffer;
    XStringListToTextProperty(&pointer, 1, &windowName);
    pointer = displayName;
    XStringListToTextProperty(&pointer, 1, &iconName);

    sizeHints->base_width = imagewidth;
    sizeHints->min_width = imagewidth;
    sizeHints->max_width = imagewidth;
    sizeHints->base_height = imageheight;
    sizeHints->min_height = imageheight;
    sizeHints->max_height = imageheight;
    sizeHints->flags = PMinSize | PMaxSize | PBaseSize;

    wmHints->initial_state = NormalState;
    wmHints->input = True;
    wmHints->flags = StateHint | InputHint;

    classHints->res_name = Argv[0];
    classHints->res_class = "XWatchWin";

    XSetWMProperties(dpy2, copyWin, &windowName, &iconName,
	    Argv, Argc, sizeHints, wmHints, classHints);

    /* Set local window manager protocol */
    delw = XInternAtom(dpy2, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(dpy2, copyWin, &delw, 1);

    gc = XDefaultGC(dpy2, 0);	/* Get a default graphics context */

    /* Put the window up on the display */
    XMapWindow(dpy2, copyWin);
    XFree(wmHints);
    XFree(classHints);
    /* sizeHints will be used later */

    /* watch when the window being watched is dying */
    XSelectInput(dpy, win, VisibilityChangeMask | StructureNotifyMask);

    gettimeofday(&oldTime, &zone);	/* Get the current time. */

    /* Prepare variables for select() */
    FD_ZERO(&readfd);
    FD_ZERO(&writefd);
    FD_ZERO(&exceptfd);
    maxfdnum = ConnectionNumber(dpy);
    FD_SET(maxfdnum, &readfd);
    tmp = ConnectionNumber(dpy2);
    FD_SET(tmp, &readfd);
    memcpy((char *) &savedreadfd, (char *) &readfd, sizeof(fd_set));

    if (maxfdnum < tmp) {
	maxfdnum = tmp;
    }

    while (1) {
	struct timeval  deltaTime;
	XEvent          event;
	char            buf[4];

	/* Look for window events, but don't sit around... */
	while (XPending(dpy)) {
	    XNextEvent(dpy, &event);
	    switch (event.type) {
	    case DestroyNotify:
		quit();
		break;

	    case ConfigureNotify:
		if ((event.xconfigure.width != imagewidth) ||
			(event.xconfigure.height != imageheight)) {
		    imagewidth = event.xconfigure.width;
		    imageheight = event.xconfigure.height;

		    sizeHints->base_width = imagewidth;
		    sizeHints->min_width = imagewidth;
		    sizeHints->max_width = imagewidth;
		    sizeHints->base_height = imageheight;
		    sizeHints->min_height = imageheight;
		    sizeHints->max_height = imageheight;
		    sizeHints->flags = PMinSize | PMaxSize | PBaseSize;

		    XSetWMNormalHints(dpy2, copyWin, sizeHints);
		    XResizeWindow(dpy2, copyWin, imagewidth, imageheight);
		}
		break;

	    case UnmapNotify:
		srcWinVisible = False;
		break;

	    case VisibilityNotify:
		if (event.xvisibility.state == VisibilityFullyObscured) {
		    srcWinVisible = False;
		} else {
		    srcWinVisible = True;
		}
		break;
	    }
	}

	while (XPending(dpy2)) {
	    XNextEvent(dpy2, &event);
	    switch (event.type) {
	    case KeyRelease:
		if (XLookupString(&event.xkey, buf, 4, NULL, NULL)
			&& (buf[0] == 'q' || buf[0] == 'Q')) {
		    quit();
		}
		break;

	    case UnmapNotify:
		dstWinVisible = False;
		break;

	    case VisibilityNotify:
		if (event.xvisibility.state == VisibilityFullyObscured) {
		    dstWinVisible = False;
		} else {
		    dstWinVisible = True;
		}
		break;

	    case ClientMessage:
		if ((Atom) event.xclient.data.l[0] == delw)
		    quit();
		break;

	    case DestroyNotify:
		quit();
		break;
	    }
	}

	/*
	 * Have 'updateTime' seconds passed?
	 * Is the source window watchable?
	 * Is the destination window drawable?
	 */

	if (!srcWinVisible || !dstWinVisible) {
	    memcpy((char *) &readfd, (char *) &savedreadfd, sizeof(fd_set));
	    (void) select(maxfdnum + 1, &readfd, &writefd, &exceptfd, NULL);
	} else {
	    gettimeofday(&currentTime, &zone);	/* Get the current time. */
	    deltaTime.tv_sec = currentTime.tv_sec - oldTime.tv_sec;
	    deltaTime.tv_usec = currentTime.tv_usec - oldTime.tv_usec;
	    realTime(&deltaTime);

	    if (deltaTime.tv_sec >= updateTime) {
		XDefineCursor(dpy2, copyWin, theCursorWatch);
		XFlush(dpy2);

		/* Store an image of the original window in an XImage */
		image = XGetImage(dpy, (Drawable) win,
			0, 0,
			imagewidth, imageheight,
			AllPlanes, imageform);
		ConvertImage(image);
		XDefineCursor(dpy2, copyWin, theCursorDot);

		/* Put the original window's image into the copy window */
		XPutImage(dpy2, copyWin, gc, image, 0, 0, 0, 0,
			imagewidth, imageheight);
		XDestroyImage(image);	/* Free memory used by the image */
		XSync(dpy2, False);

		/* Update the current time */
		oldTime.tv_sec = currentTime.tv_sec;
		oldTime.tv_usec = currentTime.tv_usec;
	    } else {
		deltaTime.tv_sec = updateTime - deltaTime.tv_sec;
		deltaTime.tv_usec = MILLION - deltaTime.tv_usec;
		memcpy((char *) &readfd, (char *) &savedreadfd, sizeof(fd_set));
		(void) select(maxfdnum + 1, &readfd, &writefd, &exceptfd,
			&deltaTime);
	    }
	}
    }				/* end while(1) */
}				/* end function WatchWindow */

/* --------------------------------------------------------------------- */

/* Given the name of a window and the top of a ... */
/* ...window tree, this function will try to find... */
/* the Window ID corresponding to the window name... */
/* ...given as argument. */
Window
GetWindowByName(Window window, char *windowName)
{
    Window          rootWin, parentWin, wID;
    Window         *childWinList;
    unsigned int    numChildren, i;
    char           *childWinName;

    if (strcasecmp(windowName, "root") == 0 ||
    		strcasecmp(windowName, "XRootWindow") == 0)
	return XDefaultRootWindow(dpy);

    /* Get information about windows that are children... */
    XQueryTree(dpy, window,
	       &rootWin, &parentWin, &childWinList,	/* ...of 'window'. */
	       &numChildren);
    for (i = 0; i < numChildren; i++) {	/* Look at each child of 'window' */
	/* Get the name of that window */
	XFetchName(dpy, childWinList[i], &childWinName);
	if (childWinName != NULL) {	/* Is there a name attached to this
					 * window? */
	    /* Is this the window the user is looking for? */
	    if (WinNamesEqual(windowName, childWinName)) {
		/* Free up space taken by list of windows */
		XFree((unsigned char *) childWinList);
		/* Return space taken by this window's name */
		XFree((unsigned char *) childWinName);
		/* Yes, return the Window ID of this window */
		return (childWinList[i]);
	    }
	    /* Return space taken by this window's name */
	    XFree((unsigned char *) childWinName);
	}			/* end if childWinName... */
    }				/* end for i=0... */

    /*
     * If this section of code is reached, then no match was found at this
     * level of the tree
     */
    for (i = 0; i < numChildren; i++) {	/* Recurse on the children of this
					 * window */
	wID = GetWindowByName(childWinList[i], windowName);
	if (wID) {		/* Was a match found? */
	    /* Free up space taken by list of windows */
	    XFree((unsigned char *) childWinList);
	    return (wID);	/* Return the ID of the window that matched */
	}
    }				/* end for i=0... */

    /*
     * If this section of code is reached, then no match was found below this
     * level of the tree
     */
    /* Free up space taken by list of windows */
    XFree((unsigned char *) childWinList);
    return ((Window) 0);	/* No match was found, return 0. */
}				/* end function GetWindowByName */

/* --------------------------------------------------------------------- */

void
ConvertImage(XImage * image)
{
    int             i, j;

    /*
     * conversion between images of different depths.  if the two depths are
     * the same, nothing is done.  If the two depths are '1' and '8' (either
     * way), uses special case code and ZPixmaps.  Otherwise, uses XYPixmap
     * and general code below
     */

    /*
     * note:  on every color/greyscale server I've had the pleasure to deal
     * with (IBM RT Megapel and Sun cgfour), XGetImage(XYPixmap) returns an
     * image in which the planes are (in my opinion) reversed. That is, plane
     * 0 (the first plane in the image) corresponds to the highest order bit
     * plane returned by the server.
     * 
     * So, in order to correctly display the image when the two depths are
     * different, I have to move the planes around.  Examples:  when
     * displaying a 1-bit image on an 8-bit display, I convert the image by
     * making up an 8-bit image, and copying the 1-bit plane to the 8th plane
     * in the 8-bit image, rather than to the 1st plane.
     * 
     * Likewise, when displaying an 8-bit image on a 1-bit display, I make up a
     * 1-bit image, and copy the 8th plane to the (only) plane in the 1-bit
     * image.
     * 
     * In theory, if I was displaying a 4 bit image on an 8-bit display, I'd
     * copy planes 0-3 of the 4-bit image to planes 4-7, respectively, in the
     * 8-bit image.
     * 
     * The code does this, though I haven't checked to see if it does it
     * correctly, or even if this is the right thing to do.
     * 
     * --jhb
     */

    if (dpy2depth == image->depth)
	return;

    if (dpy2depth == 8 || image->depth == 1) {
	unsigned char  *iptr, *optr, *ilptr, *olptr, *tmp;
	int             obperlin, bit;

	obperlin = ((image->width * 8 + image->bitmap_pad - 1)
		    / image->bitmap_pad) * (image->bitmap_pad / 8);

	ilptr = (unsigned char *) image->data;
	olptr = tmp = (unsigned char *) malloc(image->height * obperlin);
	if (!olptr) {
	    fprintf(stderr, "couldn't allocate image\n");
	    exit(1);
	}
	for (i = 0; i < image->height; i++) {
	    iptr = ilptr;
	    optr = olptr;
	    for (j = bit = 0; j < image->width; j++) {
		*optr++ = (*iptr & 0x80) ? 1 : 0;
		*iptr <<= 1;
		if (!(++bit & 7))
		    iptr++;
	    }
	    ilptr += image->bytes_per_line;
	    olptr += obperlin;
	}

	free(image->data);
	image->data = (char *) tmp;
	image->bytes_per_line = obperlin;
	image->depth = image->bits_per_pixel = dpy2depth;
    } else if (dpy2depth == 1 || image->depth == 8) {	/* compress ZPixmap 8->1 */
	unsigned char  *iptr, *optr, *ilptr, *olptr, *tmp;
	int             obperlin, bit;

	obperlin = ((image->width + image->bitmap_pad - 1)
		    / image->bitmap_pad) * (image->bitmap_pad / 8);

	ilptr = (unsigned char *) image->data;
	olptr = tmp = (unsigned char *) malloc(image->height * obperlin);
	if (!olptr) {
	    fprintf(stderr, "couldn't allocate image\n");
	    exit(1);
	}
	for (i = 0; i < image->height; i++) {
	    iptr = ilptr;
	    optr = olptr;
	    for (j = bit = 0; j < image->width; j++) {
		*optr = (*optr << 1) | (*iptr++ & 0x01);
		if (!(++bit & 7))
		    optr++;
	    }
	    ilptr += image->bytes_per_line;
	    olptr += obperlin;
	}

	free(image->data);
	image->data = (char *) tmp;
	image->bytes_per_line = obperlin;
	image->depth = image->bits_per_pixel = dpy2depth;
    } else if (dpy2depth > image->depth) {	/* expand XYPixmap */
	unsigned char  *tmp;
	long            planelen = image->height * image->bytes_per_line;

	tmp = (unsigned char *) malloc(planelen * dpy2depth);
	if (!tmp) {
	    fprintf(stderr, "couldn't allocate image\n");
	    exit(1);
	}
	memset((char *) tmp, '\0', planelen * dpy2depth);

	memcpy((char *) tmp + (dpy2depth - image->depth) * planelen,
		image->data, image->depth * planelen);

	free(image->data);
	image->data = (char *) tmp;
	image->depth = dpy2depth;
    } else if (dpy2depth < image->depth) {	/* compress XYPixmap */
	unsigned char  *tmp;
	long            planelen = image->height * image->bytes_per_line;

	tmp = (unsigned char *) malloc(planelen * dpy2depth);
	if (!tmp) {
	    fprintf(stderr, "couldn't allocate image\n");
	    exit(1);
	}
	memcpy((char *) tmp, image->data + (image->depth - dpy2depth) *
		planelen, dpy2depth * planelen);

	free(image->data);
	image->data = (char *) tmp;
	image->depth = dpy2depth;
    }
}

/* --------------------------------------------------------------------- */

void
realTime(struct timeval *tv)
{
    while (tv->tv_usec < 0) {
	tv->tv_sec--;
	tv->tv_usec += MILLION;
    }
    while (tv->tv_usec >= MILLION) {
	tv->tv_sec++;
	tv->tv_usec -= MILLION;
    }
    if (tv->tv_sec < 0) {
	tv->tv_sec = 0;
    }
}

/* --------------------------------------------------------------------- */

void
quit()
{
    XDestroyImage(image);
    XCloseDisplay(dpy);
    XCloseDisplay(dpy2);
    exit(0);
}

/* --------------------------------------------------------------------- */
