/* 
  mxUID -- An UID datatype

  Copyright (c) 1998-2000, Marc-Andre Lemburg; mailto:mal@lemburg.com
  Copyright (c) 2000-2015, eGenix.com Software GmbH; mailto:info@egenix.com
  See the documentation for further copyright information or contact
  the author (mailto:mal@lemburg.com).

*/

/* Debug defines: */
/*#define MAL_MEM_DEBUG*/
/*#define MAL_DEBUG*/
/*#define MAL_REF_DEBUG*/

/* Logging file used by debugging facility */
#ifndef MAL_DEBUG_OUTPUTFILE
# define MAL_DEBUG_OUTPUTFILE "mxUID.log"
#endif

/* We want all our symbols to be exported */
#define MX_BUILDING_MXUID

/* Mark the module as Py_ssize_t clean. */
#define PY_SSIZE_T_CLEAN 1

/* Setup mxstdlib memory management */
#if 1
# define MAL_USE_PYMALLOC
#else
# define MAL_USE_C_MALLOC
#endif

#include "mx.h"
#include "mxUID.h"
#include <ctype.h>
#include <time.h>

/* Version number: Major.Minor.Patchlevel */
#define MXUID_VERSION "3.2.9"

/* Additional includes needed for mxUID_GetCurrentTime() API */
# if defined(HAVE_SYS_TIME_H) && defined(TIME_WITH_SYS_TIME)
#  include <sys/time.h>
#  include <time.h>
# else
#  include <time.h>
# endif
# ifdef HAVE_UNISTD_H
#  include <unistd.h>
# endif

/* Define this to have the module use a free list for UIDs */
#ifndef Py_DEBUG
#define MXUID_FREELIST
#endif

/* Define this to enable the speedup in mxUID_SchemeUsesRelativePaths()
   that uses hard-coded values for the uses_relative part of the
   scheme dict entries in mxUID_SchemeDict below. Saves a
   dictionary lookup for every join. */
#define HARDCODE_SCHEMES_USES_RELATIVE

/* --- module doc-string -------------------------------------------------- */

static char *Module_docstring = 

 MXUID_MODULE" -- An UID datatype.\n\n"

 "Version "MXUID_VERSION"\n\n"

 "Copyright (c) 1998-2000, Marc-Andre Lemburg; mailto:mal@lemburg.com\n"
 "Copyright (c) 2000-2015, eGenix.com Software GmbH; mailto:info@egenix.com\n\n"
 "                 All Rights Reserved\n\n"
 "See the documentation for further information on copyrights,\n"
 "or contact the author."
;

/* --- module globals ----------------------------------------------------- */

static PyObject *mxUID_Error;		/* Error Exception object */

static unsigned int mxUID_IDCounter;		/* ID Counter (20 bit) */
static unsigned int mxUID_HostID;		/* Host ID (16 bit) */
static unsigned int mxUID_ProcessID;		/* Process ID (16 bit) */

/* Flag telling us whether the module was initialized or not. */
static int mxUID_Initialized = 0;

/* --- forward declarations ----------------------------------------------- */

/* --- internal macros ---------------------------------------------------- */

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
	modname = PyString_AsString(v);
    if (modname == NULL) {
	PyErr_Clear();
	modname = MXUID_MODULE;
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

#if 0
/* Helper for adding integer constants. Check for errors with
   PyErr_Occurred() */
static 
void insint(PyObject *dict,
	    char *name,
	    int value)
{
    PyObject *v = PyInt_FromLong((long)value);
    PyDict_SetItemString(dict, name, v);
    Py_XDECREF(v);
}
#endif

#ifdef Py_NEWSTYLENUMBER
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
#endif

/* --- internal functions --- */

static
unsigned int mxUID_CRC32(char *str,
			 Py_ssize_t len)
{
    Py_ssize_t i,j;
    register unsigned char *s = (unsigned char *)str;
    register int x;
    register int y;

    x = 0;
    y = 0;
    for (i = 0, j = len + 1; i < len; i++,j--) {
	register unsigned char c = s[i];
	x += c;
	x &= 0xFFFF;
	y += (j & 0xFFFF) * c;
	y &= 0xFFFF;
    }
    return (unsigned int)(x + (y<<16));
}
    
static
unsigned short mxUID_CRC16(char *str,
			   Py_ssize_t len)
{
    Py_ssize_t i,j;
    register unsigned char *s = (unsigned char *)str;
    register int x;
    register int y;

    x = 0;
    y = 0;
    for (i = 0, j = len + 1; i < len; i++,j--) {
	register unsigned char c = s[i];
	x += c;
	x &= 0xFF;
	y += (j & 0xFFFF) * c;
	y &= 0xFF;
    }
    return (unsigned short)(x + (y<<8));
}

/* Folds a string in with length in_len into a string out having
   length out_len. The output string is zero terminated; its buffer
   must have size out_len + 1. 

   XXX This functions doesn't really do what it's supposed to... better
       not rely on it's output !

*/

static
void mxUID_Fold(char *in_str,
		Py_ssize_t in_len,
		char *out_str,
		Py_ssize_t out_len)
{
    Py_ssize_t chunksize;
    register unsigned char *in = (unsigned char *)in_str;
    register unsigned char *out = (unsigned char *)out_str;
    
    /* First pass */
    chunksize = min(in_len, out_len);
    memcpy(out, in, chunksize);
    if (chunksize < out_len)
	memset(out + chunksize, 0, out_len - chunksize);
    in += chunksize;
    in_len -= chunksize;

    /* Next passes */
    while (in_len > 0) {
	register Py_ssize_t i;
	chunksize = min(in_len, out_len);
	for (i = 0; i < chunksize; i++)
	    out[i] ^= in[i];
	in += chunksize;
	in_len -= chunksize;
    }

    /* Zero terminate */
    out[out_len] = '\0';
}

/* Fold a long integer to 16 bits of data */

static
unsigned short mxUID_FoldInteger(long value)
{
    unsigned short result = 0;
    Py_ssize_t i;
    
    for (i = 0; i < sizeof(value) / 2; i++) {
	result ^= value & 0xFFFF;
	value >>= 16;
    }
    return result;
}

/* Apply a one-time pad to a data string. The data string should only
   use lowercase HEX characters (all other characters are passed
   through unchanged). */

static
int mxUID_OneTimePad(unsigned char *data,
		     unsigned char *out,
		     Py_ssize_t data_len,
		     unsigned char *otp,
		     Py_ssize_t otp_len)
{
    register Py_ssize_t i, j;
    static char charbase[] = "0123456789abcdef";

    if (otp_len <= 0 || otp == NULL) {
	memcpy(out, data, data_len);
	return 0;
    }
    
    for (i = 0, j = 0; i < data_len; i++) {
	register unsigned char c = data[i];
	register unsigned char x = otp[j];
	
	if (c >= '0' && c <= '9')
	    c = c - '0';
	else if (c >= 'a' && c <= 'f')
	    c = c - 'a' + 10;
#if 0
	else if (c >= 'A' && c <= 'F')
	    c = c - 'A' + 10;
#endif
	else {
	    out[i] = c;
	    goto next;
	}
	out[i] = charbase[(c ^ x ^ (x >> 4)) & 0x0F];

    next:
	DPRINTF("i=%i, j=%i, data[i]='%c', ot[j]='%c', c=%i, x=%i -> out[i]='%c'\n", 
	       i, j, data[i], otp[j], c, x, out[i]);
	if (++j >= otp_len)
	    j = 0;
    }
    return 0;
}

/* Verify an unencrypted UID string uid with length uid_len using the
   embedded CRC-16 value. If the UID string was generated using a code
   string, this must be given too. Otherwise, NULL may be used. */

static
int mxUID_Verify(char *uid,
		 Py_ssize_t uid_len,
		 char *code)
{
    int crc16;
    int value = -1;

    if (uid_len < 32)
	return 0;
    crc16 = (int)mxUID_CRC16(uid, uid_len - 4);
    if (sscanf((char *)(uid + uid_len - 4), "%x", &value) <= 0)
	return 0;
    DPRINTF("crc=%i, value=%i\n", crc16, value);
    if (crc16 != value)
	return 0;
    if (code) {
	Py_ssize_t code_len = strlen(code);
	if (uid_len != 32 + code_len)
	    return 0;
	if (code_len == 0)
	    return 1;
	return (memcmp(code, uid + 28, strlen(code)) == 0);
    }
    else if (uid_len != 32)
	return 0;
    return 1;
}

/* Returns the current time in Unix ticks.

   The function tries to use the gettimeofday() API in BSD systems and
   falls back to time() for all others.

   -1.0 is returned in case of an error.

   (Taken from mxDateTime.c.)

*/

static
double mxUID_GetCurrentTime(void)
{
#ifdef HAVE_GETTIMEOFDAY
    struct timeval tv;

# ifdef GETTIMEOFDAY_NO_TZ
    if (!gettimeofday(&tv))
# else
    if (!gettimeofday(&tv, 0))
# endif
	return ((double)tv.tv_sec + (double)tv.tv_usec * 1e-6);
    else
	return -1.0;
#else
    time_t ticks;

    time(&ticks);
    return (double) ticks;
#endif
}

/* --- C API --------------------------------------------------------------- */

/* Build a new UID string for object with address id.

   code is optionally included in UID if given. It may be NULL.
   timestamp should be a double indicating Unix ticks, or -1 to have
   the API use the current time.

   The output buffer uid must have room for at least 512
   bytes. uid_len is set to the uid data length. It must be preset to
   the buffer's size.

   Returns the Python string object on success, NULL on error.

*/
 
static
PyObject *mxUID_New(void *obj,
		    char *code,
		    double timestamp)
{
    unsigned int t_hi, t_lo, id;
    Py_ssize_t len;
    int crc16;
    char uid[256];
    
    /* Build UID */
    if (code) {
	Py_Assert(strlen(code) < 100,
		  PyExc_ValueError,
		  "len(code) must be <100");
    }
    else
	code = "";

    /* For the object id simply take the address and fold into 16
       bits */
    id = mxUID_FoldInteger((long)obj);

    /* Build timestamp */
    if (timestamp == -1)
	timestamp = mxUID_GetCurrentTime();
    if (timestamp >= 0) {
	timestamp *= 97.5;
	t_hi = (unsigned int)(timestamp / 4294967296.0);
	t_lo = (unsigned int)(timestamp - t_hi * 4294967296.0);
	Py_Assert(t_hi <= 0xFF,
		  PyExc_ValueError,
		  "timestamp value too large");
    }
    else 
	Py_Error(PyExc_ValueError,
		 "timestamp must be positive");

    /* Format the UID string. Also see mxUID_Verify() and mangle()
       demangle() in UID.py. */
    len = sprintf(uid,
		  "%06x"		/* 24-bit counter */
		  "%02x%08x" 		/* 40-bit timestamp */
		  "%04x"		/* 16-bit pid */
		  "%04x"		/* 16-bit hostid */
		  "%04x"    		/* id(v) */
		  "%.100s"		/* optional code */
		  ,
		  mxUID_IDCounter & 0xFFFFFF,
		  t_hi,t_lo,
		  mxUID_ProcessID,
		  mxUID_HostID,
		  id,
		  code);
    Py_Assert(len < sizeof(uid) - 5,
	      PyExc_SystemError,
	      "internal error in mxUID_UID: buffer overflow");

    /* Increment counter; overflow is intended */
    mxUID_IDCounter += 1000003;

    /* Add 16 bit CRC value in HEX */
    crc16 = (int)mxUID_CRC16(uid, len);
    len += sprintf(&uid[len], "%04x", crc16);

    return PyString_FromStringAndSize(uid, len);

 onError:
    return NULL;
}

/* Extracts the ticks timestamp from an unencrypted UID string uid */

static
double mxUID_ExtractTimestamp(unsigned char *uid)
{
    int i;
    double ticks = 0.0,
	   base = 1.0;
    
    for (i = 15; i >= 6; i--) {
	char c = uid[i];
	int value;
	
	if (c >= '0' && c <= '9')
	    value = c - '0';
	else if (c >= 'a' && c <= 'f')
	    value = c - 'a' + 10;
	else if (c >= 'A' && c <= 'F')
	    value = c - 'A' + 10;
	else
	    /* Hmm, a buggy character: ignore... */
	    value = 0;

	/*printf("i=%i s[i]=%c value=%i, ticks=%f, base=%f\n",
	  i,c,value,ticks,base);*/

	if (value)
	    ticks += base * value;
	base *= 16.0;
    }
    return ticks / 97.5;
}

/* --- Python API ---------------------------------------------------------- */

Py_C_Function_WithKeywords( 
	       mxUID_UID,
	       "UID(object=None, code='', timestamp=None)\n\n"
	       "Create a new UID string for object. code is optionally\n"
	       "included in the UID. timestamp must be a ticks float and\n"
	       "defaults to the current time."
	       )
{
    PyObject *object = Py_None;
    char *code = NULL;
    double timestamp = -1;

    Py_KeywordsGet3Args("|Osd", object, code, timestamp);

    /* Build UID string */
    return mxUID_New(object, code, timestamp);

 onError:
    return NULL;
}

Py_C_Function_WithKeywords(
	       mxUID_setids,
	       "setids(hostid, pid, counter)\n\n"
	       "Sets the IDs to be used by the module. The function\n"
	       "can take keyword arguments in case only some values\n"
	       "need to be changed. All others are then left untouched."
	       )
{
    unsigned int hostid=mxUID_HostID;
    unsigned int pid=mxUID_ProcessID;
    unsigned int counter=mxUID_IDCounter;
    
    Py_KeywordsGet3Args("|iii", hostid, pid, counter);

    mxUID_IDCounter = counter;

    /* Map to 16 bits */
    mxUID_HostID = mxUID_FoldInteger(hostid);
    mxUID_ProcessID = mxUID_FoldInteger(pid);
    
    Py_ReturnNone();

 onError:
    return NULL;
}

Py_C_Function( mxUID_getids,
	       "getids()\n\n"
	       "Returns a tuple (hostid16, pid16, counter) of the currently\n"
	       "active values used for UID generation."
	       )
{
    Py_NoArgsCheck();

    Py_Return3Args("(iii)", 
		   mxUID_HostID, mxUID_ProcessID, mxUID_IDCounter);

 onError:
    return NULL;
}

Py_C_Function( mxUID_timestamp,
	       "timestamp(uid)\n\n"
	       "Returns the timestamp encoded in the UID."
	       )
{
    unsigned char *s;
    Py_ssize_t len;

    Py_Get2Args("s#", s, len);

    Py_Assert(len > 10 && len < 256,
	      PyExc_ValueError,
	      "need a UID string");

    return PyFloat_FromDouble(mxUID_ExtractTimestamp(s));

 onError:
    return NULL;
}

Py_C_Function( mxUID_verify,
	       "verify(uid[, code])\n\n"
	       "Verifies the given UID string and returns 0/1 depending\n"
	       "on whether the UID matches the mxUID layout. code must be\n"
	       "given in case it was used to create the UID."
	       )
{
    char *s;
    Py_ssize_t len;
    char *code = NULL;
    
    Py_Get3Args("s#|s", 
		s, len, code);

    Py_Assert(len > 10 && len < 256,
	      PyExc_ValueError,
	      "need a UID string");
    
    return PyInt_FromLong(mxUID_Verify(s, len, code));

 onError:
    return NULL;
}

Py_C_Function( mxUID_fold,
	       "fold(string[,size=8])\n\n"
	       "Returns a new string with size bytes, which is deduced\n"
	       "from string by XOR folding it."
	       )
{
    char *s;
    Py_ssize_t size = 8;
    Py_ssize_t len;
    PyObject *v = NULL;
    
    Py_Get3Args("s#|"Py_SSIZE_T_PARSERMARKER,
		s, len, size);

    v = PyString_FromStringAndSize(NULL,size);
    if (!v)
	goto onError;
    
    mxUID_Fold(s, len, PyString_AS_STRING(v), size);
    return v;

 onError:
    Py_XDECREF(v);
    return NULL;
}

Py_C_Function( mxUID_otp,
	       "otp(data, pad)\n\n"
	       "Returns a one-time padded version of data using\n"
	       "pad as basis. This function is idempotent."
	       )
{
    unsigned char *data;
    Py_ssize_t data_len;
    unsigned char *otp;
    Py_ssize_t otp_len;
    PyObject *v = NULL;
    
    Py_Get4Args("s#s#", 
		data, data_len, otp, otp_len);

    v = PyString_FromStringAndSize(NULL, data_len);
    if (!v)
	goto onError;

    mxUID_OneTimePad(data, (unsigned char *)PyString_AS_STRING(v), 
		     data_len, otp, otp_len);
    return v;

 onError:
    Py_XDECREF(v);
    return NULL;
}

Py_C_Function( mxUID_crc,
	       "crc(string[,bits=32])\n\n"
	       "Returns a CRC integer calculated from string having the\n"
	       "given number of bits (16 or 32)."
	       )
{
    int bits = 32;
    Py_ssize_t len;
    long crc;
    char *s;
    
    Py_Get3Args("s#|i",
		s, len, bits);

    if (bits == 32)
	crc = (long)mxUID_CRC32(s,len);
    else if (bits == 16)
	crc = (long)mxUID_CRC16(s,len);
    else
	Py_Error(PyExc_ValueError,
		 "only 16 or 32 bit CRCs are supported");

    return PyInt_FromLong((long)crc);

 onError:
    return NULL;
}

/* --- module init --------------------------------------------------------- */

/* Python Method Table */

static 
PyMethodDef Module_methods[] =
{   
    Py_MethodWithKeywordsListEntry("UID",mxUID_UID),
    Py_MethodListEntry("timestamp",mxUID_timestamp),
    Py_MethodListEntry("verify",mxUID_verify),
    Py_MethodListEntry("crc",mxUID_crc),
    Py_MethodListEntry("fold",mxUID_fold),
    Py_MethodListEntry("otp",mxUID_otp),
    Py_MethodWithKeywordsListEntry("setids",mxUID_setids),
    Py_MethodListEntryNoArgs("getids",mxUID_getids),
    {NULL,NULL} /* end of list */
};

/* C API table */
static
mxUIDModule_APIObject mxUIDModuleAPI =
{
    mxUID_New,
    mxUID_ExtractTimestamp,
};

/* Cleanup function */
static 
void mxUIDModule_Cleanup(void)
{
    /* Reset mxUID_Initialized flag */
    mxUID_Initialized = 0;
}

/* create PyMethodObjects and register them in the module's dict */
MX_EXPORT(void) 
     initmxUID(void)
{
    PyObject *module, *moddict, *api;

    if (mxUID_Initialized)
	Py_Error(PyExc_SystemError,
		 "can't initialize "MXUID_MODULE" more than once");

    /* Create module */
    module = Py_InitModule4(MXUID_MODULE, /* Module name */
			    Module_methods, /* Method list */
			    Module_docstring, /* Module doc-string */
			    (PyObject *)NULL, /* always pass this as *self */
			    PYTHON_API_VERSION); /* API Version */
    if (module == NULL)
	goto onError;

    /* Add some constants to the module's dict */
    moddict = PyModule_GetDict(module);
    PyDict_SetItemString(moddict, 
			 "__version__",
			 PyString_FromString(MXUID_VERSION));

    if (!(mxUID_Error = insexc(moddict,"Error",PyExc_StandardError)))
	goto onError;

    /* Register cleanup function */
    if (Py_AtExit(mxUIDModule_Cleanup)) {
	/* XXX what to do if we can't register that function ??? */
	DPRINTF("* Failed to register mxUID cleanup function\n");
    }

    /* Export C API */
    api = PyCObject_FromVoidPtr((void *)&mxUIDModuleAPI, NULL);
    if (api == NULL)
	goto onError;
    PyDict_SetItemString(moddict,MXUID_MODULE"API",api);
    Py_DECREF(api);

    /* We are now initialized */
    mxUID_Initialized = 1;

 onError:
    /* Check for errors and report them */
    if (PyErr_Occurred())
	Py_ReportModuleInitError(MXUID_MODULE);
    return;
}
