/** $Id: tape.c 1182 2008-12-22 22:08:36Z dchassin $
	Copyright (C) 2008 Battelle Memorial Institute
	@file tape.c
	@addtogroup tapes Players and recorders (tape)
	@ingroup modules

	Tape players and recorders are used to manage the boundary conditions
	and record properties of objects during simulation.  There are two kinds
	of players and two kinds of recorders:
	- \p player is used to play a recording of a single value to property of an object
	- \p shaper is used to play a periodic scaled shape to a property of groups of objects
	- \p recorder is used to collect a recording of one of more properties of an object
	- \p collector is used to collect an aggregation of a property from a group of objects
 @{
 **/

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <ctype.h>
#include "gridlabd.h"
#include "object.h"
#include "aggregate.h"
#include "histogram.h"

#define _TAPE_C

#include "tape.h"
#include "file.h"
#include "odbc.h"

#define MAP_DOUBLE(X,LO,HI) {#X,VT_DOUBLE,&X,LO,HI}
#define MAP_INTEGER(X,LO,HI) {#X,VT_INTEGER,&X,LO,HI}
#define MAP_STRING(X) {#X,VT_STRING,X,sizeof(X),0}
#define MAP_END {NULL}

VARMAP varmap[] = {
	/* add module variables you want to be available using module_setvar in core */
	MAP_STRING(timestamp_format),
	MAP_END
};

extern CLASS *player_class;
extern CLASS *shaper_class;
extern CLASS *recorder_class;
extern CLASS *collector_class;


/* The following hack is required to stringize LIBEXT as passed in from
 * Makefile and used by snprintf below to construct the library name. */
#define _STR(x) #x
#define STR(x) _STR(x)


#ifdef WIN32
#define WIN32_LEAN_AND_MEAN		// Exclude rarely-used stuff from Windows headers
#define _WIN32_WINNT 0x0400
#include <windows.h>
#define LIBPREFIX
#ifndef LIBEXT
#define LIBEXT .dll
#endif
#define DLLOAD(P) LoadLibrary(P)
#define DLSYM(H,S) GetProcAddress((HINSTANCE)H,S)
#define snprintf _snprintf
#else /* ANSI */
#include "dlfcn.h"
#define LIBPREFIX "lib"
#ifndef LIBEXT
#define LIBEXT .so
#else
#endif
#define DLLOAD(P) dlopen(P,RTLD_LAZY)
#define DLSYM(H,S) dlsym(H,S)
#endif

static TAPEFUNCS *funcs = NULL;
static char1024 tape_gnuplot_path;

typedef int (*OPENFUNC)(void *, char *, char *);
typedef char *(*READFUNC)(void *, char *, unsigned int);
typedef int (*WRITEFUNC)(void *, char *, char *);
typedef int (*REWINDFUNC)(void *);
typedef void (*CLOSEFUNC)(void *);

TAPEFUNCS *get_ftable(char *mode){
	/* check what we've already loaded */
	char256 modname;
	TAPEFUNCS *fptr = funcs;
	TAPEOPS *ops = NULL;
	void *lib = NULL;
	CALLBACKS **c = NULL;
	char *tpath = NULL;
	while(fptr != NULL){
		if(strcmp(fptr->mode, mode) == 0)
			return fptr;
		fptr = fptr->next;
	}
	/* fptr = NULL */
	fptr = malloc(sizeof(TAPEFUNCS));
	if(fptr == NULL)
	{
		GL_THROW("get_ftable(char *mode='%s'): out of memory", mode);
		return NULL; /* out of memory */
	}
	snprintf(modname, 1024, LIBPREFIX "tape_%s" STR(LIBEXT), mode);
	tpath = gl_findfile(modname, NULL, 0|4);
	if(tpath == NULL){
		GL_THROW("unable to locate %s", modname);
		return NULL;
	}
	lib = fptr->hLib = DLLOAD(tpath);
	if(fptr->hLib == NULL){
		GL_THROW("tape module: unable to load DLL for %s", modname);
		return NULL;
	}
	c = (CALLBACKS **)DLSYM(lib, "callback");
	if(c)
		*c = callback;
	//	nonfatal ommission
	ops = fptr->collector = malloc(sizeof(TAPEOPS));
	ops->open = (OPENFUNC)DLSYM(lib, "open_collector");
	ops->read = NULL;
	ops->write = (WRITEFUNC)DLSYM(lib, "write_collector");
	ops->rewind = NULL;
	ops->close = (CLOSEFUNC)DLSYM(lib, "close_collector");
	ops = fptr->player = malloc(sizeof(TAPEOPS));
	ops->open = (OPENFUNC)DLSYM(lib, "open_player");
	ops->read = (READFUNC)DLSYM(lib, "read_player");
	ops->write = NULL;
	ops->rewind = (REWINDFUNC)DLSYM(lib, "rewind_player");
	ops->close = (CLOSEFUNC)DLSYM(lib, "close_player");
	ops = fptr->recorder = malloc(sizeof(TAPEOPS));
	ops->open = (OPENFUNC)DLSYM(lib, "open_recorder");
	ops->read = NULL;
	ops->write = (WRITEFUNC)DLSYM(lib, "write_recorder");
	ops->rewind = NULL;
	ops->close = (CLOSEFUNC)DLSYM(lib, "close_recorder");
	ops = fptr->histogram = malloc(sizeof(TAPEOPS));
	ops->open = (OPENFUNC)DLSYM(lib, "open_histogram");
	ops->read = NULL;
	ops->write = (WRITEFUNC)DLSYM(lib, "write_histogram");
	ops->rewind = NULL;
	ops->close = (CLOSEFUNC)DLSYM(lib, "close_histogram");
	ops = fptr->shaper = malloc(sizeof(TAPEOPS));
	ops->open = (OPENFUNC)DLSYM(lib, "open_shaper");
	ops->read = (READFUNC)DLSYM(lib, "read_shaper");
	ops->write = NULL;
	ops->rewind = (REWINDFUNC)DLSYM(lib, "rewind_shaper");
	ops->close = (CLOSEFUNC)DLSYM(lib, "close_shaper");
	fptr->next = funcs;
	funcs = fptr;
	return funcs;
}

EXPORT CLASS *init(CALLBACKS *fntable, void *module, int argc, char *argv[])
{
	struct recorder my;

	if (set_callback(fntable)==NULL)
	{
		errno = EINVAL;
		return NULL;
	}

	/* globals for the tape module*/
	sprintf(tape_gnuplot_path, "c:/Program Files/GnuPlot/bin/wgnuplot.exe");
	gl_global_create("tape::gnuplot_path",PT_char1024,&tape_gnuplot_path,NULL);

	/* register the first class implemented, use SHARE to reveal variables */
	player_class = gl_register_class(module,"player",sizeof(struct player),PC_BOTTOMUP); 
	PUBLISH_STRUCT(player,char32,property);
	PUBLISH_STRUCT(player,char1024,file);
	PUBLISH_STRUCT(player,char8,filetype);
	PUBLISH_STRUCT(player,int32,loop);

	/* register the first class implemented, use SHARE to reveal variables */
	shaper_class = gl_register_class(module,"shaper",sizeof(struct shaper),PC_BOTTOMUP); 
	PUBLISH_STRUCT(shaper,char1024,file);
	PUBLISH_STRUCT(shaper,char8,filetype);
	PUBLISH_STRUCT(shaper,char256,group);
	PUBLISH_STRUCT(shaper,char32,property);
	PUBLISH_STRUCT(shaper,double,magnitude);
	PUBLISH_STRUCT(shaper,double,events);

	/* register the other classes as needed, */
	recorder_class = gl_register_class(module,"recorder",sizeof(struct recorder),PC_POSTTOPDOWN);
	PUBLISH_STRUCT(recorder,char1024,property);
	PUBLISH_STRUCT(recorder,char32,trigger);
	PUBLISH_STRUCT(recorder,char1024,file);
	PUBLISH_STRUCT(recorder,int64,interval);
	PUBLISH_STRUCT(recorder,int32,limit);
	PUBLISH_STRUCT(recorder,char1024,plotcommands);
	PUBLISH_STRUCT(recorder,char32,xdata);
	PUBLISH_STRUCT(recorder,char32,columns);
	
	if(gl_publish_variable(recorder_class,PT_enumeration, "output", ((char*)&(my.output) - (char *)&my) ,
			PT_KEYWORD, "SCREEN", SCREEN,
			PT_KEYWORD, "EPS",    EPS,
			PT_KEYWORD, "GIF",    GIF,
			PT_KEYWORD, "JPG",    JPG,
			PT_KEYWORD, "PDF",    PDF,
			PT_KEYWORD, "PNG",    PNG,
			PT_KEYWORD, "SVG",    SVG, 
			NULL) < 1)
		GL_THROW("Could not publish property output for recorder");

	/* register the other classes as needed, */
	collector_class = gl_register_class(module,"collector",sizeof(struct collector),PC_POSTTOPDOWN);
	PUBLISH_STRUCT(collector,char1024,property);
	PUBLISH_STRUCT(collector,char32,trigger);
	PUBLISH_STRUCT(collector,char1024,file);
	PUBLISH_STRUCT(collector,int64,interval);
	PUBLISH_STRUCT(collector,int32,limit);
	PUBLISH_STRUCT(collector,char256,group);

	/* new histogram() */
	new_histogram(module);

	/* always return the first class registered */
	return player_class;
}

EXPORT int check(void)
{
	unsigned int errcount=0;

	/* check players */
	{	OBJECT *obj=NULL;
		FINDLIST *players = gl_find_objects(FL_NEW,FT_CLASS,SAME,"tape",FT_END);
		while ((obj=gl_find_next(players,obj))!=NULL)
		{
			struct player *pData = OBJECTDATA(obj,struct player);
			if (gl_findfile(pData->file,NULL,FF_EXIST)==NULL)
			{
				errcount++;
				gl_error("player %s (id=%d) uses the file '%s', which cannot be found", obj->name?obj->name:"(unnamed)", obj->id, pData->file);
			}
		}
	}

	/* check shapers */
	{	OBJECT *obj=NULL;
		FINDLIST *shapers = gl_find_objects(FL_NEW,FT_CLASS,SAME,"shaper",FT_END);
		while ((obj=gl_find_next(shapers,obj))!=NULL)
		{
			struct shaper *pData = OBJECTDATA(obj,struct shaper);
			if (gl_findfile(pData->file,NULL,FF_EXIST)==NULL)
			{
				errcount++;
				gl_error("shaper %s (id=%d) uses the file '%s', which cannot be found", obj->name?obj->name:"(unnamed)", obj->id, pData->file);
			}
		}
	}

	return errcount;
}

int do_kill()
{
	/* if global memory needs to be released, this is the time to do it */
	return 0;
}

/**@}*/
