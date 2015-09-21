/* 
  mxProxy -- Proxy wrapper type

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
# define MAL_DEBUG_OUTPUTFILE "mxProxy.log"
#endif

/* We want all our symbols to be exported */
#define MX_BUILDING_MXPROXY

/* Mark the module as Py_ssize_t clean. */
#define PY_SSIZE_T_CLEAN 1

/* Setup mxstdlib memory management */
#if 1
# define MAL_USE_PYMALLOC
#else
# define MAL_USE_C_MALLOC
#endif

#include "mx.h"
#include "mxProxy.h"

/* Version number: Major.Minor.Patchlevel */
#define MXPROXY_VERSION "3.2.9"

/* Define these to have the module use free lists (saves malloc calls) */
#ifndef Py_DEBUG
#define MXPROXY_FREELIST
#endif

/* --- module doc-string -------------------------------------------------- */

static char *Module_docstring = 

 MXPROXY_MODULE" -- Generic proxy wrapper type. Version "MXPROXY_VERSION"\n\n"

 "Copyright (c) 1998-2000, Marc-Andre Lemburg; mailto:mal@lemburg.com\n"
 "Copyright (c) 2000-2015, eGenix.com Software GmbH; mailto:info@egenix.com\n\n"
 "                 All Rights Reserved\n\n"
 "See the documentation for further information on copyrights,\n"
 "or contact the author."
;

/* --- module globals ----------------------------------------------------- */

static PyObject *mxProxy_AccessError;		/* AccessError Exception 
						   object */

static PyObject *mxProxy_LostReferenceError;	/* LostReferenceError
						   Exception object */

static PyObject *mxProxy_InternalError;		/* InternalError Exception 
						   object */

/* Free lists for Proxy and ProxyDelta objects */
#ifdef MXPROXY_FREELIST
static mxProxyObject *mxProxy_FreeList = NULL;
#endif

/* Dictionary mapping id integers to (object, [proxy1,proxy2,...])
   tuples. See WeakReference APIs below for details. */

static PyObject *mxProxy_WeakReferences;	

/* Flag telling us whether the module was initialized or not. */
static int mxProxy_Initialized = 0;

/* --- forward declarations ----------------------------------------------- */

staticforward PyTypeObject mxProxy_Type;
staticforward PyMethodDef mxProxy_Methods[];
static int mxProxy_DefuncObjectReference(mxProxyObject *self);
static int mxProxy_FinalizeWeakReferences(void);

/* --- internal macros ---------------------------------------------------- */

#define _mxProxy_Check(v) \
        (((mxProxyObject *)(v))->ob_type == &mxProxy_Type)

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
	modname = MXPROXY_MODULE;
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

/* Helper for adding integer constants to a dictionary. Check for
   errors with PyErr_Occurred() */
static 
void insstr(PyObject *dict,
	    char *name,
	    char *value)
{
    PyObject *v = PyString_FromString(value);
    PyDict_SetItemString(dict, name, v);
    Py_XDECREF(v);
}

#if 0
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
#endif

/* Converts a sequence to a lookup dictionary with entries
   key:None. For strings the string is taken as key, for other types
   the object's __name__ attribute. */

static
PyObject *seq2dict(PyObject *sequence)
{
    Py_ssize_t i,len;
    PyObject *v,*w;

    len = PySequence_Length(sequence);
    if (len < 0)
	goto onError;

    v = PyDict_New();
    for (i = 0; i < len; i++) {
	w = PySequence_GetItem(sequence,i);
	if (!w) {
	    Py_DECREF(v);
	    goto onError;
	}
	/* Not a string, so take the object's name instead */
	if (!PyString_Check(w)) {
	    PyObject *name;

	    name = PyObject_GetAttrString(w,"__name__");
	    if (!name) {
		Py_DECREF(w);
		Py_DECREF(v);
		goto onError;
	    }
	    Py_DECREF(w);
	    w = name;
	}
	PyDict_SetItem(v,w,Py_None);
	Py_DECREF(w);
    }
    return v;

 onError:
    return NULL;
}

/* (Re)Init the weak reference implementation. */

static
int mxProxy_InitWeakReferences(void)
{
    /* Finalize first... */
    if (mxProxy_WeakReferences != NULL &&
	mxProxy_FinalizeWeakReferences())
	goto onError;

    /* Create weak references dictionary */
    mxProxy_WeakReferences = PyDict_New();
    if (mxProxy_WeakReferences == NULL)
	goto onError;
    return 0;

 onError:
    return -1;
}

/* Store the object in the mxProxy_WeakReferences dict to keep it
   alive. Returns a new index object for the dictionary. */

static
int mxProxy_RegisterWeakReference(mxProxyObject *proxy,
				  PyObject *object)
{
    PyObject *id = 0,*v,*w;
    int rc;

    id = PyInt_FromLong((long)object);
    if (id == NULL)
	goto onError;
    DPRINTF("mxProxy_RegisterWeakReference: "
	    "proxy at %0lx, object at %0lx -> id at %0lx\n",
	    (long)proxy,(long)object,(long)id);
    Py_Assert(mxProxy_WeakReferences != NULL && 
	      mxProxy_WeakReferences->ob_refcnt > 0,
	      mxProxy_InternalError,
	      "mxProxy_WeakReferences dict is not available");

    v = PyDict_GetItem(mxProxy_WeakReferences,id);
    if (v && PyTuple_Check(v)) {
	mxProxyObject *p;
	
	/* Existing entry... add the proxy object to the linked list 
	   of weak proxies for this object */
	Py_Assert(PyTuple_GET_ITEM(v,0) == object,
		  mxProxy_InternalError,
		  "inconsistency in mxProxy_WeakReferences dict");
	p = (mxProxyObject *)PyCObject_AsVoidPtr(PyTuple_GET_ITEM(v,1));
	if (!p)
	    goto onError;
	while (p->next_weak_proxy != NULL)
	    p = p->next_weak_proxy;
	p->next_weak_proxy = proxy;
	DPRINTF("mxProxy_RegisterWeakReference: "
		"added a new weak proxy to the list\n");
	DPRINTF("                               "
		"refcounts... id:%i\n",
		id->ob_refcnt);
    }
    else {
	/* New entry: id:(object,CObject -> proxy) */
	w = PyCObject_FromVoidPtr((void*)proxy,NULL);
	if (!w)
	    goto onError;
	v = PyTuple_New(2);
	if (!v) {
	    Py_DECREF(w);
	    goto onError;
	}
	Py_INCREF(object);
	PyTuple_SET_ITEM(v,0,object);
	PyTuple_SET_ITEM(v,1,w);
	rc = PyDict_SetItem(mxProxy_WeakReferences,id,v);
	Py_DECREF(v);
	if (rc != 0)
	    goto onError;
	DPRINTF("mxProxy_RegisterWeakReference: "
		"added a new weak dict entry id %0lx in dict %0lx\n",
		(long)id,(long)mxProxy_WeakReferences);
	DPRINTF("                               "
		"refcounts... id:%i, dict:%i\n",
		id->ob_refcnt,mxProxy_WeakReferences->ob_refcnt);
    }
    
    proxy->object = id;
    proxy->next_weak_proxy = NULL;
    
    return 0;

 onError:
    Py_XDECREF(id);
    return -1;
}

/* Walks along the weak proxy linked list rooted at proxy and defuncs
   all proxies by notifying them via mxProxy_DefuncObjectReference().  

*/
static
int mxProxy_DefuncWeakProxies(mxProxyObject *proxy)
{
    DPRINTF("mxProxy_DefuncWeakProxies: proxy at %0lx\n",(long)proxy);
    do {
	mxProxy_DefuncObjectReference(proxy);
	proxy = proxy->next_weak_proxy;
    } while (proxy != NULL);
    if (PyErr_Occurred()) {
	DPRINTF("mxProxy_DefuncWeakProxies: "
		"errors occurred during notification\n");
	goto onError;
    }
    return 0;
    
 onError:
    return -1;
}

/* Removes the object reference from mxProxy_WeakReferences.

   This calls mxProxy_DefuncWeakProxies() to defunc all weak proxies
   using the object.

*/

static
int mxProxy_CollectWeakReference(mxProxyObject *proxy)
{
    PyObject *id,*v;

    id = proxy->object;
    DPRINTF("mxProxy_CollectWeakReference: "
	    "proxy at %0lx, id at %0lx\n",
	    (long)proxy,(long)id);
    Py_Assert(mxProxy_WeakReferences != NULL && 
	      mxProxy_WeakReferences->ob_refcnt > 0,
	      mxProxy_InternalError,
	      "mxProxy_WeakReferences dict is not available");
    if (id == NULL) {
	DPRINTF("mxProxy_CollectWeakReference: "
		"nothing to do -- proxy is defunct\n");
	return 0;
    }
    DPRINTF("                              "
	    "refcounts... id:%i, dict:%i\n",
	    id->ob_refcnt,mxProxy_WeakReferences->ob_refcnt);

    v = PyDict_GetItem(mxProxy_WeakReferences,id);
    if (v && PyTuple_Check(v)) {
	mxProxyObject *p;
	int rc;

	/* Keep the id object alive until the dict entry is removed. */
	Py_INCREF(id);

	/* Call the hook on all weak proxies in the linked list */
	DPRINTF("mxProxy_CollectWeakReference: "
		"notifying all weak proxies for this object\n");
	p = PyCObject_AsVoidPtr(PyTuple_GET_ITEM(v,1));
	if (!p)
	    goto onError;
	if (mxProxy_DefuncWeakProxies(p))
	    goto onError;

	/* Remove the mxProxy_WeakReferences entry for this object */
	DPRINTF("mxProxy_CollectWeakReference: "
		"removing the weak dict entry id %0lx from dict %0lx\n",
		(long)id,(long)mxProxy_WeakReferences);
	DPRINTF("                              "
		"refcounts... id:%i, dict:%i\n",
		id->ob_refcnt,mxProxy_WeakReferences->ob_refcnt);
	rc = PyDict_DelItem(mxProxy_WeakReferences,id);

	/* Dereference the id object, possibly GCing it and return. */
	Py_DECREF(id);
	return rc;
    }
    else
	Py_Error(mxProxy_InternalError,
		 "object not found in mxProxy_WeakReferences dict");
    return 0;

 onError:
    return -1;
}

/* Remove the reference to object in mxProxy_WeakReferences. 

   This can cause the object to be garbage collected. The index object
   is not DECREFed. */

static
int mxProxy_DeregisterWeakReference(mxProxyObject *proxy)
{
    PyObject *id,*v,*w;

    id = proxy->object;
    DPRINTF("mxProxy_DeregisterWeakReference: proxy at %0lx, id at %0lx\n",
	    (long)proxy,(long)id);
    Py_Assert(mxProxy_WeakReferences != NULL && 
	      mxProxy_WeakReferences->ob_refcnt > 0,
	      mxProxy_InternalError,
	      "mxProxy_WeakReferences dict is not available");
    if (id == NULL) {
	DPRINTF("mxProxy_DeregisterWeakReference: "
		"nothing to do -- proxy is defunct\n");
	return 0;
    }

    v = PyDict_GetItem(mxProxy_WeakReferences,id);
    if (v && PyTuple_Check(v)) {

	if (PyTuple_GET_ITEM(v,0)->ob_refcnt == 1)
	    /* GC the object along with the entry in
               mxProxy_WeakReferences */
	    return mxProxy_CollectWeakReference(proxy);

	else {
	    /* Remove the proxy object from the list */
	    mxProxyObject *p;

	    p = (mxProxyObject *)PyCObject_AsVoidPtr(PyTuple_GET_ITEM(v,1));
	    if (!p)
		goto onError;

	    if (p == proxy) {
		/* Change root of list */
		if (p->next_weak_proxy == NULL) {
		    /* No more entries in the list: remove the
		       mxProxy_WeakReferences entry altogether */
		    DPRINTF("mxProxy_DeregisterWeakReference: "
			    "removing weak reference dict entry\n");
		    return PyDict_DelItem(mxProxy_WeakReferences,id);
		}
		DPRINTF("mxProxy_DeregisterWeakReference: "
			"removing proxy entry at root of list\n");
		w = PyCObject_FromVoidPtr((void*)p->next_weak_proxy,NULL);
		if (!w)
		    goto onError;
		Py_DECREF(PyTuple_GET_ITEM(v,1));
		PyTuple_SET_ITEM(v,1,w);
		return 0;
	    }

	    else {
		/* Delink from list (contains at least 2 entries) */
		mxProxyObject *q;
		
		do {
		    q = p;
		    p = p->next_weak_proxy;
		} while (p && (p != proxy));
		Py_Assert(p != NULL,
			  mxProxy_InternalError,
			  "proxy object no longer in linked list");
		q->next_weak_proxy = p->next_weak_proxy;
		DPRINTF("mxProxy_DeregisterWeakReference: "
			"removed proxy entry from list\n");
	    }
	}
    }
    else {
	DPRINTF("mxProxy_DeregisterWeakReference: id not found !\n");
	Py_Error(mxProxy_InternalError,
		 "object not found in mxProxy_WeakReferences dict");
    }
    return 0;

 onError:
    return -1;
}

/* Returns a new reference to the weak referenced object indexed by
   id. 

   This also checks the refcount on the object. In case it has dropped
   to 1, the object is garbage collected, all proxies are informed of
   this and a LostReferenceError is raised. 

   If id is NULL a LostReferenceError is raised.

*/

static
PyObject *mxProxy_GetWeakReferenceObject(mxProxyObject *proxy)
{
    PyObject *id,*v,*w;

    id = proxy->object;
    DPRINTF("mxProxy_GetWeakReferenceObject: proxy at %0lx, id at %0lx\n",
	    (long)proxy,(long)id);
    Py_Assert(mxProxy_WeakReferences != NULL && 
	      mxProxy_WeakReferences->ob_refcnt > 0,
	      mxProxy_InternalError,
	      "mxProxy_WeakReferences dict is not available");
    Py_Assert(id != NULL,
	      mxProxy_LostReferenceError,
	      "object already garbage collected");

    v = PyDict_GetItem(mxProxy_WeakReferences,id);
    if (v && PyTuple_Check(v)) {
	if (PyTuple_GET_ITEM(v,0)->ob_refcnt == 1) {
	    /* GC the object along with the entry in
               mxProxy_WeakReferences and return an error */
	    DPRINTF("mxProxy_GetWeakReferenceObject: "
		    "garbage collecting object\n");
	    mxProxy_CollectWeakReference(proxy);
	    Py_Error(mxProxy_LostReferenceError,
		     "object already garbage collected");
	}
	w = PyTuple_GET_ITEM(v,0);
	Py_INCREF(w);
	DPRINTF("mxProxy_GetWeakReferenceObject: returning object at %0lx\n",
		(long)w);
	return w;
    }
    else
	Py_Error(mxProxy_InternalError,
		 "object not found in mxProxy_WeakReferences dict");

 onError:
    return NULL;
}

static
int _mxProxy_CollectWeakReferences(int force)
{
    PyObject *id,*v,*collect = 0;
    mxProxyObject *proxy;
    Py_ssize_t i;

    Py_Assert(mxProxy_WeakReferences != NULL &&
	      mxProxy_WeakReferences->ob_refcnt > 0,
	      mxProxy_InternalError,
	      "mxProxy_WeakReferences dict is not available");

    collect = PyList_New(0);
    if (!collect)
	goto onError;

    /* Find and mark the root proxies in mxProxy_WeakReferences
       pointing to (phantom) objects with refcount 1 (these are only
       artificially kept alive). */
    DPRINTF("mxProxy_CheckWeakReferenceDict: "
	    "checking for phantom objects\n");
    i = 0;
    while (PyDict_Next(mxProxy_WeakReferences, &i, &id, &v)) {
	if (PyTuple_Check(v) && 
	    (force || PyTuple_GET_ITEM(v,0)->ob_refcnt == 1)) {
	    proxy = (mxProxyObject *)PyCObject_AsVoidPtr(
						  PyTuple_GET_ITEM(v,1));
	    if (!proxy)
		goto onError;
	    DPRINTF("mxProxy_CheckWeakReferenceDict: "
		    "found proxy at %0lx, object at %0lx -> id at %0lx\n",
		    (long)proxy,(long)PyTuple_GET_ITEM(v,0),(long)id);
	    PyList_Append(collect,(PyObject *)proxy);
	}
    }
    
    /* Collect the weak references via the root proxies */
    for (i = 0; i < PyList_GET_SIZE(collect); i++) {

	proxy = (mxProxyObject *)PyList_GET_ITEM(collect,i);
	id = proxy->object;

	/* Defunc the weak proxies in the linked list */
	if (mxProxy_DefuncWeakProxies(proxy))
	    goto onError;

	/* Remove the mxProxy_WeakReferences entry for this object */
	DPRINTF("mxProxy_CheckWeakReferenceDict: "
		"removing the weak dict entry for id at %0x\n",id);
	if (PyDict_DelItem(mxProxy_WeakReferences,id))
	    goto onError;
    }

    Py_DECREF(collect);
    return 0;
    
 onError:
    Py_XDECREF(collect);
    return -1;
}

/* Checks the mxProxy_WeakReferences dict for entries which are only
   artificially kept alive.  */

static
int mxProxy_CheckWeakReferenceDict(void)
{
    return _mxProxy_CollectWeakReferences(0);
}

/* Remove the weak reference dict entries and then try to collect the
   mxProxy_WeakReferences dict itself. Sets mxProxy_WeakReferences to
   NULL, so all subsequent weak reference actions cause errors.  
   
   It is not an error to call this API again after completed
   finalization.

*/

static
int mxProxy_FinalizeWeakReferences(void)
{
    if (mxProxy_WeakReferences == NULL ||
	mxProxy_WeakReferences->ob_refcnt <= 0)
	return 0;
    if (_mxProxy_CollectWeakReferences(1))
	goto onError;
    Py_DECREF(mxProxy_WeakReferences);
    mxProxy_WeakReferences = NULL;
    return 0;

 onError:
    return -1;
}

/* --- Proxy Object -------------------------------------------------*/

/* --- allocation --- */

/* Create a new Proxy instance for object. interface may be a lookup
   dictionary (values are not used), a sequence (which is converted to
   a lookup dictionary by this function) or NULL (to disable the interface
   check). passobj can be used to reextract the wrapped object.

   If weak is true, a weak reference to the object will be created.
   The object itself will be stored in the global
   mxProxy_WeakReferences dictionary and the reference count on the
   object be checked prior to every operation.

*/
   
static
mxProxyObject *mxProxy_New(PyObject *object,
			   PyObject *interface,
			   PyObject *passobj,
			   int weak)
{
    mxProxyObject *proxy;

    /* Convert the interface object into a lookup dictionary */
    if (interface) {

	if (PyDict_Check(interface)) 
	    Py_INCREF(interface);

	/* Convert sequence to a dictionary */
	else if (PySequence_Check(interface)) {
	    interface = seq2dict(interface);
	    if (!interface)
		return NULL;
	}
	else
	    Py_Error(PyExc_TypeError,
		  "interface must be a dictionary, a sequence or not given");
    }
    
    /* Allocate the object */
#ifdef MXPROXY_FREELIST
    if (mxProxy_FreeList) {
	proxy = mxProxy_FreeList;
	mxProxy_FreeList = *(mxProxyObject **)mxProxy_FreeList;
	proxy->ob_type = &mxProxy_Type;
	_Py_NewReference(proxy);
    }
    else
#endif 
	 {
	proxy = PyObject_NEW(mxProxyObject,&mxProxy_Type);
	if (proxy == NULL) {
	    Py_XDECREF(interface);
	    return NULL;
	}
    }

    /* Keep a (weak) reference to the object */
    proxy->isWeak = (weak > 0);
    if (!weak) {
	Py_INCREF(object);
	proxy->object = object;
	proxy->next_weak_proxy = NULL;
    }
    else {
	/* Store the index object instead of the object itself. */
	if (mxProxy_RegisterWeakReference(proxy, object)) {
	    /* Make sure we deallocate the object without going through
	       mxProxy_Free() (as a result of a Py_DECREF). */
	    mxPy_UNREF(proxy);
	    PyObject_Del(proxy);
	    goto onError;
	}
    }

    /* Errors may not occur after this line, or the onError-mechanism
       must be changed. */

    /* Init vars */
    proxy->interface = interface;
    Py_XINCREF(passobj);
    proxy->passobj = passobj;

    /* Cache some methods (this is only done for strong referencing
       proxies since the method object contain a string reference to
       the object) */
    if (!weak && !PyMethod_Check(object) && !PyCFunction_Check(object)) {
	PyObject *v;

        v = PyObject_GetAttrString(object,"__public_getattr__");
	if (!v)
	    PyErr_Clear();
	proxy->public_getattr = v;

	v = PyObject_GetAttrString(object,"__public_setattr__");
	if (!v)
	    PyErr_Clear();
	proxy->public_setattr = v;

	v = PyObject_GetAttrString(object,"__cleanup__");
	if (!v)
	    PyErr_Clear();
	proxy->cleanup = v;
    }
    else {
	proxy->public_getattr = NULL;
	proxy->public_setattr = NULL;
	proxy->cleanup = NULL;
    }
    
    return proxy;

 onError:
    return NULL;
}

/* --- deallocation --- */

static
void mxProxy_Free(mxProxyObject *proxy)
{
    PyObject *error_type, *error_value, *error_traceback;

    DPRINTF("mxProxy_Free: instance at %0lx\n",(long)proxy);

    /* Allow cleanup of the wrapped object; errors are printed to stderr */
    if (proxy->cleanup) {
	PyObject *v;
	
	/* Reanimate the Proxy */
	Py_INCREF(proxy);
	
	/* Save the current exception, since the call could produce a
	   new one which will get cleared. */
	PyErr_Fetch(&error_type, &error_value, &error_traceback);

	DPRINTF("mxProxy_Free: __cleanup__\n");
	v = PyEval_CallObject(proxy->cleanup,(PyObject *)NULL);
	if (!v) {
	    /* We print the error only if Python is run in debug mode
	       since this sometimes causes strange core dumps... */
	    if (PyErr_Occurred() && Py_DebugFlag) {
		fprintf(stderr,"Error in ");
		PyObject_Print(proxy->cleanup,stderr,Py_PRINT_RAW);
		fprintf(stderr," ignored:\n");
		PyErr_Print();
	    }
	    else if (Py_VerboseFlag) {
		fprintf(stderr,"Error in ");
		PyObject_Print(proxy->cleanup,stderr,Py_PRINT_RAW);
		fprintf(stderr,
			" ignored.\n"
			"(run in debug mode to have the error printed)\n");
	    }
	    PyErr_Clear();
	}
	else
	    Py_DECREF(v);
	
	/* Restore the previous exception */
	PyErr_Restore(error_type, error_value, error_traceback);

	/* We might be referenced again somewhere... */
	if (proxy->ob_refcnt > 1) {
	    DPRINTF("mxProxy_Free: kept alive; refcnt=%i\n",proxy->ob_refcnt);
	    Py_DECREF(proxy);
	    return;
	}
    }

    /* Remove the object reference */
    DPRINTF("mxProxy_Free: removing object reference\n");
    if (proxy->isWeak) {
	/* Reanimate the Proxy */
	Py_INCREF(proxy);

	/* Save the current exception, since the call could produce a
	   new one which will get cleared. */
	PyErr_Fetch(&error_type, &error_value, &error_traceback);

	/* Deregister the weak reference. XXX Errors are ignored. */
	if (mxProxy_DeregisterWeakReference(proxy))
	    PyErr_Clear();

	/* Restore the previous exception */
	PyErr_Restore(error_type, error_value, error_traceback);

	/* We might be referenced again somewhere... */
	if (proxy->ob_refcnt > 1) {
	    DPRINTF("mxProxy_Free: kept alive; refcnt=%i\n",proxy->ob_refcnt);
	    Py_DECREF(proxy);
	    return;
	}
    }
    /* proxy->object could have already be defunct by a call to
       mxProxy_DefuncObjectReference(). */
    Py_XDECREF(proxy->object);

    /* Now remove all references */
    DPRINTF("mxProxy_Free: removing other references\n");
    Py_XDECREF(proxy->interface);
    Py_XDECREF(proxy->passobj);
    Py_XDECREF(proxy->public_getattr);
    Py_XDECREF(proxy->public_setattr);
    Py_XDECREF(proxy->cleanup);

    DPRINTF("mxProxy_Free: refcnt=%i\n",proxy->ob_refcnt);
	
#ifdef MXPROXY_FREELIST
    /* Append to free list */
    *(mxProxyObject **)proxy = mxProxy_FreeList;
    mxProxy_FreeList = proxy;
#else
    PyObject_Del(proxy);
#endif
    DPRINTF("mxProxy_Free: dealloced\n");
}

/* --- internal functions --- */

/* Is direct access to slot name allowed ? Returns 1/0. 

   Slots are named just like the hooks that are available for them on
   instance objects, e.g. '__call__' for tp_call. name has to be an
   object; this is typically an interned string object.

*/
static
int mxProxy_SlotAccessAllowed(mxProxyObject *self,
			      PyObject *name)
{
    PyObject *v;
    
    DPRINTF("mxProxy_SlotAccessAllowed: "
	    "proxy at %0lx, object/index at %0lx, slot '%.200s' -- ",
	    (long)self,(long)self->object,PyString_AS_STRING(name));

    /* Is interface access restricted ? */
    if (!self->interface) {
	DPRINTF("yes (unrestricted)\n");
	return 1;
    }
    
    /* Check if the interface dict allows access (if given) */
    v = PyDict_GetItem(self->interface,name);
    if (!v) {
	PyErr_Clear();
	DPRINTF("no\n");
	return 0;
    }
    DPRINTF("yes\n");
    return 1;
}

/* This hook is called to inform the weak referencing Proxy of the
   fact that his object reference is about to be garbage collected.

   The hook DECREFs index object stored in self->object and then sets
   self->object to NULL. */

static
int mxProxy_DefuncObjectReference(mxProxyObject *self)
{
    DPRINTF("mxProxy_DefuncObjectReference: "
	    "proxy at %0lx, object/index at %0lx\n",
	    (long)self,(long)self->object);
    Py_XDECREF(self->object);
    self->object = NULL;
    return 0;
}

/* Get attribute name from the wrapped object */

static
PyObject *mxProxy_GetattrObject(PyObject *obj,
				PyObject *name)
{
    mxProxyObject *self = (mxProxyObject *)obj;
    PyObject *v;

    DPRINTF("mxProxy_GetattrObject: %.200s\n",
	    PyString_AsString(PyObject_Str(name)));

    /* Access to Proxy methods is never restricted */
    if (PyString_Check(name)) {
	char *sname = PyString_AS_STRING(name);
	
	if (sname[0] == 'p' && sname[1] == 'r' && 
	    sname[2] == 'o' && sname[3] == 'x' &&
	    sname[4] == 'y' && sname[5] == '_')
	    return Py_FindMethod(mxProxy_Methods,
				 (PyObject *)self,sname);
    }
    
    /* Check if the interface dict allows access (if given) */
    if (self->interface) {
	v = PyDict_GetItem(self->interface,name);
	if (!v) {
	    PyErr_Clear();
	    goto noAccess;
	}
    }

    /* Get the attribute via the __public_getattr__ hook (if given),
       or use the standard method; note that weak Proxies do not cache
       the __public_getattr__ hook. */
    if (self->public_getattr) {
	PyObject *arg;

	arg = PyTuple_New(1);
	if (!arg)
	    goto onError;
	Py_INCREF(name);
	PyTuple_SET_ITEM(arg,0,name);
	v = PyEval_CallObject(self->public_getattr,arg);
	Py_DECREF(arg);
    }
    else {
	if (self->isWeak) {
	    PyObject *object;
	
	    object  = mxProxy_GetWeakReferenceObject(self);
	    if (!object)
		goto onError;
	    v = PyObject_GetAttr(object,name);
	    Py_DECREF(object);
	}
	else
	    v = PyObject_GetAttr(self->object,name);
    }
    if (!v)
	goto onError;

    /* Check if we have to wrap a method (which carries a reference to
       the wrapped object) */
    if (PyMethod_Check(v) || PyCFunction_Check(v)) {
	static PyObject *callinterface;
	PyObject *w;
	
	DPRINTF("mxProxy_GetattrObject: wrapping method %.200s\n",
		PyString_AsString(PyObject_Str(v)));
	if (callinterface == NULL)
	    callinterface = Py_BuildValue("{s:O}","__call__",Py_None);
	w = (PyObject *)mxProxy_New(v,callinterface,NULL,0);
	Py_DECREF(v);
	v = w;
    }
    return v;

 noAccess:
    if (PyString_Check(name)) {
	Py_ErrorWithArg(mxProxy_AccessError,
			"attribute read access (%.200s) denied",
			PyString_AS_STRING(name));
    }
    else {
	Py_Error(mxProxy_AccessError,
		 "attribute read access denied");
    }

 onError:
    return NULL;
}

/* Set attribute name of the wrapped object to value; return -1 in
   case of error, 0 otherwise. */

static
int mxProxy_SetattrObject(PyObject *obj,
			  PyObject *name,
			  PyObject *value)
{
    mxProxyObject *self = (mxProxyObject *)obj;
    PyObject *v;
    
    DPRINTF("mxProxy_SetattrObject: %.200s to %.200s\n",
	    PyString_AsString(PyObject_Str(name)),
	    PyString_AsString(PyObject_Repr(value)));

    /* Check if the interface dict allows access (if given) */
    if (self->interface) {
	v = PyDict_GetItem(self->interface,name);
	if (!v) {
	    PyErr_Clear();
	    goto noAccess;
	}
    }

    /* Use the __public_setattr__ hook (if given), or the standard
       method; note that weak Proxies do not cache the
       __public_setattr__ hook. */
    if (self->public_setattr) {
	PyObject *arg;

	arg = PyTuple_New(2);
	if (!arg)
	    goto onError;
	Py_INCREF(name);
	PyTuple_SET_ITEM(arg,0,name);
	Py_INCREF(value);
	PyTuple_SET_ITEM(arg,1,value);
	v = PyEval_CallObject(self->public_setattr,arg);
	Py_DECREF(arg);
	if (!v)
	    goto onError;
	Py_DECREF(v);
	return 0;
    }
    else {
	if (self->isWeak) {
	    PyObject *object;
	    int rc;
	    
	    object  = mxProxy_GetWeakReferenceObject(self);
	    if (!object)
		goto onError;
	    rc = PyObject_SetAttr(object,name,value);
	    Py_DECREF(object);
	    return rc;
	}
	else
	    return PyObject_SetAttr(self->object,name,value);
    }
    
 noAccess:
    if (PyString_Check(name)) {
	Py_ErrorWithArg(mxProxy_AccessError,
			"attribute write access (%.200s) denied",
			PyString_AS_STRING(name));
    }
    else {
	Py_Error(mxProxy_AccessError,
		 "attribute write access denied");
    }

 onError:
    return -1;
}

/* --- API functions --- */

/* --- methods --- */

#define proxy ((mxProxyObject*)self)

Py_C_Function( mxProxy_proxy_object,
	       "proxy_object(passobj)\n\n"
	       "Returns the wrapped object as-is provided the passobj\n"
	       "matches the one given at creation time."
	       )
{
    PyObject *passobj;
    
    Py_GetArg("O",passobj);
    if (passobj == proxy->passobj) {
	if (proxy->isWeak) {
	    PyObject *object;
	    
	    object  = mxProxy_GetWeakReferenceObject(proxy);
	    if (!object)
		goto onError;
	    return object;
	}
	else {
	    Py_INCREF(proxy->object);
	    return proxy->object;
	}
    }
    Py_Error(mxProxy_AccessError,
	     "wrong pass-object");
 onError:
    return NULL;
}

Py_C_Function( mxProxy_proxy_defunct,
	       "proxy_defunct()\n\n"
	       "Returns 1 iff the referenced object has already been\n"
	       "garbage collected."
	       )
{
    Py_NoArgsCheck();
    if (proxy->object == NULL)
	return PyInt_FromLong(1);
    else
	return PyInt_FromLong(0);

 onError:
    return NULL;
}

Py_C_Function( mxProxy_proxy_getattr,
	       "proxy_getattr(name)\n\n"
	       "Tries to get the attribute name from the wrapped object\n"
	       "and returns it. The normal access restriction apply."
	       )
{
    PyObject *name;
    
    Py_GetArg("O",name);
    return mxProxy_GetattrObject(self, name);
    
 onError:
    return NULL;
}

Py_C_Function( mxProxy_proxy_setattr,
	       "proxy_setattr(name,value)\n\n"
	       "Tries to set the attribute name of the wrapped object\n"
	       "to value. The normal access restriction apply."
	       )
{
    PyObject *name,*value;
    
    Py_Get2Args("OO",name,value);
    if (mxProxy_SetattrObject(self, name, value))
	goto onError;
    Py_ReturnNone();
    
 onError:
    return NULL;
}

#undef proxy

/* --- slots --- */

static
PyObject *mxProxy_Repr(PyObject *obj)
{
    mxProxyObject *self = (mxProxyObject *)obj;
    char t[100];

    if (self->isWeak) {
	if (self->object)
	    sprintf(t,"<WeakProxy object at %lx>",(long)self);
	else
	    sprintf(t,"<defunct WeakProxy object at %lx>",(long)self);
    }
    else
	sprintf(t,"<Proxy object at %lx>",(long)self);
    return PyString_FromString(t);
}

/* Macros for defining slot APIs with 0,1,2 and 3 arguments */

#define SLOT_API_PPP(name,slot,function,arg1type,arg2type,arg3type,rctype,errorrc) \
static										   \
rctype name(PyObject *obj, arg1type v, arg2type w, arg3type x)		   \
{										   \
    mxProxyObject *self = (mxProxyObject *)obj;		\
    static PyObject *slotstr;							   \
										   \
    if (slotstr == NULL)								   \
	slotstr = PyString_InternFromString(#slot);			   \
    Py_Assert(mxProxy_SlotAccessAllowed(self,slotstr),				   \
	      mxProxy_AccessError,						   \
	      #slot" access denied");					   \
    if (self->isWeak) {								   \
        PyObject *object;							   \
        rctype rc;								   \
	object = mxProxy_GetWeakReferenceObject(self);				   \
	if (!object)								   \
	    goto onError;							   \
        rc = function(object,v,w,x);						   \
        Py_DECREF(object);							   \
	return rc;								   \
    }										   \
    else									   \
        return function(self->object,v,w,x);					   \
										   \
 onError:									   \
    return errorrc;								   \
}

#define SLOT_API_PP(name,slot,function,arg1type,arg2type,rctype,errorrc)	   \
static										   \
rctype name(PyObject *obj, arg1type v, arg2type w)			   \
{										   \
    mxProxyObject *self = (mxProxyObject *)obj;		\
    static PyObject *slotstr;							   \
										   \
    if (slotstr == NULL)								   \
	slotstr = PyString_InternFromString(#slot);			   \
    Py_Assert(mxProxy_SlotAccessAllowed(self,slotstr),				   \
	      mxProxy_AccessError,						   \
	      #slot" access denied");					   \
    if (self->isWeak) {								   \
        PyObject *object;							   \
        rctype rc;								   \
	object = mxProxy_GetWeakReferenceObject(self);				   \
	if (!object)								   \
	    goto onError;							   \
        rc = function(object,v,w);						   \
        Py_DECREF(object);							   \
	return rc;								   \
    }										   \
    else									   \
        return function(self->object,v,w);					   \
										   \
 onError:									   \
    return errorrc;								   \
}

#define SLOT_API_P(name,slot,function,arg1type,rctype,errorrc)			   \
static										   \
rctype name(PyObject *obj, arg1type v)					   \
{										   \
    mxProxyObject *self = (mxProxyObject *)obj;		\
    static PyObject *slotstr;							   \
										   \
    if (slotstr == NULL)								   \
	slotstr = PyString_InternFromString(#slot);			   \
    Py_Assert(mxProxy_SlotAccessAllowed(self,slotstr),				   \
	      mxProxy_AccessError,						   \
	      #slot" access denied");					   \
    if (self->isWeak) {								   \
        PyObject *object;							   \
        rctype rc;								   \
	object = mxProxy_GetWeakReferenceObject(self);				   \
	if (!object)								   \
	    goto onError;							   \
        rc = function(object,v);						   \
        Py_DECREF(object);							   \
	return rc;								   \
    }										   \
    else									   \
        return function(self->object,v);					   \
										   \
 onError:									   \
    return errorrc;								   \
}

#define SLOT_API(name,slot,function,rctype,errorrc)	\
static							\
rctype name(PyObject *obj)				\
{							\
    mxProxyObject *self = (mxProxyObject *)obj;		\
    static PyObject *slotstr;				\
							\
    if (slotstr == NULL)				\
	slotstr = PyString_InternFromString(#slot);	\
    Py_Assert(mxProxy_SlotAccessAllowed(self,slotstr),	\
	      mxProxy_AccessError,			\
	      #slot" access denied");			\
    if (self->isWeak) {					\
        PyObject *object;				\
        rctype rc;					\
	object = mxProxy_GetWeakReferenceObject(self);	\
	if (!object)					\
	    goto onError;				\
        rc = function(object);				\
        Py_DECREF(object);				\
	return rc;					\
    }							\
    else						\
        return function(self->object);			\
							\
 onError:						\
    return errorrc;					\
}

/* Basic slots */
SLOT_API_PP(mxProxy_Call,__call__,PyEval_CallObjectWithKeywords,PyObject*,PyObject*,PyObject*,NULL)
SLOT_API(mxProxy_Hash,__hash__,PyObject_Hash,long,-1)
SLOT_API(mxProxy_Str,__str__,PyObject_Str,PyObject*,NULL)
SLOT_API_P(mxProxy_Compare,__cmp__,PyObject_Compare,PyObject*,int,-1)

/* Mapping */
SLOT_API_P(mxProxy_GetItem,__getitem__,PyObject_GetItem,PyObject*,PyObject*,NULL)
SLOT_API_PP(mxProxy_SetItem,__setitem__,PyObject_SetItem,PyObject*,PyObject*,int,-1)
SLOT_API(mxProxy_Length,__len__,PyObject_Length,Py_ssize_t,-1)

/* Sequence */
SLOT_API_P(mxProxy_Concat,__add__,PySequence_Concat,PyObject*,PyObject*,NULL)
SLOT_API_P(mxProxy_Repeat,__repeat__,PySequence_Repeat,Py_ssize_t,PyObject*,NULL)
SLOT_API_P(mxProxy_GetIndex,__getitem__,PySequence_GetItem,Py_ssize_t,PyObject*,NULL)
SLOT_API_PP(mxProxy_GetSlice,__getslice__,PySequence_GetSlice,Py_ssize_t,Py_ssize_t,PyObject*,NULL)
SLOT_API_PP(mxProxy_SetIndex,__setitem__,PySequence_SetItem,Py_ssize_t,PyObject*,int,-1)
SLOT_API_PPP(mxProxy_SetSlice,__getitem__,PySequence_SetSlice,Py_ssize_t,Py_ssize_t,PyObject*,int,-1)

/* Number

   Note: number coercion does not work yet, so most of these are
   currently useless !

*/
SLOT_API_P(mxProxy_Add,__add__,PyNumber_Add,PyObject*,PyObject*,NULL)
SLOT_API_P(mxProxy_Sub,__sub__,PyNumber_Subtract,PyObject*,PyObject*,NULL)
SLOT_API_P(mxProxy_Multiply,__mul__,PyNumber_Multiply,PyObject*,PyObject*,NULL)
SLOT_API_P(mxProxy_Divide,__div__,PyNumber_Divide,PyObject*,PyObject*,NULL)
SLOT_API_P(mxProxy_Remainder,__mod__,PyNumber_Remainder,PyObject*,PyObject*,NULL)
SLOT_API_P(mxProxy_Divmod,__divmod__,PyNumber_Divmod,PyObject*,PyObject*,NULL)
SLOT_API_PP(mxProxy_Power,__pow__,PyNumber_Power,PyObject*,PyObject*,PyObject*,NULL)
SLOT_API(mxProxy_Negative,__neg__,PyNumber_Negative,PyObject*,NULL)
SLOT_API(mxProxy_Positive,__pos__,PyNumber_Positive,PyObject*,NULL)
SLOT_API(mxProxy_Absolute,__abs__,PyNumber_Absolute,PyObject*,NULL)
SLOT_API(mxProxy_IsTrue,__true__,PyObject_IsTrue,int,-1)
SLOT_API(mxProxy_Invert,__invert__,PyNumber_Invert,PyObject*,NULL)
SLOT_API_P(mxProxy_Lshift,__lshift__,PyNumber_Lshift,PyObject*,PyObject*,NULL)
SLOT_API_P(mxProxy_Rshift,__rshift__,PyNumber_Rshift,PyObject*,PyObject*,NULL)
SLOT_API_P(mxProxy_And,__and__,PyNumber_And,PyObject*,PyObject*,NULL)
SLOT_API_P(mxProxy_Xor,__str__,PyNumber_Xor,PyObject*,PyObject*,NULL)
SLOT_API_P(mxProxy_Or,__or__,PyNumber_Or,PyObject*,PyObject*,NULL)
    /* coerce ? */
SLOT_API(mxProxy_Int,__int__,PyNumber_Int,PyObject*,NULL)
SLOT_API(mxProxy_Long,__long__,PyNumber_Long,PyObject*,NULL)
SLOT_API(mxProxy_Float,__float__,PyNumber_Float,PyObject*,NULL)
    /* oct, hex ? */

/* Python Type Tables */

static PyNumberMethods mxProxy_NumberSlots = {
    mxProxy_Add, 			/*nb_add*/
    mxProxy_Sub, 			/*nb_subtract*/
    mxProxy_Multiply, 			/*nb_multiply*/
    mxProxy_Divide, 			/*nb_divide*/
    mxProxy_Remainder, 			/*nb_remainder*/
    mxProxy_Divmod, 			/*nb_divmod*/
    mxProxy_Power, 			/*nb_power*/
    mxProxy_Negative, 			/*nb_negative*/
    mxProxy_Positive, 			/*nb_positive*/
    mxProxy_Absolute, 			/*nb_absolute*/
    mxProxy_IsTrue, 			/*nb_nonzero*/
    mxProxy_Invert, 			/*nb_invert*/
    mxProxy_Lshift, 			/*nb_lshift*/
    mxProxy_Rshift, 			/*nb_rshift*/
    mxProxy_And, 			/*nb_and*/
    mxProxy_Xor, 			/*nb_xor*/
    mxProxy_Or, 			/*nb_or*/
    0,					/*nb_coerce*/
    mxProxy_Int, 			/*nb_int*/
    mxProxy_Long, 			/*nb_long*/
    mxProxy_Float, 			/*nb_float*/
    0, 					/*nb_oct*/
    0, 					/*nb_hex*/
};

static PySequenceMethods mxProxy_SequenceSlots = {
    mxProxy_Length, 			/*sq_length*/
    mxProxy_Concat, 			/*sq_concat*/
    mxProxy_Repeat, 			/*sq_repeat*/
    mxProxy_GetIndex, 			/*sq_item*/
    mxProxy_GetSlice,			/*sq_slice*/
    mxProxy_SetIndex,			/*sq_ass_item*/
    mxProxy_SetSlice,			/*sq_ass_slice*/
};

static PyMappingMethods mxProxy_MappingSlots = {
    mxProxy_Length,			/*mp_length*/
    mxProxy_GetItem, 			/*mp_subscript*/
    mxProxy_SetItem,			/*mp_ass_subscript*/
};

statichere
PyTypeObject mxProxy_Type = {
    PyObject_HEAD_INIT(0)		/* init at startup ! */
    0,			  		/*ob_size*/
    "Proxy",	  			/*tp_name*/
    sizeof(mxProxyObject),      	/*tp_basicsize*/
    0,			  		/*tp_itemsize*/
    /* slots */
    (destructor)mxProxy_Free,		/*tp_dealloc*/
    0,		 		 	/*tp_print*/
    0,				  	/*tp_getattr -- see tp_getattro */
    0,				  	/*tp_setattr -- see tp_setattro */
    mxProxy_Compare, 	       		/*tp_compare*/
    mxProxy_Repr,	  		/*tp_repr*/
    &mxProxy_NumberSlots, 		/*tp_as_number*/
    &mxProxy_SequenceSlots,		/*tp_as_sequence*/
    &mxProxy_MappingSlots,		/*tp_as_mapping*/
    mxProxy_Hash,			/*tp_hash*/
    mxProxy_Call,			/*tp_call*/
    mxProxy_Str,			/*tp_str*/
    mxProxy_GetattrObject,		/*tp_getattro*/
    mxProxy_SetattrObject,		/*tp_setattro*/
    0,					/*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT,			/*tp_flags*/
    0,					/* tp_doc */
    0,					/* tp_traverse */
    0,					/* tp_clear */
    0,					/* tp_richcompare */
    0,					/* tp_weaklistoffset */
    0,					/* tp_iter */
    0,					/* tp_iternext */
    mxProxy_Methods,			/* tp_methods */
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

/* Python Method Table

   Note: Proxy method names must start with 'proxy_' and cannot be
         access restricted.

 */

statichere
PyMethodDef mxProxy_Methods[] =
{   
    Py_MethodListEntry("proxy_getattr",mxProxy_proxy_getattr),
    Py_MethodListEntry("proxy_setattr",mxProxy_proxy_setattr),
    Py_MethodListEntry("proxy_object",mxProxy_proxy_object),
    Py_MethodListEntryNoArgs("proxy_defunct",mxProxy_proxy_defunct),
    {NULL,NULL} /* end of list */
};

/* --- module interface ---------------------------------------------------- */

Py_C_Function( mxProxy_Proxy,
	       "Proxy(object,interface=None,passobj=None)\n"
	       )
{
    PyObject *object,*interface = NULL,*passobj=NULL;

    Py_Get3Args("O|OO",object,interface,passobj);
    if (interface == Py_None)
	interface = NULL;
    if (passobj == Py_None)
	passobj = NULL;
    return (PyObject *)mxProxy_New(object,interface,passobj,0);
 onError:
    return NULL;
}

Py_C_Function( mxProxy_WeakProxy,
	       "WeakProxy(object,interface=None,passobj=None)\n"
	       )
{
    PyObject *object,*interface = NULL,*passobj=NULL;

    Py_Get3Args("O|OO",object,interface,passobj);
    if (interface == Py_None)
	interface = NULL;
    if (passobj == Py_None)
	passobj = NULL;
    return (PyObject *)mxProxy_New(object,interface,passobj,1);
 onError:
    return NULL;
}

Py_C_Function( mxProxy_checkweakrefs,
	       "checkweakrefs()\n"
	       )
{
    Py_NoArgsCheck();
    if (mxProxy_CheckWeakReferenceDict())
	goto onError;
    Py_ReturnNone();

 onError:
    return NULL;
}

Py_C_Function( mxProxy_initweakrefs,
	       "initweakrefs()\n"
	       )
{
    Py_NoArgsCheck();
    if (mxProxy_InitWeakReferences())
	goto onError;
    Py_ReturnNone();

 onError:
    return NULL;
}

Py_C_Function( mxProxy_finalizeweakrefs,
	       "finalizeweakrefs()\n"
	       )
{
    Py_NoArgsCheck();
    if (mxProxy_FinalizeWeakReferences())
	goto onError;
    Py_ReturnNone();

 onError:
    return NULL;
}

/* Python Method Table */

static 
PyMethodDef Module_methods[] =
{   
    Py_MethodListEntry("Proxy",mxProxy_Proxy),
    Py_MethodListEntry("WeakProxy",mxProxy_WeakProxy),
    Py_MethodListEntryNoArgs("checkweakrefs",mxProxy_checkweakrefs),
    Py_MethodListEntryNoArgs("initweakrefs",mxProxy_initweakrefs),
    Py_MethodListEntryNoArgs("finalizeweakrefs",mxProxy_finalizeweakrefs),
    {NULL,NULL} /* end of list */
};

/* Cleanup function */
static 
void mxProxyModule_Cleanup(void)
{
#ifdef MXPROXY_FREELIST
    {
	mxProxyObject *d = mxProxy_FreeList;
	while (d != NULL) {
	    mxProxyObject *v = d;
	    d = *(mxProxyObject **)d;
	    PyObject_Del(v);
	}
	mxProxy_FreeList = NULL;
    }
#endif
    /* mxProxy_WeakReferences should have a refcount of zero by now;
       if it doesn't, then someone got hold of the dictionary
       available through the module dictionary... too bad: weak
       references won't work then. */
    if (mxProxy_WeakReferences != NULL && 
	mxProxy_WeakReferences->ob_refcnt > 0) {
	DPRINTF("mxProxyModule_Cleanup: "
		"memory leak: weakref dictionary still alive\n");
    }
    mxProxy_WeakReferences = NULL;

    /* Reset mxProxy_Initialized flag */
    mxProxy_Initialized = 0;
}

/* Create PyMethodObjects and register them in the module's dict */
MX_EXPORT(void) 
     initmxProxy(void)
{
    PyObject *module, *moddict;

    if (mxProxy_Initialized)
	Py_Error(PyExc_SystemError,
		 "can't initialize "MXPROXY_MODULE" more than once");

    /* Init type objects */
    PyType_Init(mxProxy_Type);

    /* Create module */
    module = Py_InitModule4(MXPROXY_MODULE, /* Module name */
			    Module_methods, /* Method list */
			    Module_docstring, /* Module doc-string */
			    (PyObject *)NULL, /* always pass this as *self */
			    PYTHON_API_VERSION); /* API Version */
    if (module == NULL)
	goto onError;

    /* Init globals */
#ifdef MXPROXY_FREELIST
    mxProxy_FreeList = NULL;
#endif

    /* Register cleanup function */
    if (Py_AtExit(mxProxyModule_Cleanup))
	/* XXX what to do if we can't register that function ??? */;

    /* Init weak reference implementation */
    if (mxProxy_InitWeakReferences())
	goto onError;

    /* Add some constants to the module's dict */
    moddict = PyModule_GetDict(module);
    if (moddict == NULL)
	goto onError;
    insstr(moddict,"__version__",MXPROXY_VERSION);

    /* Errors */
    if (!(mxProxy_AccessError = insexc(moddict,
				       "AccessError",
				       PyExc_AttributeError)))
	goto onError;
    if (!(mxProxy_LostReferenceError = insexc(moddict,
					      "LostReferenceError",
					      mxProxy_AccessError)))
	goto onError;
    if (!(mxProxy_InternalError = insexc(moddict,
					 "InternalError",
					 PyExc_StandardError)))
	goto onError;

    /* Type objects */
    Py_INCREF(&mxProxy_Type);
    PyDict_SetItemString(moddict,"ProxyType",
			 (PyObject *)&mxProxy_Type);
    DPRINTF("initmxProxy: module dict refcnt = %i\n",
	    moddict->ob_refcnt);

    /* We are now initialized */
    mxProxy_Initialized = 1;

 onError:
    /* Check for errors and report them */
    if (PyErr_Occurred())
	Py_ReportModuleInitError(MXPROXY_MODULE);
    return;
}
