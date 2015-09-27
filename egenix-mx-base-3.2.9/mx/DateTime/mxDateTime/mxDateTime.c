/*
  mxDateTime -- Generic date/time types

  Copyright (c) 1997-2000, Marc-Andre Lemburg; mailto:mal@lemburg.com
  Copyright (c) 2000-2015, eGenix.com Software GmbH; mailto:info@egenix.com
  See the documentation for further copyright information or contact
  the author (mailto:mal@lemburg.com).

  XXX Update the "mixed" coercion hack when the number protocol changes
      to introduce new binary&ternary functions.

*/

/* Version number: Major.Minor.Patchlevel */
#define MXDATETIME_VERSION "3.2.9"

/* Define this to aid in finding memory leaks */
/*#define MAL_MEM_DEBUG*/
/*#define MAL_DEBUG*/

/* Logging file used by debugging facility */
#ifndef MAL_DEBUG_OUTPUTFILE
# define MAL_DEBUG_OUTPUTFILE "mxDateTime.log"
#endif

/* We want all our symbols to be exported */
#define MX_BUILDING_MXDATETIME

/* Uncomment this to make the old interfaces available too */
/*#define OLD_INTERFACE*/

/* mx.DateTime can use its own API for querying the current time from
   the OS, or reuse the Python time.time() function. The latter is
   more portable, but slower. Define the following symbol to use the
   faster native API.  */
/*#define USE_FAST_GETCURRENTTIME*/

/* Mark the module as Py_ssize_t clean. */
#define PY_SSIZE_T_CLEAN 1

/* Setup mxstdlib memory management */
#if 1
# define MAL_USE_PYMALLOC
#else
# define MAL_USE_C_MALLOC
#endif

/* Some additional switches are needed on some platforms to make
   strptime() and timegm() available. */
#ifndef _GNU_SOURCE
# define _GNU_SOURCE 1
#endif

#include "mx.h"
#include "mxDateTime.h"

#ifdef USE_FAST_GETCURRENTTIME

/* Additional includes needed for native mxDateTime_GetCurrentTime()
   API */
# if defined(HAVE_SYS_TIME_H) && defined(TIME_WITH_SYS_TIME)
#  include <sys/time.h>
#  include <time.h>
# else
#  include <time.h>
# endif
# ifdef HAVE_UNISTD_H
#  include <unistd.h>
# endif
# ifdef HAVE_FTIME
#  include <sys/timeb.h>
# endif

#endif

/* We need floor() and ceil() for ticks conversions. */
#include <math.h>

/* The module makes use of two functions called strftime() and
   strptime() for the conversion between strings and date/time
   values. Since not all C compilers know about these functions,
   you can turn these features on/off be defining the following
   symbols (if you ran the Python configuration script then it will
   provide this information for you -- on other platforms than
   Unix you may have to define them manually). */
/*#define HAVE_STRFTIME*/
/*#define HAVE_STRPTIME*/
/*#define HAVE_TIMEGM*/

/* The start size for output from strftime. */
#define STRFTIME_OUTPUT_SIZE	1024

/* Define these to have the module use free lists (saves malloc calls); we
   don't use free lists for Python debug builds, since they get in the way
   with Python's object tracking headers. See #1470. */
#ifndef Py_DEBUG
#define MXDATETIME_FREELIST
#define MXDATETIMEDELTA_FREELIST
#endif

/* Define this to enable the copy-protocol (__copy__, __deepcopy__) */
#define COPY_PROTOCOL

/* Define to have the seconds part rounded when assigning to tm_sec in
   tm structs; note that rounding can result in the seconds part to be
   displayed as 60 instead of wrapping to the next minute, hour,
   etc. */
/*#define ROUND_SECONDS_IN_TM_STRUCT*/

/* Define to make type subclassable. Note that this only works in
   Python 2.2 and above. */
/*#define WANT_SUBCLASSABLE_TYPES*/

/* For interop with the datetime module that's available in Python 2.3
   and above (the C API got added in Python 2.4): */

#if PY_VERSION_HEX >= 0x02040000

# include "datetime.h"

/* These checks try to avoid having to load the datetime module. Once
   it is loaded, the pointer-based fast type checks are applied. */

# define mx_PyDate_Check(op)						\
    (mxDateTime_PyDateTimeAPI_Initialized ?							\
         PyDate_Check((op)) :						\
         Py_StringsCompareEqual((op)->ob_type->tp_name, "datetime.date"))

# define mx_PyDateTime_Check(op)					\
    (mxDateTime_PyDateTimeAPI_Initialized ?							\
         PyDateTime_Check((op)) :					\
         Py_StringsCompareEqual((op)->ob_type->tp_name, "datetime.datetime"))

# define mx_PyTime_Check(op)						\
    (mxDateTime_PyDateTimeAPI_Initialized ?							\
         PyTime_Check((op)) :						\
         Py_StringsCompareEqual((op)->ob_type->tp_name, "datetime.time"))

# define mx_PyDelta_Check(op)						\
    (mxDateTime_PyDateTimeAPI_Initialized ?							\
         PyDelta_Check((op)) :						\
         Py_StringsCompareEqual((op)->ob_type->tp_name, "datetime.timedelta"))

/* datetime.h doesn't provide a macro for this... */
# define mx_PyDeltaInSeconds(x)					  \
         (((PyDateTime_Delta *)x)->days * SECONDS_PER_DAY	  \
	  + (double)((PyDateTime_Delta *)x)->seconds	          \
	  + (double)((PyDateTime_Delta *)x)->microseconds * 1e-6)

/* This is a helper for working with time objects */
# define mx_PyTimeInSeconds(x)					\
         ((double)(PyDateTime_TIME_GET_HOUR(x) * 3600		\
                   + PyDateTime_TIME_GET_MINUTE(x) * 60		\
                   + PyDateTime_TIME_GET_SECOND(x))		\
          + (double)PyDateTime_TIME_GET_MICROSECOND(x) * 1e-6)

/* Flag telling us whether the datetime API struct was initialized or not. */
static int mxDateTime_PyDateTimeAPI_Initialized = 0;

/* Helper to reset the PyDateTimeAPI (the global itself is defined in
   datetime.h) */

static
int mx_Reset_PyDateTimeAPI(void)
{
    mxDateTime_PyDateTimeAPI_Initialized = 0;
    return 0;
}

/* Helper to make sure the PyDateTimeAPI is loaded */

static
int mx_Require_PyDateTimeAPI(void)
{
    if (mxDateTime_PyDateTimeAPI_Initialized && PyDateTimeAPI)
	/* Already initialized */
	return 0;

    /* Init the PyDateTimeAPI global */
    PyDateTime_IMPORT;
    if (!PyDateTimeAPI)
	goto onError;
    mxDateTime_PyDateTimeAPI_Initialized = 1;
    return 0;

 onError:
    return -1;
}

/* Helper to lazily init the PyDateTimeAPI. */

static
int mx_Init_PyDateTimeAPI(void)
{
    PyObject *sys_modules, *datetime_module;

    /* Make sure that the pointer is reset when initializing the C
       API */
    mx_Reset_PyDateTimeAPI();

    /* Check sys.modules for the datetime module and load the API if
       the module is present */
    sys_modules = PySys_GetObject("modules");
    if (sys_modules) {
	DPRINTF("mx_Init_PyDateTimeAPI(): found sys.modules\n");
	datetime_module = PyDict_GetItemString(sys_modules, "datetime");
	if (datetime_module) {
	    DPRINTF("mx_Init_PyDateTimeAPI(): datetime module loaded; "
		    "loading the C API\n");
	    /* Init the PyDateTimeAPI global */
	    if (mx_Require_PyDateTimeAPI())
		goto onError;
	}
	else {
	    DPRINTF("mx_Init_PyDateTimeAPI(): "
		    "datetime module not (yet) loaded");
	    PyErr_Clear();
	}
    }
    else
	DPRINTF("mx_Init_PyDateTimeAPI(): could not find sys.modules\n");
    return 0;

 onError:
    return -1;
}

# define HAVE_PYDATETIME 1

#endif

/* --- configuration checks ----------------------------------------------- */

#if PY_VERSION_HEX < 0x02020000
# undef WANT_SUBCLASSABLE_TYPES
#endif

/* --- module helpers ----------------------------------------------------- */

/* Seconds in a day (as double) */
#define SECONDS_PER_DAY ((double) 86400.0)

/* abstime value limit (as double). 

   The limit itself does not belong to the range of accepted values. Includes
   one leap second per day.

*/
#define MAX_ABSTIME_VALUE ((double) 86401.0)
#define MIN_ABSTIME_VALUE ((double) 0.0)

/* Year value limits (as long).

   The limits are included in the range of permitted values.  

   The values are determined by using:
   DateTimeFromAbsDateTime(sys.maxint - (365*2), 0).year

*/
#if LONG_MAX > 2147483648
/* 64+-bit platforms */
# define MAX_YEAR_VALUE ((long) 25252734927766553)
#else
/* 32-bit platforms */
# define MAX_YEAR_VALUE ((long) 5879609)
#endif
# define MIN_YEAR_VALUE ((long) - MAX_YEAR_VALUE + 1)

/* absdate value limits (as long).

   The limits are included in the range of permitted values. 

   The values are determined by using:
   Date(DateTimeFromAbsDateTime(sys.maxint - (365*2), 0).year, 12, 31).absdate

*/
#if LONG_MAX > 2147483648
/* 64+-bit platforms:  */
# define MAX_ABSDATE_VALUE ((long) 9223372036854775234)
#else
/* 32-bit platforms */
# define MAX_ABSDATE_VALUE ((long) 2147483090)
#endif
# define MIN_ABSDATE_VALUE ((long) - MAX_ABSDATE_VALUE)

/* DateTimeDelta seconds limits (as double). 

   C IEEE 754 doubles have a precision of 53 bits, so that's our limit for
   deltas without losing information. On 32-bit platforms, we also have to pay
   attention to the broken down day value which is stored as a long.  This
   further reduces the range. The limits are included in the range of
   permitted values.

*/
#if LONG_MAX > 2147483648
/* 64+-bit platforms:  */
# define MAX_DATETIMEDELTA_SECONDS ((double) 9007199254740992.0)
#else
/* 32-bit platforms */
# define MAX_DATETIMEDELTA_SECONDS ((double) 185542587100800.0)
#endif
#define MIN_DATETIMEDELTA_SECONDS ((double) - MAX_DATETIMEDELTA_SECONDS)

/* Test for negativeness of doubles */
#define DOUBLE_IS_NEGATIVE(x) ((x) < (double) 0.0)

/* Swap the comparison op to adjust for swapped arguments */
static int _swapped_args_richcompare_op[6] = {
    Py_GT, Py_GE, Py_EQ, Py_NE, Py_LT, Py_LE,
};
#define SWAPPED_ARGS_RICHCOMPARE_OP(op) _swapped_args_richcompare_op[op]

/* --- module doc-string -------------------------------------------------- */

static char *Module_docstring = 

 MXDATETIME_MODULE" -- Generic date/time types. Version "MXDATETIME_VERSION"\n\n"

 "Copyright (c) 1997-2000, Marc-Andre Lemburg; mailto:mal@lemburg.com\n"
 "Copyright (c) 2000-2015, eGenix.com Software GmbH; mailto:info@egenix.com\n\n"
 "                 All Rights Reserved\n\n"
 "See the documentation for further information on copyrights,\n"
 "or contact the author."
;

/* --- module globals ----------------------------------------------------- */

static PyObject *mxDateTime_Error;		/* Error Exception
						   object */
static PyObject *mxDateTime_RangeError;		/* RangeError
						   Exception object */

static PyObject *mxDateTime_GregorianCalendar;	/* String 'Gregorian' */
static PyObject *mxDateTime_JulianCalendar;	/* String 'Julian' */

static int mxDateTime_POSIXConform = 0;		/* Does the system use POSIX
						   time_t values ? */

static int mxDateTime_DoubleStackProblem = 0;	/* Does the system
						   have a problem
						   passing doubles on
						   the stack ? */

/* Table with day offsets for each month (0-based, without and with leap) */
static int month_offset[2][13] = {
    { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365 },
    { 0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335, 366 }
};

/* Table of number of days in a month (0-based, without and with leap) */
static int days_in_month[2][12] = {
    { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 },
    { 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 }
};

/* Free lists for DateTime and DateTimeDelta objects */
#ifdef MXDATETIME_FREELIST
static mxDateTimeObject *mxDateTime_FreeList = NULL;
#endif
#ifdef MXDATETIMEDELTA_FREELIST
static mxDateTimeDeltaObject *mxDateTimeDelta_FreeList = NULL;
#endif

/* This must be a callable function that returns the current local
   time in Unix ticks. It is set by the mxDateTime/__init__.py module
   to the standard Python time.time() function. */
static PyObject *mxDateTime_nowapi = NULL;

/* Flag telling us whether the module was initialized or not. */
static int mxDateTime_Initialized = 0;

/* --- forward declarations ----------------------------------------------- */

staticforward PyTypeObject mxDateTime_Type;
staticforward PyMethodDef mxDateTime_Methods[];

staticforward PyTypeObject mxDateTimeDelta_Type;
staticforward PyMethodDef mxDateTimeDelta_Methods[];

staticforward
PyObject *mxDateTimeDelta_FromDaysEx(long days,
				     double seconds);

staticforward
PyObject *mxDateTimeDelta_FromSeconds(double seconds);

/* --- internal macros ---------------------------------------------------- */

#ifndef WANT_SUBCLASSABLE_TYPES

#define _mxDateTime_Check(v) \
        (((mxDateTimeObject *)(v))->ob_type == &mxDateTime_Type)

#define _mxDateTimeDelta_Check(v) \
        (((mxDateTimeDeltaObject *)(v))->ob_type == \
	 &mxDateTimeDelta_Type)

#define _mxDateTime_CheckExact(v) _mxDateTime_Check(v)
#define _mxDateTimeDelta_CheckExact(v) _mxDateTimeDelta_Check(v)

#else

#define _mxDateTime_Check(v) \
        PyObject_TypeCheck(v, &mxDateTime_Type)

#define _mxDateTimeDelta_Check(v) \
        PyObject_TypeCheck(v, &mxDateTimeDelta_Type)

#define _mxDateTime_CheckExact(v) \
        (((mxDateTimeObject *)(v))->ob_type == &mxDateTime_Type)

#define _mxDateTimeDelta_CheckExact(v) \
        (((mxDateTimeDeltaObject *)(v))->ob_type == \
	 &mxDateTimeDelta_Type)

#endif

/* --- module helpers ----------------------------------------------------- */

/* Create an exception object, insert it into the module dictionary
   under the given name and return the object pointer; this is NULL in
   case an error occurred. base can be given to indicate the base
   object to be used by the exception object. It should be NULL
   otherwise */

static 
PyObject *insexc(PyObject *moddict,
		 char *name,
		 PyObject *base)
{
    PyObject *v;
    char fullname[256];
    char *modname;
    char *dot;
    
    v = PyDict_GetItemString(moddict, "__name__");
    if (v == NULL)
	modname = NULL;
    else
	modname = mxPyText_AsString(v);
    if (modname == NULL) {
	PyErr_Clear();
	modname = MXDATETIME_MODULE;
    }
    /* The symbols from this extension are imported into
       mx.<packagename>. We trim the name to not confuse the user with
       an overly long package path. */
    strcpy(fullname, modname);
    dot = strchr(fullname, '.');
    if (dot)
	dot = strchr(dot+1, '.');
    if (dot)
	strcpy(dot+1, name);
    else
	sprintf(fullname, "%s.%s", modname, name);

    v = PyErr_NewException(fullname, base, NULL);
    if (v == NULL)
	return NULL;
    if (PyDict_SetItemString(moddict,name,v))
	return NULL;
    return v;
}

/* Helper for adding integer constants to a dictionary. Check for
   errors with PyErr_Occurred() */
static 
void insint(PyObject *dict,
	    char *name,
	    int value)
{
    PyObject *v = PyInt_FromLong((long)value);
    PyDict_SetItemString(dict, name, v);
    Py_XDECREF(v);
}

#if 0
/* Helper for adding string constants to a dictionary. Check for
   errors with PyErr_Occurred() */
static 
void insstr(PyObject *dict,
	    char *name,
	    char *value)
{
    PyObject *v = mxPyText_FromString(value);
    PyDict_SetItemString(dict, name, v);
    Py_XDECREF(v);
}
#endif

/* Helper for adding objects to dictionaries. Check for errors with
   PyErr_Occurred() */
static 
void insobj(PyObject *dict,
	    char *name,
	    PyObject *v)
{
    PyDict_SetItemString(dict, name, v);
    Py_XDECREF(v);
}

static
PyObject *notimplemented1(PyObject *v)
{
    Py_Error(PyExc_TypeError,
	     "operation not implemented");
 onError:
    return NULL;
}

static
PyObject *notimplemented2(PyObject *v, PyObject *w)
{
    Py_Error(PyExc_TypeError,
	     "operation not implemented");
 onError:
    return NULL;
}

static
PyObject *notimplemented3(PyObject *u, PyObject *v, PyObject *w)
{
    Py_Error(PyExc_TypeError,
	     "operation not implemented");
 onError:
    return NULL;
}

/* --- DateTime Object -------------------------------------------------*/

/* --- allocation --- */

static
mxDateTimeObject *mxDateTime_New(void)
{
    mxDateTimeObject *datetime;

#ifdef MXDATETIME_FREELIST
    if (mxDateTime_FreeList) {
	datetime = mxDateTime_FreeList;
	mxDateTime_FreeList = *(mxDateTimeObject **)mxDateTime_FreeList;
	datetime->ob_type = &mxDateTime_Type;
	_Py_NewReference((PyObject *)datetime);
    }
    else
#endif 
	 {
	datetime = PyObject_NEW(mxDateTimeObject,&mxDateTime_Type);
	if (datetime == NULL)
	    return NULL;
    }

    return datetime;
}

/* --- deallocation --- */

static
void mxDateTime_Free(mxDateTimeObject *datetime)
{
#ifdef MXDATETIME_FREELIST
    /* Append mxDateTime objects to free list */
# ifdef WANT_SUBCLASSABLE_TYPES
    if (_mxDateTime_CheckExact(datetime))
# endif
	{
	*(mxDateTimeObject **)datetime = mxDateTime_FreeList;
	mxDateTime_FreeList = datetime;
    }
#else
    PyObject_Del(datetime);
#endif
}

#ifdef WANT_SUBCLASSABLE_TYPES
static
void mxDateTime_Deallocate(mxDateTimeObject *datetime)
{
    datetime->ob_type->tp_free((PyObject *)datetime);
}
#endif

/* --- internal functions --- */

#ifdef USE_FAST_GETCURRENTTIME

/* Returns the current time in Unix ticks.

   The function tries to use the most accurate API available on the
   system.

   -1.0 is returned in case of an error.

*/

static
double mxDateTime_GetCurrentTime(void)
{
# if defined(HAVE_CLOCK_GETTIME)
    
    /* Use clock_gettime(), which has ns resolution */
    struct timespec ts;

    if (!clock_gettime(CLOCK_REALTIME, &ts))
	return ((double)ts.tv_sec + (double)ts.tv_nsec * 1e-9);
    else
	return -1.0;

# elif defined(HAVE_GETTIMEOFDAY)

    /* Use gettimeofday(), which has us resolution */
    struct timeval tv;

#  ifdef GETTIMEOFDAY_NO_TZ
    if (!gettimeofday(&tv))
#  else
    if (!gettimeofday(&tv, 0))
#  endif
	return ((double)tv.tv_sec + (double)tv.tv_usec * 1e-6);
    else
	return -1.0;

# elif defined(HAVE_FTIME)

    /* Use ftime(), which provides ms resolution */
    struct timeb tb;

    ftime(&tb);
    return ((double)tb.time + (double)tb.millitm * 1e-3);

# else

    /* Use time(), which usually only has seconds resolution */
    time_t ticks;

    time(&ticks);
    return (double) ticks;

# endif
}

/* Try to determine the clock resolution. 

*/

static
double mxDateTime_GetClockResolution(void)
{
# if defined(HAVE_CLOCK_GETTIME)
#  if defined(HAVE_CLOCK_GETRES)

    /* clock_gettime() is supposed to have ns resolution, but apparently
       this is not true on all systems. */
    struct timespec ts;

    if (!clock_getres(CLOCK_REALTIME, &ts))
	return ((double)ts.tv_sec + (double)ts.tv_nsec * 1e-9);
    else
	return -1.0;
#  else
    /* We'll have to believe the man-page */
    return 1e-9;
#  endif

# elif defined(HAVE_GETTIMEOFDAY)

    /* gettimeofday() has us resolution according to the man-page */
    return 1e-6;

# elif defined(HAVE_FTIME)

    /* ftime() provides ms resolution according to the man-page*/
    return 1e-3;

# else

    /* time() usually only has seconds resolution */
    return 1.0;

# endif
}

#else

/* Get the current time in Unix ticks. 

   This function reuses the time.time() of the Python time module,
   which is more portable but is slower than the above implementation.

*/

static
double mxDateTime_GetCurrentTime(void)
{
    double fticks;
    PyObject *v;

    /* Call the mxDateTime_nowapi function and use its return
       value as basis for the DateTime instance's value. */
    if (mxDateTime_nowapi == NULL)
	Py_Error(mxDateTime_Error,
		 "no current time API set");
    v = PyEval_CallObject(mxDateTime_nowapi, NULL);
    if (!v)
	goto onError;
    fticks = PyFloat_AsDouble(v);
    Py_DECREF(v);
    if (fticks == -1.0 && PyErr_Occurred())
	goto onError;

#if 0
    /* Round to the nearest micro-second: we use the same approach
       taken in the builtin round(). */
    fticks *= 1e6;
    if (DOUBLE_IS_NEGATIVE(fticks))
	fticks = ceil(fticks - 0.5);
    else
	fticks = floor(fticks + 0.5);
    fticks /= 1e6;
#endif    

    return fticks;

 onError:
    return -1.0;
}

#endif

/* Fix a second value for display as string.

   Seconds are rounded to the nearest microsecond in order to avoid
   cases where e.g. 3.42 gets displayed as 03.41 or 3.425 is diplayed
   as 03.42.

   Special care is taken for second values which would cause rounding
   to 60.00 -- these values are truncated to 59.99 to avoid the value
   of 60.00 due to rounding to show up even when the indictated time
   does not point to a leap second. The same is applied for rounding
   towards 61.00 (leap seconds).

   The second value returned by this function should be formatted
   using '%05.2f' (which rounds to 2 decimal places).

*/

static
double mxDateTime_FixSecondDisplay(register double second)
{
    /* Special case for rounding towards 60. */
    if (second >= 59.995 && second < 60.0)
	return 59.99;

    /* Special case for rounding towards 61. */
    if (second >= 60.995 && second < 61.0)
	return 60.99;

    /* Round to the nearest microsecond */
    second = (second * 1e6 + 0.5) / 1e6;

    return second;
}

/* This function checks whether the system uses the POSIX time_t rules
   (which do not support leap seconds) or a time package with leap
   second support enabled. Return 1 if it uses POSIX time_t values, 0
   otherwise.

   POSIX: 1986-12-31 23:59:59 UTC == 536457599

   With leap seconds:		  == 536457612

   (since there were 13 leapseconds in the years 1972-1985 according
   to the tz package available from ftp://elsie.nci.nih.gov/pub/)

*/

static
int mxDateTime_POSIX(void)
{
    time_t ticks = 536457599;
    struct tm *tm;

    memset(&tm,0,sizeof(tm));
    tm = gmtime(&ticks);
    if (tm == NULL)
	return 0;
    if (tm->tm_hour == 23 &&
	tm->tm_min == 59 &&
	tm->tm_sec == 59 &&
	tm->tm_mday == 31 &&
	tm->tm_mon == 11 &&
	tm->tm_year == 86)
	return 1;
    else
	return 0;
}

static
int mxDateTime_CheckDoubleStackProblem(double value)
{
    if (value == SECONDS_PER_DAY)
	return 1;
    else
	return 0;
}

#ifndef HAVE_TIMEGM
/* Calculates the conversion of the datetime instance to Unix ticks.

   For instances pointing to localtime, localticks will hold the
   corresponding Unix ticks value. In case the instance points to GMT
   time, gmticks will hold the correct ticks value.

   In both cases, gmtoffset will hold the GMT offset (local-GMT).

   Returns -1 (and sets an exception) to indicate errors; 0
   otherwise. 

   Note:

   There's some integer rounding error in the mktime() function that
   triggers near MAXINT on Solaris. The error was reported by Joe Van
   Andel <vanandel@ucar.edu> among others:

    Ooops: 2038-01-18 22:52:31.00 t = 2147467951 diff = -4294857600.0

   On 64-bit Alphas running DEC OSF, Tony Ibbs <tony@lsl.co.uk>
   reports:

    Ooops: 1901-12-13 21:57:57.00 t = 2147487973 diff = -4294967296.0
    ...(the diffs stay the same)...
    Ooops: 1969-12-31 10:10:54.00 t = 4294917550 diff = -4294967296.0

   Note the years ! Some rollover is happening near 2^31-1 even
   though Alphas happen to use 64-bits. This could be a bug in this
   function or in DEC's mktime() implementation.

*/

static
int mxDateTime_CalcTicks(mxDateTimeObject *datetime,
			 double *localticks,
			 double *gmticks,
			 double *gmtoffset)
{
    struct tm tm;
    struct tm *gmt;
    time_t ticks;
    double offset;

    Py_Assert(datetime->calendar == MXDATETIME_GREGORIAN_CALENDAR,
	      mxDateTime_Error,
	      "can only convert the Gregorian calendar to ticks");
    Py_Assert((long)((int)datetime->year) == datetime->year,
	      mxDateTime_RangeError,
	      "year out of range for ticks conversion");
    
    /* Calculate floor()ed ticks value  */
    memset(&tm,0,sizeof(tm));
    tm.tm_hour = (int)datetime->hour;
    tm.tm_min = (int)datetime->minute;
    tm.tm_sec = (int)datetime->second;
    tm.tm_mday = (int)datetime->day;
    tm.tm_mon = (int)datetime->month - 1;
    tm.tm_year = (int)datetime->year - 1900;
    tm.tm_wday = -1;
    tm.tm_yday = (int)datetime->day_of_year - 1;
    tm.tm_isdst = -1; /* unknown */
    ticks = mktime(&tm);
    if (ticks == (time_t)-1 && tm.tm_wday == -1) {
	/* XXX Hack to allow conversion during DST switching. */
        tm.tm_hour = 0;
	tm.tm_min = 0;
	tm.tm_sec = 0;
	ticks = mktime(&tm);
	if (ticks == (time_t)-1 && tm.tm_wday == -1)
	    Py_Error(mxDateTime_Error,
		     "cannot convert value to a Unix ticks value");
	ticks += ((int)datetime->hour * 3600
		  + (int)datetime->minute * 60
		  + (int)datetime->second);
    }

    /* Add fraction for localticks */
    *localticks = ((double)ticks
		   + (datetime->abstime - floor(datetime->abstime)));
    
    /* Now compare local time and GMT time */
    gmt = gmtime(&ticks);
    if (gmt == NULL)
	Py_Error(mxDateTime_Error,
		 "cannot convert value to a Unix ticks value");

    /* Check whether we have the same day and prepare offset */
    if (gmt->tm_mday != tm.tm_mday) {
	double localdate = (tm.tm_year * 10000 + 
			    tm.tm_mon *  100 +
			    tm.tm_mday);
	double gmdate = (gmt->tm_year * 10000 +
			 gmt->tm_mon * 100 + 
			 gmt->tm_mday);
	if (localdate < gmdate)
	    offset = -SECONDS_PER_DAY;
	else
	    offset = SECONDS_PER_DAY;
    }
    else
	offset = 0.0;

    /* Calculate difference in seconds */
    offset += ((datetime->hour - gmt->tm_hour) * 3600.0
	       + (datetime->minute - gmt->tm_min) * 60.0
	       + (floor(datetime->second) - (double)gmt->tm_sec));
    *gmtoffset = offset;
    *gmticks = *localticks + offset;
    return 0;
    
 onError:
    return -1;
}
#endif

/* These functions work for positive *and* negative years for
   compilers which round towards zero and ones that always round down
   to the nearest integer. */

/* Return 1/0 iff year points to a leap year in calendar. */

static
int mxDateTime_Leapyear(register long year,
			int calendar)
{
    if (calendar == MXDATETIME_GREGORIAN_CALENDAR)
	return (year % 4 == 0) && ((year % 100 != 0) || (year % 400 == 0));
    else
	return (year % 4 == 0);
}

/* Return the day of the week for the given absolute date. */

static
int mxDateTime_DayOfWeek(register long absdate)
{
    int day_of_week;

    if (absdate >= 1)
	day_of_week = (absdate - 1) % 7;
    else
	day_of_week = 6 - ((-absdate) % 7);
    return day_of_week;
}

/* Return the year offset, that is the absolute date of the day
   31.12.(year-1) in the given calendar.

   For the Julian calendar we shift the absdate (which is measured
   using the Gregorian Epoch) value by two days because the Epoch
   (0001-01-01) in the Julian calendar lies 2 days before the Epoch in
   the Gregorian calendar.

   Years around the Epoch (mathematical approach, not necessarily
   historically correct):

   Year  0005 (5 AD) - yearoffset: 1461,not a leap year
   Year  0004 (4 AD) - yearoffset: 1095,leap year
   Year  0003 (3 AD) - yearoffset: 730, not a leap year
   Year  0002 (2 AD) - yearoffset: 365, not a leap year
   Year  0001 (1 AD) - yearoffset: 0, not a leap year
   Year  0000 (1 BC) - yearoffset: -366, leap year
   Year -0001 (2 BC) - yearoffset: -731, not a leap year
   Year -0002 (3 BC) - yearoffset: -1096, not a leap year
   Year -0003 (4 BC) - yearoffset: -1461, not a leap year
   Year -0004 (5 BC) - yearoffset: -1827, leap year
   Year -0005 (6 BC) - yearoffset: -2192, not a leap year

*/

static 
long mxDateTime_YearOffset(register long year,
			   int calendar)
{
    if (year >= 1) {
	/* For years >= 1, we can simply count the number of days
	   between the Epoch and the given year */
	year--;
	if (calendar == MXDATETIME_GREGORIAN_CALENDAR)
	    return year*365 + year/4 - year/100 + year/400;

	else if (calendar == MXDATETIME_JULIAN_CALENDAR)
	    return year*365 + year/4 - 2;

	else {
	    Py_Error(mxDateTime_Error,
		     "unknown calendar");
	}
    } else {
	/* For years <= 0, we need to reverse the sign of the year (to
	   avoid integer rounding issues with negative numbers on some
	   platforms) and compensate for the year 0 being a leap
	   year */
	year = -year;
	if (calendar == MXDATETIME_GREGORIAN_CALENDAR)
	    return -(year*365 + year/4 - year/100 + year/400) - 366;

	else if (calendar == MXDATETIME_JULIAN_CALENDAR)
	    return -(year*365 + year/4) - 366 - 2;

	else {
	    Py_Error(mxDateTime_Error,
		     "unknown calendar");
	}
    }

 onError:
    return -1;
}


/* Normalize the data and calculate the absolute date, year offset and
   whether the year is a leap year or not.

   Returns -1 in case of an error, 0 otherwise.

*/

static
int mxDateTime_NormalizedDate(long year,
			      int month,
			      int day,
			      int calendar,
			      long *absdate_output,
			      long *yearoffset_output,
			      int *leap_output,
			      long *normalized_year,
			      int *normalized_month,
			      int *normalized_day)
{
    int leap;
    long yearoffset, absdate;

    /* Range check */
    Py_AssertWithArg(year >= MIN_YEAR_VALUE && year <= MAX_YEAR_VALUE,
		     mxDateTime_RangeError,
		     "year out of range: %ld",
		     year);

    /* Is it a leap year ? */
    leap = mxDateTime_Leapyear(year, calendar);

    /* Negative month values indicate months relative to the years end */
    if (month < 0)
	month += 13;
    Py_AssertWithArg(month >= 1 && month <= 12,
		     mxDateTime_RangeError,
		     "month out of range (1-12): %i",
		     month);

    /* Negative values indicate days relative to the months end */
    if (day < 0)
	day += days_in_month[leap][month - 1] + 1;
    Py_AssertWithArg(day >= 1 && day <= days_in_month[leap][month - 1],
		     mxDateTime_RangeError,
		     "day out of range: %i",
		     day);

    yearoffset = mxDateTime_YearOffset(year, calendar);
    if (yearoffset == -1 && PyErr_Occurred())
	goto onError;

    absdate = day + month_offset[leap][month - 1] + yearoffset;
    Py_AssertWith2Args(absdate >= MIN_ABSDATE_VALUE &&
		       absdate <= MAX_ABSDATE_VALUE,
		       mxDateTime_RangeError,
		       "year out of range: %ld (absdate %ld)",
		       year, absdate);

    DPRINTF("mxDateTime_NormalizedDate: "
	    "year=%ld month=%i day=%i yearoffset=%ld leap=%i absdate=%ld\n",
	    year, month, day, yearoffset, leap, absdate);

    if (absdate_output)
	*absdate_output = absdate;
    if (yearoffset_output)
	*yearoffset_output = yearoffset;
    if (leap_output)
	*leap_output = leap;
    if (normalized_year)
	*normalized_year = year;
    if (normalized_month)
	*normalized_month = month;
    if (normalized_day)
	*normalized_day = day;
    return 0;

 onError:
    return -1;
}


#ifdef HAVE_PYDATETIME

/* Note: This API is currently only used to support datetime module
   interaction */

/* Return the absolute date of the given date in absdate.

   Returns -1 in case of an error, 0 otherwise.

*/

static 
int mxDateTime_AbsDate(register long year,
		       register int month,
		       register int day,
		       int calendar,
		       long *absdate)
{
    return mxDateTime_NormalizedDate(year, month, day,
				     calendar, absdate,
				     NULL, NULL,
				     NULL, NULL, NULL);
}

#endif

/* Sets the date part of the DateTime object using the indicated
   calendar. 

   XXX This could also be done using some integer arithmetics rather
       than with this iterative approach...

*/

static
int mxDateTime_SetFromAbsDate(register mxDateTimeObject *datetime,
			      long absdate,
			      int calendar)
{
    register long year;
    long yearoffset;
    int leap, dayoffset;
    int *monthoffset;

    DPRINTF("mxDateTime_SetFromAbsDate(datetime=%x,absdate=%ld,calendar=%i)\n",
	    datetime,absdate,calendar);

    /* Range check */
    Py_AssertWithArg(absdate >= MIN_ABSDATE_VALUE &&
		     absdate <= MAX_ABSDATE_VALUE,
		     mxDateTime_RangeError,
		     "absdate out of range: %ld",
		     absdate);

    /* Approximate year */
    if (calendar == MXDATETIME_GREGORIAN_CALENDAR)
	year = (long)(((double)absdate) / 365.2425);
    else if (calendar == MXDATETIME_JULIAN_CALENDAR)
	year = (long)(((double)absdate) / 365.25);
    else
	Py_Error(mxDateTime_Error,
		 "unknown calendar");
    if (absdate > 0)
	year++;

    /* Apply corrections to reach the correct year */
    while (1) {
	/* Calculate the year offset */
	yearoffset = mxDateTime_YearOffset(year, calendar);
	if (yearoffset == -1 && PyErr_Occurred())
	    goto onError;
	DPRINTF(" trying year = %ld yearoffset = %ld\n",
		year, yearoffset);

	/* Backward correction: absdate must be greater than the
	   yearoffset */
	if (yearoffset >= absdate) {
	    year--;
	    DPRINTF(" backward correction\n");
	    continue;
	}

	dayoffset = absdate - yearoffset;
	leap = mxDateTime_Leapyear(year, calendar);

	/* Forward correction: years only have 365/366 days */
	if (dayoffset > 365) {
	    if (leap && dayoffset > 366) {
		year++;
		DPRINTF(" forward correction (leap year)\n");
		continue;
	    }
	    else if (!leap) {
		year++;
		DPRINTF(" forward correction (non-leap year)\n");
		continue;
	    }
	}

	/* Done */
	DPRINTF(" using year = %ld leap = %i dayoffset = %i\n",
		year,leap,dayoffset);

	break;
    }

    datetime->year = year;
    datetime->calendar = calendar;

    /* Now iterate to find the month */
    monthoffset = month_offset[leap];
    {
	register int month;
	
	for (month = 1; month < 13; month++)
	    if (monthoffset[month] >= dayoffset)
		break;
	datetime->month = month;
	datetime->day = dayoffset - month_offset[leap][month-1];
    }
    
    datetime->day_of_week = mxDateTime_DayOfWeek(absdate);
    datetime->day_of_year = dayoffset;
    
    return 0;

 onError:
    return -1;
}

/* Sets the time part of the DateTime object. */

static
int mxDateTime_SetFromAbsTime(mxDateTimeObject *datetime,
			      double abstime)
{
    int inttime;
    int hour,minute;
    double second;

    DPRINTF("mxDateTime_SetFromAbsTime(datetime=%x,abstime=%.20f)\n",
	    (long)datetime,abstime);

    /* Range check */
    Py_AssertWithArg(abstime >= MIN_ABSTIME_VALUE &&
		     abstime <= MAX_ABSTIME_VALUE,
		     mxDateTime_RangeError,
		     "abstime out of range: %i",
		     (int)abstime);
    
    /* Determine broken down values */
    inttime = (int)abstime;
    if (inttime == 86400) {
	/* Special case for leap seconds */
	hour = 23;
	minute = 59;
	second = 60.0 + abstime - (double)inttime;
    }
    else {
	hour = inttime / 3600;
	minute = (inttime % 3600) / 60;
	second = abstime - (double)(hour*3600 + minute*60);
    }

    datetime->hour = hour;
    datetime->minute = minute;
    datetime->second = second;

    return 0;

 onError:
    return -1;
}

/* Set the instance's value using the given date and time. calendar
   may be set to the flags: MXDATETIME_GREGORIAN_CALENDAR,
   MXDATETIME_JULIAN_CALENDAR to indicate the calendar to be used. */

static
int mxDateTime_SetFromDateAndTime(mxDateTimeObject *datetime,
				  long year,
				  int month,
				  int day,
				  int hour,
				  int minute,
				  double second,
				  int calendar)
{
    double comdate;
    
    if (datetime == NULL) {
	PyErr_BadInternalCall();
	goto onError;
    }

    DPRINTF("mxDateTime_SetFromDateAndTime("
	    "datetime=%x year=%ld month=%i day=%i "
	    "hour=%i minute=%i second=%f calendar=%i)\n",
	    datetime,year,month,day,hour,minute,second,calendar);

    /* Calculate the absolute date */
    {
	long yearoffset,absdate;

	if (mxDateTime_NormalizedDate(year, month, day, 
				      calendar,
				      &absdate, &yearoffset, NULL,
				      &year, &month, &day))
	    goto onError;
	DPRINTF("mxDateTime_SetFromDateAndTime: "
		"yearoffset=%ld absdate=%ld "
		"year=%ld month=%i day=%i (normalized)\n",
		yearoffset,absdate,
		year,month,day);

	datetime->absdate = absdate;
	
	datetime->year = year;
	datetime->month = month;
	datetime->day = day;

	datetime->day_of_week = mxDateTime_DayOfWeek(absdate);
	datetime->day_of_year = (short)(absdate - yearoffset);

	datetime->calendar = calendar;

	comdate = (double)absdate - 693594.0;
    }

    /* Calculate the absolute time */
    {
	Py_AssertWithArg(hour >= 0 && hour <= 23,
			 mxDateTime_RangeError,
			 "hour out of range (0-23): %i",
			 hour);
	Py_AssertWithArg(minute >= 0 && minute <= 59,
			 mxDateTime_RangeError,
			 "minute out of range (0-59): %i",
			 minute);
	Py_AssertWithArg(second >= (double)0.0 && 
			 (second < (double)60.0 || 
			  (hour == 23 && minute == 59 && 
			   second < (double)61.0)),
			 mxDateTime_RangeError,
			 "second out of range (0.0 - <60.0; <61.0 for 23:59): %i",
			 (int)second);

	datetime->abstime = (double)(hour*3600 + minute*60) + second;

	datetime->hour = hour;
	datetime->minute = minute;
	datetime->second = second;

	if (DOUBLE_IS_NEGATIVE(comdate))
	    comdate -= datetime->abstime / SECONDS_PER_DAY;
	else
	    comdate += datetime->abstime / SECONDS_PER_DAY;
	datetime->comdate = comdate;
    }
    return 0;
 onError:
    return -1;
}

/* Set the instance's value using the given absolute date and
   time. The calendar used is the Gregorian. */

static
int mxDateTime_SetFromAbsDateTime(mxDateTimeObject *datetime,
				  long absdate,
				  double abstime,
				  int calendar)
{
    if (datetime == NULL) {
	PyErr_BadInternalCall();
	goto onError;
    }
    
    DPRINTF("mxDateTime_SetFromAbsDateTime(datetime=%x,"
	    "absdate=%ld,abstime=%.20f,calendar=%i)\n",
	    datetime,absdate,abstime,calendar);
    DPRINTF("mxDateTime_SetFromAbsDateTime: "
	    "abstime is %.20f, diff %.20f, as int %i\n", 
	   abstime,
	   abstime - SECONDS_PER_DAY,
	   (int)abstime);

    /* Store given values; bounds checks will be done below */
    datetime->absdate = absdate;
    datetime->abstime = abstime;
    
    /* Calculate COM date */
    {
	register double comdate;
	
	comdate = (double)(datetime->absdate - 693594);
	if (DOUBLE_IS_NEGATIVE(comdate))
	    comdate -= datetime->abstime / SECONDS_PER_DAY;
	else
	    comdate += datetime->abstime / SECONDS_PER_DAY;
	datetime->comdate = comdate;
    }

    /* Calculate the date */
    if (mxDateTime_SetFromAbsDate(datetime,
				  datetime->absdate,
				  calendar))
	goto onError;
    
    /* Calculate the time */
    if (mxDateTime_SetFromAbsTime(datetime,
				  datetime->abstime))
	goto onError;

    return 0;
 onError:
    return -1;
}

/* Set the instance's value using the given Windows COM date.  The
   calendar used is the Gregorian. */

static
int mxDateTime_SetFromCOMDate(mxDateTimeObject *datetime,
			      double comdate)
{
    long absdate;
    double abstime;

    if (datetime == NULL) {
	PyErr_BadInternalCall();
	goto onError;
    }
    
    /* Store given values; bounds checks will be done below */
    datetime->comdate = comdate;

    /* XXX should provide other means to calculate the broken down
       values for these huge values. */
    Py_AssertWithArg((double)MIN_YEAR_VALUE <= comdate &&
		     comdate <= (double)MAX_YEAR_VALUE,
		     mxDateTime_RangeError,
		     "DateTime COM date out of range: %i",
		     (int)comdate);

    absdate = (long)comdate;
    abstime = (comdate - (double)absdate) * SECONDS_PER_DAY;
    if (DOUBLE_IS_NEGATIVE(abstime))
	abstime = -abstime;
    absdate += 693594;
    DPRINTF("mxDateTime_SetFromCOMDate: absdate=%ld abstime=%f\n",
	    absdate,abstime);
    datetime->absdate = absdate;
    datetime->abstime = abstime;
    
    /* Calculate the date */
    if (mxDateTime_SetFromAbsDate(datetime,
				  absdate,
				  MXDATETIME_GREGORIAN_CALENDAR))
	goto onError;
    
    /* Calculate the time */
    if (mxDateTime_SetFromAbsTime(datetime,
				  abstime))
	goto onError;

    return 0;
 onError:
    return -1;
}

/* --- API functions --- */

static
PyObject *mxDateTime_FromDateAndTime(long year,
				     int month,
				     int day,
				     int hour,
				     int minute,
				     double second)
{
    mxDateTimeObject *datetime;
    
    datetime = mxDateTime_New();
    if (datetime == NULL)
	return NULL;
    if (mxDateTime_SetFromDateAndTime(datetime,
				      year,month,day,
				      hour,minute,second,
				      MXDATETIME_GREGORIAN_CALENDAR))
	goto onError;

    return (PyObject *)datetime;
 onError:
    Py_DECREF(datetime);
    return NULL;
}

/* Alias */
#define mxDateTime_FromGregorianDateAndTime mxDateTime_FromDateAndTime

static
PyObject *mxDateTime_FromJulianDateAndTime(long year,
					   int month,
					   int day,
					   int hour,
					   int minute,
					   double second)
{
    mxDateTimeObject *datetime;
    
    datetime = mxDateTime_New();
    if (datetime == NULL)
	return NULL;
    if (mxDateTime_SetFromDateAndTime(datetime,
				      year,month,day,
				      hour,minute,second,
				      MXDATETIME_JULIAN_CALENDAR))
	goto onError;

    return (PyObject *)datetime;
 onError:
    Py_DECREF(datetime);
    return NULL;
}

static
PyObject *mxDateTime_FromAbsDateAndTime(long absdate,
					double abstime)
{
    mxDateTimeObject *datetime;
    
    datetime = mxDateTime_New();
    if (datetime == NULL)
	return NULL;
    if (mxDateTime_SetFromAbsDateTime(datetime,
				      absdate,
				      abstime,
				      MXDATETIME_GREGORIAN_CALENDAR))
	goto onError;

    return (PyObject *)datetime;
 onError:
    Py_DECREF(datetime);
    return NULL;
}

static
PyObject *mxDateTime_FromAbsDateTime(long absdate,
				     double abstime,
				     int calendar)
{
    mxDateTimeObject *datetime;
    
    datetime = mxDateTime_New();
    if (datetime == NULL)
	return NULL;
    if (mxDateTime_SetFromAbsDateTime(datetime,
				      absdate,
				      abstime,
				      calendar))
	goto onError;

    return (PyObject *)datetime;

 onError:
    Py_DECREF(datetime);
    return NULL;
}

/* Creates a new DateTime instance using datetime as basis by adding
   the given offsets to the value of datetime and then re-normalizing
   them.

   The resulting DateTime instance will use the same calendar as
   datetime.

*/

static
PyObject *mxDateTime_FromDateTimeAndOffset(mxDateTimeObject *datetime,
					   long absdate_offset,
					   double abstime_offset)
{
    mxDateTimeObject *dt;
    long days;
    long absdate = datetime->absdate;
    double abstime = datetime->abstime;

    absdate += absdate_offset;
    abstime += abstime_offset;

    /* Normalize */
    if (abstime < 0 && abstime >= -SECONDS_PER_DAY) {
	abstime += SECONDS_PER_DAY;
	absdate -= 1;
    }
    if (abstime >= SECONDS_PER_DAY && abstime < 2*SECONDS_PER_DAY) {
	abstime -= SECONDS_PER_DAY;
	absdate += 1;
    }
    /* Some compilers and/or processors (e.g. gcc 2.95.3 on Mandrake)
       have troubles with getting rounding right even though 86400.0
       IS exactly representable using IEEE floats... that's why we are
       extra careful here. */
    while (DOUBLE_IS_NEGATIVE(abstime)) {
	days = (long)(-abstime / SECONDS_PER_DAY);
	if (days == 0)
	    days = 1;
	days++;
	abstime += days * SECONDS_PER_DAY;
	absdate -= days;
    }
    while (abstime >= SECONDS_PER_DAY) {
	days = (long)(abstime / SECONDS_PER_DAY);
	if (days == 0)
	    days = 1;
	abstime -= days * SECONDS_PER_DAY;
	absdate += days;
    }
    if (mxDateTime_DoubleStackProblem &&
	abstime >= (double)8.63999999999999854481e+04) {
	DPRINTF("mxDateTime_FromDateTimeAndOffset: "
		"triggered double work-around: "
		"abstime is %.20f, diff %.20e, as int %i\n", 
		abstime,
		abstime - SECONDS_PER_DAY,
		(int)abstime);
	absdate += 1;
	abstime = 0.0;
    }
    
    dt = mxDateTime_New();
    if (dt == NULL)
	return NULL;
    if (mxDateTime_SetFromAbsDateTime(dt,
				      absdate,
				      abstime,
				      datetime->calendar))
	goto onError;

    return (PyObject *)dt;

 onError:
    Py_DECREF(dt);
    return NULL;
}

static
PyObject *mxDateTime_FromAbsDays(double absdays)
{
    mxDateTimeObject *datetime;
    long absdate;
    double abstime,fabsdays;
    
    datetime = mxDateTime_New();
    if (datetime == NULL)
	return NULL;
    fabsdays = floor(absdays);
    Py_AssertWithArg(fabsdays > -LONG_MAX && fabsdays < LONG_MAX,
		     mxDateTime_RangeError,
		     "absdays out of range: %i",
		     (int)absdays);
    absdate = (long)fabsdays + 1;
    abstime = (absdays - fabsdays) * SECONDS_PER_DAY;
    if (mxDateTime_SetFromAbsDateTime(datetime,
				      absdate,
				      abstime,
				      MXDATETIME_GREGORIAN_CALENDAR))
	goto onError;

    return (PyObject *)datetime;

 onError:
    Py_DECREF(datetime);
    return NULL;
}

static
PyObject *mxDateTime_FromTuple(PyObject *v)
{
    mxDateTimeObject *datetime = 0;
    long year;
    int month,day,hour,minute;
    double second;

    if (!PyTuple_Check(v)) {
	PyErr_BadInternalCall();
	return NULL;
    }
    if (!PyArg_ParseTuple(v,
	"liiiid;need a date/time 6-tuple (year,month,day,hour,minute,second)",
			  &year,&month,&day,&hour,&minute,&second))
	return NULL;
    
    datetime = mxDateTime_New();
    if (datetime == NULL)
	return NULL;
    if (mxDateTime_SetFromDateAndTime(datetime,
				      year,month,day,
				      hour,minute,second,
				      MXDATETIME_GREGORIAN_CALENDAR))
	goto onError;

    return (PyObject *)datetime;

 onError:
    Py_DECREF(datetime);
    return NULL;
}

static
PyObject *mxDateTime_FromTmStruct(struct tm *tm)
{
    mxDateTimeObject *datetime = 0;
    
    datetime = mxDateTime_New();
    if (datetime == NULL)
	return NULL;
    
    if (mxDateTime_SetFromDateAndTime(datetime,
				      tm->tm_year + 1900,
				      tm->tm_mon + 1,
				      tm->tm_mday,
				      tm->tm_hour,
				      tm->tm_min,
				      (double)tm->tm_sec,
				      MXDATETIME_GREGORIAN_CALENDAR))
	goto onError;

    return (PyObject *)datetime;

 onError:
    Py_DECREF(datetime);
    return NULL;
}

static
PyObject *mxDateTime_FromTicks(double ticks)
{
    mxDateTimeObject *datetime = 0;
    struct tm *tm;
    double seconds;
    time_t tticks = (time_t)ticks;
    
    datetime = mxDateTime_New();
    if (datetime == NULL)
	return NULL;

    /* Conversion is done to local time */
    tm = localtime(&tticks);
    if (tm == NULL)
	Py_Error(mxDateTime_Error,
		 "could not convert ticks value to local time");
    /* Add fraction */
    seconds = floor((double)tm->tm_sec) + (ticks - floor(ticks));

    if (mxDateTime_SetFromDateAndTime(datetime,
				      tm->tm_year + 1900,
				      tm->tm_mon + 1,
				      tm->tm_mday,
				      tm->tm_hour,
				      tm->tm_min,
				      seconds,
				      MXDATETIME_GREGORIAN_CALENDAR))
	goto onError;

    return (PyObject *)datetime;

 onError:
    Py_DECREF(datetime);
    return NULL;
}

static
PyObject *mxDateTime_FromGMTicks(double ticks)
{
    mxDateTimeObject *datetime = 0;
    struct tm *tm;
    double seconds;
    time_t tticks = (time_t)ticks;
    
    datetime = mxDateTime_New();
    if (datetime == NULL)
	return NULL;
    /* Conversion is done to GMT time */
    tm = gmtime(&tticks);
    /* Add fraction */
    seconds = floor((double)tm->tm_sec) + (ticks - floor(ticks));
    if (mxDateTime_SetFromDateAndTime(datetime,
				      tm->tm_year + 1900,
				      tm->tm_mon + 1,
				      tm->tm_mday,
				      tm->tm_hour,
				      tm->tm_min,
				      seconds,
				      MXDATETIME_GREGORIAN_CALENDAR))
	goto onError;

    return (PyObject *)datetime;

 onError:
    Py_DECREF(datetime);
    return NULL;
}

static
PyObject *mxDateTime_FromCOMDate(double comdate)
{
    mxDateTimeObject *datetime = 0;

    datetime = mxDateTime_New();
    if (datetime == NULL)
	return NULL;
    if (mxDateTime_SetFromCOMDate(datetime,comdate))
	goto onError;

    return (PyObject *)datetime;

 onError:
    Py_DECREF(datetime);
    return NULL;
}

static
struct tm *mxDateTime_AsTmStruct(mxDateTimeObject *datetime,
				 struct tm *tm)
{
    Py_Assert((long)((int)datetime->year) == datetime->year,
	      mxDateTime_RangeError,
	      "year out of range for tm struct conversion");

    memset(tm,0,sizeof(tm));
    tm->tm_hour = (int)datetime->hour;
    tm->tm_min = (int)datetime->minute;
#if ROUND_SECONDS_IN_TM_STRUCT
    tm->tm_sec = (int)(datetime->second + 0.5); /* Round the value */
#else
    tm->tm_sec = (int)datetime->second;
#endif
    tm->tm_mday = (int)datetime->day;
    tm->tm_mon = (int)datetime->month - 1;
    tm->tm_year = (int)datetime->year - 1900;
    tm->tm_wday = ((int)datetime->day_of_week + 1) % 7;
    tm->tm_yday = (int)datetime->day_of_year - 1;
    tm->tm_isdst = -1; /* unknown */
    return tm;

 onError:
    return NULL;
}

static
double mxDateTime_AsCOMDate(mxDateTimeObject *datetime)
{
    return datetime->comdate;
}

/* This global is set to
   -1 if mktime() auto-corrects the value of the DST flag to whatever the
      value should be for the given point in time (which is bad)
    0 if the global has not yet been initialized
    1 if mktime() does not correct the value and returns proper values
 */

static int mktime_works = 0;

static
int init_mktime_works(void)
{
    struct tm tm;
    time_t a,b;
    
    /* Does mktime() in general and specifically DST = -1 work ? */
    memset(&tm,0,sizeof(tm));
    tm.tm_mday = 1;
    tm.tm_mon = 5;
    tm.tm_year = 98;
    tm.tm_isdst = -1;
    a = mktime(&tm);
    Py_Assert(a != (time_t)-1,
	      PyExc_SystemError,
	      "mktime() returned an error (June)");
    memset(&tm,0,sizeof(tm));
    tm.tm_mday = 1;
    tm.tm_mon = 0;
    tm.tm_year = 98;
    tm.tm_isdst = -1;
    a = mktime(&tm);
    Py_Assert(a != (time_t)-1,
	      PyExc_SystemError,
	      "mktime() returned an error (January)");

    /* Some mktime() implementations return (time_t)-1 when setting
       DST to anything other than -1. Others adjust DST without
       looking at the given setting. */

    /* a = (Summer, DST = 0) */
    memset(&tm,0,sizeof(tm));
    tm.tm_mday = 1;
    tm.tm_mon = 5;
    tm.tm_year = 98;
    tm.tm_isdst = 0;
    a = mktime(&tm);
    if (a == (time_t)-1) {
	mktime_works = -1;
	return 0;
    }

    /* b = (Summer, DST = 1) */
    memset(&tm,0,sizeof(tm));
    tm.tm_mday = 1;
    tm.tm_mon = 5;
    tm.tm_year = 98;
    tm.tm_isdst = 1;
    b = mktime(&tm);
    if (a == (time_t)-1 || a == b) {
	mktime_works = -1;
	return 0;
    }
    
    /* a = (Winter, DST = 0) */
    memset(&tm,0,sizeof(tm));
    tm.tm_mday = 1;
    tm.tm_mon = 0;
    tm.tm_year = 98;
    tm.tm_isdst = 0;
    a = mktime(&tm);
    if (a == (time_t)-1) {
	mktime_works = -1;
	return 0;
    }

    /* b = (Winter, DST = 1) */
    memset(&tm,0,sizeof(tm));
    tm.tm_mday = 1;
    tm.tm_mon = 0;
    tm.tm_year = 98;
    tm.tm_isdst = 1;
    b = mktime(&tm);
    if (a == (time_t)-1 || a == b) {
	mktime_works = -1;
	return 0;
    }
    
    mktime_works = 1;
    return 0;
 onError:
    return -1;
}

/* Returns the ticks value for datetime assuming it stores a datetime
   value in local time. 
   
   offsets is subtracted from the resulting ticks value (this can be
   used to implement DST handling). 

   dst is passed to the used mktime() C lib API and can influence the
   calculation: dst == 1 means that the datetime value should be
   interpreted with DST on, dst == 0 with DST off. Note that this
   doesn't work on all platforms. dst == -1 means: use the DST value
   in affect at the given point in time.

*/

static
double mxDateTime_AsTicksWithOffset(mxDateTimeObject *datetime,
				    double offset,
				    int dst)
{
    struct tm tm;
    time_t tticks;
    double ticks;
    
    Py_Assert(datetime->calendar == MXDATETIME_GREGORIAN_CALENDAR,
	      mxDateTime_Error,
	      "can only convert the Gregorian calendar to ticks");
    Py_Assert((long)((int)datetime->year) == datetime->year,
	      mxDateTime_RangeError,
	      "year out of range for ticks conversion");
    
    memset(&tm,0,sizeof(tm));
    tm.tm_hour = (int)datetime->hour;
    tm.tm_min = (int)datetime->minute;
    tm.tm_sec = (int)datetime->second;
    tm.tm_mday = (int)datetime->day;
    tm.tm_mon = (int)datetime->month - 1;
    tm.tm_year = (int)datetime->year - 1900;
    tm.tm_wday = -1;
    tm.tm_yday = (int)datetime->day_of_year - 1;
    tm.tm_isdst = dst;
    /* mktime uses local time ! */
    tticks = mktime(&tm);
    if (tticks == (time_t)-1 && tm.tm_wday == -1)
	Py_Error(mxDateTime_Error,
		 "cannot convert value to a time value");
    /* Check if mktime messes up DST */
    if (dst >= 0 && mktime_works <= 0) {
	if (mktime_works == 0) {
	    if (init_mktime_works() < 0)
		goto onError;
	}
	if (mktime_works < 0)
	    Py_Error(PyExc_SystemError,
	       "mktime() doesn't support setting DST to anything but -1");
    }
    /* Add fraction and turn into a double and subtract offset */
    ticks = (double)tticks
	    + (datetime->abstime - floor(datetime->abstime))
	    - offset;
    return ticks;

 onError:
    return -1.0;
}

static
double mxDateTime_AsTicks(mxDateTimeObject *datetime)
{
    return mxDateTime_AsTicksWithOffset(datetime,0,-1);
}

/* Returns the ticks value for datetime assuming it stores a UTC
   datetime value. 

   offsets is subtracted from the resulting ticks value before
   returning it. This is useful to implement time zone handling.

*/

static
double mxDateTime_AsGMTicksWithOffset(mxDateTimeObject *datetime,
				      double offset)
{
    Py_Assert(datetime->calendar == MXDATETIME_GREGORIAN_CALENDAR,
	      mxDateTime_Error,
	      "can only convert the Gregorian calendar to ticks");

    /* For POSIX style calculations there's nothing much to do... */
    if (mxDateTime_POSIXConform) {
	return ((datetime->absdate - 719163) * SECONDS_PER_DAY 
		+ datetime->abstime
		- offset);
    }

#ifdef HAVE_TIMEGM
    {
	/* Use timegm() API */
	struct tm tm;
	time_t tticks;

	Py_Assert((long)((int)datetime->year) == datetime->year,
		  mxDateTime_RangeError,
		  "year out of range for ticks conversion");

	/* Use timegm() if not POSIX conform: the time package knows about
	   leap seconds so we use that information too. */
	memset(&tm,0,sizeof(tm));
	tm.tm_hour = (int)datetime->hour;
	tm.tm_min = (int)datetime->minute;
	tm.tm_sec = (int)datetime->second;
	tm.tm_mday = (int)datetime->day;
	tm.tm_mon = (int)datetime->month - 1;
	tm.tm_year = (int)datetime->year - 1900;
	tm.tm_wday = ((int)datetime->day_of_week + 1) % 7;
	tm.tm_yday = (int)datetime->day_of_year - 1;
	tm.tm_isdst = 0;
	/* timegm uses UTC ! */
	tticks = timegm(&tm);
	Py_Assert(tticks != (time_t)-1,
		  mxDateTime_Error,
		  "cannot convert value to a time value");
	/* Add fraction and turn into a double */
	return ((double)tticks
		+ (datetime->abstime - floor(datetime->abstime))
		- offset);
    }
#else
    {
	/* Work around with a trick... */
	double localticks,gmticks,gmtoffset;

	if (mxDateTime_CalcTicks(datetime,
				 &localticks,&gmticks,&gmtoffset))
	    goto onError;
	return gmticks - offset;
    }
#endif

 onError:
    return -1.0;
}

static
double mxDateTime_AsGMTicks(mxDateTimeObject *datetime)
{
    return mxDateTime_AsGMTicksWithOffset(datetime,0);
}

/* Returns the UTC offset at the given time; assumes local time is
   stored in the instance. */

static
double mxDateTime_GMTOffset(mxDateTimeObject *datetime)
{
    double gmticks,ticks;
    
    gmticks = mxDateTime_AsGMTicks(datetime);
    if (gmticks == -1.0 && PyErr_Occurred())
	goto onError;
    ticks = mxDateTime_AsTicksWithOffset(datetime,0,-1);
    if (ticks == -1.0 && PyErr_Occurred())
	goto onError;
    return gmticks - ticks;

 onError:
    return -1.0;
}

/* Return the instance's value in absolute days: days since 0001-01-01
   0:00:00 using fractions for parts of a day. */

static
double mxDateTime_AsAbsDays(mxDateTimeObject *datetime)
{
    return ((double)(datetime->absdate - 1) + 
	    datetime->abstime / SECONDS_PER_DAY);
}

/* Return broken down values of the instance. This call returns the
   values as stored in the instance regardeless of the used
   calendar. */

static
int mxDateTime_BrokenDown(mxDateTimeObject *datetime,
			  long *year,
			  int *month,
			  int *day,
			  int *hour,
			  int *minute,
			  double *second)
{
    if (year)
	*year = (long)datetime->year;
    if (month)
	*month = (int)datetime->month;
    if (day)
	*day = (int)datetime->day;
    if (hour)
	*hour = (int)datetime->hour;
    if (minute)
	*minute = (int)datetime->minute;
    if (second)
	*second = (double)datetime->second;
    return 0;
}

/* Return the instance's value as broken down values using the Julian
   calendar. */

static
int mxDateTime_AsJulianDate(mxDateTimeObject *datetime,
			    long *pyear,
			    int *pmonth,
			    int *pday,
			    int *phour,
			    int *pminute,
			    double *psecond,
			    int *pday_of_week,
			    int *pday_of_year)
{
    long absdate = datetime->absdate;
    long year;
    int month,day,dayoffset;

    /* Get the date in the Julian calendar */
    if (datetime->calendar != MXDATETIME_JULIAN_CALENDAR) {
	mxDateTimeObject temp;

	/* Recalculate the date from the absdate value */
	if (mxDateTime_SetFromAbsDate(&temp,
				      absdate,
				      MXDATETIME_JULIAN_CALENDAR))
	    goto onError;
	year = temp.year;
	month = temp.month;
	day = temp.day;
	dayoffset = temp.day_of_year;
    }
    else {
	year = datetime->year;
	month = datetime->month;
	day = datetime->day;
	dayoffset = datetime->day_of_year;
    }
    
    if (pyear)
	*pyear = (long)year;
    if (pmonth)
	*pmonth = (int)month;
    if (pday)
	*pday = (int)day;

    if (phour)
	*phour = (int)datetime->hour;
    if (pminute)
	*pminute = (int)datetime->minute;
    if (psecond)
	*psecond = (double)datetime->second;

    if (pday_of_week)
	*pday_of_week = mxDateTime_DayOfWeek(absdate);
    if (pday_of_year)
	*pday_of_year = (int)dayoffset;

    return 0;

 onError:
    return -1;
}

/* Return the instance's value as broken down values using the Gregorian
   calendar. */

static
int mxDateTime_AsGregorianDate(mxDateTimeObject *datetime,
			    long *pyear,
			    int *pmonth,
			    int *pday,
			    int *phour,
			    int *pminute,
			    double *psecond,
			    int *pday_of_week,
			    int *pday_of_year)
{
    long absdate = datetime->absdate;
    long year;
    int month,day,dayoffset;

    /* Recalculate the date in the Gregorian calendar */
    if (datetime->calendar != MXDATETIME_GREGORIAN_CALENDAR) {
	mxDateTimeObject temp;

	/* Recalculate the date  from the absdate value */
	if (mxDateTime_SetFromAbsDate(&temp,
				      absdate,
				      MXDATETIME_GREGORIAN_CALENDAR))
	    goto onError;
	year = temp.year;
	month = temp.month;
	day = temp.day;
	dayoffset = temp.day_of_year;
    }
    else {
	year = datetime->year;
	month = datetime->month;
	day = datetime->day;
	dayoffset = datetime->day_of_year;
    }
    
    if (pyear)
	*pyear = (long)year;
    if (pmonth)
	*pmonth = (int)month;
    if (pday)
	*pday = (int)day;

    if (phour)
	*phour = (int)datetime->hour;
    if (pminute)
	*pminute = (int)datetime->minute;
    if (psecond)
	*psecond = (double)datetime->second;

    if (pday_of_week)
	*pday_of_week = mxDateTime_DayOfWeek(absdate);
    if (pday_of_year)
	*pday_of_year = (int)dayoffset;

    return 0;

 onError:
    return -1;
}

/* Returns the DST setting for the given DateTime instance assuming it
   refers to local time. -1 is returned in case it cannot be
   determined, 0 if it is not active, 1 if it is. For calendars other
   than the Gregorian the function always returns -1. 

   XXX If mktime() returns -1 for isdst, try harder using the hack in
       timegm.py.

*/

static
int mxDateTime_DST(mxDateTimeObject *datetime)
{
    struct tm tm;
    time_t ticks;
    
    if (datetime->calendar != MXDATETIME_GREGORIAN_CALENDAR)
	return -1;
    if ((long)((int)datetime->year) != datetime->year)
	return -1;

    memset(&tm,0,sizeof(tm));
    tm.tm_hour = (int)datetime->hour;
    tm.tm_min = (int)datetime->minute;
    tm.tm_sec = (int)datetime->second;
    tm.tm_mday = (int)datetime->day;
    tm.tm_mon = (int)datetime->month - 1;
    tm.tm_year = (int)datetime->year - 1900;
    tm.tm_wday = -1;
    tm.tm_isdst = -1;
    ticks = mktime(&tm);
    if (ticks == (time_t)-1 && tm.tm_wday == -1)
	return -1;
    return tm.tm_isdst;
}

/* Returns a Python string containing the locale's timezone name for
   the given DateTime instance (assuming it refers to local time).
   "???"  is returned in case it cannot be determined.  */

static
PyObject *mxDateTime_TimezoneString(mxDateTimeObject *datetime)
{
    struct tm tm;
    time_t ticks;
    char tz[255];

    if (datetime->calendar != MXDATETIME_GREGORIAN_CALENDAR)
	return mxPyText_FromString("???");
    if ((long)((int)datetime->year) != datetime->year)
	return mxPyText_FromString("???");

#ifndef HAVE_STRFTIME
    return mxPyText_FromString("???");
#else
    memset(&tm,0,sizeof(tm));
    tm.tm_hour = (int)datetime->hour;
    tm.tm_min = (int)datetime->minute;
    tm.tm_sec = (int)datetime->second;
    tm.tm_mday = (int)datetime->day;
    tm.tm_mon = (int)datetime->month - 1;
    tm.tm_year = (int)datetime->year - 1900;
    tm.tm_wday = -1;
    tm.tm_isdst = mxDateTime_DST(datetime);
    ticks = mktime(&tm);
    if (ticks == (time_t)-1 && tm.tm_wday == -1)
	return mxPyText_FromString("???");
    strftime(tz,sizeof(tm),"%Z",&tm);
    return mxPyText_FromString(tz);
#endif
}

/* Returns the ISO week notation for the given DateTime instance as
   tuple (year,isoweek,isoday). The algorithm also Works for negative
   dates.

   XXX Check this algorithm for the Julian calendar.
   
*/

static
PyObject *mxDateTime_ISOWeekTuple(mxDateTimeObject *datetime)
{
    int week;
    long year = datetime->year;
    int day;

    /* Estimate */
    week = (datetime->day_of_year-1) - datetime->day_of_week + 3;
    if (week >= 0)
	week = week / 7 + 1;
    day = datetime->day_of_week + 1;
    DPRINTF("mxDateTime_ISOWeekTuple: estimated year, week, day = %ld, %i, %i\n",
	    year,week,day);

    /* Verify */
    if (week < 0) {
	/* The day lies in last week of the previous year */
	year--;
	if ((week > -2) || 
	    (week == -2 && mxDateTime_Leapyear(year,datetime->calendar)))
	    week = 53;
	else	    
	    week = 52;
    }
    else if (week == 53) {
	/* Check if the week belongs to year or year+1 */
	if (31-datetime->day + datetime->day_of_week < 3) {
	    week = 1;
	    year++;
	}
    }
    DPRINTF("mxDateTime_ISOWeekTuple: corrected year, week, day = %ld, %i, %i\n",
	    year,week,day);
    return Py_BuildValue("lii",year,week,day);
}

/* Return a string identifying the used calendar. */

static
PyObject *mxDateTime_CalendarString(mxDateTimeObject *datetime)
{
    PyObject *v = mxDateTime_GregorianCalendar;
    
    switch (datetime->calendar) {
    case MXDATETIME_GREGORIAN_CALENDAR: 
	break;
    case MXDATETIME_JULIAN_CALENDAR:
	v = mxDateTime_JulianCalendar;
	break;
    default:
	Py_Error(PyExc_SystemError,
		 "Internal error in mxDateTime: wrong calendar value");
    }
    
    Py_INCREF(v);
    return v;

 onError:
    return NULL;
}

/* Writes a string representation to buffer. If the string does not
   fit the buffer, nothing is written. */

static
void mxDateTime_AsString(mxDateTimeObject *self,
			 char *buffer,
			 int buffer_len)
{
    double second;

    if (!buffer || buffer_len < 50)
	return;
    second = mxDateTime_FixSecondDisplay(self->second);
    if (self->year >= 0)
	sprintf(buffer,"%04li-%02i-%02i %02i:%02i:%05.2f",
		(long)self->year,(int)self->month,(int)self->day,
		(int)self->hour,(int)self->minute,
		(float)second);
    else
	sprintf(buffer,"-%04li-%02i-%02i %02i:%02i:%05.2f",
		(long)-self->year,(int)self->month,(int)self->day,
		(int)self->hour,(int)self->minute,
		(float)second);
}

/* Returns a string indicating the date in ISO format. */

static
PyObject *mxDateTime_DateString(mxDateTimeObject *self)
{
    char buffer[50];

    if (self->year >= 0)
	sprintf(buffer,"%04li-%02i-%02i",
		(long)self->year,(int)self->month,(int)self->day);
    else
	sprintf(buffer,"-%04li-%02i-%02i",
		(long)-self->year,(int)self->month,(int)self->day);

    return mxPyText_FromString(buffer);
}

/* Returns a string indicating the time in ISO format. */

static
PyObject *mxDateTime_TimeString(mxDateTimeObject *self)
{
    char buffer[50];
    double second;

    second = mxDateTime_FixSecondDisplay(self->second);
    sprintf(buffer,"%02i:%02i:%05.2f",
	    (int)self->hour,(int)self->minute,(float)second);

    return mxPyText_FromString(buffer);
}

#ifdef MS_WIN32

/* The Windows C lib strftime() tends to crash easily in various
   settings, e.g.
   - using an unsupported %f format char
   - % as last char in the format string
   - trying to render leap seconds (seconds == 60)
   - trying to render days > 31 (see #1349)
   
   Returns -1 in case of an error, 0 otherwise.

*/

static
int _mxDateTime_CheckWindowsStrftime(char *fmt,
				     struct tm *tm)
{
    char *p;

    /* Range checks */
    Py_Assert(tm->tm_sec < 60,
	      PyExc_ValueError,
	      ".strftime() cannot format leap seconds on Windows");
    Py_Assert(tm->tm_mday <= 31,
	      PyExc_ValueError,
	      ".strftime() cannot format days > 31 on Windows");

    /* Scan format string for invalid codes */
    for (p = fmt; *p != '\0'; p++) {
	register char code;
	if (*p != '%')
	    continue;
	code = *++p;
	/* Check for supported format codes; see
	   https://msdn.microsoft.com/en-us/library/fe06s4ak.aspx */
	switch (code) {
	case 'a':
	case 'A':
	case 'b':
	case 'B':
	case 'c':
	case 'd':
	case 'H':
	case 'I':
	case 'i':
	case 'm':
	case 'M':
	case 'p':
	case 'S':
	case 'U':
	case 'w':
	case 'W':
	case 'x':
	case 'X':
	case 'y':
	case 'Y':
	case 'z':
	case 'Z':
	case '%':
	    continue;
	case '\0':
	    Py_Error(PyExc_ValueError,
		     "format string may not end with a '%' character "
		     "on Windows");
	default:
	    Py_ErrorWithArg(PyExc_ValueError,
			    "format code '%c' not supported on Windows",
			    code);
	}
    }
    return 0;

  onError:
    return -1;
}

#endif

/* --- methods --- */

#define datetime ((mxDateTimeObject*)self)

#ifdef HAVE_STRFTIME
Py_C_Function( mxDateTime_strftime,
	       "strftime(formatstr)")
{
    PyObject *v;
    char *fmt = 0;
    char *output = 0;
    Py_ssize_t len_output,size_output = STRFTIME_OUTPUT_SIZE;
    struct tm tm;

    Py_GetArg("|s",fmt);
    
    if (!fmt)
	/* We default to the locale's standard date/time format */
	fmt = "%c";

    Py_Assert((long)((int)datetime->year) == datetime->year,
	      mxDateTime_RangeError,
	      "year out of range for strftime() formatting");

    /* Init tm struct */
    memset(&tm,0,sizeof(tm));
    tm.tm_mday = (int)datetime->day;
    tm.tm_mon = (int)datetime->month - 1;
    tm.tm_year = (int)datetime->year - 1900;
    tm.tm_hour = (int)datetime->hour;
    tm.tm_min = (int)datetime->minute;
#if ROUND_SECONDS_IN_TM_STRUCT
    tm.tm_sec = (int)(datetime->second + 0.5); /* Round the value */
#else
    tm.tm_sec = (int)datetime->second;
#endif
    tm.tm_wday = ((int)datetime->day_of_week + 1) % 7;
    tm.tm_yday = (int)datetime->day_of_year - 1;
    tm.tm_isdst = mxDateTime_DST(datetime);

#ifdef MS_WIN32
    if (_mxDateTime_CheckWindowsStrftime(fmt, &tm))
	goto onError;
#endif

    output = new(char,size_output);

    while (1) {
	if (output == NULL) {
	    PyErr_NoMemory();
	    goto onError;
	}
    	len_output = strftime(output,size_output,fmt,&tm);
	if (len_output == size_output) {
	    size_output *= 2;
	    output = resize(output,char,size_output);
	}
	else
	    break;
    }
    v = mxPyText_FromStringAndSize(output,len_output);
    if (v == NULL)
	goto onError;
    free(output);
    return v;

 onError:
    if (output)
	free(output);
    return NULL;
}
#endif

Py_C_Function( mxDateTime_tuple,
	       "tuple()\n"
	       "Return a (year,month,day,hour,minute,second,day_of_week,\n"
	       "day_of_year,dst) tuple.")
{
    int dst;
    
    Py_NoArgsCheck();
    dst = mxDateTime_DST(datetime);
    return Py_BuildValue("liiiiiiii",
			 (long)datetime->year,
			 (int)datetime->month,
			 (int)datetime->day,
			 (int)datetime->hour,
			 (int)datetime->minute,
			 (int)datetime->second,
			 (int)datetime->day_of_week,
			 (int)datetime->day_of_year,
			 dst);
 onError:
    return NULL;
}

Py_C_Function( mxDateTime_Julian,
	       "Julian()\n"
	       "Return an instance pointing to the same date and time,\n"
	       "but using the Julian calendar. If the instance already\n"
	       "uses the Julian calendar, a new reference to it is returned."
	       )
{
    long year;
    int month,day,hour,minute,day_of_week,day_of_year;
    double second;

    Py_NoArgsCheck();

    if (datetime->calendar == MXDATETIME_JULIAN_CALENDAR) {
	Py_INCREF(datetime);
	return (PyObject *)datetime;
    }
    if (mxDateTime_AsJulianDate(datetime,
				&year,
				&month,
				&day,
				&hour,
				&minute,
				&second,
				&day_of_week,
				&day_of_year))
	goto onError;
    return mxDateTime_FromJulianDateAndTime(year,
					    month,
					    day,
					    hour,
					    minute,
					    second);
 onError:
    return NULL;
}

Py_C_Function( mxDateTime_Gregorian,
	       "Gregorian()\n"
	       "Return an instance pointing to the same date and time,\n"
	       "but using the Gregorian calendar. If the instance already\n"
	       "uses the Gregorian calendar, a new reference to it is returned."
	       )
{
    long year;
    int month,day,hour,minute,day_of_week,day_of_year;
    double second;

    Py_NoArgsCheck();

    if (datetime->calendar == MXDATETIME_GREGORIAN_CALENDAR) {
	Py_INCREF(datetime);
	return (PyObject *)datetime;
    }
    if (mxDateTime_AsGregorianDate(datetime,
				   &year,
				   &month,
				   &day,
				   &hour,
				   &minute,
				   &second,
				   &day_of_week,
				   &day_of_year))
	goto onError;
    return mxDateTime_FromGregorianDateAndTime(year,
					       month,
					       day,
					       hour,
					       minute,
					       second);
 onError:
    return NULL;
}

Py_C_Function_WithKeywords( 
	       mxDateTime_rebuild,
	       "rebuild(year=None,month=None,day=None,hour=None,minute=None,second=None)\n\n"
	       "Returns a DateTime-object with the given date/time values\n"
	       "replaced by new values."
	       )
{
    long year;
    int month,day,hour,minute;
    double second;

    /* Get the broken down values */
    year = datetime->year;
    month = datetime->month;
    day = datetime->day;
    hour = datetime->hour;
    minute = datetime->minute;
    second = datetime->second;

    /* Override them with parameters */
    Py_KeywordsGet6Args("|liiiid",year,month,day,hour,minute,second);

    /* Build new object */
    if (datetime->calendar == MXDATETIME_GREGORIAN_CALENDAR)
	return mxDateTime_FromGregorianDateAndTime(year,
						   month,
						   day,
						   hour,
						   minute,
						   second);
    else
	return mxDateTime_FromJulianDateAndTime(year,
						month,
						day,
						hour,
						minute,
						second);
 onError:
    return NULL;
}

#ifdef HAVE_PYDATETIME
Py_C_Function_WithKeywords( 
	       mxDateTime_pydate,
	       "pydate()\n\n"
	       "Returns a datetime.date object with just the date values."
	       )
{
    Py_NoArgsCheck();

    /* Convert values */
    Py_Assert(datetime->year > 0 && datetime->year <= 9999,
	      PyExc_ValueError,
	      "DateTime object values out of range for "
	      "dateime.date objects");

    /* Make sure the PyDateTimeAPI is loaded */
    if (mx_Require_PyDateTimeAPI())
	goto onError;

    /* Build new object */
    return PyDate_FromDate((int)datetime->year,
			   (int)datetime->month,
			   (int)datetime->day);
 onError:
    return NULL;
}

Py_C_Function_WithKeywords( 
	       mxDateTime_pydatetime,
	       "pydatetime()\n\n"
	       "Returns a datetime.datetime object with the same values."
	       )
{
    int second, microsecond;
    
    Py_NoArgsCheck();

    /* Convert values */
    Py_Assert(datetime->year > 0 && datetime->year <= 9999,
	      PyExc_ValueError,
	      "DateTime object values out of range for "
	      "dateime.datetime objects");
    second = (int)(datetime->second);
    microsecond = (int)((datetime->second - (double)second) * 1e6);
    
    /* Make sure the PyDateTimeAPI is loaded */
    if (mx_Require_PyDateTimeAPI())
	goto onError;

    /* Build new object */
    return PyDateTime_FromDateAndTime((int)datetime->year,
				      (int)datetime->month,
				      (int)datetime->day,
				      (int)datetime->hour,
				      (int)datetime->minute,
				      second,
				      microsecond);
 onError:
    return NULL;
}

Py_C_Function_WithKeywords( 
	       mxDateTime_pytime,
	       "pytime()\n\n"
	       "Returns a datetime.time object with just the time values."
	       )
{
    int second, microsecond;
    
    Py_NoArgsCheck();

    /* Convert values */
    second = (int)(datetime->second);
    microsecond = (int)((datetime->second - (double)second) * 1e6);
    
    /* Make sure the PyDateTimeAPI is loaded */
    if (mx_Require_PyDateTimeAPI())
	goto onError;

    /* Build new object */
    return PyTime_FromTime((int)datetime->hour,
			   (int)datetime->minute,
			   second,
			   microsecond);
 onError:
    return NULL;
}
#endif

#ifdef COPY_PROTOCOL
Py_C_Function( mxDateTime_copy,
	       "copy([memo])\n\n"
	       "Return a new reference for the instance. This function\n"
	       "is used for the copy-protocol. Real copying doesn't take\n"
	       "place, since the instances are immutable.")
{
    PyObject *memo;
    
    Py_GetArg("|O",memo);
    Py_INCREF(datetime);
    return (PyObject *)datetime;
 onError:
    return NULL;
}
#endif

Py_C_Function( mxDateTime_ticks,
	       "ticks([offset=0.0,dst=-1])\n\n"
	       "Return a time.time()-like value, representing the objects\n"
	       "value assuming it is local time. The conversion is done\n"
	       "using mktime() with the DST flag set to dst. offset is\n"
	       "subtracted from the resulting ticks value.")
{
    double ticks,offset = 0.0;
    int dst = -1;
    
    Py_Get2Args("|di",offset,dst);
    ticks = mxDateTime_AsTicksWithOffset(datetime,offset,dst);
    if (ticks == -1.0 && PyErr_Occurred())
	goto onError;
    return PyFloat_FromDouble(ticks);
 onError:
    return NULL;
}

Py_C_Function( mxDateTime_gmticks,
	       "gmticks(offset=0.0)\n\n"
	       "Return a time.time()-like value, representing the objects\n"
	       "value assuming it is UTC time. offset is subtracted from\n"
	       "the resulting ticks value.")
{
    double ticks,offset = 0.0;
    
    Py_GetArg("|d",offset)
    ticks = mxDateTime_AsGMTicksWithOffset(datetime,offset);
    if (ticks == -1.0 && PyErr_Occurred())
	goto onError;
    return PyFloat_FromDouble(ticks);
 onError:
    return NULL;
}

Py_C_Function( mxDateTime_gmtoffset,
	       "gmtoffset()\n\n"
	       "Returns a DateTimeDelta instance representing the UTC offset\n"
	       "for datetime assuming that the stored values refer to local\n"
	       "time. If you subtract this value from datetime, you'll get\n"
	       "UTC time."
	       )
{
    double offset;
    
    Py_NoArgsCheck();
    offset = mxDateTime_GMTOffset(datetime);
    if (offset == -1.0 && PyErr_Occurred())
	goto onError;
    return mxDateTimeDelta_FromSeconds(offset);
    
 onError:
    return NULL;
}

Py_C_Function( mxDateTime_gmtime,
	       "gmtime()\n\n"
	       "Returns a DateTime instance representing datetime in UTC\n"
	       "time assuming that the stored values refer to local time."
	       )
{
    double offset;
    
    Py_NoArgsCheck();
    offset = mxDateTime_GMTOffset(datetime);
    if (offset == -1.0 && PyErr_Occurred())
	goto onError;
    return mxDateTime_FromDateTimeAndOffset(datetime, 0, -offset);
    
 onError:
    return NULL;
}

Py_C_Function( mxDateTime_localtime,
	       "localtime()\n\n"
	       "Returns a DateTime instance representing datetime in local\n"
	       "time assuming that the stored values refer to UTC time."
	       )
{
    double gmticks;

    Py_NoArgsCheck();
    gmticks = mxDateTime_AsGMTicks(datetime);
    if (gmticks == -1.0 && PyErr_Occurred())
	goto onError;
    
    return mxDateTime_FromTicks(gmticks);

 onError:
    return NULL;
}

Py_C_Function( mxDateTime_COMDate,
	       "COMDate()\n\n"
	       "Return a float where the whole part is the number of days\n"
	       "in the Gregorian calendar since 30.12.1899 and the fraction\n"
	       "part equals abstime/86400.0.")
{
    double comdate;
    
    Py_NoArgsCheck();
    comdate = mxDateTime_AsCOMDate(datetime);
    return PyFloat_FromDouble(comdate);

 onError:
    return NULL;
}

Py_C_Function( mxDateTime_absvalues,
	       "absvalues()")
{
    Py_NoArgsCheck();
    Py_Return2("ld",datetime->absdate,datetime->abstime);

 onError:
    return NULL;
}

Py_C_Function( mxDateTime_weekday,
	       "weekdday()\n"
	       "Return the day of the week as integer; same as .day_of_week.\n"
	       "This API is needed for datetime.date() compatibility.")
{
    Py_NoArgsCheck();
    Py_Return("i", datetime->day_of_week);

 onError:
    return NULL;
}

#undef datetime

/* Python Method Table */

statichere
PyMethodDef mxDateTime_Methods[] =
{   
#ifdef HAVE_STRFTIME
    Py_MethodListEntry("strftime",mxDateTime_strftime),
#endif
    Py_MethodListEntryNoArgs("tuple",mxDateTime_tuple),
    Py_MethodListEntryNoArgs("Julian",mxDateTime_Julian),
    Py_MethodListEntryNoArgs("Gregorian",mxDateTime_Gregorian),
    Py_MethodListEntryNoArgs("COMDate",mxDateTime_COMDate),
    Py_MethodListEntryNoArgs("absvalues",mxDateTime_absvalues),
#ifdef HAVE_STRFTIME
    Py_MethodListEntry("Format",mxDateTime_strftime),
#endif
#ifdef COPY_PROTOCOL
    Py_MethodListEntry("__deepcopy__",mxDateTime_copy),
    Py_MethodListEntry("__copy__",mxDateTime_copy),
#endif
    Py_MethodListEntry("ticks",mxDateTime_ticks),
    Py_MethodListEntry("gmticks",mxDateTime_gmticks),
    Py_MethodListEntryNoArgs("gmtoffset",mxDateTime_gmtoffset),
    Py_MethodListEntryNoArgs("gmtime",mxDateTime_gmtime),
    Py_MethodListEntryNoArgs("localtime",mxDateTime_localtime),
    Py_MethodWithKeywordsListEntry("rebuild",mxDateTime_rebuild),
    /* Interfaces needed for Python's datetime module compatibility */
    Py_MethodListEntryNoArgs("timetuple",mxDateTime_tuple),
    Py_MethodListEntryNoArgs("weekday",mxDateTime_weekday),
#ifdef HAVE_PYDATETIME
    Py_MethodListEntryNoArgs("pydate",mxDateTime_pydate),
    Py_MethodListEntryNoArgs("pydatetime",mxDateTime_pydatetime),
    Py_MethodListEntryNoArgs("pytime",mxDateTime_pytime),
#endif
#ifdef OLD_INTERFACE
    /* Old interface */
    Py_MethodListEntryNoArgs("as_tuple",mxDateTime_tuple),
    Py_MethodListEntryNoArgs("as_COMDate",mxDateTime_COMDate),
    Py_MethodListEntryNoArgs("as_ticks",mxDateTime_ticks),
#endif
    {NULL,NULL} /* end of list */
};

/* --- slots --- */

static
PyObject *mxDateTime_AsFloat(PyObject *obj)
{
    mxDateTimeObject *self = (mxDateTimeObject *)obj;
    double ticks;

    ticks = mxDateTime_AsTicksWithOffset(self,0,-1);
    if (ticks == -1.0 && PyErr_Occurred())
	goto onError;
    return PyFloat_FromDouble(ticks);
 onError:
    return NULL;
}

static
PyObject *mxDateTime_AsInt(PyObject *obj)
{
    mxDateTimeObject *self = (mxDateTimeObject *)obj;
    double ticks;

    ticks = mxDateTime_AsTicksWithOffset(self,0,-1);
    if (ticks == -1.0 && PyErr_Occurred())
	goto onError;
    return PyInt_FromLong((long)ticks);
 onError:
    return NULL;
}

static
int mxDateTime_NonZero(PyObject *obj)
{
    /* A mxDateTime instance can never be zero */
    return 1;
}

static
PyObject *mxDateTime_Str(PyObject *obj)
{
    mxDateTimeObject *self = (mxDateTimeObject *)obj;
    char s[50];

    mxDateTime_AsString(self,s,sizeof(s));
    return mxPyText_FromString(s);
}

static
PyObject *mxDateTime_Repr(PyObject *obj)
{
    mxDateTimeObject *self = (mxDateTimeObject *)obj;
    char t[100];
    char s[50];

    mxDateTime_AsString(self, s, sizeof(s));
    sprintf(t,"<%s object for '%s' at %lx>", 
	    self->ob_type->tp_name, s, (long)self);
    return mxPyText_FromString(t);
}

#ifdef WANT_SUBCLASSABLE_TYPES

#define Py_GetMember_Function(object, attrname) \
        static PyObject *object##_get_##attrname(object *self, void *closure)

#define Py_SetMember_Function(object, attrname) \
        static PyObject *object##_set_##attrname(object *self, PyObject *value, void *closure)

#define Py_MemberListEntry(object, attrname) \
        {#attrname, (getter)object##_get_##attrname, (setter)object##_set_##attrname, NULL}
#define Py_MemberListEntryReadonly(object, attrname) \
        {#attrname, (getter)object##_get_##attrname, NULL, NULL}

#define mxDateTime_GetMember(attrname) \
        Py_GetMember_Function(mxDateTimeObject, attrname)
#define mxDateTime_MemberListEntryReadonly(attrname) \
        Py_MemberListEntryReadonly(mxDateTimeObject, attrname)

mxDateTime_GetMember(year) {
    return PyInt_FromLong(self->year);
}

mxDateTime_GetMember(month) {
    return PyInt_FromLong((long)self->month);
}

mxDateTime_GetMember(day) {
    return PyInt_FromLong((long)self->day);
}

mxDateTime_GetMember(hour) {
    return PyInt_FromLong((long)self->hour);
}

mxDateTime_GetMember(minute) {
    return PyInt_FromLong((long)self->minute);
}

mxDateTime_GetMember(second) {
    return PyFloat_FromDouble((double)self->second);
}

mxDateTime_GetMember(absdays) {
    return PyFloat_FromDouble((double)(self->absdate - 1) 
			      + self->abstime / SECONDS_PER_DAY);
}

mxDateTime_GetMember(absdate) {
    return PyInt_FromLong(self->absdate);
}

mxDateTime_GetMember(abstime) {
    return PyFloat_FromDouble((double)self->abstime);
}

mxDateTime_GetMember(date) {
    return mxDateTime_DateString(self);
}

mxDateTime_GetMember(time) {
    return mxDateTime_TimeString(self);
}

mxDateTime_GetMember(yearoffset) {
    long yearoffset = mxDateTime_YearOffset(self->year,self->calendar);
    if (yearoffset == -1 && PyErr_Occurred())
	return NULL;
    return PyInt_FromLong(yearoffset);
}

mxDateTime_GetMember(is_leapyear) {
    return PyInt_FromLong(mxDateTime_Leapyear(self->year,self->calendar));
}

mxDateTime_GetMember(day_of_week) {
    return PyInt_FromLong((long)self->day_of_week);
}

mxDateTime_GetMember(day_of_year) {
    return PyInt_FromLong((long)self->day_of_year);
}

mxDateTime_GetMember(days_in_month) {
    return PyInt_FromLong(days_in_month[mxDateTime_Leapyear(self->year,
							    self->calendar)]
			  [self->month - 1]);
}
    
mxDateTime_GetMember(iso_week) {
    return mxDateTime_ISOWeekTuple(self);
}

mxDateTime_GetMember(tz) {
    return mxDateTime_TimezoneString(self);
}

mxDateTime_GetMember(dst) {
    return PyInt_FromLong(mxDateTime_DST(self));
}

mxDateTime_GetMember(mjd) {
    return PyFloat_FromDouble((double)(self->absdate - 678576) + 
			      self->abstime / SECONDS_PER_DAY);
}

mxDateTime_GetMember(tjd) {
    return PyFloat_FromDouble((double)((self->absdate - 678576) % 10000) + 
			      self->abstime / SECONDS_PER_DAY);
}

mxDateTime_GetMember(tjd_myriad) {
    return PyInt_FromLong((self->absdate - 678576) / 10000 + 240);
}

mxDateTime_GetMember(jdn) {
    return PyFloat_FromDouble((double)self->absdate + 1721424.5 + 
			      self->abstime / SECONDS_PER_DAY);
}

mxDateTime_GetMember(calendar) {
    return mxDateTime_CalendarString(self);
}

/* Member access function list */

struct PyGetSetDef mxDateTime_MemberAccess[] = {
    mxDateTime_MemberListEntryReadonly(year),
    mxDateTime_MemberListEntryReadonly(month),
    mxDateTime_MemberListEntryReadonly(day),
    mxDateTime_MemberListEntryReadonly(hour),
    mxDateTime_MemberListEntryReadonly(minute),
    mxDateTime_MemberListEntryReadonly(second),
    mxDateTime_MemberListEntryReadonly(absdays),
    mxDateTime_MemberListEntryReadonly(absdate),
    mxDateTime_MemberListEntryReadonly(abstime),
    mxDateTime_MemberListEntryReadonly(date),
    mxDateTime_MemberListEntryReadonly(time),
    mxDateTime_MemberListEntryReadonly(yearoffset),
    mxDateTime_MemberListEntryReadonly(is_leapyear),
    mxDateTime_MemberListEntryReadonly(day_of_week),
    mxDateTime_MemberListEntryReadonly(day_of_year),
    mxDateTime_MemberListEntryReadonly(days_in_month),    
    mxDateTime_MemberListEntryReadonly(iso_week),
    mxDateTime_MemberListEntryReadonly(tz),
    mxDateTime_MemberListEntryReadonly(dst),
    mxDateTime_MemberListEntryReadonly(mjd),
    mxDateTime_MemberListEntryReadonly(tjd),
    mxDateTime_MemberListEntryReadonly(tjd_myriad),
    mxDateTime_MemberListEntryReadonly(jdn),
    mxDateTime_MemberListEntryReadonly(calendar),
    {0} /* sentinel */
};

#endif

static
PyObject *mxDateTime_Getattr(PyObject *obj,
			     char *name)
{
    mxDateTimeObject *self = (mxDateTimeObject *)obj;

#ifdef WANT_SUBCLASSABLE_TYPES
    DPRINTF("mxDateTime_Getattr: looking for '%s'", name);
#endif

    if (Py_WantAttr(name,"year"))
	return PyInt_FromLong(self->year);

    else if (Py_WantAttr(name,"month"))
	return PyInt_FromLong((long)self->month);

    else if (Py_WantAttr(name,"day"))
	return PyInt_FromLong((long)self->day);

    else if (Py_WantAttr(name,"hour"))
	return PyInt_FromLong((long)self->hour);

    else if (Py_WantAttr(name,"minute"))
	return PyInt_FromLong((long)self->minute);

    else if (Py_WantAttr(name,"second"))
	return PyFloat_FromDouble((double)self->second);

    else if (Py_WantAttr(name,"absdays"))
	return PyFloat_FromDouble(
		      (double)(self->absdate - 1) + 
		      self->abstime / SECONDS_PER_DAY);

    else if (Py_WantAttr(name,"absdate"))
	return PyInt_FromLong(self->absdate);

    else if (Py_WantAttr(name,"abstime"))
	return PyFloat_FromDouble((double)self->abstime);

    else if (Py_WantAttr(name,"date"))
	return mxDateTime_DateString(self);

    else if (Py_WantAttr(name,"time"))
	return mxDateTime_TimeString(self);

    else if (Py_WantAttr(name,"yearoffset")) {
	long yearoffset = mxDateTime_YearOffset(self->year,self->calendar);
	if (yearoffset == -1 && PyErr_Occurred())
	    goto onError;
	return PyInt_FromLong(yearoffset);
    }
    
    else if (Py_WantAttr(name,"is_leapyear"))
	return PyInt_FromLong(mxDateTime_Leapyear(self->year,self->calendar));
    
    else if (Py_WantAttr(name,"day_of_week"))
	return PyInt_FromLong((long)self->day_of_week);

    else if (Py_WantAttr(name,"day_of_year"))
	return PyInt_FromLong((long)self->day_of_year);

    else if (Py_WantAttr(name,"days_in_month"))
	return PyInt_FromLong(
	       days_in_month[mxDateTime_Leapyear(self->year,self->calendar)]\
	                    [self->month - 1]);
    
    else if (Py_WantAttr(name,"iso_week"))
	return mxDateTime_ISOWeekTuple(self);

    else if (Py_WantAttr(name,"tz"))
	return mxDateTime_TimezoneString(self);

    else if (Py_WantAttr(name,"dst"))
	return PyInt_FromLong(mxDateTime_DST(self));

    else if (Py_WantAttr(name,"mjd"))
	return PyFloat_FromDouble((double)(self->absdate - 678576) + 
				  self->abstime / SECONDS_PER_DAY);

    else if (Py_WantAttr(name,"tjd"))
	return PyFloat_FromDouble((double)((self->absdate - 678576) % 10000) + 
				  self->abstime / SECONDS_PER_DAY);

    else if (Py_WantAttr(name,"tjd_myriad"))
	return PyInt_FromLong((self->absdate - 678576) / 10000 + 240);

    else if (Py_WantAttr(name,"jdn"))
	return PyFloat_FromDouble((double)self->absdate + 1721424.5 + 
				  self->abstime / SECONDS_PER_DAY);

    else if (Py_WantAttr(name,"calendar"))
	return mxDateTime_CalendarString(self);

    /* For Zope security */
    else if (Py_WantAttr(name,"__roles__")) {
	Py_INCREF(Py_None);
	return Py_None;
    }
    else if (Py_WantAttr(name,"__allow_access_to_unprotected_subobjects__"))
	return PyInt_FromLong(1L);

    else if (Py_WantAttr(name,"__members__"))
	return Py_BuildValue("["
			     "sss"
			     "sss"
			     "sss"
			     "sss"
			     "sss"
			     "sss"
			     "ss"
			     "ss"
			     "]",
			     "year","month","day",
			     "hour","minute","second",
			     "absdays","absdate","abstime",
			     "yearoffset","is_leapyear","day_of_week",
			     "day_of_year","days_in_month","tz",
			     "dst","iso_week","mjd",
			     "tjd","tjd_myriad",
			     "jdn","calendar"
			     );

    return Py_FindMethod(mxDateTime_Methods,
			 (PyObject *)self,name);

 onError:
    return NULL;
}

static
long mxDateTime_Hash(PyObject *obj)
{
    mxDateTimeObject *self = (mxDateTimeObject *)obj;
    long x = 0;
    long z[sizeof(double)/sizeof(long)+1];
    register int i;
    
    /* Clear z */
    for (i = sizeof(z) / sizeof(long) - 1; i >= 0; i--)
	z[i] = 0;

    /* Copy the double onto z */
    *((double *)z) = self->absdate * SECONDS_PER_DAY + self->abstime;

    /* Hash the longs in z together using XOR */
    for (i = sizeof(z) / sizeof(long) - 1; i >= 0; i--)
	x ^= z[i];

    /* Errors are indicated by returning -1, so we have to fix
       that hash value by hand. */
    if (x == -1)
	x = 19980427;

    return x;
}

static
int mxDateTime_Compare(PyObject *left,
		       PyObject *right)
{
    mxDateTimeObject *self = (mxDateTimeObject *)left;
    mxDateTimeObject *other = (mxDateTimeObject *)right;

    DPRINTF("mxDateTime_Compare: "
	    "%s op %s\n",
	    left->ob_type->tp_name,
	    right->ob_type->tp_name);

    if (self == other)
	return 0;

    /* Short-cut */
    if (_mxDateTime_Check(left) && _mxDateTime_Check(right)) {
	long d0 = self->absdate, d1 = other->absdate;
	double t0 = self->abstime, t1 = other->abstime;

	return (d0 < d1) ? -1 : (d0 > d1) ? 1 :
	    (t0 < t1) ? -1 : (t0 > t1) ? 1 : 0;
    }
    Py_Error(PyExc_TypeError,
	     "can't compare types");

 onError:
    return -1;
}

static
PyObject *mxDateTime_RichCompare(PyObject *left,
				 PyObject *right,
				 int op)
{
    mxDateTimeObject *self = (mxDateTimeObject *)left;
    mxDateTimeObject *other = (mxDateTimeObject *)right;
    int cmp;

    DPRINTF("mxDateTime_RichCompare: "
	    "%s op %s (op=%i)\n",
	    left->ob_type->tp_name,
	    right->ob_type->tp_name,
	    op);

    /* Same type comparison */
    if (self == other)
	cmp = 0;

    else if (_mxDateTime_Check(left) && _mxDateTime_Check(right)) {
	long d0 = self->absdate, d1 = other->absdate;
	double t0 = self->abstime, t1 = other->abstime;
    
	cmp = (d0 < d1) ? -1 : (d0 > d1) ? 1 :
	    (t0 < t1) ? -1 : (t0 > t1) ? 1 : 0;
    }

    /* Make sure that we only have to deal with DateTime op <other
       type> */
    else if (_mxDateTime_Check(right)) {
	/* <other type> op DateTime */
	return mxDateTime_RichCompare(right, left,
				      SWAPPED_ARGS_RICHCOMPARE_OP(op));
    }

    else if (!_mxDateTime_Check(left)) {
	Py_INCREF(Py_NotImplemented);
	return Py_NotImplemented;
    }

    else if (_mxDateTimeDelta_Check(right)) {
	/* DateTime op DateTimeDelta: not supported */
	Py_INCREF(Py_NotImplemented);
	return Py_NotImplemented;
    }

    else if (PyFloat_Compatible(right)) {
	/* DateTime op Number -- compare ticks */
	double t1 = PyFloat_AsDouble(right);
	double t0 = mxDateTime_AsTicksWithOffset(self,0,-1);

	if (t1 == -1.0 && PyErr_Occurred()) {
	    /* Give up and let the right argument try to deal with the
	       operation. */
	    PyErr_Clear();
	    Py_INCREF(Py_NotImplemented);
	    return Py_NotImplemented;
	}
	if (t0 == -1.0 && PyErr_Occurred())
	    goto onError;

	cmp = (t0 < t1) ? -1 : (t0 > t1) ? 1 : 0;
    }
#ifdef HAVE_PYDATETIME
    else if (mx_PyDateTime_Check(right)) {
	/* DateTime op PyDateTime

	   Note: PyDateTime is a subclass of PyDate and
	   mx_PyDate_Check() also matches PyDateTime objects !

	*/
	double abstime;
	
	if (mx_Require_PyDateTimeAPI())
	    goto onError;

	abstime = (
	  (double)PyDateTime_DATE_GET_HOUR(right) * 3600.0
	  + (double)PyDateTime_DATE_GET_MINUTE(right) * 60.0
	  + (double)PyDateTime_DATE_GET_SECOND(right)
	  + (double)PyDateTime_DATE_GET_MICROSECOND(right) 
	  * 1e-6);
	cmp = (
	  (self->year < PyDateTime_GET_YEAR(right)) ? -1 :
	  (self->year > PyDateTime_GET_YEAR(right)) ? 1 :
	  (self->month < PyDateTime_GET_MONTH(right)) ? -1 :
	  (self->month > PyDateTime_GET_MONTH(right)) ? 1 :
	  (self->day < PyDateTime_GET_DAY(right)) ? -1 :
	  (self->day > PyDateTime_GET_DAY(right)) ? 1 :
	  (self->abstime < abstime) ? -1 :
	  (self->abstime > abstime) ? 1 :
	  0);
    }

    else if (mx_PyDate_Check(right)) {
	/* DateTime op PyDate */

	if (mx_Require_PyDateTimeAPI())
	    goto onError;

	cmp = (
	  (self->year < PyDateTime_GET_YEAR(right)) ? -1 :
	  (self->year > PyDateTime_GET_YEAR(right)) ? 1 :
	  (self->month < PyDateTime_GET_MONTH(right)) ? -1 :
	  (self->month > PyDateTime_GET_MONTH(right)) ? 1 :
	  (self->day < PyDateTime_GET_DAY(right)) ? -1 :
	  (self->day > PyDateTime_GET_DAY(right)) ? 1 :
	  (self->abstime > 0.0) ? 1 : 0);
    }
#endif
    else {
	Py_INCREF(Py_NotImplemented);
	return Py_NotImplemented;
    }

    /* Build result */
    switch (op) {
    case Py_EQ:
        cmp = (cmp == 0);
        break;
    case Py_NE:
        cmp = (cmp != 0);
        break;
    case Py_LE:
	cmp = (cmp == -1) || (cmp == 0);
        break;
    case Py_GE:
	cmp = (cmp == 1) || (cmp == 0);
        break;
    case Py_LT:
	cmp = (cmp == -1);
        break;
    case Py_GT:
        cmp = (cmp == 1);
        break;
    }
    return PyBool_FromLong(cmp);
    
 onError:
    return NULL;
}

static
PyObject *mxDateTime_Add(PyObject *left,
			 PyObject *right)
{
    long absdate_offset;
    double abstime_offset;

    DPRINTF("mxDateTime_Add: %s + %s\n",
	    left->ob_type->tp_name,
	    right->ob_type->tp_name);

    /* Make sure that we only have to deal with DateTime + <other
       type> */
    if (!_mxDateTime_Check(left)) {
	if (!_mxDateTime_Check(right)) {
	    Py_INCREF(Py_NotImplemented);
	    return Py_NotImplemented;
	}
	return mxDateTime_Add(right, left);
    }

    if (_mxDateTimeDelta_Check(right)) {
	/* DateTime + DateTimeDelta */
	abstime_offset = ((mxDateTimeDeltaObject *)right)->seconds;
	absdate_offset = 0;
    }
    else if (_mxDateTime_Check(right)) {
	/* DateTime + DateTime: not supported */
	Py_INCREF(Py_NotImplemented);
	return Py_NotImplemented;
    }
    else {
	double value;

	if (PyFloat_Compatible(right)) {
	    /* DateTime - Number (meaning days or fractions thereof) */
	    value = PyFloat_AsDouble(right) * SECONDS_PER_DAY;
	    if (value < 0.0 && PyErr_Occurred()) {
		/* Give up and let the right argument try to deal with
		   the operation. */
		PyErr_Clear();
		Py_INCREF(Py_NotImplemented);
		return Py_NotImplemented;
	    }
	}
#if HAVE_PYDATETIME
	else if (mx_PyDelta_Check(right)) {
	    /* DateTime + PyDelta */

	    /* Make sure the PyDateTimeAPI is loaded */
	    if (mx_Require_PyDateTimeAPI())
		goto onError;

	    value = mx_PyDeltaInSeconds(right);
	}
#endif
	else {
	    Py_INCREF(Py_NotImplemented);
	    return Py_NotImplemented;
	}
	if (value < 0.0 && PyErr_Occurred())
	    goto onError;

	DPRINTF("mxDateTime_Add: adding %f seconds\n",
		value);

	if (value == 0.0) {
	    Py_INCREF(left);
	    return left;
	}
	abstime_offset = value;
	absdate_offset = 0;
    }

    return mxDateTime_FromDateTimeAndOffset((mxDateTimeObject *)left,
					    absdate_offset,
					    abstime_offset);
 onError:
    return NULL;
}

static
PyObject *mxDateTime_Sub(PyObject *left,
			 PyObject *right)
{
    mxDateTimeObject *self = (mxDateTimeObject *)left;
    mxDateTimeObject *other = (mxDateTimeObject *)right;

    DPRINTF("mxDateTime_Sub: %s - %s\n",
	    left->ob_type->tp_name,
	    right->ob_type->tp_name);

    if (_mxDateTime_Check(left)) {
	/* DateTime - <other type> */
	long absdate_offset;
	double abstime_offset;
	
	if (_mxDateTime_Check(right)) {
	    /* DateTime - DateTime */
	    return mxDateTimeDelta_FromDaysEx(self->absdate - other->absdate, 
					      self->abstime - other->abstime);
	}
	else if (_mxDateTimeDelta_Check(right)) {
	    /* DateTime - DateTimeDelta */
	    abstime_offset = - ((mxDateTimeDeltaObject *)right)->seconds;
	    absdate_offset = 0;
	}
	else {
	    double value;

	    if (PyFloat_Compatible(right)) {
		/* DateTime - Number (meaning days or fractions thereof) */
		value = PyFloat_AsDouble(right) * SECONDS_PER_DAY;
		if (value < 0.0 && PyErr_Occurred()) {
		    /* Give up and let the right argument try to deal
		       with the operation. */
		    PyErr_Clear();
		    Py_INCREF(Py_NotImplemented);
		    return Py_NotImplemented;
		}
	    }
#ifdef HAVE_PYDATETIME
	    else if (mx_PyDelta_Check(right)) {
		/* DateTime - PyDelta */

		/* Make sure the PyDateTimeAPI is loaded */
		if (mx_Require_PyDateTimeAPI())
		    goto onError;

		value = mx_PyDeltaInSeconds(right);
	    }
	    else if (mx_PyDateTime_Check(right)) {
		/* DateTime - PyDateTime

		   Note: PyDateTime is a subclass of PyDate and
		   mx_PyDate_Check() also matches PyDateTime objects !

		*/
		double abstime;
		long absdate;
		
		if (mx_Require_PyDateTimeAPI())
		    goto onError;

		abstime = (
		  (double)PyDateTime_DATE_GET_HOUR(right) * 3600.0
		  + (double)PyDateTime_DATE_GET_MINUTE(right) * 60.0
		  + (double)PyDateTime_DATE_GET_SECOND(right)
		  + (double)PyDateTime_DATE_GET_MICROSECOND(right) * 1e-6);
		if (mxDateTime_AbsDate(PyDateTime_GET_YEAR(right),
				       PyDateTime_GET_MONTH(right),
				       PyDateTime_GET_DAY(right),
				       MXDATETIME_GREGORIAN_CALENDAR,
				       &absdate))
		    goto onError;
		return mxDateTimeDelta_FromDaysEx(
		    self->absdate - absdate, 
		    self->abstime - abstime);
	    }

	    else if (mx_PyDate_Check(right)) {
		/* DateTime - PyDate */
		long absdate;

		if (mx_Require_PyDateTimeAPI())
		    goto onError;

		if (mxDateTime_AbsDate(PyDateTime_GET_YEAR(right),
				       PyDateTime_GET_MONTH(right),
				       PyDateTime_GET_DAY(right),
				       MXDATETIME_GREGORIAN_CALENDAR,
				       &absdate))
		    goto onError;
		return mxDateTimeDelta_FromDaysEx(
		    self->absdate - absdate, 
		    self->abstime);
	    }
#endif
	    else {
		Py_INCREF(Py_NotImplemented);
		return Py_NotImplemented;
	    }
	    if (value < 0.0 && PyErr_Occurred())
		goto onError;

	    DPRINTF("mxDateTime_Sub: subtracting %f seconds\n",
		    value);

	    if (value == 0.0) {
		Py_INCREF(left);
		return left;
	    }
	    abstime_offset = -value;
	    absdate_offset = 0;
	}

	return mxDateTime_FromDateTimeAndOffset(self, 
						absdate_offset,
						abstime_offset);
    }
    else if (_mxDateTime_Check(right)) {
	/* <other type> - DateTime */
	
	if (_mxDateTimeDelta_Check(left)) {
	    /* DateTimeDelta - DateTime */
	    Py_INCREF(Py_NotImplemented);
	    return Py_NotImplemented;
	}
	else {
	    if (PyFloat_Compatible(left)) {
		/* Number (meaning days or fractions thereof) -
    		   DateTime: not supported */
		Py_INCREF(Py_NotImplemented);
		return Py_NotImplemented;
	    }
#ifdef HAVE_PYDATETIME
	    else if (mx_PyDelta_Check(left)) {
		/* PyDelta - DateTime: not supported */
		Py_INCREF(Py_NotImplemented);
		return Py_NotImplemented;
	    }

	    else if (mx_PyDateTime_Check(left)) {
		/* PyDateTime - DateTime

		   Note: PyDateTime is a subclass of PyDate and
		   mx_PyDate_Check() also matches PyDateTime objects !

		*/
		double abstime;
		long absdate;

		if (mx_Require_PyDateTimeAPI())
		    goto onError;

		abstime = (
		  (double)PyDateTime_DATE_GET_HOUR(left) * 3600.0
		  + (double)PyDateTime_DATE_GET_MINUTE(left) * 60.0
		  + (double)PyDateTime_DATE_GET_SECOND(left)
		  + (double)PyDateTime_DATE_GET_MICROSECOND(left) * 1e-6);
		if (mxDateTime_AbsDate(PyDateTime_GET_YEAR(left),
				       PyDateTime_GET_MONTH(left),
				       PyDateTime_GET_DAY(left),
				       MXDATETIME_GREGORIAN_CALENDAR,
				       &absdate))
		    goto onError;
		return mxDateTimeDelta_FromDaysEx(
		    absdate - other->absdate, 
		    abstime - other->abstime);
	    }

	    else if (mx_PyDate_Check(left)) {
		/* PyDate - DateTime */
		long absdate;

		if (mx_Require_PyDateTimeAPI())
		    goto onError;

		if (mxDateTime_AbsDate(PyDateTime_GET_YEAR(left),
				       PyDateTime_GET_MONTH(left),
				       PyDateTime_GET_DAY(left),
				       MXDATETIME_GREGORIAN_CALENDAR,
				       &absdate))
		    goto onError;
		return mxDateTimeDelta_FromDaysEx(
		    absdate - other->absdate, 
		            - other->abstime);
	    }
#endif
	    else {
		Py_INCREF(Py_NotImplemented);
		return Py_NotImplemented;
	    }
	}
    }
    else {
	Py_INCREF(Py_NotImplemented);
	return Py_NotImplemented;
    }
    
 onError:
    return NULL;
}

#ifdef WANT_SUBCLASSABLE_TYPES

/* Allocate an object without initializing it */

static
PyObject *mxDateTime_Allocate(PyTypeObject *type, 
			      int nitems)
{
    Py_Assert(nitems == 0,
	      PyExc_TypeError,
	      "DateTime objects do not have items");

    if (type == &mxDateTime_Type)
	return (PyObject *)mxDateTime_New();

    else {
	PyObject *v;

	v = PyType_GenericAlloc(type, nitems);
	if (v == NULL)
	    goto onError;

	return v;
    }
    
 onError:
    return NULL;
}

/* Initialize the object according to the args and keywords (__init__) */

static
int mxDateTime_Initialize(PyObject *obj,
			  PyObject *args,
			  PyObject *kws)
{
    mxDateTimeObject *self = (mxDateTimeObject *)obj;
    long year;
    int month = 1,
	day = 1;
    int hour = 0,
	minute = 0;
    double second = 0.0;
    
    Py_KeywordsGet6Args("l|iiiid",year,month,day,hour,minute,second);
    if (mxDateTime_SetFromDateAndTime(self,
				      year,month,day,
				      hour,minute,second,
				      MXDATETIME_GREGORIAN_CALENDAR))
	goto onError;
    return 0;

 onError:
    return -1;
}

#endif

/* Python Type Tables */

static
PyNumberMethods mxDateTime_TypeAsNumber = {

    /* These slots are not NULL-checked, so we must provide dummy functions */
    mxDateTime_Add,			/*nb_add*/
    mxDateTime_Sub,			/*nb_subtract*/
    notimplemented2,		       	/*nb_multiply*/
    notimplemented2,			/*nb_divide*/
    notimplemented2,			/*nb_remainder*/
    notimplemented2,			/*nb_divmod*/
    notimplemented3,			/*nb_power*/
    notimplemented1,			/*nb_negative*/
    notimplemented1,			/*nb_positive*/

    /* Everything below this line EXCEPT nb_nonzero (!) is NULL checked */
    0,					/*nb_absolute*/
    mxDateTime_NonZero,			/*nb_nonzero*/
    0,					/*nb_invert*/
    0,					/*nb_lshift*/
    0,					/*nb_rshift*/
    0,					/*nb_and*/
    0,					/*nb_xor*/
    0,					/*nb_or*/
    0,					/*nb_coerce*/
    mxDateTime_AsInt,			/*nb_int*/
    0,					/*nb_long*/
    mxDateTime_AsFloat,			/*nb_float*/
    0,					/*nb_oct*/
    0,					/*nb_hex*/
};

statichere
PyTypeObject mxDateTime_Type = {
    PyObject_HEAD_INIT(0)		/* init at startup ! */
    0,			  		/*ob_size*/
    "mx.DateTime.DateTime",		/*tp_name*/
    sizeof(mxDateTimeObject),      	/*tp_basicsize*/
    0,			  		/*tp_itemsize*/
    /* slots */
#ifndef WANT_SUBCLASSABLE_TYPES
    (destructor)mxDateTime_Free,	/*tp_dealloc*/
    0,		  			/*tp_print*/
    mxDateTime_Getattr,  		/*tp_getattr*/
    0,		  			/*tp_setattr*/
    mxDateTime_Compare,			/*tp_compare*/
    mxDateTime_Repr,	  		/*tp_repr*/
    &mxDateTime_TypeAsNumber, 		/*tp_as_number*/
    0,					/*tp_as_sequence*/
    0,					/*tp_as_mapping*/
    mxDateTime_Hash,			/*tp_hash*/
    0,					/*tp_call*/
    mxDateTime_Str,			/*tp_str*/
    0, 					/*tp_getattro*/
    0, 					/*tp_setattro*/
    0,					/*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_CHECKTYPES,		/*tp_flags*/
    0,					/* tp_doc */
    0,					/* tp_traverse */
    0,					/* tp_clear */
    mxDateTime_RichCompare,		/* tp_richcompare */
    0,					/* tp_weaklistoffset */
    0,					/* tp_iter */
    0,					/* tp_iternext */
    mxDateTime_Methods,			/* tp_methods */
    0,					/* tp_members */
    0,					/* tp_getset */
    0,					/* tp_base */
    0,					/* tp_dict */
    0,					/* tp_descr_get */
    0,					/* tp_descr_set */
    0,					/* tp_dictoffset */
    0,					/* tp_init */
    0,					/* tp_alloc */
    0,					/* tp_new */
    0,					/* tp_free */
#else
    (destructor)mxDateTime_Deallocate,	/*tp_dealloc*/
    0,				  	/*tp_print*/
    mxDateTime_Getattr,  		/*tp_getattr*/
    0,		  			/*tp_setattr*/
    mxDateTime_Compare,			/*tp_compare*/
    mxDateTime_Repr,	  		/*tp_repr*/
    &mxDateTime_TypeAsNumber, 		/*tp_as_number*/
    0,					/*tp_as_sequence*/
    0,					/*tp_as_mapping*/
    mxDateTime_Hash,			/*tp_hash*/
    0,					/*tp_call*/
    mxDateTime_Str,			/*tp_str*/
    PyObject_GenericGetAttr,		/*tp_getattro*/
    PyObject_GenericSetAttr,		/*tp_setattro*/
    0,					/*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE | Py_TPFLAGS_CHECKTYPES,	/* tp_flags */
    0,					/* tp_doc */
    0,					/* tp_traverse */
    0,					/* tp_clear */
    mxDateTime_RichCompare,		/* tp_richcompare */
    0,					/* tp_weaklistoffset */
    0,					/* tp_iter */
    0,					/* tp_iternext */
    mxDateTime_Methods,			/* tp_methods */
    0,					/* tp_members */
    mxDateTime_MemberAccess,		/* tp_getset */
    0,					/* tp_base */
    0,					/* tp_dict */
    0,					/* tp_descr_get */
    0,					/* tp_descr_set */
    0,					/* tp_dictoffset */
    mxDateTime_Initialize,		/* tp_init */
    mxDateTime_Allocate,		/* tp_alloc */
    PyType_GenericNew,			/* tp_new */
    (destructor)mxDateTime_Free,	/* tp_free */
    0,					/* tp_is_gc */
    0,					/* tp_bases */
    0,					/* tp_mro */
    0,					/* tp_cache */
    0,					/* tp_subclasses */
    0,					/* tp_weaklist */
#endif
};

/* --- DateTimeDelta Object -----------------------------------------*/

/* --- allocation --- */

static
mxDateTimeDeltaObject *mxDateTimeDelta_New(void)
{
    mxDateTimeDeltaObject *delta;

#ifdef MXDATETIMEDELTA_FREELIST
    if (mxDateTimeDelta_FreeList) {
	delta = mxDateTimeDelta_FreeList;
	mxDateTimeDelta_FreeList = \
	    *(mxDateTimeDeltaObject **)mxDateTimeDelta_FreeList;
	delta->ob_type = &mxDateTimeDelta_Type;
	_Py_NewReference((PyObject *)delta);
    }
    else
#endif	
	 {
	delta = PyObject_NEW(mxDateTimeDeltaObject,&mxDateTimeDelta_Type);
	if (delta == NULL)
	    return NULL;
    }

    return delta;
}

/* --- deallocation --- */

static
void mxDateTimeDelta_Free(mxDateTimeDeltaObject *delta)
{
    
#ifdef MXDATETIMEDELTA_FREELIST
# ifdef WANT_SUBCLASSABLE_TYPES
    if (_mxDateTimeDelta_CheckExact(delta))
# endif
	{
	    /* Append to free list */
	    *(mxDateTimeDeltaObject **)delta = mxDateTimeDelta_FreeList;
	    mxDateTimeDelta_FreeList = delta;
	}
#else
    PyObject_Del(delta);
#endif
}

/* --- internal functions --- */

/* We may have a need for this in the future: */

#define mxDateTimeDelta_SetFromDaysEx(delta,days,seconds) \
        mxDateTimeDelta_SetFromSeconds(delta, SECONDS_PER_DAY*days + seconds)

static
int mxDateTimeDelta_SetFromSeconds(mxDateTimeDeltaObject *delta,
				   double seconds)
{
    if (delta == NULL) {
	PyErr_BadInternalCall();
	goto onError;
    }

    /* Store the internal seconds value as-is */
    delta->seconds = seconds;

    /* The broken down values are always positive: force seconds to be
       positive. */
    if (DOUBLE_IS_NEGATIVE(seconds))
	seconds = -seconds;

    /* Range check */
    Py_Assert(seconds <= MAX_DATETIMEDELTA_SECONDS,
	      mxDateTime_RangeError,
	      "DateTimeDelta value out of range");

    /* Calculate the broken down time */
    {
	register long wholeseconds;
	long day;
	short hour,minute;
	double second;
	
	/* Calculate day part and then normalize seconds to be in the
	   range 0 <= seconds < 86400.0 */ 
	day = (long)(seconds / SECONDS_PER_DAY);
	seconds -= SECONDS_PER_DAY * (double)day;
	/* Some compilers (e.g. gcc 2.95.3 on Mandrake) have troubles
	   with getting rounding right even though 86400.0 IS exactly
	   representable using IEEE floats... */
	if (seconds >= SECONDS_PER_DAY) {
	    day++;
	    seconds -= SECONDS_PER_DAY;
	}
	/* We might still run into over/underflow errors, so report those as
	   RangeError */
	Py_AssertWithArg(seconds >= 0 && seconds <= SECONDS_PER_DAY + 1.0,
			 mxDateTime_RangeError,
			 "DateTimeDelta value out of range - "
			 "can't normalize seconds value: %i",
			 (int)seconds);
	/* Calculate the other parts based on the normalized seconds
           value */
	wholeseconds = (long)seconds;
	hour = (short)(wholeseconds / 3600);
	minute = (short)((wholeseconds % 3600) / 60);
	second = seconds - (double)(hour*3600 + minute*60);
	/* Fix a possible rounding error */
	if (DOUBLE_IS_NEGATIVE(second))
	    second = 0.0;

	DPRINTF("mxDateTimeDelta_SetFromSeconds: "
		"seconds=%f day=%ld hour=%i minute=%i second=%f\n",
		delta->seconds,day,hour,minute,second);

	delta->day = day;
	delta->hour = (signed char)hour;
	delta->minute = (signed char)minute;
	delta->second = second;
    }
    return 0;

 onError:
    return -1;
}

/* --- API functions --- */

statichere
PyObject *mxDateTimeDelta_FromDaysEx(long days,
				     double seconds)
{
    mxDateTimeDeltaObject *delta;
    
    delta = mxDateTimeDelta_New();
    if (delta == NULL)
	return NULL;

    if (mxDateTimeDelta_SetFromDaysEx(delta,days,seconds))
	goto onError;

    return (PyObject *)delta;
 onError:
    Py_DECREF(delta);
    return NULL;
}

statichere
PyObject *mxDateTimeDelta_FromSeconds(double seconds)
{
    mxDateTimeDeltaObject *delta;
    
    delta = mxDateTimeDelta_New();
    if (delta == NULL)
	return NULL;

    if (mxDateTimeDelta_SetFromSeconds(delta,seconds))
	goto onError;

    return (PyObject *)delta;
 onError:
    Py_DECREF(delta);
    return NULL;
}

static
PyObject *mxDateTimeDelta_FromDays(double days)
{
    mxDateTimeDeltaObject *delta;
    
    delta = mxDateTimeDelta_New();
    if (delta == NULL)
	return NULL;

    if (mxDateTimeDelta_SetFromSeconds(delta,days * SECONDS_PER_DAY))
	goto onError;

    return (PyObject *)delta;
 onError:
    Py_DECREF(delta);
    return NULL;
}

static
PyObject *mxDateTimeDelta_FromTime(int hours,
				   int minutes,
				   double seconds)
{
    mxDateTimeDeltaObject *delta;
    
    delta = mxDateTimeDelta_New();
    if (delta == NULL)
	return NULL;

    seconds += (double)(hours * 3600 + minutes * 60);

    if (mxDateTimeDelta_SetFromSeconds(delta,seconds))
	goto onError;

    return (PyObject *)delta;
 onError:
    Py_DECREF(delta);
    return NULL;
}

static
PyObject *mxDateTimeDelta_FromTuple(PyObject *v)
{
    mxDateTimeDeltaObject *delta = 0;
    int days;
    double seconds;

    if (!PyTuple_Check(v)) {
	PyErr_BadInternalCall();
	return NULL;
    }
    if (!PyArg_ParseTuple(v,
	"id;need a 2-tuple (days,seconds)",
			  &days,&seconds))
	return NULL;
    
    delta = mxDateTimeDelta_New();
    if (delta == NULL)
	return NULL;

    if (mxDateTimeDelta_SetFromDaysEx(delta,days,seconds))
	goto onError;

    return (PyObject *)delta;

 onError:
    Py_DECREF(delta);
    return NULL;
}

static
PyObject *mxDateTimeDelta_FromTimeTuple(PyObject *v)
{
    mxDateTimeDeltaObject *delta = 0;
    double hours,minutes,seconds;

    if (!PyTuple_Check(v)) {
	PyErr_BadInternalCall();
	return NULL;
    }
    if (!PyArg_ParseTuple(v,
	"ddd;need a 3-tuple (hours,minutes,seconds)",
			  &hours,&minutes,&seconds))
	return NULL;
    
    delta = mxDateTimeDelta_New();
    if (delta == NULL)
	return NULL;

    if (mxDateTimeDelta_SetFromSeconds(delta,
	     hours * 3600.0 + minutes * 60.0 + seconds))
	goto onError;

    return (PyObject *)delta;

 onError:
    Py_DECREF(delta);
    return NULL;
}

static
double mxDateTimeDelta_AsDouble(mxDateTimeDeltaObject *delta)
{
    return delta->seconds;
}

static
double mxDateTimeDelta_AsDays(mxDateTimeDeltaObject *delta)
{
    return delta->seconds / SECONDS_PER_DAY;
}

static
int mxDateTimeDelta_BrokenDown(mxDateTimeDeltaObject *delta,
			       long *day,
			       int *hour,
			       int *minute,
			       double *second)
{
    if (day)
	*day = (long)delta->day;
    if (hour)
	*hour = (int)delta->hour;
    if (minute)
	*minute = (int)delta->minute;
    if (second)
	*second = (double)delta->second;
    return 0;
}

/* Writes a string representation to buffer. If the string does not
   fit the buffer, nothing is written. */

static
void mxDateTimeDelta_AsString(mxDateTimeDeltaObject *self,
			      char *buffer,
			      int buffer_len)
{
    double second;

    if (!buffer || buffer_len < 50)
	return;
    second = mxDateTime_FixSecondDisplay(self->second);
    if (self->day != 0) {
	if (self->seconds >= 0.0)
	    sprintf(buffer,"%ld:%02i:%02i:%05.2f",
		    (long)self->day,(int)self->hour,
		    (int)self->minute,(float)second);
	else
	    sprintf(buffer,"-%ld:%02i:%02i:%05.2f",
		    (long)self->day,(int)self->hour,
		    (int)self->minute,(float)second);
    }
    else {
	if (self->seconds >= 0.0)
	    sprintf(buffer,"%02i:%02i:%05.2f",
		    (int)self->hour,(int)self->minute,(float)second);
	else
	    sprintf(buffer,"-%02i:%02i:%05.2f",
		    (int)self->hour,(int)self->minute,(float)second);
    }
}

/* --- methods --- (should have lowercase extension) */

#define delta ((mxDateTimeDeltaObject*)self)

#ifdef HAVE_STRFTIME
Py_C_Function( mxDateTimeDelta_strftime,
	       "strftime(formatstr)\n\n"
	       "Returns a formatted string of the time (ignoring the sign).\n"
	       "Of course, it only makes sense to use time related\n"
	       "specifiers. The delta sign is not taken into account.\n"
	       "All values are shown positive.")
{
    PyObject *v;
    struct tm tm;
    char *fmt;
    char *output = 0;
    Py_ssize_t len_output,size_output = STRFTIME_OUTPUT_SIZE;

    Py_GetArg("s",fmt);
    
    Py_Assert((long)((int)delta->day) == delta->day,
	      mxDateTime_RangeError,
	      "days out of range for strftime() formatting");

    /* Init to the epoch */
    memset(&tm,0,sizeof(tm));
    tm.tm_year = 0;

    /* Store the values in their appropriate places */
    tm.tm_mday = (int)delta->day;
    tm.tm_hour = (int)delta->hour;
    tm.tm_min = (int)delta->minute;
    tm.tm_sec = (int)delta->second;

#ifdef MS_WIN32
    if (_mxDateTime_CheckWindowsStrftime(fmt, &tm))
	goto onError;
#endif

    output = new(char,size_output);

    while (1) {
	if (output == NULL) {
	    PyErr_NoMemory();
	    goto onError;
	}
    	len_output = strftime(output,size_output,fmt,&tm);
	if (len_output == size_output) {
	    size_output *= 2;
	    output = resize(output,char,size_output);
	}
	else
	    break;
    }
    v = mxPyText_FromStringAndSize(output,len_output);
    if (v == NULL)
	goto onError;
    free(output);
    return v;

 onError:
    if (output)
	free(output);
    return NULL;
}
#endif

Py_C_Function( mxDateTimeDelta_absvalues,
	       "absvalues()\n\n"
	       "Return a (absdays,absseconds) tuple. The absseconds part is\n"
	       "normalized in such way that it is always < 86400.0. The\n"
	       "values can be used to do date/time calculations.\n"
	       "Both are signed.")
{
    long days;
    double seconds;
    
    Py_NoArgsCheck();

    seconds = delta->seconds;
    days = (long)(seconds / SECONDS_PER_DAY);
    seconds = seconds - SECONDS_PER_DAY * (double)days;
    return Py_BuildValue("ld",
			 days,
			 seconds);
    
 onError:
    return NULL;
}

Py_C_Function( mxDateTimeDelta_tuple,
	       "tuple()\n\n"
	       "Return a (day,hour,minute,second) tuple. The values are all\n"
	       "signed and use the same conventions as the attributes of\n"
	       "the same name.")
{
    Py_NoArgsCheck();
    if (!DOUBLE_IS_NEGATIVE(delta->seconds))
	return Py_BuildValue("liii",
			     (long)delta->day,
			     (int)delta->hour,
			     (int)delta->minute,
			     (int)delta->second);
    else
	return Py_BuildValue("liii",
			     -(long)delta->day,
			     -(int)delta->hour,
			     -(int)delta->minute,
			     -(int)delta->second);
    
 onError:
    return NULL;
}

Py_C_Function_WithKeywords( 
	       mxDateTimeDelta_rebuild,
	       "rebuild(day=None,hour=None,minute=None,second=None)\n\n"
	       "Returns a DateTimeDelta object with the given time values\n"
	       "replaced by new values."
	       )
{
    double day, hour, minute, second;
    
    /* Get the broken down values */
    day = (double) delta->day;
    hour = (double) delta->hour;
    minute = (double) delta->minute;
    second = delta->second;
    
    /* Override them with parameters */
    Py_KeywordsGet4Args("|dddd",day,hour,minute,second);

    /* Build new object */
    return mxDateTimeDelta_FromSeconds(day*SECONDS_PER_DAY
				       + hour*3600.0
				       + minute*60.0
				       + second);

 onError:
    return NULL;
}

#ifdef HAVE_PYDATETIME
Py_C_Function_WithKeywords( 
	       mxDateTimeDelta_pytime,
	       "pytime()\n\n"
	       "Returns a datetime.time object with the same values."
	       )
{
    int second, microsecond;
    
    Py_NoArgsCheck();

    /* Convert values */
    Py_Assert(delta->day == 0,
	      PyExc_ValueError,
	      "cannot convert DateTimeDelta spanning days "
	      "to a dateime.time object");
    second = (int)(delta->second);
    microsecond = (int)((delta->second - (double)second) * 1e6);
    
    /* Make sure the PyDateTimeAPI is loaded */
    if (mx_Require_PyDateTimeAPI())
	goto onError;

    /* Build new object */
    return PyTime_FromTime((int)delta->hour,
			   (int)delta->minute,
			   second,
			   microsecond);
 onError:
    return NULL;
}

Py_C_Function_WithKeywords( 
	       mxDateTimeDelta_pytimedelta,
	       "pytimedelta()\n\n"
	       "Returns a datetime.timedelta object with the same values."
	       )
{
    int days;
    double remaining_seconds;
    int seconds, microseconds;
    
    Py_NoArgsCheck();

    /* Convert values */
    days = (int)(delta->seconds / SECONDS_PER_DAY);
    remaining_seconds = delta->seconds - (double)days * SECONDS_PER_DAY;
    seconds = (int)remaining_seconds;
    microseconds = (int)((remaining_seconds - (double)seconds) * 1e6);
    
    /* Make sure the PyDateTimeAPI is loaded */
    if (mx_Require_PyDateTimeAPI())
	goto onError;

    /* Build new object */
    return PyDelta_FromDSU(days, seconds, microseconds);

 onError:
    return NULL;
}
#endif

#ifdef COPY_PROTOCOL
Py_C_Function( mxDateTimeDelta_copy,
	       "copy([memo])\n"
	       "Return a new reference for the instance. This function\n"
	       "is used for the copy-protocol. Real copying doesn't take\n"
	       "place, since the instances are immutable.")
{
    PyObject *memo;
    
    Py_GetArg("|O",memo);
    Py_INCREF(delta);
    return (PyObject *)delta;
 onError:
    return NULL;
}
#endif

#ifdef OLD_INTERFACE

/* Old interface */
Py_C_Function( mxDateTimeDelta_as_timetuple,
               "as_timetuple()\n\n"
               "Return a (hour,minute,second) tuple. The values are all\n"
               "signed.")
{
    Py_NoArgsCheck();
    if (!DOUBLE_IS_NEGATIVE(delta->seconds))
        return Py_BuildValue("iid",
                             (int)delta->hour,
                             (int)delta->minute,
                             delta->second);
    else
        return Py_BuildValue("iid",
                             -(int)delta->hour,
                             -(int)delta->minute,
                             -delta->second);
    
 onError:
    return NULL;
}

Py_C_Function( mxDateTimeDelta_as_ticks,
               "as_ticks()\n\n"
               "Return the objects value in seconds. Days are converted\n"
               "using 86400.0 seconds. The returned value is signed.")
{
    Py_NoArgsCheck();
    return PyFloat_FromDouble(mxDateTimeDelta_AsDouble(delta));
 onError:
    return NULL;
}
#endif

#undef delta

/* --- slots --- */

static
PyObject *mxDateTimeDelta_AsFloat(PyObject *obj)
{
    mxDateTimeDeltaObject *self = (mxDateTimeDeltaObject *)obj;
    
    return PyFloat_FromDouble(mxDateTimeDelta_AsDouble(self));
}

static
PyObject *mxDateTimeDelta_AsInt(PyObject *obj)
{
    mxDateTimeDeltaObject *self = (mxDateTimeDeltaObject *)obj;

    return PyInt_FromLong((long)mxDateTimeDelta_AsDouble(self));
}

static
PyObject *mxDateTimeDelta_Str(PyObject *obj)
{
    mxDateTimeDeltaObject *self = (mxDateTimeDeltaObject *)obj;
    char s[50];

    mxDateTimeDelta_AsString(self,s,sizeof(s));
    return mxPyText_FromString(s);
}

static
PyObject *mxDateTimeDelta_Repr(PyObject *obj)
{
    mxDateTimeDeltaObject *self = (mxDateTimeDeltaObject *)obj;
    char t[100];
    char s[50];

    mxDateTimeDelta_AsString(self, s, sizeof(s));
    sprintf(t,"<%s object for '%s' at %lx>", 
	    self->ob_type->tp_name, s, (long)self);
    return mxPyText_FromString(t);
}

static
PyObject *mxDateTimeDelta_Getattr(PyObject *obj,
				  char *name)
{
    mxDateTimeDeltaObject *self = (mxDateTimeDeltaObject *)obj;

    if (Py_WantAttr(name,"hour")) {
	if (!DOUBLE_IS_NEGATIVE(self->seconds))
	    return PyInt_FromLong((long)self->hour);
	else
	    return PyInt_FromLong(-(long)self->hour);
    }
    else if (Py_WantAttr(name,"minute")) {
	if (!DOUBLE_IS_NEGATIVE(self->seconds))
	    return PyInt_FromLong((long)self->minute);
	else
	    return PyInt_FromLong(-(long)self->minute);
    }
    else if (Py_WantAttr(name,"second")) {
	if (!DOUBLE_IS_NEGATIVE(self->seconds))
	    return PyFloat_FromDouble(self->second);
	else
	    return PyFloat_FromDouble(-self->second);
    }
    else if (Py_WantAttr(name,"day")) {
	if (!DOUBLE_IS_NEGATIVE(self->seconds))
	    return PyInt_FromLong((long)self->day);
	else
	    return PyInt_FromLong(-(long)self->day);
    }
    else if (Py_WantAttr(name,"seconds"))
	return PyFloat_FromDouble(self->seconds);

    else if (Py_WantAttr(name,"minutes"))
	return PyFloat_FromDouble(self->seconds / 60.0);

    else if (Py_WantAttr(name,"hours"))
	return PyFloat_FromDouble(self->seconds / 3600.0);

    else if (Py_WantAttr(name,"days"))
	return PyFloat_FromDouble(self->seconds / SECONDS_PER_DAY);
    
    /* For Zope security */
    else if (Py_WantAttr(name,"__roles__")) {
	Py_INCREF(Py_None);
	return Py_None;
    }
    else if (Py_WantAttr(name,"__allow_access_to_unprotected_subobjects__"))
	return PyInt_FromLong(1L);

    else if (Py_WantAttr(name,"__members__"))
	return Py_BuildValue("[ssssssss]",
			     "hour","minute","second",
			     "day","seconds","minutes",
			     "hours","days");

    return Py_FindMethod(mxDateTimeDelta_Methods,
			 (PyObject *)self,name);
}

static
long mxDateTimeDelta_Hash(PyObject *obj)
{
     mxDateTimeDeltaObject *self = (mxDateTimeDeltaObject *)obj;
    long x = 0;
    long z[sizeof(double)/sizeof(long)+1];
    register int i;
    
    /* Clear z */
    for (i = sizeof(z) / sizeof(long) - 1; i >= 0; i--)
	z[i] = 0;

    /* Copy the double onto z */
    *((double *)z) = self->seconds;

    /* Hash the longs in z together using XOR */
    for (i = sizeof(z) / sizeof(long) - 1; i >= 0; i--)
	x ^= z[i];

    /* Errors are indicated by returning -1, so we have to fix
       that hash value by hand. */
    if (x == -1)
	x = 19980428;

    return x;
}

static
int mxDateTimeDelta_Compare(PyObject *left,
			    PyObject *right)
{
    mxDateTimeDeltaObject *self = (mxDateTimeDeltaObject *)left;
    mxDateTimeDeltaObject *other = (mxDateTimeDeltaObject *)right;

    DPRINTF("mxDateTimeDelta_Compare: "
	    "%s op %s\n",
	    left->ob_type->tp_name,
	    right->ob_type->tp_name);

    if (self == other)
	return 0;

    /* Short-cut */
    if (_mxDateTimeDelta_Check(left) && _mxDateTimeDelta_Check(right)) {
	double i = self->seconds;
	double j = other->seconds;

	return (i < j) ? -1 : (i > j) ? 1 : 0;
    }

    Py_Error(PyExc_TypeError,
	     "can't compare types");

 onError:
    return -1;
}

static
PyObject *mxDateTimeDelta_RichCompare(PyObject *left,
				      PyObject *right,
				      int op)
{
    mxDateTimeDeltaObject *self = (mxDateTimeDeltaObject *)left;
    mxDateTimeDeltaObject *other = (mxDateTimeDeltaObject *)right;
    int cmp;

    DPRINTF("mxDateTimeDelta_RichCompare: "
	    "%s op %s (op=%i)\n",
	    left->ob_type->tp_name,
	    right->ob_type->tp_name,
	    op);

    /* Same type comparison */
    if (self == other)
	cmp = 0;

    else if (_mxDateTimeDelta_Check(left) && _mxDateTimeDelta_Check(right)) {
	double i = self->seconds;
	double j = other->seconds;

	cmp = (i < j) ? -1 : (i > j) ? 1 : 0;
    }

    /* Make sure that we only have to deal with DateTimeDelta op
       <other type> */
    else if (_mxDateTimeDelta_Check(right)) {
	return mxDateTimeDelta_RichCompare(right, left,
					   SWAPPED_ARGS_RICHCOMPARE_OP(op));
    }

    else if (!_mxDateTimeDelta_Check(left)) {
	Py_INCREF(Py_NotImplemented);
	return Py_NotImplemented;
    }

    else if (_mxDateTime_Check(right)) {
	/* DateTimeDelta op DateTime: not supported */
	Py_INCREF(Py_NotImplemented);
	return Py_NotImplemented;
    }

    else if (PyFloat_Compatible(right)) {
	/* DateTimeDelta op number -- compare seconds */
	double t1 = PyFloat_AsDouble(right);
	double t0 = mxDateTimeDelta_AsDouble(self);

	if (t1 == -1.0 && PyErr_Occurred()) {
	    /* Give up and let the right argument try to deal with the
	       operation. */
	    PyErr_Clear();
	    Py_INCREF(Py_NotImplemented);
	    return Py_NotImplemented;
	}
	if (t0 == -1.0 && PyErr_Occurred())
	    goto onError;

	cmp = (t0 < t1) ? -1 : (t0 > t1) ? 1 : 0;
    }
#ifdef HAVE_PYDATETIME
    else if (mx_PyDelta_Check(right)) {
	/* DateTimeDelta op PyDelta */
	double t0, t1;

	/* Make sure the PyDateTimeAPI is loaded */
	if (mx_Require_PyDateTimeAPI())
	    goto onError;

	t0 = mxDateTimeDelta_AsDouble(self);
	t1 = mx_PyDeltaInSeconds(right);
	if ((t0 == -1.0 || t1 == -1.0) && PyErr_Occurred())
	    goto onError;

	cmp = (t0 < t1) ? -1 : (t0 > t1) ? 1 : 0;
    }
    else if (mx_PyTime_Check(right)) {
	/* DateTimeDelta op PyTime */
	double t0, t1;

	/* Make sure the PyDateTimeAPI is loaded */
	if (mx_Require_PyDateTimeAPI())
	    goto onError;

	t0 = mxDateTimeDelta_AsDouble(self);
	t1 = mx_PyTimeInSeconds(right);
	if ((t0 == -1.0 || t1 == -1.0) && PyErr_Occurred())
	    goto onError;

	cmp = (t0 < t1) ? -1 : (t0 > t1) ? 1 : 0;
    }
#endif
    else {
	Py_INCREF(Py_NotImplemented);
	return Py_NotImplemented;
    }

    /* Build result */
    switch (op) {
    case Py_EQ:
        cmp = (cmp == 0);
        break;
    case Py_NE:
        cmp = (cmp != 0);
        break;
    case Py_LE:
	cmp = (cmp == -1) || (cmp == 0);
        break;
    case Py_GE:
	cmp = (cmp == 1) || (cmp == 0);
        break;
    case Py_LT:
	cmp = (cmp == -1);
        break;
    case Py_GT:
        cmp = (cmp == 1);
        break;
    }
    return PyBool_FromLong(cmp);
    
 onError:
    return NULL;
}

static
PyObject *mxDateTimeDelta_Negative(PyObject *obj)
{
    mxDateTimeDeltaObject *self = (mxDateTimeDeltaObject *)obj;

    return mxDateTimeDelta_FromSeconds(-self->seconds);
}

static
PyObject *mxDateTimeDelta_Positive(PyObject *obj)
{
    mxDateTimeDeltaObject *self = (mxDateTimeDeltaObject *)obj;

    Py_INCREF(self);
    return (PyObject *)self;
}

static
PyObject *mxDateTimeDelta_Abs(PyObject *obj)
{
    mxDateTimeDeltaObject *self = (mxDateTimeDeltaObject *)obj;

    if (self->seconds >= 0) {
	Py_INCREF(self);
	return (PyObject *)self;
    }
    else
	return mxDateTimeDelta_FromSeconds(-self->seconds);
}

static
int mxDateTimeDelta_NonZero(PyObject *obj)
{
    mxDateTimeDeltaObject *self = (mxDateTimeDeltaObject *)obj;

    return (self->seconds != (double)0.0);
}

static
PyObject *mxDateTimeDelta_Add(PyObject *left,
			      PyObject *right)
{
    mxDateTimeDeltaObject *self = (mxDateTimeDeltaObject *)left;
    mxDateTimeDeltaObject *other = (mxDateTimeDeltaObject *)right;

    DPRINTF("mxDateTimeDelta_Add: %s + %s\n",
	    left->ob_type->tp_name,
	    right->ob_type->tp_name);

    /* Make sure that we only have to deal with DateTimeDelta + <other
       type> */
    if (!_mxDateTimeDelta_Check(left)) {
	if (!_mxDateTimeDelta_Check(right)) {
	    Py_INCREF(Py_NotImplemented);
	    return Py_NotImplemented;
	}
	return mxDateTimeDelta_Add(right, left);
    }

    if (_mxDateTimeDelta_Check(right)) {
	/* DateTimeDelta + DateTimeDelta */
	return mxDateTimeDelta_FromSeconds(self->seconds + other->seconds);
    }
    else if (_mxDateTime_Check(right)) {
	/* DateTimeDelta + DateTime */
	return mxDateTime_Add(right, left);
    }
    else {
	double value;

	if (PyFloat_Compatible(right)) {
	    /* DateTimeDelta + Number (meaning seconds) */
	    value = PyFloat_AsDouble(right);
	    if (value == -1.0 && PyErr_Occurred()) {
		/* Give up and let the right argument try to deal with
    		   the operation. */
		PyErr_Clear();
		Py_INCREF(Py_NotImplemented);
		return Py_NotImplemented;
	    }
	}
#ifdef HAVE_PYDATETIME
	else if (mx_PyDelta_Check(right)) {
	    /* DateTimeDelta + PyDelta */

	    /* Make sure the PyDateTimeAPI is loaded */
	    if (mx_Require_PyDateTimeAPI())
		goto onError;

	    value = mx_PyDeltaInSeconds(right);
	}
	else if (mx_PyTime_Check(right)) {

    	    /* Make sure the PyDateTimeAPI is loaded */
	    if (mx_Require_PyDateTimeAPI())
		goto onError;

	    /* DateTimeDelta + PyTime */
	    value = mx_PyTimeInSeconds(right);
	}
#endif
	else {
	    Py_INCREF(Py_NotImplemented);
	    return Py_NotImplemented;
	}
	if (value < 0.0 && PyErr_Occurred())
	    goto onError;

	DPRINTF("mxDateTimeDelta_Add: adding %f seconds\n",
		value);

	if (value == 0.0) {
	    Py_INCREF(left);
	    return left;
	}
	return mxDateTimeDelta_FromSeconds(self->seconds + value);
    }

 onError:
    return NULL;
}

static
PyObject *mxDateTimeDelta_Sub(PyObject *left,
			      PyObject *right)
{
    mxDateTimeDeltaObject *self = (mxDateTimeDeltaObject *)left;
    mxDateTimeDeltaObject *other = (mxDateTimeDeltaObject *)right;

    DPRINTF("mxDateTimeDelta_Sub: %s - %s\n",
	    left->ob_type->tp_name,
	    right->ob_type->tp_name);

    if (_mxDateTimeDelta_Check(left)) {
	/* DateTimeDelta - <other type> */

	if (_mxDateTimeDelta_Check(right)) {
	    /* DateTimeDelta - DateTimeDelta */
	    return mxDateTimeDelta_FromSeconds(self->seconds - other->seconds);
	}
	else if (_mxDateTime_Check(right)) {
	    /* DateTimeDelta - DateTime: not supported */
	    Py_INCREF(Py_NotImplemented);
	    return Py_NotImplemented;
	}
	else {
	    double value;

	    if (PyFloat_Compatible(right)) {
		/* DateTimeDelta - Number (meaning seconds) */
		value = PyFloat_AsDouble(right);
		if (value == -1.0 && PyErr_Occurred()) {
		    /* Give up and let the right argument try to deal
		       with the operation. */
		    PyErr_Clear();
		    Py_INCREF(Py_NotImplemented);
		    return Py_NotImplemented;
		}
	    }
#ifdef HAVE_PYDATETIME
	    else if (mx_PyDelta_Check(right)) {
		/* DateTimeDelta - PyDelta */
		
		/* Make sure the PyDateTimeAPI is loaded */
		if (mx_Require_PyDateTimeAPI())
		    goto onError;

		value = mx_PyDeltaInSeconds(right);
	    }
	    else if (mx_PyTime_Check(right)) {
		
		/* Make sure the PyDateTimeAPI is loaded */
		if (mx_Require_PyDateTimeAPI())
		    goto onError;

		/* DateTimeDelta - PyTime */
		value = mx_PyTimeInSeconds(right);
	    }
#endif
	    else {
		Py_INCREF(Py_NotImplemented);
		return Py_NotImplemented;
	    }
	    if (value < 0.0 && PyErr_Occurred())
		goto onError;

	    DPRINTF("mxDateTimeDelta_Sub: subtracting %f seconds\n",
		    value);

	    if (value == 0.0) {
		Py_INCREF(left);
		return left;
	    }
	    return mxDateTimeDelta_FromSeconds(self->seconds - value);
	}
    }
    else if (_mxDateTimeDelta_Check(right)) {
	/* <other type> - DateTimeDelta */

	if (_mxDateTime_Check(left)) {
	    /* DateTime - DateTimeDelta */
	    return mxDateTime_Sub(left, right);
	}
	else {
	    double value;

	    if (PyFloat_Compatible(left)) {
		/* Number (meaning seconds) - DateTimeDelta: not
		   supported */
		Py_INCREF(Py_NotImplemented);
		return Py_NotImplemented;
	    }
#ifdef HAVE_PYDATETIME
	    else if (mx_PyDelta_Check(left)) {
		/* PyDelta - DateTimeDelta */

		/* Make sure the PyDateTimeAPI is loaded */
		if (mx_Require_PyDateTimeAPI())
		    goto onError;

		value = mx_PyDeltaInSeconds(left);
	    }
	    else if (mx_PyTime_Check(left)) {

		/* Make sure the PyDateTimeAPI is loaded */
		if (mx_Require_PyDateTimeAPI())
		    goto onError;

		/* PyTime - DateTimeDelta */
		value = mx_PyTimeInSeconds(left);
	    }
#endif
	    else {
		Py_INCREF(Py_NotImplemented);
		return Py_NotImplemented;
	    }
	    if (value < 0.0 && PyErr_Occurred())
		goto onError;

	    if (value == 0.0) {
		Py_INCREF(right);
		return right;
	    }
	    return mxDateTimeDelta_FromSeconds(value - other->seconds);
	}
    }
    else {
	Py_INCREF(Py_NotImplemented);
	return Py_NotImplemented;
    }

 onError:
    return NULL;
}

static
PyObject *mxDateTimeDelta_Multiply(PyObject *left,
				   PyObject *right)
{
    mxDateTimeDeltaObject *self = (mxDateTimeDeltaObject *)left;

    DPRINTF("mxDateTimeDelta_Multiply: %s * %s\n",
	    left->ob_type->tp_name,
	    right->ob_type->tp_name);

    if (_mxDateTimeDelta_Check(left)) {
	/* DateTimeDelta * <other type> */

	if (_mxDateTimeDelta_Check(right)) {
	    /* DateTimeDelta * DateTimeDelta: not supported */
	    Py_INCREF(Py_NotImplemented);
	    return Py_NotImplemented;
	}
	else if (_mxDateTime_Check(right)) {
	    /* DateTimeDelta * DateTime: not supported */
	    Py_INCREF(Py_NotImplemented);
	    return Py_NotImplemented;
	}
	else if (PyFloat_Compatible(right)) {
	    /* DateTimeDelta * Number */
	    double value = PyFloat_AsDouble(right);

	    if (value == -1.0 && PyErr_Occurred()) {
		/* Give up and let the right argument try to deal with
    		   the operation. */
		PyErr_Clear();
		Py_INCREF(Py_NotImplemented);
		return Py_NotImplemented;
	    }

	    if (value == 1.0) {
		Py_INCREF(self);
		return (PyObject *)self;
	    }
	    return mxDateTimeDelta_FromSeconds(self->seconds * value);
	}
	else {
	    Py_INCREF(Py_NotImplemented);
	    return Py_NotImplemented;
	}
    }
    else if (_mxDateTimeDelta_Check(right)) {
	/* <other type> * DateTimeDelta: same as DateTimeDelta *
	   <other type> */
	return mxDateTimeDelta_Multiply(right, left);
    }
    else {
	Py_INCREF(Py_NotImplemented);
	return Py_NotImplemented;
    }
    /* Silence compiler warning */
    goto onError;

 onError:
    return NULL;
}

static
PyObject *mxDateTimeDelta_Divide(PyObject *left,
				 PyObject *right)
{
    mxDateTimeDeltaObject *self = (mxDateTimeDeltaObject *)left;
    mxDateTimeDeltaObject *other = (mxDateTimeDeltaObject *)right;
    double value;

    DPRINTF("mxDateTimeDelta_Divide: %s / %s\n",
	    left->ob_type->tp_name,
	    right->ob_type->tp_name);

    if (_mxDateTimeDelta_Check(left)) {
	/* DateTimeDelta / <other type> */

	if (_mxDateTimeDelta_Check(right)) {
	    /* DateTimeDelta / DateTimeDelta */
	    value = other->seconds;
	}
	else if (_mxDateTime_Check(right)) {
	    /* DateTimeDelta / DateTime: not supported */
	    Py_INCREF(Py_NotImplemented);
	    return Py_NotImplemented;
	}
#ifdef HAVE_PYDATETIME
	else if (mx_PyDelta_Check(right)) {
	    /* DateTimeDelta / PyDelta */
	    
	    /* Make sure the PyDateTimeAPI is loaded */
	    if (mx_Require_PyDateTimeAPI())
		goto onError;

	    value = mx_PyDeltaInSeconds(right);
	}
	else if (mx_PyTime_Check(right)) {
	    
	    /* Make sure the PyDateTimeAPI is loaded */
	    if (mx_Require_PyDateTimeAPI())
		goto onError;

	    /* DateTimeDelta / PyTime */
	    value = mx_PyTimeInSeconds(right);
	}
#endif
	else if (PyFloat_Compatible(right)) {
	    /* DateTimeDelta / Number */
	    value = PyFloat_AsDouble(right);
	
	    if (value == -1.0 && PyErr_Occurred()) {
		/* Give up and let the right argument try to deal with
    		   the operation. */
		PyErr_Clear();
		Py_INCREF(Py_NotImplemented);
		return Py_NotImplemented;
	    }
	    Py_Assert(value != 0.0,
		      PyExc_ZeroDivisionError,
		      "DateTimeDelta zero division");
	    if (value == 1.0) {
		Py_INCREF(left);
		return left;
	    }
	    return mxDateTimeDelta_FromSeconds(self->seconds / value);
	}
	else {
	    Py_INCREF(Py_NotImplemented);
	    return Py_NotImplemented;
	}
	if (value < 0.0 && PyErr_Occurred())
	    goto onError;
	Py_Assert(value != 0.0,
		  PyExc_ZeroDivisionError,
		  "DateTimeDelta zero division");
	return PyFloat_FromDouble(self->seconds / value);
    }
    else if (_mxDateTimeDelta_Check(right)) {
	/* <other type> / DateTimeDelta */

	if (_mxDateTime_Check(left)) {
	    /* DateTime / DateTimeDelta: not supported */
	    Py_INCREF(Py_NotImplemented);
	    return Py_NotImplemented;
	}
#ifdef HAVE_PYDATETIME
	else if (mx_PyDelta_Check(left)) {
	    /* PyDelta / DateTimeDelta */
	    
	    /* Make sure the PyDateTimeAPI is loaded */
	    if (mx_Require_PyDateTimeAPI())
		goto onError;
	
	    value = mx_PyDeltaInSeconds(left);
	}
	else if (mx_PyTime_Check(left)) {
	    
	    /* Make sure the PyDateTimeAPI is loaded */
	    if (mx_Require_PyDateTimeAPI())
		goto onError;
	
	    /* PyTime / DateTimeDelta */
	    value = mx_PyTimeInSeconds(left);
	}
#endif
	else if (PyFloat_Compatible(left)) {
	    /* Number / DateTimeDelta: not supported */
	    Py_INCREF(Py_NotImplemented);
	    return Py_NotImplemented;
	}
	else {
	    Py_INCREF(Py_NotImplemented);
	    return Py_NotImplemented;
	}
	if (value < 0.0 && PyErr_Occurred())
	    goto onError;
	Py_Assert(other->seconds != 0.0,
		  PyExc_ZeroDivisionError,
		  "DateTimeDelta zero division");
	return PyFloat_FromDouble(value / other->seconds);
    }
    else {
	Py_INCREF(Py_NotImplemented);
	return Py_NotImplemented;
    }

 onError:
    return NULL;
}

/* Python Type Tables */

static 
PyNumberMethods mxDateTimeDelta_TypeAsNumber = {

    /* These slots are not NULL-checked, so we must provide dummy functions */
    mxDateTimeDelta_Add,			/*nb_add*/
    mxDateTimeDelta_Sub,			/*nb_subtract*/
    mxDateTimeDelta_Multiply,			/*nb_multiply*/
    mxDateTimeDelta_Divide,			/*nb_divide*/
    notimplemented2,				/*nb_remainder*/
    notimplemented2,				/*nb_divmod*/
    notimplemented3,				/*nb_power*/
    mxDateTimeDelta_Negative,			/*nb_negative*/
    mxDateTimeDelta_Positive,			/*nb_positive*/

    /* Everything below this line EXCEPT nb_nonzero (!) is NULL checked */
    mxDateTimeDelta_Abs,			/*nb_absolute*/
    mxDateTimeDelta_NonZero,			/*nb_nonzero*/
    0,						/*nb_invert*/
    0,						/*nb_lshift*/
    0,						/*nb_rshift*/
    0,						/*nb_and*/
    0,						/*nb_xor*/
    0,						/*nb_or*/
    0,						/*nb_coerce*/
    mxDateTimeDelta_AsInt,			/*nb_int*/
    0,						/*nb_long*/
    mxDateTimeDelta_AsFloat,			/*nb_float*/
    0,						/*nb_oct*/
    0,						/*nb_hex*/
};

statichere
PyTypeObject mxDateTimeDelta_Type = {
    PyObject_HEAD_INIT(0)		/* init at startup ! */
    0,			  		/*ob_size*/
    "mx.DateTime.DateTimeDelta",	/*tp_name*/
    sizeof(mxDateTimeDeltaObject),   	/*tp_basicsize*/
    0,			  		/*tp_itemsize*/
    /* slots */
    (destructor)mxDateTimeDelta_Free,	/*tp_dealloc*/
    0,  				/*tp_print*/
    mxDateTimeDelta_Getattr,  		/*tp_getattr*/
    0,		  			/*tp_setattr*/
    mxDateTimeDelta_Compare,  		/*tp_compare*/
    mxDateTimeDelta_Repr,		/*tp_repr*/
    &mxDateTimeDelta_TypeAsNumber,	/*tp_as_number*/
    0,					/*tp_as_sequence*/
    0,					/*tp_as_mapping*/
    mxDateTimeDelta_Hash,		/*tp_hash*/
    0,					/*tp_call*/
    mxDateTimeDelta_Str,		/*tp_str*/
    0, 					/*tp_getattro*/
    0, 					/*tp_setattro*/
    0,					/*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_CHECKTYPES,		/*tp_flags*/
    0,					/* tp_doc */
    0,					/* tp_traverse */
    0,					/* tp_clear */
    mxDateTimeDelta_RichCompare,	/* tp_richcompare */
    0,					/* tp_weaklistoffset */
    0,					/* tp_iter */
    0,					/* tp_iternext */
    mxDateTimeDelta_Methods,		/* tp_methods */
    0,					/* tp_members */
    0,					/* tp_getset */
    0,					/* tp_base */
    0,					/* tp_dict */
    0,					/* tp_descr_get */
    0,					/* tp_descr_set */
    0,					/* tp_dictoffset */
    0,					/* tp_init */
    0,					/* tp_alloc */
    0,					/* tp_new */
    0,					/* tp_free */
};

/* Python Method Table */

statichere
PyMethodDef mxDateTimeDelta_Methods[] =
{   
    Py_MethodListEntryNoArgs("absvalues",mxDateTimeDelta_absvalues),
    Py_MethodListEntryNoArgs("tuple",mxDateTimeDelta_tuple),
#ifdef HAVE_STRFTIME
    Py_MethodListEntry("strftime",mxDateTimeDelta_strftime),
#endif
    Py_MethodWithKeywordsListEntry("rebuild",mxDateTimeDelta_rebuild),
#ifdef HAVE_PYDATETIME
    Py_MethodListEntryNoArgs("pytime",mxDateTimeDelta_pytime),
    Py_MethodListEntryNoArgs("pytimedelta",mxDateTimeDelta_pytimedelta),
#endif
#ifdef COPY_PROTOCOL
    Py_MethodListEntry("__deepcopy__",mxDateTimeDelta_copy),
    Py_MethodListEntry("__copy__",mxDateTimeDelta_copy),
#endif
#ifdef OLD_INTERFACE
    /* Old interface */
    Py_MethodListEntryNoArgs("as_tuple",mxDateTimeDelta_absvalues),
    Py_MethodListEntryNoArgs("as_timetuple",mxDateTimeDelta_as_timetuple),
    Py_MethodListEntryNoArgs("as_ticks",mxDateTimeDelta_as_ticks),
#endif
    {NULL,NULL} /* end of list */
};

/* --- Other functions ----------------------------------------------------- */

Py_C_Function( mxDateTime_DateTime,
	       "DateTime(year,month=1,day=1,hour=0,minute=0,second=0.0)\n\n"
	       "Returns a DateTime-object reflecting the given date\n"
	       "and time. Seconds can be given as float to indicate\n"
	       "fractions. Note that the function does not accept keyword args."
	       )
{
    long year;
    int month = 1,
	day = 1;
    int hour = 0,
	minute = 0;
    double second = 0.0;
    
    Py_Get6Args("l|iiiid",year,month,day,hour,minute,second);
    return mxDateTime_FromDateAndTime(year,month,day,hour,minute,second);
 onError:
    return NULL;
}

Py_C_Function( mxDateTime_JulianDateTime,
	       "JulianDateTime(year,month=1,day=1,hour=0,minute=0,second=0.0)\n\n"
	       "Returns a DateTime-object reflecting the given Julian date\n"
	       "and time. Seconds can be given as float to indicate\n"
	       "fractions.  Note that the function does not accept keyword args."
	       )
{
    long year;
    int month = 1,
	day = 1;
    int hour = 0,
	minute = 0;
    double second = 0.0;
    
    Py_Get6Args("l|iiiid",year,month,day,hour,minute,second);
    return mxDateTime_FromJulianDateAndTime(year,month,day,hour,minute,second);
 onError:
    return NULL;
}

Py_C_Function( mxDateTime_DateTimeFromAbsDateTime,
	       "DateTimeFromAbsDateTime(absdate[,abstime=0.0,calendar=Gregorian])\n\n"
	       "Returns a DateTime-object for the given absolute values.\n"
	       "Note that the function does not accept keyword args.")
{
    long absdate;
    double abstime = 0.0;
    char *calendar_name = NULL;
    int calendar;

    Py_Get3Args("l|ds", absdate, abstime, calendar_name);

    /* Convert calendar name to calendar integer; XXX Should turn this
       into a helper function */
    if (calendar_name) {
	if (Py_StringsCompareEqual(calendar_name,
				   MXDATETIME_GREGORIAN_CALENDAR_STRING))
	    calendar = MXDATETIME_GREGORIAN_CALENDAR;
	else if (Py_StringsCompareEqual(calendar_name,
				   MXDATETIME_JULIAN_CALENDAR_STRING))
	    calendar = MXDATETIME_JULIAN_CALENDAR;
	else {
	    Py_ErrorWithArg(PyExc_ValueError,
			    "unsupported calendar name: %s",
			    calendar_name);
	}
    }
    else
	calendar = MXDATETIME_GREGORIAN_CALENDAR;

    return mxDateTime_FromAbsDateTime(absdate, abstime, calendar);
 onError:
    return NULL;
}

#ifdef HAVE_STRPTIME
Py_C_Function( mxDateTime_strptime,
	       "strptime(str,formatstr,default=None)\n\n"
	       "Returns a DateTime-object reflecting the parsed\n"
	       "date and time; default can be given to set default values\n"
	       "for parts not given in the string. If not given,\n"
	       "1.1.0001 0:00:00.00 is used instead."
	       )
{
    char *str;
    char *fmt;
    char *lastchr;
    int len_str,pos;
    struct tm tm;
    PyObject *defvalue = NULL;

    Py_Get3Args("ss|O",str,fmt,defvalue);
    
    len_str = strlen(str);
    if (defvalue) {
	Py_Assert(_mxDateTime_Check(defvalue),
		  PyExc_TypeError,
		  "default must be a DateTime instance");
	if (!mxDateTime_AsTmStruct((mxDateTimeObject *)defvalue, &tm))
	    goto onError;
    }
    else {
	/* Init to 1.1.0001 0:00:00.00 */
	memset(&tm,0,sizeof(tm));
	tm.tm_mday = 1;
	tm.tm_year = -1899;
    }

    /* Parse */
    lastchr = strptime(str, fmt, &tm);
    Py_Assert(lastchr != NULL,
	      mxDateTime_Error,
	      "strptime() parsing error");
    pos = (int)(lastchr - str);
    if (pos != len_str)
	Py_ErrorWith2Args(mxDateTime_Error,
			  "strptime() parsing error at position %i: '%.200s'",
			  pos, str);
    return mxDateTime_FromTmStruct(&tm);
    
 onError:
    return NULL;
}
#endif

Py_C_Function( mxDateTime_DateTimeFromCOMDate,
	       "DateTimeFromCOMDate(comdate)\n\n"
	       "Returns a DateTime-object reflecting the given date\n"
	       "and time.")
{
    double comdate;
    
    Py_GetArg("d",comdate);
    return mxDateTime_FromCOMDate(comdate);
 onError:
    return NULL;
}

#ifdef OLD_INTERFACE
Py_C_Function( mxDateTime_DateTimeFromTicks,
	       "DateTimeFromTicks(ticks)\n\n"
	       "Returns a DateTime-object reflecting the given time\n"
	       "value. Conversion is done to local time (similar to\n"
	       "time.localtime()).")
{
    double ticks;
    
    Py_GetArg("d",ticks);
    return mxDateTime_FromTicks(ticks);
 onError:
    return NULL;
}
#endif

Py_C_Function( mxDateTime_DateTimeFromAbsDays,
	       "DateTimeFromAbsDays(absdays)\n\n"
	       "Returns a DateTime-object reflecting the given time\n"
	       "value (days since the epoch).")
{
    double absdays;
    
    Py_GetArg("d",absdays);
    return mxDateTime_FromAbsDays(absdays);
 onError:
    return NULL;
}

Py_C_Function( mxDateTime_DateTimeDelta,
	       "DateTimeDelta(days[,hours=0.0,minutes=0.0,seconds=0.0])\n\n"
	       "Returns a DateTimeDelta-object reflecting the given\n"
	       "day and time delta.  Note that the function does not accept\n"
	       "keyword args."
	       )
{
    double days,
	hours = 0.0, 
	minutes = 0.0,
	seconds = 0.0;
    
    Py_Get4Args("d|ddd",days,hours,minutes,seconds);

    return mxDateTimeDelta_FromSeconds(days*SECONDS_PER_DAY
				       + hours*3600.0
				       + minutes*60.0
				       + seconds);
 onError:
    return NULL;
}

#if OLD_INTERFACE
Py_C_Function( mxDateTime_TimeDelta,
	       "TimeDelta(hours[,minutes=0,seconds=0.0])\n\n"
	       "Returns a DateTimeDelta-object reflecting the given\n"
	       "time delta. Seconds can be given as float to indicate\n"
	       "fractions. Note that the function does not accept keyword args."
	       )
{
    double hour, 
	minute=0.0,
	second=0.0;
    
    Py_Get3Args("d|dd",hour,minute,second);
    return mxDateTimeDelta_FromSeconds(hour*3600.0
				       + minute*60.0
				       + second);
 onError:
    return NULL;
}
#endif

Py_C_Function( mxDateTime_DateTimeDeltaFromSeconds,
	       "DateTimeDeltaFromSeconds(seconds)\n\n"
	       "Returns a DateTimeDelta-object reflecting the given time\n"
	       "value.")
{
    double seconds;
    
    Py_GetArg("d",seconds);
    return mxDateTimeDelta_FromSeconds(seconds);
 onError:
    return NULL;
}

Py_C_Function( mxDateTime_DateTimeDeltaFromDays,
	       "DateTimeDeltaFromDays(days)\n\n"
	       "Returns a DateTimeDelta-object reflecting the given time\n"
	       "value given in days (fractions are allowed).")
{
    double days;
    
    Py_GetArg("d",days);
    return mxDateTimeDelta_FromDays(days);
 onError:
    return NULL;
}

Py_C_Function( mxDateTime_now,
	       "now()\n\n"
	       "Returns a DateTime-object reflecting the current local time."
	       )
{
    double fticks;

    Py_NoArgsCheck();
    fticks = mxDateTime_GetCurrentTime();
    if (fticks == -1 && PyErr_Occurred())
	goto onError;
    return mxDateTime_FromTicks(fticks);

 onError:
    return NULL;
}

Py_C_Function( mxDateTime_utc,
	       "utc()\n\n"
	       "Returns a DateTime-object reflecting the current UTC time."
	       )
{
    double fticks;

    Py_NoArgsCheck();
    fticks = mxDateTime_GetCurrentTime();
    if (fticks == -1 && PyErr_Occurred())
	goto onError;
    return mxDateTime_FromGMTicks(fticks);

 onError:
    return NULL;
}

Py_C_Function( mxDateTime_cmp,
	       "cmp(value1,value2[,accuracy=0.0])\n\n"
	       "Compares two DateTime[Delta] objects. If accuracy is\n"
	       "given, then equality will result in case the absolute\n"
	       "difference between the two values is less than or equal\n"
	       "to accuracy.")
{
    PyObject *a,*b;
    double acc = 0.0;
    
    Py_Get3Args("OO|d",a,b,acc);

    if (_mxDateTime_Check(a) && _mxDateTime_Check(b)) {
	/* cmp(DateTime,DateTime) */
	register long datediff = ((mxDateTimeObject *)b)->absdate -
	                         ((mxDateTimeObject *)a)->absdate;
	register double timediff = ((mxDateTimeObject *)b)->abstime -
	                         ((mxDateTimeObject *)a)->abstime;

	if ((datediff >= 0 && datediff <= (long)(acc / SECONDS_PER_DAY)) ||
	    (datediff < 0 && -datediff <= (long)(acc / SECONDS_PER_DAY))) {
	    if ((!DOUBLE_IS_NEGATIVE(timediff) && timediff <= acc) ||
		(DOUBLE_IS_NEGATIVE(timediff) && -timediff <= acc))
		return PyInt_FromLong(0l);
	    else if (DOUBLE_IS_NEGATIVE(timediff))
		return PyInt_FromLong(1l);
	    else
		return PyInt_FromLong(-1l);
	}
	else if (datediff < 0)
	    return PyInt_FromLong(1l);
	else
	    return PyInt_FromLong(-1l);
    }

    else if (_mxDateTimeDelta_Check(a) && _mxDateTimeDelta_Check(b)) {
	/* cmp(DateTimeDelta,DateTimeDelta) */
	register double timediff = ((mxDateTimeDeltaObject *)b)->seconds -
	                           ((mxDateTimeDeltaObject *)a)->seconds;
	
	if ((!DOUBLE_IS_NEGATIVE(timediff) && timediff <= acc) ||
	    (DOUBLE_IS_NEGATIVE(timediff) && -timediff <= acc))
	    return PyInt_FromLong(0l);
	else if (DOUBLE_IS_NEGATIVE(timediff))
	    return PyInt_FromLong(1l);
	else
	    return PyInt_FromLong(-1l);
    }

    else
	Py_Error(PyExc_TypeError,
		 "objects must be DateTime[Delta] instances");

 onError:
    return NULL;
}

Py_C_Function( mxDateTime_setnowapi,
	       "setnowapi(fct)\n\n"
	       "Sets the current time API used by now(). This must be\n"
	       "a callable function which returns the current local time in\n"
	       "Unix ticks."
	       )
{
    PyObject *v;

    Py_GetArg("O",v);

    Py_Assert(PyCallable_Check(v),
	      PyExc_TypeError,
	      "argument must be callable");

    Py_INCREF(v);
    mxDateTime_nowapi = v;

    Py_ReturnNone();
 onError:
    return NULL;
}

/* --- module interface ---------------------------------------------------- */

/* Python Method Table */

static 
PyMethodDef Module_methods[] =
{   
    Py_MethodListEntryNoArgs("now",mxDateTime_now),
    Py_MethodListEntry("DateTime",mxDateTime_DateTime),
    Py_MethodListEntry("DateTimeDelta",mxDateTime_DateTimeDelta),
#ifdef HAVE_STRPTIME
    Py_MethodListEntry("strptime",mxDateTime_strptime),
#endif
    Py_MethodListEntry("DateTimeFromCOMDate",mxDateTime_DateTimeFromCOMDate),
    Py_MethodListEntry("DateTimeFromAbsDateTime",mxDateTime_DateTimeFromAbsDateTime),
    Py_MethodListEntry("DateTimeFromAbsDays",mxDateTime_DateTimeFromAbsDays),
    Py_MethodListEntry("DateTimeDeltaFromSeconds",mxDateTime_DateTimeDeltaFromSeconds),
    Py_MethodListEntry("DateTimeDeltaFromDays",mxDateTime_DateTimeDeltaFromDays),
    Py_MethodListEntry("cmp",mxDateTime_cmp),
    Py_MethodListEntryNoArgs("utc",mxDateTime_utc),
    Py_MethodListEntry("JulianDateTime",mxDateTime_JulianDateTime),
    Py_MethodListEntry("setnowapi",mxDateTime_setnowapi),
#ifdef OLD_INTERFACE
    Py_MethodListEntry("Date",mxDateTime_DateTime),
    Py_MethodListEntry("Time",mxDateTime_TimeDelta),
    Py_MethodListEntry("Timestamp",mxDateTime_DateTime),
    Py_MethodListEntry("TimeDelta",mxDateTime_TimeDelta),
    Py_MethodListEntry("DateTimeFromTicks",mxDateTime_DateTimeFromTicks),
    Py_MethodListEntry("DateTimeDeltaFromTicks",mxDateTime_DateTimeDeltaFromSeconds),
#endif
    {NULL,NULL} /* end of list */
};

/* C API table - always add new things to the end for binary
   compatibility. */
static
mxDateTimeModule_APIObject mxDateTimeModuleAPI =
{
    &mxDateTime_Type,
    mxDateTime_FromAbsDateAndTime,
    mxDateTime_FromTuple,
    mxDateTime_FromDateAndTime,
    mxDateTime_FromTmStruct,
    mxDateTime_FromTicks,
    mxDateTime_FromCOMDate,
    mxDateTime_AsTmStruct,
    mxDateTime_AsTicks,
    mxDateTime_AsCOMDate,
    &mxDateTimeDelta_Type,
    mxDateTimeDelta_FromDaysEx,
    mxDateTimeDelta_FromTime,
    mxDateTimeDelta_FromTuple,
    mxDateTimeDelta_FromTimeTuple,
    mxDateTimeDelta_AsDouble,
    mxDateTime_FromAbsDays,
    mxDateTime_AsAbsDays,
    mxDateTimeDelta_FromDays,
    mxDateTimeDelta_AsDays,
    mxDateTime_BrokenDown,
    mxDateTimeDelta_BrokenDown,
    mxDateTime_FromAbsDateTime,
};

/* Cleanup function */
static 
void mxDateTimeModule_Cleanup(void)
{
#ifdef MXDATETIME_FREELIST
    {
	mxDateTimeObject *d = mxDateTime_FreeList;
	while (d != NULL) {
	    mxDateTimeObject *v = d;
	    d = *(mxDateTimeObject **)d;
	    PyObject_Del(v);
	}
	mxDateTime_FreeList = NULL;
    }
#endif
#ifdef MXDATETIMEDELTA_FREELIST
    {
	mxDateTimeDeltaObject *d = mxDateTimeDelta_FreeList;
	while (d != NULL) {
	    mxDateTimeDeltaObject *v = d;
	    d = *(mxDateTimeDeltaObject **)d;
	    PyObject_Del(v);
	}
	mxDateTimeDelta_FreeList = NULL;
    }
#endif
    /* XXX Calling Py_DECREF() in a Py_AtExit() function is dangerous. */
#if 1
    /* Drop reference to the now API callable. */
    Py_XDECREF(mxDateTime_nowapi);
    mxDateTime_nowapi = NULL;
#endif
#ifdef HAVE_PYDATETIME
    mx_Reset_PyDateTimeAPI();
#endif
    /* Reset mxDateTime_Initialized flag */
    mxDateTime_Initialized = 0;
}

/* Create PyMethodObjects and register them in the module's dict */
MX_EXPORT(void) 
     initmxDateTime(void)
{
    PyObject *module, *moddict;

    if (mxDateTime_Initialized)
	Py_Error(PyExc_SystemError,
		 "can't initialize "MXDATETIME_MODULE" more than once");

    /* Init type objects */
    PyType_Init(mxDateTime_Type);
    PyType_Init(mxDateTimeDelta_Type);

    /* Init globals */
    mxDateTime_POSIXConform = mxDateTime_POSIX();
#ifdef MXDATETIME_FREELIST
    mxDateTime_FreeList = NULL;
#endif
#ifdef MXDATETIMEDELTA_FREELIST
    mxDateTimeDelta_FreeList = NULL;
#endif
    mxDateTime_DoubleStackProblem = mxDateTime_CheckDoubleStackProblem(
					   SECONDS_PER_DAY - (double)7.27e-12);

    /* Create module */
    module = Py_InitModule4(MXDATETIME_MODULE, /* Module name */
			    Module_methods, /* Method list */
			    Module_docstring, /* Module doc-string */
			    (PyObject *)NULL, /* always pass this as *self */
			    PYTHON_API_VERSION); /* API Version */
    if (module == NULL)
	goto onError;

    /* Register cleanup function */
    if (Py_AtExit(mxDateTimeModule_Cleanup)) {
	/* XXX what to do if we can't register that function ??? */
	DPRINTF("* Failed to register mxDateTime cleanup function\n");
    }

    /* Add some constants to the module's dict */
    moddict = PyModule_GetDict(module);
    if (moddict == NULL)
	goto onError;
    insobj(moddict,"__version__",mxPyText_FromString(MXDATETIME_VERSION));
    insint(moddict,"POSIX",mxDateTime_POSIXConform);
#ifdef USE_FAST_GETCURRENTTIME
    /* Clock resolution */
    insobj(moddict,
	   "now_resolution",
	   PyFloat_FromDouble(mxDateTime_GetClockResolution()));
#endif

    /* Calendar string constants */
    if (!(mxDateTime_GregorianCalendar = mxPyText_FromString(
	      MXDATETIME_GREGORIAN_CALENDAR_STRING)))
	goto onError;
    mxPyText_InternInPlace(&mxDateTime_GregorianCalendar);
    PyDict_SetItemString(moddict,
			 MXDATETIME_GREGORIAN_CALENDAR_STRING,
			 mxDateTime_GregorianCalendar);
    if (!(mxDateTime_JulianCalendar = mxPyText_FromString(
	      MXDATETIME_JULIAN_CALENDAR_STRING)))
	goto onError;
    mxPyText_InternInPlace(&mxDateTime_JulianCalendar);
    PyDict_SetItemString(moddict,
			 MXDATETIME_JULIAN_CALENDAR_STRING,
			 mxDateTime_JulianCalendar);

    /* Errors */
    if (!(mxDateTime_Error = insexc(moddict,"Error",PyExc_ValueError)))
	goto onError;
    if (!(mxDateTime_RangeError = insexc(moddict,"RangeError",
					 mxDateTime_Error)))
	goto onError;

    /* Type objects */
    Py_INCREF(&mxDateTime_Type);
    PyDict_SetItemString(moddict,"DateTimeType",
			 (PyObject *)&mxDateTime_Type);
    Py_INCREF(&mxDateTimeDelta_Type);
    PyDict_SetItemString(moddict,"DateTimeDeltaType",
			 (PyObject *)&mxDateTimeDelta_Type);

    /* Export C API; many thanks to Jim Fulton for pointing this out to me */
    insobj(moddict,MXDATETIME_CAPI_OBJECT,
	   PyCObject_FromVoidPtr((void *)&mxDateTimeModuleAPI, NULL));

    DPRINTF("* Loaded "MXDATETIME_MODULE" C extension at 0x%0lx.\n",
	    (long)module);
    DPRINTF("Notes: "
	    "sizeof(time_t) = %i, sizeof(int) = %i, sizeof(long) = %i\n",
	    sizeof(time_t),sizeof(int), sizeof(long));

#ifdef HAVE_PYDATETIME
    /* Init the Python datetime module's API if already loaded */
    if (mx_Init_PyDateTimeAPI())
	goto onError;
#endif

    /* We are now initialized */
    mxDateTime_Initialized = 1;

 onError:
    /* Check for errors and report them */
    if (PyErr_Occurred())
	Py_ReportModuleInitError(MXDATETIME_MODULE);
    return;
}
