/* $XConsortium: dsimple.h,v 1.2 88/09/06 17:38:34 jim Exp $ */
/*
 * Just_display.h: This file contains the definitions needed to use the
 *                 functions in just_display.c.  It also declares the global
 *                 variables dpy, screen, and program_name which are needed to
 *                 use just_display.c.
 *
 * Written by Mark Lillibridge.   Last updated 7/1/87
 *
 * Send bugs, etc. to chariot@athena.mit.edu.
 */

    /* Global variables used by routines in just_display.c */

extern char *program_name;       /* Name of this program */
extern Display *dpy;                                 /* The current display */
extern int screen;                                   /* The current screen */

#define INIT_NAME program_name=argv[0]        /* use this in main to setup
                                                 program_name */

    /* Declaritions for functions in just_display.c */

void Fatal_Error(char *msg, ...);
char *Malloc(size_t);
char *Realloc(void*,size_t);
char *Get_Display_Name(int*,char **);
Display *Open_Display(char *);
void Setup_Display_And_Screen(int *, char **);
XFontStruct *Open_Font(char *);
void Beep(void);
Pixmap ReadBitmapFile(Drawable,char *,int*,int*,int*,int*);
void WriteBitmapFile(char *,Pixmap,int,int,int,int);
Window Select_Window_Args(int *,char **);

#define X_USAGE "[host:display]"              /* X arguments handled by
						 Get_Display_Name */
#define SELECT_USAGE "[{-root|-id <id>|-font <font>|-name <name>}]"

/*
 * Other_stuff.h: Definitions of routines in other_stuff.
 *
 * Written by Mark Lillibridge.   Last updated 7/1/87
 *
 * Send bugs, etc. to chariot@athena.mit.edu.
 */

unsigned long Resolve_Color(Window,char *);
Pixmap Bitmap_To_Pixmap(Display *,Drawable,GC,Pixmap,int,int);
Window Select_Window(Display *);
void out(void);
void blip(void);
Window Window_With_Name(Display *,Window,char *);
void usage(void);
