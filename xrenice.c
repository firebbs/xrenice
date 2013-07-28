#include "config.h"
#include <sys/resource.h>
#include <X11/Xlib.h>
#include <X11/Xos.h>
#include <X11/Xfuncs.h>
#include <X11/Xutil.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#ifdef HAVE_WCHAR_H
#include <wchar.h>
#endif
#ifdef HAVE_WCTYPE_H
#include <wctype.h>
#endif

#ifndef HAVE_WCTYPE_H
#define iswprint(x) isprint(x)
#endif

#include <X11/Xatom.h>

#include "dsimple.h"

#define MAXSTR 500000
#define MAXELEMENTS 64
#define LOWEST_PRIORITY 20
#define HIGHEST_PRIORITY -20
#ifndef min
#define min(a,b)  ((a) < (b) ? (a) : (b))
#endif

typedef struct {
  int thunk_count;
  const char *propname;
  long value;
  Atom extra_encoding;
  const char *extra_value;
  const char *format;
  const char *dformat;
} thunk;

static thunk *
Create_Thunk_List (void)
{
    thunk *tptr;

    tptr = malloc(sizeof(thunk));
    if (!tptr)
	Fatal_Error("Out of memory!");

    tptr->thunk_count = 0;

    return tptr;
}

#ifdef notused
static void
Free_Thunk_List (thunk *list)
{
    free(list);
}
#endif

static thunk *
Add_Thunk (thunk *list, thunk t)
{
    int i;

    i = list->thunk_count;

    list = realloc(list, (i+1)*sizeof(thunk));
    if (!list)
	Fatal_Error("Out of memory!");

    list[i++] = t;
    list->thunk_count = i;

    return list;
}





static const char *
Skip_Digits (const char *string)
{
    while (isdigit((unsigned char) string[0])) string++;
    return string;
}

static const char *
Scan_Long (const char *string, long *value)
{
    if (!isdigit((unsigned char) *string))
	Fatal_Error("Bad number: %s.", string);

    *value = atol(string);
    return Skip_Digits(string);
}



/*
 *
 * Atom to format, dformat mapping Manager
 *
 */

#define D_FORMAT "0x"              /* Default format for properties */
#define D_DFORMAT " = $0+\n"       /* Default display pattern for properties */

static thunk *_property_formats = NULL;   /* Holds mapping */

static void
Apply_Default_Formats (const char **format, const char **dformat)
{
    if (!*format)
	*format = D_FORMAT;
    if (!*dformat)
	*dformat = D_DFORMAT;
}

static void
Lookup_Formats (Atom atom, const char **format, const char **dformat)
{
    int i;

    if (_property_formats)
	for (i = _property_formats->thunk_count-1; i >= 0; i--)
	    if (_property_formats[i].value == atom) {
		if (!*format)
		    *format = _property_formats[i].format;
		if (!*dformat)
		    *dformat = _property_formats[i].dformat;
		break;
	    }
}

static void
Add_Mapping (Atom atom, const char *format, const char *dformat)
{
    thunk t = {0};

    if (!_property_formats)
	_property_formats = Create_Thunk_List();

    t.value = atom;
    t.format = format;
    t.dformat = dformat;

    _property_formats = Add_Thunk(_property_formats, t);
}

/*
 *
 * Setup_Mapping: Routine to setup default atom to format, dformat mapping:
 * 
 */

typedef struct _propertyRec {
    const char *	name;
    Atom		atom;
    const char *	format;
    const char *	dformat;
} propertyRec;



static propertyRec windowPropTable[] = {
    {"CARDINAL",	XA_CARDINAL,	 "0c",	      0 },
    {"WINDOW",		XA_WINDOW,	 "32x",	      ": window id # $0+\n" },

};


static void
Setup_Mapping (void)
{
    int n;
    propertyRec *p;
    
	n = sizeof(windowPropTable) / sizeof(propertyRec);
	p = windowPropTable;
    
    for ( ; --n >= 0; p++) {
	if (! p->atom) {
	    p->atom = XInternAtom(dpy, p->name, True);
	    if (p->atom == None)
		continue;
	}
	Add_Mapping(p->atom, p->format, p->dformat);
    }	
}

static const char *
GetAtomName (Atom atom)
{
    int n;
    propertyRec *p;

	n = sizeof(windowPropTable) / sizeof(propertyRec);
	p = windowPropTable;
    
    for ( ; --n >= 0; p++)
	if (p->atom == atom)
	    return p->name;

    return NULL;
}


static char _formatting_buffer[MAXSTR+100];
static char _formatting_buffer2[21];


static const char *
Format_Unsigned (long wrd)
{
    snprintf(_formatting_buffer2, sizeof(_formatting_buffer2), "%lu", wrd);
    return _formatting_buffer2;
}

static const char *
Format_Signed (long wrd)
{
    snprintf(_formatting_buffer2, sizeof(_formatting_buffer2), "%ld", wrd);
    return _formatting_buffer2;
}

/*ARGSUSED*/
static int
ignore_errors (Display *dpy, XErrorEvent *ev)
{
    return 0;
}

static const char *
Format_Atom (Atom atom)
{
    const char *found;
    char *name;
    XErrorHandler handler;

    if ((found = GetAtomName(atom)) != NULL)
	return found;

    handler = XSetErrorHandler (ignore_errors);
    name = XGetAtomName(dpy, atom);
    XSetErrorHandler(handler);
    if (! name)
	snprintf(_formatting_buffer, sizeof(_formatting_buffer),
		 "undefined atom # 0x%lx", atom);
    else {
	int namelen = strlen(name);
	if (namelen > MAXSTR) namelen = MAXSTR;
	memcpy(_formatting_buffer, name, namelen);
	_formatting_buffer[namelen] = '\0';
	XFree(name);
    }
    return _formatting_buffer;
}



/*
 *
 * The Format Manager: a group of routines to manage "formats"
 *
 */


static int
Get_Format_Size (const char *format)
{
    long size;

    Scan_Long(format, &size);

    /* Check for legal sizes */
    if (size != 0 && size != 8 && size != 16 && size != 32)
	Fatal_Error("bad format: %s", format);

    return (int) size;
}

static char
Get_Format_Char (const char *format, int i)
{
    long size;

    /* Remove # at front of format */
    format = Scan_Long(format, &size);
    if (!*format)
	Fatal_Error("bad format: %s", format);

    /* Last character repeats forever... */
    if (i >= (int)strlen(format))
	i = strlen(format)-1;

    return format[i];
}

static const char *
Format_Thunk (thunk t, char format_char)
{
    long value;
    value = t.value;

    switch (format_char) {
      case 'c':
	return Format_Unsigned(value);
      case 'i':
	return Format_Signed(value);
	return Format_Atom(value);
     default:
	Fatal_Error("bad format character: %c", format_char);
    }
}

static const char *
Format_Thunk_I (thunk *thunks, const char *format, int i)
{
    if (i >= thunks->thunk_count)
	return "<field not available>";

    return Format_Thunk(thunks[i], Get_Format_Char(format, i));
}



static const char *
Handle_Dollar_sign (const char *dformat, thunk *thunks, const char *format)
{
    long i;
    char * ss="";

    dformat = Scan_Long(dformat, &i);

    if (dformat[0] == '+') {
	int seen = 0;
	dformat++;
	for (; i < thunks->thunk_count; i++) {
	    if (seen)
		printf(", ");
	    seen = 1;
	    //strcat(ss,(char) Format_Thunk_I(thunks, format, (int) i));
	    ss = (char *) Format_Thunk_I(thunks, format, (int) i);
	}
    } else
	printf("%s", Format_Thunk_I(thunks, format, (int) i));

    return ss;
}



static void
Set_Display_Priority (thunk *thunks, const char *dformat, const char *format,int priority)
{
    char c;
    int pid;
    const char * piid;
    int prio;
    const int which = PRIO_PROCESS;
	dformat++;
	dformat++;
	dformat++;
    while ((c = *(dformat++)))
	switch (c) {
	  case '$':
	    piid =  Handle_Dollar_sign(dformat, thunks, format);
	    break;
	}

	printf ("PROCESS PID: %s\n", piid);
	pid=atoi(piid);
	//printf ("\n-- %d \n", pid);
	prio=getpriority(which, (id_t) pid);
	printf ("PRIORITY BEFORE: %d", prio);
	prio=setpriority(which, (id_t) pid, priority);
	
	prio=getpriority(which, (id_t) pid);
	printf ("\nPRIORITY AFTER: %d\n", prio);
	
}


static void
Display_Priority (thunk *thunks, const char *dformat, const char *format)
{
    char c;
    int pid=0;
    const char * piid="";
    int prio;
    const int which = PRIO_PROCESS;
    
    dformat++;
    dformat++;
    dformat++;
    while ((c = *(dformat++)))
	switch (c) {
	  case '$':
	    piid =  Handle_Dollar_sign(dformat, thunks, format);
	    break;
	}

	printf ("PROCESS PID: %s\n", piid);
	prio=getpriority(which, (id_t) pid);
	printf ("PRIORITY: %d\n", prio);
		
}
/*
 *
 * Routines to convert property data to thunks
 *
 */

static long
Extract_Value (const char **pointer, int *length, int size, int signedp)
{
    long value;

    switch (size) {
      case 8:
	if (signedp)
	    value = * (const signed char *) *pointer;
	else
	    value = * (const unsigned char *) *pointer;
	*pointer += 1;
	*length -= 1;
	break;
      case 16:
	if (signedp)
	    value = * (const short *) *pointer;
	else
	    value = * (const unsigned short *) *pointer;
	*pointer += sizeof(short);
	*length -= sizeof(short);
	break;
      case 32:
	if (signedp)
	    value = * (const long *) *pointer;
	else
	    value = * (const unsigned long *) *pointer & 0xffffffff;
	*pointer += sizeof(long);
	*length -= sizeof(long);
	break;
      default:
	abort();
    }
    return value;
}

static long
Extract_Len_String (const char **pointer, int *length, int size, const char **string)
{
    int len;

    if (size != 8)
	Fatal_Error("can't use format character 's' with any size except 8.");
    len = 0; *string = *pointer;
    while ((len++, --*length, *((*pointer)++)) && *length>0);

    return len;
}

static long
Extract_Icon (const char **pointer, int *length, int size, const char **icon)
{
    int len = 0;

    if (size != 32)
	Fatal_Error("can't use format character 'o' with any size except 32.");

    len = *length;
    *icon = *pointer;
    *length = 0;
    return len;
}

static thunk *
Break_Down_Property (const char *pointer, int length, Atom type, const char *format, int size)
{
    thunk *thunks;
    thunk t = {0};
    int i;
    char format_char;

    thunks = Create_Thunk_List();
    i = 0;

    while (length >= size/8) {
	format_char = Get_Format_Char(format, i);
	if (format_char == 's' || format_char == 'u')
	    t.value = Extract_Len_String(&pointer,&length,size,&t.extra_value);
	else if (format_char == 't') {
	    t.extra_encoding = type;
	    t.value = Extract_Len_String(&pointer,&length,size,&t.extra_value);
	}
	else if (format_char == 'o')
	    t.value = Extract_Icon (&pointer,&length,size,&t.extra_value);
	else
	    t.value = Extract_Value(&pointer,&length,size,format_char=='i');
	thunks = Add_Thunk(thunks, t);
	i++;
    }

    return thunks;
}


static Window target_win = 0;
static int max_len = MAXSTR;


static const char *
Get_Window_Property_Data_And_Type (Atom atom,
                                   long *length, Atom *type, int *size)
{
    Atom actual_type;
    int actual_format;
    unsigned long nitems;
    unsigned long nbytes;
    unsigned long bytes_after;
    unsigned char *prop;
    int status;
	
    status = XGetWindowProperty(dpy, target_win, atom, 0, (max_len+3)/4,
				False, AnyPropertyType, &actual_type,
				&actual_format, &nitems, &bytes_after,
				&prop);
    if (status == BadWindow)
	Fatal_Error("window id # 0x%lx does not exists!", target_win);
    if (status != Success)
	Fatal_Error("XGetWindowProperty failed!");

    if (actual_format == 32)
	nbytes = sizeof(long);
    else if (actual_format == 16)
	nbytes = sizeof(short);
    else if (actual_format == 8)
	nbytes = 1;
    else if (actual_format == 0)
        nbytes = 0;
    else
	abort();
    *length = min(nitems * nbytes, max_len);
    *type = actual_type;
    *size = actual_format;
    return (const char *)prop;
}


static void
Set_Prio (int priority)
{
    const char *data;
    long length;
    Atom atom, type;
    thunk *thunks;
    int size, fsize;
    const char *prop="_NET_WM_PID";
    const char *format = NULL;
    const char *dformat = NULL;
    atom = XInternAtom(dpy, prop, True);
    
    if (atom == None) {
	printf(":  no such atom on any window.\n");
	return;
    }

    data = Get_Window_Property_Data_And_Type(atom, &length, &type, &size);
    if (!size) {
	puts(":  not found.");
	return;
    }


    Lookup_Formats(atom, &format, &dformat);
    if (type != None)
	Lookup_Formats(type, &format, &dformat);
    Apply_Default_Formats(&format, &dformat);

    fsize = Get_Format_Size(format);
    if (fsize != size && fsize != 0) {
	printf(": Type mismatch: assumed size %d bits, actual size %d bits.\n",
	       fsize, size);
	return;
    }

    thunks = Break_Down_Property(data, (int)length, type, format, size);

    Set_Display_Priority(thunks, dformat, format, priority);
}

static void
Get_Prio ()
{
    const char *data;
    long length;
    Atom atom, type;
    thunk *thunks;
    int size, fsize;
    const char *prop="_NET_WM_PID";
    const char *format = NULL;
    const char *dformat = NULL;
    atom = XInternAtom(dpy, prop, True);
    
    if (atom == None) {
	printf(":  no such atom on any window.\n");
	return;
    }

    data = Get_Window_Property_Data_And_Type(atom, &length, &type, &size);
    if (!size) {
	puts(":  not found.");
	return;
    }


    Lookup_Formats(atom, &format, &dformat);
    if (type != None)
	Lookup_Formats(type, &format, &dformat);
    Apply_Default_Formats(&format, &dformat);

    fsize = Get_Format_Size(format);
    if (fsize != size && fsize != 0) {
	printf(": Type mismatch: assumed size %d bits, actual size %d bits.\n",
	       fsize, size);
	return;
    }

    thunks = Break_Down_Property(data, (int)length, type, format, size);

    Display_Priority(thunks, dformat, format);
}




/*
 * 
 * Routines for parsing command line:
 *
 */

void
usage (void)
{
    static const char help_message[] = 
"By default program using highest priority: -20\n"
"You can set custom priority using enviroment XRENICEPRIO or argument -p.\n"
"Arguments:\n"
"    -p priority                         set custom priority\n"
"    -g                                  get priority\n";


    fflush (stdout);
    fprintf (stderr,
	     "usage:  %s [-p priority]\n", 
	     program_name);
    fprintf (stderr, "%s\n", help_message);
    exit (1);
}



/*
 *
 * The Main Program:
 *
 */


int
main (int argc, char **argv)
{
    Bool frame_only = False;
    int priority;
    char * prio;
        
    INIT_NAME;
    
    priority=HIGHEST_PRIORITY;
    Setup_Display_And_Screen(&argc, argv);

    target_win = Select_Window_Args(&argc, argv);

    Setup_Mapping();
    if ((prio = getenv("XRENICEPRIO"))) {
	priority=atoi(prio);
    }
	while (argv++, --argc>0 && **argv == '-') {
		if (!strcmp(argv[0], "-"))
	    continue;
	    if (!strcmp(argv[0], "-p")) {
			if (argc>1)
				priority=atoi(argv[1]);
			continue;
		}
		if (!strcmp(argv[0], "-g")) {
			if (target_win == None)
				target_win = Select_Window(dpy, !frame_only);
			Get_Prio();
			exit(0);
		}
		usage();
	}
	
	if (target_win == None)
		target_win = Select_Window(dpy, !frame_only);
  
	Set_Prio(priority);
    exit (0);
}
