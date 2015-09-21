/* 
  mxBeeBase -- BeeBase C extension

  Copyright (c) 1998-2000, Marc-Andre Lemburg; mailto:mal@lemburg.com
  Copyright (c) 2000-2015, eGenix.com Software GmbH; mailto:info@egenix.com
  See the documentation for further copyright information or contact
  the author (mailto:mal@lemburg.com).

*/

/* Define this to aid in finding memory leaks */
/*#define MAL_MEM_DEBUG*/
/*#define MAL_DEBUG*/

/* Logging file used by debugging facility */
#ifndef MAL_DEBUG_OUTPUTFILE
# define MAL_DEBUG_OUTPUTFILE "mxBeeBase.log"
#endif

/* We want all our symbols to be exported */
#define MX_BUILDING_MXBEEBASE

/* Mark the module as Py_ssize_t clean. */
#define PY_SSIZE_T_CLEAN 1

/* Setup mxstdlib memory management */
#if 1
# define MAL_USE_PYMALLOC
#else
# define MAL_USE_C_MALLOC
#endif

#include "mx.h"
#include "mxBeeBase.h"

/* Version number: Major.Minor.Patchlevel */
#define MXBEEBASE_VERSION "3.2.9"

/* Define these to have the module use free lists (saves malloc calls) */
#ifndef Py_DEBUG
/*#define MXBEEINDEX_FREELIST*/
#define MXBEECURSOR_FREELIST
#endif

/* --- module doc-string -------------------------------------------------- */

static char *Module_docstring = 

 MXBEEBASE_MODULE" -- BeeBase objects and functions. Version "MXBEEBASE_VERSION"\n\n"

 "Copyright (c) 1998-2000, Marc-Andre Lemburg; mailto:mal@lemburg.com\n"
 "Copyright (c) 2000-2015, eGenix.com Software GmbH; mailto:info@egenix.com\n\n"
 "                 All Rights Reserved\n\n"
 "See the documentation for further information on copyrights,\n"
 "or contact the author."
;

/* --- module globals ----------------------------------------------------- */

static PyObject *mxBeeIndex_Error;		/* Exception object */
static PyObject *mxBeeCursor_Error;		/* Exception object */

/* Special keys */
static PyObject *mxBeeIndex_FirstKey;		/* First key */
static PyObject *mxBeeIndex_LastKey;		/* Last key */

/* Free lists for BeeIndex objects */
#ifdef MXBEEINDEX_FREELIST
static mxBeeIndexObject *mxBeeIndex_FreeList = NULL;
#endif

/* Free lists for BeeCursor objects */
#ifdef MXBEECURSOR_FREELIST
static mxBeeCursorObject *mxBeeCursor_FreeList = NULL;
#endif

/* Flag telling us whether the module was initialized or not. */
static int mxBeeBase_Initialized = 0;

/* --- forward declarations ----------------------------------------------- */

staticforward PyTypeObject mxBeeIndex_Type;
staticforward PyMethodDef mxBeeIndex_Methods[];

staticforward PyTypeObject mxBeeCursor_Type;
staticforward PyMethodDef mxBeeCursor_Methods[];

staticforward
mxBeeCursorObject *mxBeeCursor_New(mxBeeIndexObject *beeindex,   /* Index object */
				   bCursor *c                 /* bCursor */
				   );

/* --- internal macros ---------------------------------------------------- */

#define _mxBeeIndex_Check(v) \
        (((mxBeeIndexObject *)(v))->ob_type == &mxBeeIndex_Type)

#define _mxBeeCursor_Check(v) \
        (((mxBeeCursorObject *)(v))->ob_type == &mxBeeCursor_Type)

/* --- module helpers ----------------------------------------------------- */

/* Create an exception object, insert it into the module dictionary
   under the given name and return the object pointer; this is NULL in
   case an error occurred. */

static 
PyObject *insexc(PyObject *moddict,
		 char *name)
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
	modname = MXBEEBASE_MODULE;
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


    v = PyErr_NewException(fullname, NULL, NULL);
    if (v == NULL)
	return NULL;
    if (PyDict_SetItemString(moddict,name,v))
	return NULL;
    return v;
}

#if 0
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

/* Helper for adding objects to dictionaries. Check for errors with
   PyErr_Occurred() */
static 
PyObject *insstr(PyObject *dict,
		 char *name,
		 char *value)
{
    PyObject *v;
    
    v = PyString_FromString(value);
    if (!v)
	return NULL;
    if (PyDict_SetItemString(dict, name, v))
	return NULL;
    return v;
}

/* --- Internal functions --- */

void mxBeeBase_ReportError(bError rc) 
{
    switch (rc) {

    case bErrKeyNotFound:
	Py_Error(PyExc_KeyError,
		 "key not found");

    case bErrDupKeys:
	Py_Error(PyExc_KeyError,
		 "duplicate key");

    case bErrSectorSize:
	Py_Error(PyExc_ValueError,
		 "illegal sector size (too small or not 0 mod 4)");

    case bErrFileNotOpen:
	Py_ErrorWithArg(PyExc_IOError,
			"could not open file: '%s'",strerror(errno));

    case bErrFileExists:
	Py_Error(PyExc_IOError,
		 "file exists");

    case bErrNotWithDupKeys:
	Py_Error(mxBeeIndex_Error,
		 "not allowed with duplicate keys");

    case bErrBufferInvalid:
	Py_Error(mxBeeCursor_Error,
		 "buffer invalid - no data available");

    case bErrIO:
	Py_ErrorWith2Args(PyExc_IOError,
			  "in BeeIndex: '%s' (btr.c line %i)",strerror(errno),
			  bErrLineNo);

    case bErrMemory:
	Py_ErrorWithArg(PyExc_MemoryError,
			"in BeeIndex (line %i)",bErrLineNo);

    default:
	Py_Error(PyExc_SystemError,
		 "unknown error");
    }
 onError:
    return;
}

/* --- BeeBase Index Object -------------------------------------------*/

/* --- Allocation --- */

static
mxBeeIndexObject *mxBeeIndex_New(char *filename,   /* name of beeindex file */
				 int filemode,	   /* how to open the
						      file */
				 int keySize,      /* length of key in
						      bytes */
				 int sectorSize,   /* size of sector
						      on disk */
				 bCompFunc comp,   /* key compare
						      function */
				 mxObjectFromKeyFunc ofk, /* key
							     conversion */
				 mxKeyFromObjectFunc kfo, /* functions */
				 int allow_dupkeys /* allow duplicate
						      keys ? */
				 )
{
    mxBeeIndexObject *beeindex = 0;
    bDescription *info;
    bError rc;
    char *iName;
    int filename_size;

    /* Copy the filename into iName; we could have used strdup() here, but
       this code allows us to use pymalloc. */
    filename_size = strlen(filename) + 1;
    iName = new(char, filename_size);
    if (iName == NULL)
	Py_Error(PyExc_MemoryError,
		 "Out of memory");
    memcpy(iName, filename, filename_size);

    /* Allocate the object */
#ifdef MXBEEINDEX_FREELIST
    if (mxBeeIndex_FreeList) {
	beeindex = mxBeeIndex_FreeList;
	mxBeeIndex_FreeList = *(mxBeeIndexObject **)mxBeeIndex_FreeList;
	beeindex->ob_type = &mxBeeIndex_Type;
	_Py_NewReference(beeindex);
    }
    else
#endif 
	 {
	beeindex = PyObject_NEW(mxBeeIndexObject,&mxBeeIndex_Type);
	if (beeindex == NULL)
	    goto onError;
    }

    /* Init description */
    info = &beeindex->info;
    info->iName = iName;
    info->keySize = keySize;
    info->dupKeys = (allow_dupkeys != 0);
    info->sectorSize = sectorSize;
    info->comp = comp;
    info->filemode = filemode;

    /* Conversion routines */
    beeindex->ObjectFromKey = ofk;
    beeindex->KeyFromObject = kfo;

    /* Reset update count (also see mxBeeIndex_Clear()) */
    beeindex->updates = 0;

    /* Invalidate length cache */
    beeindex->length = -1;
    beeindex->length_state = -1;
    
    /* Open the beeindex */
    rc = bOpen(beeindex->info, &(beeindex->handle));
    if (rc != bErrOk) {
	beeindex->handle = 0;
	mxBeeBase_ReportError(rc);
	goto onError;
    }

    DPRINTF("mxBeeIndex_New: instance at %0lx\n",(long)beeindex);

    return beeindex;

 onError:
    Py_XDECREF(beeindex);
    return NULL;
}

/* --- Deallocation --- */

static
void mxBeeIndex_Free(mxBeeIndexObject *beeindex)
{
    DPRINTF("mxBeeIndex_Free: instance at %0lx\n",(long)beeindex);

    if (beeindex->handle)
	/* Close beeindex file, flushing any unsaved data */
	bClose(beeindex->handle);

    /* Free filename */
    free(beeindex->info.iName);
    beeindex->info.iName = NULL;
    
#ifdef MXBEEINDEX_FREELIST
    /* Append to free list */
    *(mxBeeIndexObject **)beeindex = mxBeeIndex_FreeList;
    mxBeeIndex_FreeList = beeindex;
#else
    PyObject_Del(beeindex);
#endif
}

/* --- Key management routines --- */

/* Use Python strings as keys (these may not contain embedded NULs) */
static
void *mxBeeIndex_KeyFromString(mxBeeIndexObject *beeindex,
			       PyObject *key)
{
    Py_Assert(PyString_Check(key),
	      PyExc_TypeError,
	      "keys must be strings");
    Py_AssertWithArg((int)PyString_GET_SIZE(key) < beeindex->info.keySize,
		     PyExc_TypeError,
		     "keys must not exceed length %li",
		     (unsigned long)beeindex->info.keySize - 1);
    Py_Assert((unsigned int) PyString_GET_SIZE(key) == strlen(PyString_AS_STRING(key)),
	      PyExc_TypeError,
	      "keys may not have embedded null bytes");

    return (void*)PyString_AS_STRING(key);
    
 onError:
    return NULL;
}

static
PyObject *mxBeeIndex_StringFromKey(mxBeeIndexObject *beeindex,
				   void *key)
{
    return PyString_FromString((char*)key);
}

static
int mxBeeIndex_CompareStrings(size_t keysize, 
			      const void *key1, 
			      const void *key2) 
{
    return strcmp((char*)key1, (char*)key2);
}

/* Use fixed length Python strings as keys (these may contain embedded
   NULs) */
static
void *mxBeeIndex_KeyFromFixedLengthString(mxBeeIndexObject *beeindex,
					  PyObject *key)
{
    Py_Assert(PyString_Check(key),
	      PyExc_TypeError,
	      "keys must be strings");
    Py_AssertWithArg((int)PyString_GET_SIZE(key) == beeindex->info.keySize - 1,
		     PyExc_TypeError,
		     "keys must have fixed length %li",
		     (unsigned long)beeindex->info.keySize - 1);

    return (void*)PyString_AS_STRING(key);
    
 onError:
    return NULL;
}

static
PyObject *mxBeeIndex_FixedLengthStringFromKey(mxBeeIndexObject *beeindex,
					      void *key)
{
    return PyString_FromStringAndSize((char*)key, 
				      (Py_ssize_t)(beeindex->info.keySize - 1));
}

static
int mxBeeIndex_CompareFixedLengthStrings(size_t keysize, 
					 const void *key1, 
					 const void *key2) 
{
    return memcmp((char*)key1, (char*)key2, keysize);
}

/* Use Python integer as keys */
static
void *mxBeeIndex_KeyFromInteger(mxBeeIndexObject *beeindex,
				PyObject *key)
{
    Py_Assert(PyInt_Check(key),
	      PyExc_TypeError,
	      "keys must be integers");
    return (void*)&PyInt_AS_LONG(key);
    
 onError:
    return NULL;
}

static
PyObject *mxBeeIndex_IntegerFromKey(mxBeeIndexObject *beeindex,
				    void *key)
{
    return PyInt_FromLong(*(long*)key);
}

static
int mxBeeIndex_CompareLongs(size_t keysize, 
			    const void *key1, 
			    const void *key2) 
{
    unsigned long a = *(unsigned long *)key1;
    unsigned long b = *(unsigned long *)key2;
    return (a == b) ? CC_EQ : (a > b) ? CC_GT : CC_LT;
}

/* Use Python floats as keys */
static
void *mxBeeIndex_KeyFromFloat(mxBeeIndexObject *beeindex,
			      PyObject *key)
{
    Py_Assert(PyFloat_Check(key),
	      PyExc_TypeError,
	      "keys must be floats");
    return (void*)&PyFloat_AS_DOUBLE(key);
    
 onError:
    return NULL;
}

static
PyObject *mxBeeIndex_FloatFromKey(mxBeeIndexObject *beeindex,
				  void *key)
{
    return PyFloat_FromDouble(*(double*)key);
}

static
int mxBeeIndex_CompareDoubles(size_t keysize, 
			      const void *key1, 
			      const void *key2) 
{
    double a = *(double *)key1;
    double b = *(double *)key2;
    return (a == b) ? CC_EQ : (a > b) ? CC_GT : CC_LT;
}

/* Python object to record address conversion.

   Returns 0 and raises an exception in case of an error. 
*/
static
bRecAddr mxBeeIndex_RecordAddressFromObject(PyObject *address)
{
    unsigned long value;
    
    if (!address)
	goto onError;

    /* Short cut */
    if (PyInt_Check(address))
	return (bRecAddr)PyInt_AS_LONG(address);

    /* file.tell() will return longs on platforms which have long file
       support */
    if (PyLong_Check(address))
	value = PyLong_AsUnsignedLong(address);
    else
	value = (unsigned long) PyInt_AsLong(address);
    if (value == (unsigned long) -1 && PyErr_Occurred())
	goto onError;
    return (bRecAddr)value;
    
 onError:
    PyErr_SetString(PyExc_TypeError,
		    "record address must be an integer or long");
    return 0;
}

/* Record address to Python object conversion.

   If the value fits into an Python integer object, then an integer is
   returned. Otherwise, a Python long object is used.

   Returns NULL and raises an exception in case of an error. 

*/
static
PyObject *mxBeeIndex_ObjectFromRecordAddress(bRecAddr recaddr)
{
    if ((unsigned long)recaddr > INT_MAX)
	return PyLong_FromUnsignedLong((unsigned long)recaddr);
    else
	return PyInt_FromLong((long)recaddr);
}

/* --- API functions --- */

static
long mxBeeIndex_FindKey(mxBeeIndexObject *self,
			PyObject *obj)
{
    bError rc;
    bCursor c;
    bRecAddr record = 0;
    void *key = self->KeyFromObject(self,obj);
    
    if (!key)
	goto onError;

    rc = bFindKey(self->handle,&c,key,&record);
    if (rc != bErrOk) {
	mxBeeBase_ReportError(rc);
	goto onError;
    }
    return (long)record;

 onError:
    return -1;
}

static
int mxBeeIndex_DeleteKey(mxBeeIndexObject *self,
			 PyObject *obj)
{
    bError rc;
    bRecAddr record = 0;
    void *key = self->KeyFromObject(self,obj);
    
    if (!key)
	goto onError;
    
    rc = bDeleteKey(self->handle,key,&record);
    if (rc != bErrOk) {
	mxBeeBase_ReportError(rc);
	goto onError;
    }

    /* Increment update count */
    self->updates++;

    return 0;

 onError:
    return -1;
   
}

static
int mxBeeIndex_SetKey(mxBeeIndexObject *self,
		      PyObject *obj,
		      PyObject *recaddr)
{
    bError rc;
    bRecAddr record;
    void *key = self->KeyFromObject(self,obj);
    
    if (!key)
	goto onError;
    
    record = mxBeeIndex_RecordAddressFromObject(recaddr);
    if (record == 0 && PyErr_Occurred())
	goto onError;

    /* Either insert or update the key; if dupkeys are allowed, only
       inserts are possible */
    if (!self->info.dupKeys) {
	rc = bUpdateKey(self->handle,key,record);
	if (rc == bErrKeyNotFound)
	    rc = bInsertKey(self->handle,key,record);
    }
    else
	rc = bInsertKey(self->handle,key,record);
    
    if (rc != bErrOk) {
	mxBeeBase_ReportError(rc);
	goto onError;
    }

    /* Increment update count */
    self->updates++;

    return 0;

 onError:
    return -1;
}

/* Clear the beeindex by reopening the file as new file. */

static
int mxBeeIndex_Clear(mxBeeIndexObject *self)
{
    bError rc;
    int filemode = self->info.filemode;

    Py_Assert(filemode != 1,
	      PyExc_IOError,
	      "beeindex is read-only");

    /* Close the file */
    if (self->handle)
	bClose(self->handle);

    /* Reopen the file as new file */
    self->info.filemode = 2;

    /* Open the beeindex */
    rc = bOpen(self->info,&(self->handle));
    if (rc != bErrOk) {
	mxBeeBase_ReportError(rc);
	goto onError;
    }

    /* Increment the update count */
    self->updates++;

    /* Invalidate length cache */
    self->length = -1;
    self->length_state = -1;
    
    /* Restore filemode */
    self->info.filemode = filemode;

    DPRINTF("mxBeeIndex_Clear: instance at %0lx\n",(long)self);

    return 0;

 onError:
    return -1;
}

/* --- Methods --- */

#define beeindex ((mxBeeIndexObject*)self)

Py_C_Function( mxBeeIndex_flush,
	       "flush()\n\n"
	       "Flush all buffers"
	       )
{
    bError rc;
    
    Py_NoArgsCheck();

    Py_Assert(beeindex->handle != NULL,
	      mxBeeIndex_Error,
	      "index is closed");
    rc = bFlush(beeindex->handle);
    if (rc != bErrOk) {
	mxBeeBase_ReportError(rc);
	goto onError;
    }
    Py_ReturnNone();

 onError:
    return NULL;
}

Py_C_Function( mxBeeIndex_close,
	       "close()\n\n"
	       "Close the index and flush all buffers"
	       )
{
    bError rc;
    
    Py_NoArgsCheck();

    if (beeindex->handle) {
	rc = bClose(beeindex->handle);
	if (rc != bErrOk) {
	    mxBeeBase_ReportError(rc);
	    goto onError;
	}
	beeindex->handle = NULL;
    }
    Py_ReturnNone();
    
 onError:
    return NULL;
}

Py_C_Function( mxBeeIndex_clear,
	       "clear()\n\n"
	       "Clear the index"
	       )
{
    Py_NoArgsCheck();

    if (mxBeeIndex_Clear(beeindex))
	goto onError;
    Py_ReturnNone();
    
 onError:
    return NULL;
}

Py_C_Function( mxBeeIndex_get,
	       "get(key,default=None)\n\n"
	       "Find the value for key. If key is not found, default is\n"
	       "returned. With dupkeys enabled, the first matching key is\n"
	       "used."
	       )
{
    PyObject *obj,*def = Py_None;
    bCursor c;
    bError rc;
    void *key;
    bRecAddr record = 0;

    Py_Get2Args("O|O",obj,def);

    Py_Assert(beeindex->handle != NULL,
	      mxBeeIndex_Error,
	      "index is closed");

    /* Find key */
    key = beeindex->KeyFromObject(beeindex,obj);
    if (!key)
	goto onError;
    rc = bFindKey(beeindex->handle,&c,key,&record);
    if (rc == bErrKeyNotFound) {
	Py_INCREF(def);
	return def;
    }
    if (rc != bErrOk) {
	mxBeeBase_ReportError(rc);
	goto onError;
    }
    return mxBeeIndex_ObjectFromRecordAddress(record);
    
 onError:
    return NULL;
}

Py_C_Function( mxBeeIndex_has_key,
	       "has_key(key)\n\n"
	       "Returns 1/0 depending on whether the key is found or not."
	       )
{
    PyObject *obj;
    bCursor c;
    bError rc;
    void *key;
    bRecAddr record = 0;

    Py_GetArg("O",obj);

    Py_Assert(beeindex->handle != NULL,
	      mxBeeIndex_Error,
	      "index is closed");

    /* Find key */
    key = beeindex->KeyFromObject(beeindex,obj);
    if (!key)
	goto onError;
    rc = bFindKey(beeindex->handle,&c,key,&record);
    if (rc == bErrKeyNotFound) {
	Py_INCREF(Py_False);
	return Py_False;
    }
    if (rc != bErrOk) {
	mxBeeBase_ReportError(rc);
	goto onError;
    }
    Py_INCREF(Py_True);
    return Py_True;
    
 onError:
    return NULL;
}

Py_C_Function( mxBeeIndex_cursor,
	       "cursor(key[,default])\n\n"
	       "Return a cursor pointing to key in the index. If dupkeys\n"
	       "are enabled, the cursors will always point to the first\n"
	       "of possibly multiple key entries found. In case no key\n"
	       "is found, default is returned if given or a KeyError\n"
	       "raised. Note that cursors only remain valid as long as\n"
	       "the index does not change."
	       )
{
    PyObject *obj,*v,*def = NULL;
    bCursor c;
    bError rc;

    Py_Get2Args("O|O",obj,def);

    Py_Assert(beeindex->handle != NULL,
	      mxBeeIndex_Error,
	      "index is closed");

    /* Find key */
    if (obj == mxBeeIndex_FirstKey)
	rc = bFindFirstKey(beeindex->handle,&c,NULL,NULL);
    else if (obj == mxBeeIndex_LastKey)
	rc = bFindLastKey(beeindex->handle,&c,NULL,NULL);
    else {
	void *key;
	key = beeindex->KeyFromObject(beeindex,obj);
	if (!key)
	    goto onError;
	rc = bFindKey(beeindex->handle,&c,key,NULL);
    }
    if (rc == bErrKeyNotFound && def) {
	Py_INCREF(def);
	return def;
    }
    if (rc != bErrOk) {
	mxBeeBase_ReportError(rc);
	goto onError;
    }

    /* Create cursor */
    v = (PyObject*)mxBeeCursor_New(beeindex,&c);
    if (!v)
	goto onError;

    return v;
    
 onError:
    return NULL;
}

Py_C_Function( mxBeeIndex_keys,
	       "keys()\n\n"
	       "Return a list of keys stored in the index. The list is\n"
	       "sorted ascending."
	       )
{
    bError rc;
    bCursor c;
    PyObject *v = 0;
    PyObject *w;

    Py_NoArgsCheck();

    Py_Assert(beeindex->handle != NULL,
	      mxBeeIndex_Error,
	      "index is closed");

    v = PyList_New(0);
    if (!v)
	goto onError;
    
    /* Find first */
    rc = bFindFirstKey(beeindex->handle,&c,NULL,NULL);
    if (rc == bErrKeyNotFound)
	return v;
    if (rc != bErrOk) {
	mxBeeBase_ReportError(rc);
	goto onError;
    }

    while (1) {
	w = beeindex->ObjectFromKey(beeindex,c.key);
	if (!w)
	    goto onError;
	PyList_Append(v,w);
	Py_DECREF(w);

	rc = bFindNextKey(beeindex->handle,&c,NULL,NULL);
	if (rc == bErrKeyNotFound)
	    break;
	if (rc != bErrOk) {
	    mxBeeBase_ReportError(rc);
	    goto onError;
	}
    }
    
    return v;
    
 onError:
    Py_XDECREF(v);
    return NULL;
}

Py_C_Function( mxBeeIndex_values,
	       "values()\n\n"
	       "Return a list of values stored in the index. The list is\n"
	       "sorted in ascending key order."
	       )
{
    bError rc;
    bCursor c;
    PyObject *v = 0;
    PyObject *w;
    bRecAddr rec;

    Py_NoArgsCheck();

    Py_Assert(beeindex->handle != NULL,
	      mxBeeIndex_Error,
	      "index is closed");

    v = PyList_New(0);
    if (!v)
	goto onError;

    /* Find first */
    rc = bFindFirstKey(beeindex->handle,&c,NULL,&rec);
    if (rc == bErrKeyNotFound)
	return v;
    if (rc != bErrOk) {
	mxBeeBase_ReportError(rc);
	goto onError;
    }

    while (1) {
	w = mxBeeIndex_ObjectFromRecordAddress(rec);
	if (!w)
	    goto onError;
	PyList_Append(v,w);
	Py_DECREF(w);

	rc = bFindNextKey(beeindex->handle,&c,NULL,&rec);
	if (rc == bErrKeyNotFound)
	    break;
	if (rc != bErrOk) {
	    mxBeeBase_ReportError(rc);
	    goto onError;
	}
    }
    
    return v;
    
 onError:
    Py_XDECREF(v);
    return NULL;
}

Py_C_Function( mxBeeIndex_items,
	       "items()\n\n"
	       "Return a list of (key,value) tuples of all items stored\n"
	       "in the index. The list is sorted ascending by key."
	       )
{
    bError rc;
    bCursor c;
    PyObject *v = 0;
    bRecAddr rec;

    Py_NoArgsCheck();

    Py_Assert(beeindex->handle != NULL,
	      mxBeeIndex_Error,
	      "index is closed");

    v = PyList_New(0);
    if (!v)
	goto onError;

    /* Find first */
    rc = bFindFirstKey(beeindex->handle,&c,NULL,&rec);
    if (rc == bErrKeyNotFound)
	return v;
    if (rc != bErrOk) {
	mxBeeBase_ReportError(rc);
	goto onError;
    }

    while (1) {
	PyObject *key,*value,*t;
	
	key = beeindex->ObjectFromKey(beeindex,c.key);
	if (!key)
	    goto onError;
	value = mxBeeIndex_ObjectFromRecordAddress(rec);
	if (!value) {
	    Py_DECREF(key);
	    goto onError;
	}
	t = PyTuple_New(2);
	if (!t) {
	    Py_DECREF(key);
	    Py_DECREF(value);
	    goto onError;
	}
	PyTuple_SET_ITEM(t,0,key);
	PyTuple_SET_ITEM(t,1,value);
	PyList_Append(v,t);
	Py_DECREF(t);

	rc = bFindNextKey(beeindex->handle,&c,NULL,&rec);
	if (rc == bErrKeyNotFound)
	    break;
	if (rc != bErrOk) {
	    mxBeeBase_ReportError(rc);
	    goto onError;
	}
    }
    
    return v;
    
 onError:
    Py_XDECREF(v);
    return NULL;
}

Py_C_Function( mxBeeIndex_delete,
	       "delete(key[,record])\n\n"
	       "Delete an entry. The record address is only needed in case\n"
	       "the index allows dupkeys."
	       )
{
    PyObject *obj;
    PyObject *recaddr = NULL;
    bError rc = bErrOk;
    bRecAddr record;
    void *key = NULL;
    
    Py_Get2Args("O|O",obj,recaddr);

    Py_Assert(beeindex->handle != NULL,
	      mxBeeIndex_Error,
	      "index is closed");

    if (beeindex->info.dupKeys)
	Py_Assert(recaddr != NULL,
		  PyExc_ValueError,
		  "record address must be given if dupkeys are allowed");

    key = beeindex->KeyFromObject(beeindex,obj);
    if (!key)
	goto onError;
    record = mxBeeIndex_RecordAddressFromObject(recaddr);
    if (record == 0 && PyErr_Occurred())
	goto onError;

    /* Delete key using record address if dupkeys is enabled */
    rc = bDeleteKey(beeindex->handle,key,&record);
    if (rc != bErrOk) {
	mxBeeBase_ReportError(rc);
	goto onError;
    }

    /* Increment update count */
    beeindex->updates++;

    Py_ReturnNone();

 onError:
    return NULL;
}

Py_C_Function( mxBeeIndex_update,
	       "update(key,value[,oldvalue])\n\n"
	       "Update an entry. The oldvalue is needed in case\n"
	       "the index allows dupkeys."
	       )
{
    PyObject *obj;
    bError rc = bErrOk;
    PyObject *value;
    PyObject *oldvalue = NULL;
    bRecAddr record, oldrecord;
    void *key = NULL;
    
    Py_Get3Args("OO|O",obj,value,oldvalue);

    Py_Assert(beeindex->handle != NULL,
	      mxBeeIndex_Error,
	      "index is closed");

    if (beeindex->info.dupKeys)
	Py_Assert(oldvalue != NULL,
		  PyExc_ValueError,
		  "oldvalue must be given if dupkeys are allowed");

    record = mxBeeIndex_RecordAddressFromObject(value);
    if (record == 0 && PyErr_Occurred())
	goto onError;
    if (oldvalue) {
	oldrecord = mxBeeIndex_RecordAddressFromObject(oldvalue);
	if (record == 0 && PyErr_Occurred())
	    goto onError;
    }
    else
	oldrecord = 0;
    key = beeindex->KeyFromObject(beeindex,obj);
    if (!key)
	goto onError;

    /* Delete key using oldrecord address if dupkeys is enabled */
    rc = bDeleteKey(beeindex->handle,key,&oldrecord);
    if (rc != bErrOk) {
	mxBeeBase_ReportError(rc);
	goto onError;
    }

    /* Insert the new key,value pair */
    rc = bInsertKey(beeindex->handle,key,record);
    if (rc != bErrOk) {
	mxBeeBase_ReportError(rc);
	goto onError;
    }

    /* Increment update count */
    beeindex->updates++;

    Py_ReturnNone();

 onError:
    return NULL;
}

Py_C_Function( mxBeeIndex_validate,
	       "validate()\n\n"
	       "Validates the BTree and return 1 for success and 0 for\n"
	       "failure. This is an internal debugging feature only."
	       )
{
    Py_NoArgsCheck();

    Py_Assert(beeindex->handle != NULL,
	      mxBeeIndex_Error,
	      "index is closed");

    return PyInt_FromLong(bValidateTree(beeindex->handle) == 0);
    
 onError:
    return NULL;
}

#undef beeindex

/* --- slots --- */

static
PyObject *mxBeeIndex_Getattr(PyObject *obj,
			     char *name)
{
    mxBeeIndexObject *self = (mxBeeIndexObject *)obj;
    
    if (Py_WantAttr(name,"closed"))
	return PyInt_FromLong((self->handle == NULL));

    else if (Py_WantAttr(name,"dupkeys"))
	return PyInt_FromLong(self->info.dupKeys);

    else if (Py_WantAttr(name,"filename"))
	return PyString_FromString(self->info.iName);

    else if (Py_WantAttr(name,"statistics")) {
	bHandle *handle = self->handle;
	Py_Assert(self->handle != NULL,
		  mxBeeIndex_Error,
		  "index is closed");
	return Py_BuildValue("iiiiiiiii",
			     self->updates,
			     handle->maxHeight,handle->nNodesIns,
			     handle->nNodesDel,handle->nKeysIns,
			     handle->nKeysDel,handle->nKeysUpd,
			     handle->nDiskReads,handle->nDiskWrites);
    }
    
    else if (Py_WantAttr(name,"__members__"))
	return Py_BuildValue("[ssss]",
			     "closed","statistics","dupkeys",
			     "filename");

    return Py_FindMethod(mxBeeIndex_Methods,
			 (PyObject *)self,name);
 onError:
    return NULL;
}

static
Py_ssize_t mxBeeIndex_Length(PyObject *obj)
{
    /* XXX Much too slow... */
    mxBeeIndexObject *self = (mxBeeIndexObject *)obj;
    bError rc;
    bCursor c;
    int i;
    
    Py_Assert(self->handle != NULL,
	      mxBeeIndex_Error,
	      "index is closed");

    if (self->length_state == self->updates)
	return self->length;

    /* Find first */
    rc = bFindFirstKey(self->handle,&c,NULL,NULL);
    if (rc == bErrKeyNotFound)
	return 0;
    if (rc != bErrOk) {
	mxBeeBase_ReportError(rc);
	goto onError;
    }
    i = 1;

    /* Scan all others */
    while (1) {
	rc = bFindNextKey(self->handle,&c,NULL,NULL);
	if (rc == bErrKeyNotFound)
	    break;
	if (rc != bErrOk) {
	    mxBeeBase_ReportError(rc);
	    goto onError;
	}
	i++;
    }

    self->length = i;
    self->length_state = self->updates;
    
    return i;

 onError:
    return -1;
}

static
PyObject *mxBeeIndex_Subscript(PyObject *obj,
			       PyObject *key)
{
    mxBeeIndexObject *self = (mxBeeIndexObject *)obj;
    bRecAddr record;

    Py_Assert(self->handle != NULL,
	      mxBeeIndex_Error,
	      "index is closed");

    record = mxBeeIndex_FindKey(self, key);
    if (record == -1 && PyErr_Occurred())
	goto onError;
    
    return mxBeeIndex_ObjectFromRecordAddress(record);

 onError:
    return NULL;
}

static
int mxBeeIndex_AssignSubscript(PyObject *obj,
			       PyObject *key,
			       PyObject *recaddr)
{
    mxBeeIndexObject *self = (mxBeeIndexObject *)obj;

    Py_Assert(self->handle != NULL,
	      mxBeeIndex_Error,
	      "index is closed");

    if (recaddr)
	return mxBeeIndex_SetKey(self, key, recaddr);
    else
	return mxBeeIndex_DeleteKey(self, key);

 onError:
    return -1;
}

/* Python Type Tables */

static PyMappingMethods mxBeeIndex_TypeAsMapping = {
	mxBeeIndex_Length, 		/*mp_length*/
	mxBeeIndex_Subscript, 		/*mp_subscript*/
	mxBeeIndex_AssignSubscript, 	/*mp_ass_subscript*/
};

statichere
PyTypeObject mxBeeIndex_Type = {
    PyObject_HEAD_INIT(0)		/* init at startup ! */
    0,			  		/*ob_size*/
    "BeeIndex",	  			/*tp_name*/
    sizeof(mxBeeIndexObject),      	/*tp_basicsize*/
    0,			  		/*tp_itemsize*/
    /* slots */
    (destructor)mxBeeIndex_Free,	/*tp_dealloc*/
    0,		  			/*tp_print*/
    mxBeeIndex_Getattr, 	 	/*tp_getattr*/
    0,		  			/*tp_setattr*/
    0,		 			/*tp_compare*/
    0,		  			/*tp_repr*/
    0,			 		/*tp_as_number*/
    0,					/*tp_as_sequence*/
    &mxBeeIndex_TypeAsMapping,		/*tp_as_mapping*/
    0,					/*tp_hash*/
    0,					/*tp_call*/
    0,					/*tp_str*/
    0,					/*tp_getattro*/
    0,					/*tp_setattro*/
    0,					/*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT,			/*tp_flags*/
    0,					/* tp_doc */
    0,					/* tp_traverse */
    0,					/* tp_clear */
    0,					/* tp_richcompare */
    0,					/* tp_weaklistoffset */
    0,					/* tp_iter */
    0,					/* tp_iternext */
    mxBeeIndex_Methods,			/* tp_methods */
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
PyMethodDef mxBeeIndex_Methods[] =
{   
    Py_MethodListEntry("get",mxBeeIndex_get),
    Py_MethodListEntry("cursor",mxBeeIndex_cursor),
    Py_MethodListEntry("has_key",mxBeeIndex_has_key),
    Py_MethodListEntryNoArgs("flush",mxBeeIndex_flush),
    Py_MethodListEntryNoArgs("close",mxBeeIndex_close),
    Py_MethodListEntryNoArgs("keys",mxBeeIndex_keys),
    Py_MethodListEntryNoArgs("values",mxBeeIndex_values),
    Py_MethodListEntryNoArgs("items",mxBeeIndex_items),
    Py_MethodListEntry("delete",mxBeeIndex_delete),
    Py_MethodListEntry("update",mxBeeIndex_update),
    Py_MethodListEntryNoArgs("clear",mxBeeIndex_clear),
    Py_MethodListEntryNoArgs("validate",mxBeeIndex_validate),
    {NULL,NULL} /* end of list */
};

/* --- BeeBase Cursor Object ------------------------------------------- */

statichere
mxBeeCursorObject *mxBeeCursor_New(mxBeeIndexObject *beeindex,   /* Index object */
				   bCursor *c                 /* bCursor */
				   )
{
    mxBeeCursorObject *cursor = 0;

    Py_Assert(beeindex->handle,
	      mxBeeCursor_Error,
	      "creating cursor for closed index");

    /* Allocate the object */
#ifdef MXBEEINDEX_FREELIST
    if (mxBeeCursor_FreeList) {
	cursor = mxBeeCursor_FreeList;
	mxBeeCursor_FreeList = *(mxBeeCursorObject **)mxBeeCursor_FreeList;
	cursor->ob_type = &mxBeeCursor_Type;
	_Py_NewReference(cursor);
    }
    else
#endif 
	 {
	cursor = PyObject_NEW(mxBeeCursorObject,&mxBeeCursor_Type);
	if (cursor == NULL)
	    goto onError;
    }

    /* Init vars */
    Py_INCREF(beeindex);
    cursor->beeindex = beeindex;
    memcpy(&cursor->c,c,sizeof(bCursor));
    cursor->adr = c->buffer->adr;
    cursor->updates = beeindex->updates;

    DPRINTF("mxBeeCursor_New: instance at %0lx\n",(long)cursor);

    return cursor;

 onError:
    Py_XDECREF(cursor);
    return NULL;
}

/* --- Deallocation --- */

static
void mxBeeCursor_Free(mxBeeCursorObject *cursor)
{
    DPRINTF("mxBeeCursor_Free: instance at %0lx\n",(long)cursor);

    /* Dereference beeindex object */
    Py_DECREF(cursor->beeindex);
    
#ifdef MXBEEINDEX_FREELIST
    /* Append to free list */
    *(mxBeeCursorObject **)cursor = mxBeeCursor_FreeList;
    mxBeeCursor_FreeList = cursor;
#else
    PyObject_Del(cursor);
#endif
}

/* --- API functions --- */

static
int mxBeeCursor_Invalid(mxBeeCursorObject *self)
{
    Py_Assert(self->beeindex->handle != NULL,
	      mxBeeCursor_Error,
	      "index is closed - cursor is invalid");
    Py_Assert(self->beeindex->updates == self->updates,
	      mxBeeCursor_Error,
	      "index was changed - cursor is invalid");
    Py_Assert(self->c.buffer && self->c.buffer->valid,
	      mxBeeCursor_Error,
	      "buffer was invalidated - cursor is invalid");
    Py_Assert(self->c.buffer->adr == self->adr,
	      mxBeeCursor_Error,
	      "buffer was overwritten - cursor is invalid");
    return 0;

 onError:
    return -1;
}

/* Move the cursor to the next beeindex key. If there are no further
   keys, return 0 and leave the cursor where it is. Otherwise return
   1. Returns -1 on error. */

static
int mxBeeCursor_NextKey(mxBeeCursorObject *self)
{
    bError rc;

    if (mxBeeCursor_Invalid(self))
	goto onError;

    /* Find key (updates cursor only on success) */
    rc = bFindNextKey(self->beeindex->handle,&self->c,NULL,NULL);
    if (rc == bErrKeyNotFound)
	return 0;
    if (rc != bErrOk) {
	mxBeeBase_ReportError(rc);
	goto onError;
    }

    /* Update cursor address */
    self->adr = self->c.buffer->adr;
    
    return 1;

 onError:
    return -1;
}

/* Move the cursor to the prev beeindex key. If there are no further
   keys, return 0 and leave the cursor where it is. Otherwise return
   1. Returns -1 on error. */

static
int mxBeeCursor_PrevKey(mxBeeCursorObject *self)
{
    bError rc;

    if (mxBeeCursor_Invalid(self))
	goto onError;

    /* Find key (updates cursor only on success) */
    rc = bFindPrevKey(self->beeindex->handle,&self->c,NULL,NULL);
    if (rc == bErrKeyNotFound)
	return 0;
    if (rc != bErrOk) {
	mxBeeBase_ReportError(rc);
	goto onError;
    }
    
    /* Update cursor address */
    self->adr = self->c.buffer->adr;
    
    return 1;

 onError:
    return -1;
}

static
PyObject *mxBeeCursor_GetKey(mxBeeCursorObject *self)
{
    PyObject *v;

    if (mxBeeCursor_Invalid(self))
	goto onError;

    /* Convert key to object */
    v = self->beeindex->ObjectFromKey(self->beeindex,self->c.key);
    if (!v)
	goto onError;

    return v;

 onError:
    return NULL;
}

static
PyObject *mxBeeCursor_GetValue(mxBeeCursorObject *self)
{
    bError rc;
    PyObject *v;
    bRecAddr rec;

    if (mxBeeCursor_Invalid(self))
	goto onError;

    /* Check that cursor is valid and read record address */
    rc = bCursorReadData(self->beeindex->handle,&self->c,NULL,&rec);
    if (rc != bErrOk) {
	mxBeeBase_ReportError(rc);
	goto onError;
    }

    v = mxBeeIndex_ObjectFromRecordAddress(rec);
    if (!v)
	goto onError;

    return v;

 onError:
    return NULL;
}

/* --- Methods --- */

#define cursor ((mxBeeCursorObject*)self)

Py_C_Function( mxBeeCursor_next,
	       "next()\n\n"
	       "Move to the next index entry. Returns 1 if there is another\n"
	       "entry, 0 otherwise. The cursor is not moved in case no\n"
	       "further entries exist."
	       )
{
    int found;
    PyObject *v;
    
    Py_NoArgsCheck();

    found = mxBeeCursor_NextKey(cursor);
    if (found < 0)
	goto onError;
    if (found) 
	v = Py_True;
    else
	v = Py_False;
    Py_INCREF(v);
    return v;

 onError:
    return NULL;
}

Py_C_Function( mxBeeCursor_prev,
	       "prev()\n\n"
	       "Move to the previous index entry. Returns 1 if there is\n"
	       "another entry, 0 otherwise. The cursor is not moved in\n"
	       "case no further entries exist."
	       )
{
    int found;
    PyObject *v;

    Py_NoArgsCheck();

    found = mxBeeCursor_PrevKey(cursor);
    if (found < 0)
	goto onError;
    if (found) 
	v = Py_True;
    else
	v = Py_False;
    Py_INCREF(v);
    return v;

 onError:
    return NULL;
}

Py_C_Function( mxBeeCursor_copy,
	       "copy()\n\n"
	       "Return a true copy of the cursor object. The copy can be\n"
	       "used independently from the original."
	       )
{
    Py_NoArgsCheck();

    if (mxBeeCursor_Invalid(cursor))
	goto onError;

    return (PyObject *)mxBeeCursor_New(cursor->beeindex,&cursor->c);

 onError:
    return NULL;
}

#undef cursor

/* --- slots --- */

static
PyObject *mxBeeCursor_Getattr(PyObject *obj,
			      char *name)
{
    mxBeeCursorObject *self = (mxBeeCursorObject *)obj;

    if (Py_WantAttr(name,"closed"))
	return PyInt_FromLong((self->beeindex->handle == NULL));

    if (Py_WantAttr(name,"key"))
	return mxBeeCursor_GetKey(self);

    if (Py_WantAttr(name,"value"))
	return mxBeeCursor_GetValue(self);

    if (Py_WantAttr(name,"valid")) {
	if (mxBeeCursor_Invalid(self)) {
	    PyErr_Clear();
	    Py_INCREF(Py_False);
	    return Py_False;
	}
	Py_INCREF(Py_True);
	return Py_True;
    }
    
    else if (Py_WantAttr(name,"__members__"))
	return Py_BuildValue("[ssss]",
			     "closed","key","value",
			     "valid");

    return Py_FindMethod(mxBeeCursor_Methods,
			 (PyObject *)self,name);
}

/* Python Type Tables */

statichere
PyTypeObject mxBeeCursor_Type = {
    PyObject_HEAD_INIT(0)		/* init at startup ! */
    0,			  		/*ob_size*/
    "BeeCursor",	  		/*tp_name*/
    sizeof(mxBeeCursorObject),      	/*tp_basicsize*/
    0,			  		/*tp_itemsize*/
    /* slots */
    (destructor)mxBeeCursor_Free,	/*tp_dealloc*/
    0,		  			/*tp_print*/
    mxBeeCursor_Getattr,  		/*tp_getattr*/
    0,		  			/*tp_setattr*/
    0,		 	       		/*tp_compare*/
    0,				  	/*tp_repr*/
    0,			 		/*tp_as_number*/
    0,					/*tp_as_sequence*/
    0,					/*tp_as_mapping*/
    0,					/*tp_hash*/
    0,					/*tp_call*/
    0,					/*tp_str*/
    0,					/*tp_getattro*/
    0,					/*tp_setattro*/
    0,					/*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT,			/*tp_flags*/
    0,					/* tp_doc */
    0,					/* tp_traverse */
    0,					/* tp_clear */
    0,					/* tp_richcompare */
    0,					/* tp_weaklistoffset */
    0,					/* tp_iter */
    0,					/* tp_iternext */
    mxBeeCursor_Methods,		/* tp_methods */
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
PyMethodDef mxBeeCursor_Methods[] =
{   
    Py_MethodListEntryNoArgs("next",mxBeeCursor_next),
    Py_MethodListEntryNoArgs("prev",mxBeeCursor_prev),
    Py_MethodListEntryNoArgs("copy",mxBeeCursor_copy),
    {NULL,NULL} /* end of list */
};

/* --- Module Interface ---------------------------------------------------- */

Py_C_Function_WithKeywords(
    mxBeeIndex_BeeStringIndex,
    "BeeStringIndex(filename,keysize,dupkeys=0,filemode=0,sectorsize=256)\n\n"
    )
{
    char *filename;
    int keysize;
    int sectorsize = 256;
    int dupkeys = 0;
    int filemode = 0;

    Py_KeywordsGet5Args("si|iii",
			filename,keysize,dupkeys,filemode,sectorsize);

    return (PyObject *)mxBeeIndex_New(filename,
				      filemode,
				      keysize + 1,
				      sectorsize,
				      mxBeeIndex_CompareStrings,
				      mxBeeIndex_StringFromKey,
				      mxBeeIndex_KeyFromString,
				      dupkeys);
 onError:
    return NULL;
}

Py_C_Function_WithKeywords(
    mxBeeIndex_BeeFixedLengthStringIndex,
    "BeeFixedLengthStringIndex(filename,keysize,dupkeys=0,filemode=0,sectorsize=256)\n\n"
    )
{
    char *filename;
    int keysize;
    int sectorsize = 256;
    int dupkeys = 0;
    int filemode = 0;

    Py_KeywordsGet5Args("si|iii",
			filename,keysize,dupkeys,filemode,sectorsize);

    return (PyObject *)mxBeeIndex_New(filename,
				      filemode,
				      keysize + 1,
				      sectorsize,
				      mxBeeIndex_CompareFixedLengthStrings,
				      mxBeeIndex_FixedLengthStringFromKey,
				      mxBeeIndex_KeyFromFixedLengthString,
				      dupkeys);
 onError:
    return NULL;
}

Py_C_Function_WithKeywords(
    mxBeeIndex_BeeIntegerIndex,
    "BeeIntegerIndex(filename,dupkeys=0,filemode=0,sectorsize=256)\n\n"
    )
{
    char *filename;
    int keysize = sizeof(long);
    int sectorsize = 256;
    int dupkeys = 0;
    int filemode = 0;

    Py_KeywordsGet4Args("s|iii",
			filename,dupkeys,filemode,sectorsize);

    return (PyObject *)mxBeeIndex_New(filename,
				      filemode,
				      keysize,
				      sectorsize,
				      mxBeeIndex_CompareLongs,
				      mxBeeIndex_IntegerFromKey,
				      mxBeeIndex_KeyFromInteger,
				      dupkeys);
 onError:
    return NULL;
}

Py_C_Function_WithKeywords(
    mxBeeIndex_BeeFloatIndex,
    "BeeFloatIndex(filename,dupkeys=0,filemode=0,sectorsize=256)\n\n"
    )
{
    char *filename;
    int keysize = sizeof(double);
    int sectorsize = 256;
    int dupkeys = 0;
    int filemode = 0;

    Py_KeywordsGet4Args("s|iii",
			filename,dupkeys,filemode,sectorsize);

    return (PyObject *)mxBeeIndex_New(filename,
				      filemode,
				      keysize,
				      sectorsize,
				      mxBeeIndex_CompareDoubles,
				      mxBeeIndex_FloatFromKey,
				      mxBeeIndex_KeyFromFloat,
				      dupkeys);
 onError:
    return NULL;
}

/* Python Method Table */

static 
PyMethodDef Module_methods[] =
{   
    Py_MethodWithKeywordsListEntry("BeeStringIndex",
				   mxBeeIndex_BeeStringIndex),
    Py_MethodWithKeywordsListEntry("BeeFixedLengthStringIndex",
				   mxBeeIndex_BeeFixedLengthStringIndex),
    Py_MethodWithKeywordsListEntry("BeeIntegerIndex",
				   mxBeeIndex_BeeIntegerIndex),
    Py_MethodWithKeywordsListEntry("BeeFloatIndex",
				   mxBeeIndex_BeeFloatIndex),
    {NULL,NULL} /* end of list */
};

/* Cleanup function */
static 
void mxBeeBaseModule_Cleanup(void)
{
#ifdef MXBEEINDEX_FREELIST
    {
	mxBeeIndexObject *d = mxBeeIndex_FreeList;
	while (d != NULL) {
	    mxBeeIndexObject *v = d;
	    d = *(mxBeeIndexObject **)d;
	    PyObject_Del(v);
	}
	mxBeeIndex_FreeList = NULL;
    }
#endif
#ifdef MXBEECURSOR_FREELIST
    {
	mxBeeCursorObject *d = mxBeeCursor_FreeList;
	while (d != NULL) {
	    mxBeeCursorObject *v = d;
	    d = *(mxBeeCursorObject **)d;
	    PyObject_Del(v);
	}
	mxBeeCursor_FreeList = NULL;
    }
#endif
    /* Reset mxBeeBase_Initialized flag */
    mxBeeBase_Initialized = 0;
}

/* Create PyMethodObjects and register them in the module's dict */
MX_EXPORT(void) 
     initmxBeeBase(void)
{
    PyObject *module, *moddict;

    if (mxBeeBase_Initialized)
	Py_Error(PyExc_SystemError,
		 "can't initialize "MXBEEBASE_MODULE" more than once");

    /* Init type objects */
    PyType_Init(mxBeeIndex_Type);
    PyType_Init(mxBeeCursor_Type);

    /* Create module */
    module = Py_InitModule4(MXBEEBASE_MODULE, /* Module name */
			    Module_methods, /* Method list */
			    Module_docstring, /* Module doc-string */
			    (PyObject *)NULL, /* always pass this as *self */
			    PYTHON_API_VERSION); /* API Version */
    if (module == NULL)
	goto onError;

    /* Init globals */
#ifdef MXBEEINDEX_FREELIST
    mxBeeIndex_FreeList = NULL;
#endif
#ifdef MXBEECURSOR_FREELIST
    mxBeeCursor_FreeList = NULL;
#endif

    /* Register cleanup function */
    if (Py_AtExit(mxBeeBaseModule_Cleanup))
	/* XXX what to do if we can't register that function ??? */;

    /* Add some constants to the module's dict */
    moddict = PyModule_GetDict(module);
    if (moddict == NULL)
	goto onError;
    insobj(moddict,"__version__",PyString_FromString(MXBEEBASE_VERSION));
    insobj(moddict,"sizeof_bNode",PyInt_FromLong(sizeof(bNode)));
    insobj(moddict,"sizeof_bKey",PyInt_FromLong(sizeof(bKey)));
    insobj(moddict,"sizeof_bRecAddr",PyInt_FromLong(sizeof(bRecAddr)));
    insobj(moddict,"sizeof_bIdxAddr",PyInt_FromLong(sizeof(bIdxAddr)));

    /* Errors */
    if (!(mxBeeIndex_Error = insexc(moddict,"BeeIndexError")))
	goto onError;
    if (!(mxBeeCursor_Error = insexc(moddict,"BeeCursorError")))
	goto onError;

    /* Special keys */
    mxBeeIndex_FirstKey = insstr(moddict,"FirstKey","FirstKey");
    if (!mxBeeIndex_FirstKey)
	goto onError;
    mxBeeIndex_LastKey = insstr(moddict,"LastKey","LastKey");
    if (!mxBeeIndex_LastKey)
	goto onError;

    /* Type objects */
    Py_INCREF(&mxBeeIndex_Type);
    PyDict_SetItemString(moddict,"BeeIndexType",
			 (PyObject *)&mxBeeIndex_Type);
    Py_INCREF(&mxBeeCursor_Type);
    PyDict_SetItemString(moddict,"BeeCursorType",
			 (PyObject *)&mxBeeCursor_Type);

    /* We are now initialized */
    mxBeeBase_Initialized = 1;

 onError:
    /* Check for errors and report them */
    if (PyErr_Occurred())
	Py_ReportModuleInitError(MXBEEBASE_MODULE);
    return;
}
