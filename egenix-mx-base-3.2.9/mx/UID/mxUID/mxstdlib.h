#ifndef MXSTDLIB_H
#define MXSTDLIB_H

/* Standard stuff I use often -- not Python specific 

   Copyright (c) 2000, Marc-Andre Lemburg; mailto:mal@lemburg.com
   Copyright (c) 2000-2015, eGenix.com Software GmbH; mailto:info@egenix.com
   See the documentation for further copyright information or contact
   the author.

 */

/* Standard includes */
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <math.h>

/* Limits */
#ifdef HAVE_LIMITS_H
# include <limits.h>
#else
# ifndef INT_MAX
#  define INT_MAX 2147483647
# endif
# ifndef LONG_MAX
#  define LONG_MAX INT_MAX
# endif
#endif

/* Disable a few MS VC++ warnings that were introduced in VS 2005, e.g.

   warning C4996: 'getenv': This function or variable may be
   unsafe. Consider using _dupenv_s instead. To disable deprecation,
   use _CRT_SECURE_NO_WARNINGS. See online help for details.

   Note that #define _CRT_SECURE_NO_WARNINGS 1 does not work for some
   reason. You have to use the #pragma warning() to disable warnings
   inside a header file.

*/
#if defined(_MSC_VER) && (_MSC_VER >= 1400)
# pragma warning(disable:4996)
#endif

/* --- My own macros for memory allocation... --------------------------- */

/* Define this macro if the code should use C malloc */
/* #define MAL_USE_C_MALLOC */ 

/* Define this macro if the code should use Python's pymalloc */
/* #define MAL_USE_PYMALLOC */ 

/* Define this macro if the code should use pymalloc and uses the
   cnew()/cnewstruct() macros.

   This is needed to only enable the helper functions when using those
   macros in pymalloc mode.

*/
/* #define MAL_USING_CNEW */

/* Decide which variant to use, if no explicit choice was made: C
   malloc or pymalloc  */
#ifdef MAL_USE_C_MALLOC
/* Force use of C malloc */
#elif !defined(MAL_USE_PYMALLOC) && defined (PYTHON_API_VERSION)
/* Default to using the Python pymalloc in case we're compiling a
   Python extension */
# define MAL_USE_PYMALLOC
#endif

#ifndef MAL_USE_PYMALLOC

/* --- Use C malloc for memory allocation --- */

/* See below for why we sometime use this */
#define mx_C_malloc_free free

# ifdef MAL_MEM_DEBUG

/* Include debug information for tracing memory leaks */

#  define newstruct(x) \
         (mxDebugPrintf("* malloc for struct "#x" (%s:%i)\n",__FILE__,__LINE__),\
	  (x *)malloc(sizeof(x)))
#  define cnewstruct(x) \
         (mxDebugPrintf("* calloc for struct "#x" (%s:%i)\n",c,__FILE__,__LINE__),\
	  (x *)calloc(sizeof(x),1))
#  define new(x,c) \
         (mxDebugPrintf("* malloc for "#c"=%i '"#x"'s (%s:%i)\n",c,__FILE__,__LINE__),\
	  (x *)malloc(sizeof(x)*(c)))
#  define cnew(x,c) \
         (mxDebugPrintf("* calloc for "#c"=%i '"#x"'s (%s:%i)\n",c,__FILE__,__LINE__),\
	  (x *)calloc((c),sizeof(x)))
#  define resize(var,x,c) \
         (mxDebugPrintf("* realloc array "#var" ("#x") at %X to size "#c"=%i (%s:%i)\n",var,c,__FILE__,__LINE__),\
	  (x *)realloc((void*)(var),sizeof(x)*(c)))
#  define varresize(var,x,bytes) \
         (mxDebugPrintf("* realloc var "#var" ("#x") at %X to %i bytes (%s:%i)\n",var,bytes,__FILE__,__LINE__),\
	  (x *)realloc((void*)(var),(bytes)))
#  define free(x) \
         (mxDebugPrintf("* freeing "#x" at %X (%s:%i)\n",x,__FILE__,__LINE__),\
	  free((void*)(x)))
# else

/* Non-debug versions of the macros */

#  define newstruct(x)		((x *)malloc(sizeof(x)))
#  define cnewstruct(x)		((x *)calloc(sizeof(x),1))
#  define new(x,c)		((x *)malloc(sizeof(x)*(c)))
#  define cnew(x,c)		((x *)calloc((c),sizeof(x)))
#  define resize(var,x,c)	((x *)realloc((void*)(var),sizeof(x)*(c)))
#  define varresize(var,x,bytes)	((x *)realloc((void*)(var),(bytes)))
#  define free(x) 		(free((void*)(x)))

# endif

#else

/* --- Use Python pymalloc for memory allocation --- */

# ifdef MAL_USING_CNEW

/* Helper needed since Python doesn't define a calloc() interface. */
static
void *mx_PyObject_CALLOC(register size_t size)
{
    register void *mem;
    if (size == 0)
	return NULL;
    mem = PyObject_MALLOC(size);
    if (mem)
	memset(mem, 0, size);
    return mem;
}

# endif

# ifdef MAL_USING_C_MALLOC_FREE

/* Helper needed since we will override free() with the Python version using
   the macros below. */
static
void mx_C_malloc_free(register void *x)
{
    free(x);
}

# endif

# ifndef MAL_MEM_DEBUG

/* Non-debug versions of the macros */

#  define newstruct(x)		((x *)PyObject_MALLOC(sizeof(x)))
#  ifdef MAL_USING_CNEW
#   define cnewstruct(x)		((x *)mx_PyObject_CALLOC(sizeof(x)))
#  endif
#  define new(x,c)		((x *)PyObject_MALLOC(sizeof(x)*(c)))
#  ifdef MAL_USING_CNEW
#   define cnew(x,c)		((x *)mx_PyObject_CALLOC(sizeof(x)*(c)))
#  endif
#  define resize(var,x,c)	((x *)PyObject_REALLOC((void*)(var),sizeof(x)*(c)))
#  define varresize(var,x,bytes)	((x *)PyObject_REALLOC((void*)(var),(bytes)))
#  define free(x) 		(PyObject_FREE((void*)(x)))

# else

/* Debug versions of the macros */

#  define newstruct(x)		\
    (mxDebugPrintf("* malloc for struct "#x" (%s:%i)\n",__FILE__,__LINE__), \
     (x *)PyObject_MALLOC(sizeof(x)))
#  ifdef MAL_USING_CNEW
#   define cnewstruct(x)		\
    (mxDebugPrintf("* calloc for struct "#x" (%s:%i)\n",c,__FILE__,__LINE__), \
     ((x *)mx_PyObject_CALLOC(sizeof(x))))
#  endif
#  define new(x,c)		\
    (mxDebugPrintf("* malloc for "#c"=%i '"#x"'s (%s:%i)\n",c,__FILE__,__LINE__), \
     ((x *)PyObject_MALLOC(sizeof(x)*(c))))
#  ifdef MAL_USING_CNEW
#   define cnew(x,c)		\
    (mxDebugPrintf("* calloc for "#c"=%i '"#x"'s (%s:%i)\n",c,__FILE__,__LINE__), \
     ((x *)mx_PyObject_CALLOC(sizeof(x)*(c))))
#  endif
#  define resize(var,x,c)	\
    (mxDebugPrintf("* realloc array "#var" ("#x") at %X to size "#c"=%i (%s:%i)\n",var,c,__FILE__,__LINE__), \
     ((x *)PyObject_REALLOC((void*)(var),sizeof(x)*(c))))
#  define varresize(var,x,bytes)	\
    (mxDebugPrintf("* realloc var "#var" ("#x") at %X to %i bytes (%s:%i)\n",var,bytes,__FILE__,__LINE__), \
     ((x *)PyObject_REALLOC((void*)(var),(bytes))))
#  define free(x) 	\
    (mxDebugPrintf("* freeing "#x" at %X (%s:%i)\n",x,__FILE__,__LINE__), \
     (PyObject_FREE((void*)(x))))

# endif

#endif

/* --- Debugging output ------------------------------------------------- */

/* Use the flag MAL_DEBUG to enable debug processing.

   The flag MAL_DEBUG_WITH_PYTHON can be used to indicate that the
   object file will be linked with Python, so we can use Python APIs
   for the debug processing here.

*/
#ifdef MAL_DEBUG_WITH_PYTHON
# ifndef PYTHON_API_VERSION
#  error "mx.h must be included when compiling with MAL_DEBUG_WITH_PYTHON"
# endif
# ifndef MAL_DEBUG
#  define MAL_DEBUG
# endif
#else
# if defined(PYTHON_API_VERSION) && defined(MAL_DEBUG)
#  define MAL_DEBUG_WITH_PYTHON
# endif
#endif

/* Indicator for the availability of these interfaces: */

#define HAVE_MAL_DEBUG

/* Name of the environment variable defining the log file name
   to be used: */

#ifndef MAL_DEBUG_OUTPUTFILE_ENV_VARIABLE
# define MAL_DEBUG_OUTPUTFILE_ENV_VARIABLE "mxLogFile"
#endif

/* File name to be used for debug logging (each object file using this
   facility may set its own logging file) if no environment variable
   is set: */

#ifndef MAL_DEBUG_OUTPUTFILE
# define MAL_DEBUG_OUTPUTFILE "mx.log"
#endif

/* Name of the environment variable defining the log file prefix to be
   used (e.g. to direct all log files into a separate directory): */

#ifndef MAL_DEBUG_OUTPUTFILEPREFIX_ENV_VARIABLE
# define MAL_DEBUG_OUTPUTFILEPREFIX_ENV_VARIABLE "mxLogFileDir"
#endif

/* File name prefix to be used for log files, if no environment
   variable is set: */

#ifndef MAL_DEBUG_OUTPUTFILEPREFIX
# define MAL_DEBUG_OUTPUTFILEPREFIX ""
#endif

/* Log id to be used */

#ifndef MAL_DEBUG_LOGID
# define MAL_DEBUG_LOGID "New Log Session"
#endif

/* Debug printf() API

   Output is written to a log file or stream. If the output file is
   not yet open, the function will try to open the file as defined by
   the environment or the program defines.  The file remains open
   until the program terminates. Subsequent changes to the environment
   are not taken into account.

   The output file is deduced in the following way:

   1. get the filename from the environment, revert to the predefined
      value

   2. get the filename prefix from the environment, revert to
      the predefined value
   
   3. if filename is one of "stderr" or "stdout" use the native
      streams for output; otherwise try to open fileprefix + filename
      reverting to stderr in case this fails.

 */

static
int mxDebugPrintf(const char *format, ...)
{
    va_list args;
    static FILE *mxDebugPrintf_file;

    if (!mxDebugPrintf_file) {
	time_t now;
	char *filename,*fileprefix;

	now = time(NULL);
	filename = getenv(MAL_DEBUG_OUTPUTFILE_ENV_VARIABLE);
	if (!filename)
	    filename = MAL_DEBUG_OUTPUTFILE;
	fileprefix = getenv(MAL_DEBUG_OUTPUTFILEPREFIX_ENV_VARIABLE);
	if (!fileprefix)
	    fileprefix = MAL_DEBUG_OUTPUTFILEPREFIX;
	if (strcmp(filename,"stdout") == 0)
	    mxDebugPrintf_file = stdout;
	else if (strcmp(filename,"stderr") == 0)
	    mxDebugPrintf_file = stderr;
	else {
	    char logfile[512];

	    if (strlen(fileprefix) + strlen(filename) > sizeof(logfile) - 1) {
		/* Hack to shut up "cc -Wall" warning that this
		   function is not used... */
		static void *mxDebugPrintf_used;
		mxDebugPrintf_used = (void *)mxDebugPrintf;

		/* Default to stderr in case the log file name doesn't
		   fit the available buffer */
		mxDebugPrintf_file = stderr;
		fprintf(mxDebugPrintf_file,
			"\n*** Log file name too long: '%s%s'; "
			"using stderr\n", fileprefix, filename);
	    }
	    else {
		/* We made sure that there is enough room in logfile,
		   so can use the unlimited APIs here */
		strcpy(logfile, fileprefix);
		strcat(logfile, filename);
		mxDebugPrintf_file = fopen(logfile,"ab");
		if (!mxDebugPrintf_file) {
		    /* Default to stderr in case the log file cannot be
		       opened */
		    mxDebugPrintf_file = stderr;
		    fprintf(mxDebugPrintf_file,
			    "\n*** Failed to open log file '%s'; "
			    "using stderr\n",logfile);
		}
	    }
	}
	fprintf(mxDebugPrintf_file,
		"\n--- "MAL_DEBUG_LOGID" --- %s\n",
		ctime(&now));
    }

    va_start(args,format);
    vfprintf(mxDebugPrintf_file,format,args);
    fflush(mxDebugPrintf_file);
    va_end(args);
    return 1;
}

#ifdef MAL_DEBUG

# ifdef MAL_DEBUG_WITH_PYTHON
/* Use the Python debug flag to enable debugging output (python -d) */
#  define DPRINTF if (Py_DebugFlag) mxDebugPrintf
#  define IF_DEBUGGING if (Py_DebugFlag) 
#  define DEBUGGING (Py_DebugFlag > 0)
# else

/* Always output debugging information */
#  define DPRINTF mxDebugPrintf
#  define IF_DEBUGGING  
#  define DEBUGGING (1)
# endif

#else

# ifndef _MSC_VER
/* This assumes that you are using an optimizing compiler which
   eliminates the resulting debug code. */
#  define DPRINTF if (0) mxDebugPrintf
#  define IF_DEBUGGING if (0) 
#  define DEBUGGING (0)
# else

/* MSVC doesn't do a good job here, so we use a different approach. */
#  define DPRINTF 0 && mxDebugPrintf
#  define IF_DEBUGGING if (0) 
#  define DEBUGGING (0)
# endif

#endif

/* --- Misc ------------------------------------------------------------- */

/* The usual bunch... */
#ifndef max
# define max(a,b) ((a>b)?(a):(b))
#endif
#ifndef min
# define min(a,b) ((a<b)?(a):(b))
#endif

/* Bit testing... returns 1 iff bit is on in value, 0 otherwise */
#ifndef testbit
# define testbit(value,bit) (((value) & (1<<(bit))) != 0)
#endif

/* Flag testing... returns 1 iff flag is on in value, 0 otherwise */
#ifndef testflag
# define testflag(value,flag) (((value) & (flag)) != 0)
#endif

/* EOF */
#endif

