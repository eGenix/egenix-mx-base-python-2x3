/* 
  mxURL -- An URL datatype

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
# define MAL_DEBUG_OUTPUTFILE "mxURL.log"
#endif

/* We want all our symbols to be exported */
#define MX_BUILDING_MXURL

/* Mark the module as Py_ssize_t clean. */
#define PY_SSIZE_T_CLEAN 1

/* Setup mxstdlib memory management */
#if 1
# define MAL_USE_PYMALLOC
#else
# define MAL_USE_C_MALLOC
#endif

#include "mx.h"
#include "mxURL.h"
#include <ctype.h>

/* Version number: Major.Minor.Patchlevel */
#define MXURL_VERSION "3.2.9"

/* Define this to have the module use a free list for URLs */
#ifndef Py_DEBUG
#define MXURL_FREELIST
#endif

/* Define this to enable the speedup in mxURL_SchemeUsesRelativePaths()
   that uses hard-coded values for the uses_relative part of the
   scheme dict entries in mxURL_SchemeDict below. Saves a
   dictionary lookup for every join. */
#define HARDCODE_SCHEMES_USES_RELATIVE

/* --- module doc-string -------------------------------------------------- */

static char *Module_docstring = 

 MXURL_MODULE" -- An URL datatype.\n\n"

 "Version "MXURL_VERSION"\n\n"

 "Copyright (c) 1998-2000, Marc-Andre Lemburg; mailto:mal@lemburg.com\n"
 "Copyright (c) 2000-2015, eGenix.com Software GmbH; mailto:info@egenix.com\n\n"
 "                 All Rights Reserved\n\n"
 "See the documentation for further information on copyrights,\n"
 "or contact the author."
;

/* --- module globals ----------------------------------------------------- */

static PyObject *mxURL_Error;		/* Error Exception object */


/* URL free list */
#ifdef MXURL_FREELIST
static mxURLObject *mxURL_FreeList = NULL;
#endif

/* Dictionary providing feature information about the different
   supported schemes:

   'http':(uses_netloc, uses_params, uses_query, uses_fragment, uses_relative)

   The entries must be tuples of integers representing true (non zero)
   / false (zero).

*/

static PyObject *mxURL_SchemeDict;

typedef struct {
    char *scheme;			/* Scheme */
    					/* Features - 1/0 */
    int uses_netloc, uses_params, uses_query, uses_fragment, uses_relative;
} mxURL_SchemeFeature;

static mxURL_SchemeFeature mxURL_SchemeFeatures[] = {
/* scheme: (uses_netloc, uses_params, uses_query, uses_fragment, uses_relative) */
    {"http",		1,1,1,1,1},
    {"https",		1,1,1,1,1},
    {"shttp",		1,1,1,1,1},
    {"mailto",		0,0,1,0,0},
    {"ftp",		1,1,0,1,1},
    {"gopher",		1,0,0,1,1},
    {"news",		1,0,0,1,1},
    {"nntp",		1,0,0,0,1},
    {"telnet",		1,0,0,1,0},
    {"file",		1,0,0,0,1},
    {"about",		0,0,0,0,0},
    {"javascript",	0,0,0,0,0},
    {"ldap",		1,0,0,0,0},
    {"svn+ssh",		1,0,0,0,1},
    /* Add new schemes here. */
};

/* 32 byte bit encoded character set of characters that are unsafe for
   inclusion in URLs; others are hexencoded as %hh. 

   XXX Still unused !!!
*/

static PyObject *mxURL_URLUnsafeCharacters;

#define URL_unsafe_chars						   \
    "\377\377\377\377\377\017\000\370\001\000\000x\001\000\000"		   \
    "\370\377\377\377\377\377\377\377\377\377\377\377\377\377\377\377\377"

    /* == TextTools.set(
	'abcdefghijklmnopqrstuvwxyz'
	'ABCDEFGHIJKLMNOPQRSTUVWXYZ'
	'0123456789/_,.-:',0) */

/* Reference to the MIME type map dictionary from mimetools.py */

static PyObject *mxURL_MIMEDict;

/* Flag telling us whether the module was initialized or not. */
static int mxURL_Initialized = 0;

/* --- forward declarations ----------------------------------------------- */

staticforward PyTypeObject mxURL_Type;
staticforward PyMethodDef mxURL_Methods[];

/* --- internal macros ---------------------------------------------------- */

#define _mxURL_Check(v) \
        (((mxURLObject *)(v))->ob_type == &mxURL_Type)

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
	modname = MXURL_MODULE;
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

/* --- URL Object -------------------------------------------------*/

/* --- allocation --- */

static
mxURLObject *mxURL_New(void)
{
    mxURLObject *url;

#ifdef MXURL_FREELIST
    if (mxURL_FreeList) {
	url = mxURL_FreeList;
	mxURL_FreeList = *(mxURLObject **)mxURL_FreeList;
	url->ob_type = &mxURL_Type;
	_Py_NewReference(url);
    }
    else
#endif
	{
	    url = PyObject_NEW(mxURLObject,&mxURL_Type);
	    if (!url)
		return NULL;
    }

    /* Init vars */
    url->url = NULL;
    url->scheme = NULL;
    url->netloc = 0;
    url->netloc_len = 0;
    url->path = 0;
    url->path_len = 0;
    url->path_normalized = 0;
    url->params = 0;
    url->params_len = 0;
    url->query = 0;
    url->query_len = 0;
    url->fragment = 0;
    url->fragment_len = 0;
    return url;
}

/* --- deallocation --- */

static
void mxURL_Free(mxURLObject *self)
{
    Py_XDECREF(self->url);
    Py_XDECREF(self->scheme);
#ifdef MXURL_FREELIST
    /* Append to free list */
    *(mxURLObject **)self = mxURL_FreeList;
    mxURL_FreeList = self;
#else
    PyObject_Del(self);
#endif
}

/* --- internal functions --- */

/* Set the scheme and feature values in url; features are encoded as
   follows: the index of a URL part is set to 0 if the scheme does not
   support this part and to -1 otherwise. scheme must have at least
   one character and no more than 19 characters. */

static
int mxURL_SetSchemeAndFeatures(mxURLObject *url,
			       char *scheme,
			       Py_ssize_t scheme_len)
{
    PyObject *features;
    PyObject *v;
    char sl[20];
    register Py_ssize_t i;
    
    DPRINTF("mxURL_SetSchemeAndFeatures: '%.*s'\n",scheme_len,scheme);
    
    Py_Assert(0 < scheme_len && scheme_len < sizeof(sl),
	      mxURL_Error,
	      "scheme length out of range");

    /* Lower the scheme string */
    for (i = 0; i < scheme_len; i++)
	sl[i] = tolower(scheme[i]);
    sl[scheme_len] = '\0';
    DPRINTF(" lowered scheme: '%s'\n",sl);

    Py_XDECREF(url->scheme);
    url->scheme = PyString_FromStringAndSize(sl,scheme_len);
    if (!url->scheme)
	goto onError;
    PyString_InternInPlace(&url->scheme);
    scheme = PyString_AS_STRING(url->scheme);

    /* Set features */
    DPRINTF(" looking up scheme '%s'\n",scheme);
    features = PyDict_GetItem(mxURL_SchemeDict,url->scheme);
    if (!features) {
	PyErr_Format(PyExc_ValueError,
		     "unknown scheme '%.100s'",
		     sl);
	goto onError;
    }
    
    Py_Assert(PyTuple_Check(features) && 
	      PyTuple_GET_SIZE(features) >= 5,
	      PyExc_TypeError,
	      "wrong scheme feature entry format");
    
    /* uses_netloc */
    v = PyTuple_GET_ITEM(features,0);
    Py_Assert(PyInt_Check(v),
	      PyExc_TypeError,
	      "scheme feature entries must be tuples of integers");
    url->netloc = PyInt_AS_LONG(v) ? -1 : 0;
    
    /* uses_params */
    v = PyTuple_GET_ITEM(features,1);
    Py_Assert(PyInt_Check(v),
	      PyExc_TypeError,
	      "scheme feature entries must be tuples of integers");
    url->params = PyInt_AS_LONG(v) ? -1 : 0;
    
    /* uses_query */
    v = PyTuple_GET_ITEM(features,2);
    Py_Assert(PyInt_Check(v),
	      PyExc_TypeError,
	      "scheme feature entries must be tuples of integers");
    url->query = PyInt_AS_LONG(v) ? -1 : 0;
    
    /* uses_fragment */
    v = PyTuple_GET_ITEM(features,3);
    Py_Assert(PyInt_Check(v),
	      PyExc_TypeError,
	      "scheme feature entries must be tuples of integers");
    url->fragment = PyInt_AS_LONG(v) ? -1 : 0;

#if 0
    /* uses_relative */
    v = PyTuple_GET_ITEM(features,4);
    Py_Assert(PyInt_Check(v),
	      PyExc_TypeError,
	      "scheme feature entries must be tuples of integers");
    url->scheme_uses_relative = PyInt_AS_LONG(v) ? -1 : 0;
#endif

    return 0;
    
 onError:
    return -1;
}

/* Normalize the path in place. Returns the new path length which will
   always be <= the length of the original one. The string will not be
   0-terminated. */

static
Py_ssize_t mxURL_NormalizePath(register char *path,
			       Py_ssize_t path_len)
{
    register Py_ssize_t i;
    register Py_ssize_t j;

    /* Scan path and "compress" along the way; 
       i is the reading index, j the writing index */
    DPRINTF("mxURL_NormalizePath: '%.*s'\n",path_len,path);
    for (i = 0, j = 0; i < path_len; ) {
	DPRINTF(" normalizing: i=%i j=%i '%.*s' + '%.*s'\n",i,j,
		j,path,path_len-i,&path[i]);
	if (path[i] == '/') {
	    /* Handle slashes */
	    if (i+1 < path_len) {
		if (path[i+1] == '/' && i > 0) {
		    /* '//' -> '/' except at the beginning */
		    i++;
		    DPRINTF(" fix '//' -> '/'\n");
		    continue;
		}
		else if (path[i+1] == '.') {
		    if (i+2 < path_len) {
			if (path[i+2] == '/') {
			    /* '/./' -> '/' */
			    i += 2;
			    DPRINTF(" fix '/./' -> '/'\n");
			    continue;
			}
			else if (path[i+2] == '.' &&
				 (i+3 >= path_len || path[i+3] == '/')) {
			    /* handle '/../' and '/..'<EOF> */

			    /* Root parent: '/..' -> '/' */
			    if (j == 0) {
				i += 3;
				DPRINTF(" root parent '/..' -> ''\n");
			    }
		    
			    /* Check for 'xxx./..' */
			    else if ((j > 0 && path[j-1] == '.')) {
				if (j == 1) {
				    /* compactify './..' to '..' */
				    path[j++] = '.';
				    DPRINTF(" compactify './..' to '..'\n");
				}
				else {
				    /* copy as is ('/..' -> '/..') */
				    path[j++] = '/';
				    path[j++] = '.';
				    path[j++] = '.';
				    DPRINTF(" copy '/..' as-is\n");
				}
				i += 3;
			    }

			    else {
				/* Find previous '/' */
				for (j--; j >= 0; j--)
				    if (path[j] == '/')
					break;
				if (j < 0) {
				    /* not found: 'a/..' -> '.') */
        			    path[0] = '.';
				    j = 1;
				    i += 3;
        			    DPRINTF(" compactify 'a/..' to '.'\n");
				}
 				else {
				    /* found: 'a/b/..' -> 'a' */
				    i += 3;
				    DPRINTF(" backtrack 'a/b/..' -> 'a'\n");
				}
			    }
			    
			    /* Special case: preserve directory
			       information ('a/b/..' -> 'a/') */
			    if (i >= path_len) {
				path[j++] = '/';
				DPRINTF(" preserve dir\n");
				break;
			    }
			    continue;
			}
		    }
		    else {
			/* remove directory dot ('/.<EOF>' -> '/') */
			path[j] = '/';
			j++;
			DPRINTF(" remove dir dot\n");
			break;
		    }
		}
	    }
	}
	else if (path[i] == '.') {
	    /* Handle dots */
	    if (i+1 < path_len) {
		if (path[i+1] == '.') {
		    /* special case: relative path backup ('..' -> '..') */
		    path[j++] = path[i++];
		    path[j++] = path[i++];
		    DPRINTF(" copy '..' as-is\n");
		    continue;
		}
	    }
	}
	/* move one character */
	path[j] = path[i];
	i++;
	j++;
    }
    DPRINTF(" returning: '%.*s' (len=%i)\n",j,path,j);
    return j;
}

/* Using the strings (given with length) a URL is built and filled
   with the appropriate values.

   If normalize is true, the path is normalized prior to setting it.

   XXX does not check features. */

static
int mxURL_SetFromBrokenDown(mxURLObject *url,
			    char *scheme,
			    Py_ssize_t scheme_len,
			    char *netloc,
			    Py_ssize_t netloc_len,
			    char *path,
			    Py_ssize_t path_len,
			    char *params,
			    Py_ssize_t params_len,
			    char *query,
			    Py_ssize_t query_len,
			    char *fragment,
			    Py_ssize_t fragment_len,
			    int normalize)
{
    Py_ssize_t len;
    char *s;			/* Pointer into work buffer */
    char buffer[256];		/* Stack buffer */
    char *workspace = buffer;	/* Work buffer */

    DPRINTF("mxURL_SetFromBrokenDown('%s',scheme='%.*s',netloc='%.*s',"
	    "path='%.*s',params='%.*s',query='%.*s',fragment='%.*s')\n",
	    url->url==NULL?"<new>":PyString_AS_STRING(url->url),
	    scheme_len, scheme_len==0?"":scheme,
	    netloc_len, netloc_len==0?"":netloc,
	    path_len, path_len==0?"":path,
	    params_len, params_len==0?"":params,
	    query_len, query_len==0?"":query,
	    fragment_len, fragment_len==0?"":fragment);

    /* Build a new url string from the broken down values */
    len = scheme_len+1 + netloc_len+1 + path_len + \
	  params_len+1 + query_len+1 + fragment_len+1 + 1;
    if (len > sizeof(buffer)) {
	workspace = new(char,len);
	if (!workspace) {
	    PyErr_NoMemory();
	    goto onError;
	}
    }
    s = workspace;

    /* Set scheme string (XXX should use mxURL_SetSchemeAndFeatures) */
    Py_XDECREF(url->scheme);
    if (scheme_len) {
	url->scheme = PyString_FromStringAndSize(scheme,scheme_len);
	if (!url->scheme)
	    goto onError;
	PyString_InternInPlace(&url->scheme);
	/* add "<scheme>:" */
	memcpy(s,scheme,scheme_len);
	s[scheme_len] = ':';
	s += scheme_len + 1;
	DPRINTF(" set scheme to: '%.*s'\n",
		PyString_GET_SIZE(url->scheme),
		PyString_AS_STRING(url->scheme));
    }
    else {
	url->scheme = NULL;
	DPRINTF(" set scheme to: NULL\n");
    }

    /* XXX Should set and check features here */
    
    /* Store indices into the url string */
    url->netloc = (short)(s - workspace) + 2;
    url->netloc_len = netloc_len;
    if (netloc_len) {
	/* add "//<netloc>" */
	s[0] = '/'; s[1] = '/';
	memcpy(&s[2],netloc,netloc_len);
	s += netloc_len + 2;
    }
    DPRINTF(" set netloc to: '%.*s'\n",
	    url->netloc_len,&workspace[url->netloc]);
    
    url->path = (short)(s - workspace);
    if (path_len) {
	/* Make sure that path is rooted if a netloc was given */
	if (netloc_len && path[0] != '/') {
	    /* add "/<path>" */
	    s[0] = '/';
	    memcpy(&s[1],path,path_len);
	    path_len++;
	}
	else {
	    /* add "<path>" */
	    memcpy(s,path,path_len);
	}
	if (normalize) {
	    path_len = mxURL_NormalizePath(s,path_len);
	    url->path_normalized = 1;
	}
	s += path_len;
    }
    url->path_len = path_len;
    DPRINTF(" set path to: '%.*s'\n",
	    url->path_len,&workspace[url->path]);
    
    url->params = (short)(s - workspace) + 1;
    url->params_len = params_len;
    if (params_len) {
	/* add ";<params>" */
	s[0] = ';';
	memcpy(&s[1],params,params_len);
	s += params_len + 1;
    }
    DPRINTF(" set params to: '%.*s'\n",
	    url->params_len,&workspace[url->params]);
    
    url->query = (short)(s - workspace) + 1;
    url->query_len = query_len;
    if (query_len) {
	/* add "?<query>" */
	s[0] = '?';
	memcpy(&s[1],query,query_len);
	s += query_len + 1;
    }
    DPRINTF(" set query to: '%.*s'\n",
	    url->query_len,&workspace[url->query]);
    
    url->fragment = (short)(s - workspace) + 1;
    url->fragment_len = fragment_len;
    if (fragment_len) {
	/* add "#<fragment>" */
	s[0] = '#';
	memcpy(&s[1],fragment,fragment_len);
	s += fragment_len + 1;
    }
    DPRINTF(" set fragment to: '%.*s'\n",
	    url->fragment_len,&workspace[url->fragment]);

    /* Set url to the new string */
    Py_XDECREF(url->url);
    url->url = PyString_FromStringAndSize(workspace,(int)(s - workspace));
    if (!url->url)
	goto onError;

    if (workspace != buffer)
	free(workspace);
    return 0;

 onError:
    if (workspace != buffer && workspace)
	free(workspace);
    return -1;
}

/* Parse the URL str according to the feature settings in url and set
   the values accordingly. Returns 0 on success, -1 otherwise. If
   normalize is true then the string's path will be normalized prior
   to setting the values. */

static
int mxURL_SetFromString(mxURLObject *url,
			register char *str,
			int normalize)
{
    register Py_ssize_t i;
    Py_ssize_t len;

    /* Character set of all allowed scheme characters; generated from
       TextTools.set(alpha + number + '+-.'). */
    static unsigned char *scheme_set = 
	(unsigned char *)(
	 "\000\000\000\000\000h\377\003\376\377"
	 "\377\007\376\377\377\007\000\000\000\000"
	 "\000\000\000\000\000\000\000\000\000\000\000\000");

    DPRINTF("mxURL_SetFromString(%0lx,'%s')\n",(long)url,str);

    len = strlen(str);

    /* Now parse the string: "scheme://netloc/path;params?query#fragment" */
    
    /* scheme: "scheme:"

       Only allows: +(alpha | digit | "+" | "-" | ".")
    */
    for (i = 0; i < len; i++) {
	if (str[i] == ':')
	    /* found */
	    break;
	else if (!Py_CharInSet(str[i],scheme_set)) {
	    /* illegal char */
	    i = len;
	    break;
	}
    }
    if (i != len) {
	DPRINTF(" looking for scheme '%.*s'\n",i,str);
	if (mxURL_SetSchemeAndFeatures(url,str,i) < 0)
	    goto onError;
	DPRINTF(" known scheme found\n");
	/* Skip behind ':' in "scheme:..." */
	i++;
    }
    else {
	/* No scheme given: reset and parse all remaining fields */
	i = 0;
	url->netloc = -1;
	url->params = -1;
	url->query = -1;
	url->fragment = -1;
	DPRINTF(" no scheme given\n");
    }

    /* netloc: "//netloc/..." or "//netloc" or "//netloc?..." 

       (depending on scheme features) 
     */
    if (url->netloc && str[i] == '/' && str[i+1] == '/') {
	register Py_ssize_t j;
	for (j = i+2; j < len; j++)
	    if (str[j] == '/' || (str[j] == '?' && url->query))
		break;
	url->netloc = i + 2;
	url->netloc_len = j - (i + 2);
	DPRINTF(" found netloc '%.*s'\n",url->netloc_len,&str[url->netloc]);
	/* Skip behind netloc */
	i = j;
	if (i >= len)
	    goto finished;
    }
    
    /* path: "...;" or "...?" or "...#" or "..." 

       (depending on scheme features) 
     */
    {
	register Py_ssize_t j;
	for (j = i; j < len; j++)
	    if ((str[j] == ';' && url->params)
		|| (str[j] == '?' && url->query)
		|| (str[j] == '#' && url->fragment))
		break;
	url->path = i;
	url->path_len = j - i;
	DPRINTF(" found path '%.*s'\n",url->path_len,&str[url->path]);
	/* Skip behind path */
	i = j;
	if (i >= len)
	    goto finished;
    }
    
    /* params: ";...?..." or ";...#..." or ";..." 

       (depending on scheme features) 
     */
    if (url->params && str[i] == ';') {
	register Py_ssize_t j;
	for (j = i+1; j < len; j++) {
	    if (str[j] == '?' && url->query)
		break;
	    if (str[j] == '#' && url->fragment)
		break;
	}
	url->params = i + 1;
	url->params_len = j - (i + 1);
	DPRINTF(" found params '%.*s'\n",url->params_len,&str[url->params]);
	/* Skip behind params */
	i = j;
	if (i >= len)
	    goto finished;
    }
    
    /* query: "?..." or "?...#..." */
    if (url->query && str[i] == '?') {
	register Py_ssize_t j;
	if (url->fragment) {
	    for (j = i+1; j < len; j++)
		if (str[j] == '#')
		    break;
	}
	else
	    j = len;
	url->query = i + 1;
	url->query_len = j - (i + 1);
	DPRINTF(" found query '%.*s'\n",url->query_len,&str[url->query]);
	/* Skip behind query */
	i = j;
	if (i >= len)
	    goto finished;
    }
    
    /* fragment */
    if (url->fragment && str[i] == '#') {
	/* "#..." */
	url->fragment = i + 1;
	url->fragment_len = len - (i + 1);
	DPRINTF(" found fragment '%.*s'\n",
		url->fragment_len,&str[url->fragment]);
    }
    
 finished:
    /* If the URL should be stored normalized we now let the
       mxURL_SetFromBrokenDown API take care of this (and some minor
       other things). */
    if (normalize) {
	if (mxURL_SetFromBrokenDown(url,
				    url->scheme ?
				    PyString_AS_STRING(url->scheme) : 0,
				    url->scheme ?
				    PyString_GET_SIZE(url->scheme) : 0,
				    &str[url->netloc],
				    url->netloc_len,
				    &str[url->path],
				    url->path_len,
				    &str[url->params],
				    url->params_len,
				    &str[url->query],
				    url->query_len,
				    &str[url->fragment],
				    url->fragment_len,
				    NORMALIZE_URL))
	    goto onError;
    }
    else {
	Py_XDECREF(url->url);
	url->url = PyString_FromString(str);
	if (!url->url)
	    goto onError;
    }
    return 0;

 onError:
    return -1;
}

/* Does the scheme support relative paths ? Returns 1/0 or -1 in case
   of an error. */

static
int mxURL_SchemeUsesRelativePaths(PyObject *scheme)
{
    PyObject *features,*v;

#ifdef HARDCODE_SCHEMES_USES_RELATIVE
    static PyObject *http_scheme, *ftp_scheme;
    
    /* Init the interned string objects used for schemes. */
    if (!http_scheme) {
	http_scheme = PyString_InternFromString("http");
	ftp_scheme = PyString_InternFromString("ftp");
	if (PyErr_Occurred())
	    goto onError;
    }

    /* Hardcoded values for some schemes. */
    if (scheme == http_scheme || scheme == ftp_scheme)
	return 1;
#endif

    /* Ok, then do the lookup... */
    features = PyDict_GetItem(mxURL_SchemeDict,scheme);
    if (!features) {
	PyErr_Format(PyExc_ValueError,
		     "unknown scheme '%s'",
		     PyString_AS_STRING(scheme));
	goto onError;
    }
    
    Py_Assert(PyTuple_Check(features) && 
	      PyTuple_GET_SIZE(features) >= 5,
	      PyExc_TypeError,
	      "wrong scheme feature entry format");

    /* uses_relative */
    v = PyTuple_GET_ITEM(features,4);
    Py_Assert(PyInt_Check(v),
	      PyExc_TypeError,
	      "scheme feature entries must be tuples of integers");
    return PyInt_AS_LONG(v) ? 1 : 0;

 onError:
    return -1;
}

/* --- API functions --- */

/* Return the base part of the URL as Python string (including the
   trailing '/'). */

static
PyObject *mxURL_Base(mxURLObject *self)
{
    register Py_ssize_t i = self->path_len - 1;
    char *path = &PyString_AS_STRING(self->url)[self->path];
	
    for (;i >= 0; i--) {
	if (path[i] == '/')
	    break;
    }
    if (i < 0)
	return PyString_FromStringAndSize("",0);
    return PyString_FromStringAndSize(path,i + 1);
}

/* Returns the path entry index of the URL as Python string (without '/').
   
   Leading and trailing '/' are ignored and not counted. Left indices
   are 0 based (the first entry is 0); negative values indicate
   entries counted from the right and are -1 based (the last entry is
   -1).
   
   An IndexError is raised if the index is out of range.

*/

static
PyObject *mxURL_PathEntry(mxURLObject *self,
			  Py_ssize_t index)
{
    register Py_ssize_t i;
    Py_ssize_t start;
    Py_ssize_t len = self->path_len;
    char *path = &PyString_AS_STRING(self->url)[self->path];
	
    /* Find start of entry (ignoring leading and trailing '/') */
    if (index > 0) {
	i = 0;
	if (path[i] == '/')
	    i++;
	for (; i < len; i++) {
	    if (path[i] == '/') {
		index--;
		if (index == 0) {
		    i++;
		    break;
		}
	    }
	}
    }
    else if (index < 0) {
	i = len - 1;
	if (path[i] == '/')
	    i--;
	for (; i >= 0; i--) {
	    if (path[i] == '/') {
		index++;
		if (index == 0) {
		    i++;
		    break;
		}
	    }
	}
	/* special case: relative URL */
	if (i < 0 && path[0] != '/' && index == -1)
	    i = 0;
    }
    else {
	i = 0;
	if (path[i] == '/')
	    i++;
    }
    Py_Assert(i >= 0 && i < len,
	      PyExc_IndexError,
	      "index out of range");
    start = i;

    /* Find end of entry */
    for (; i < len; i++)
	if (path[i] == '/')
	    break;

    return PyString_FromStringAndSize(&path[start],i - start);

 onError:
    return NULL;
}

/* Returns the path length using the semantics of mxURL_PathEntry().  */

static
Py_ssize_t mxURL_PathLength(mxURLObject *self)
{
    register Py_ssize_t i;
    Py_ssize_t len = self->path_len;
    char *path = &PyString_AS_STRING(self->url)[self->path];
    Py_ssize_t length = 0;
	
    /* Count slashes */
    for (i = 0; i < len; i++) {
	if (path[i] == '/') 
	    length++;
    }
    /* Apply corrections */
    if (len > 1) {
	if (path[0] == '/')
	    length--;
	if (path[len-1] == '/')
	    length--;
	length++;
    }
    else if (len == 1)
	length = length ? 0 : 1;

    return length;
}

/* Return the path as Python tuple of strings. '/' are not included.
   Leading and trailing '/' are ignored. */

static
PyObject *mxURL_PathTuple(mxURLObject *self)
{
    register Py_ssize_t i;
    Py_ssize_t start;
    Py_ssize_t len = self->path_len;
    char *path = &PyString_AS_STRING(self->url)[self->path];
    Py_ssize_t length;
    Py_ssize_t index = 0;
    PyObject *v = 0;
    
    length = mxURL_PathLength(self);
    if (length < 0)
	goto onError;
    v = PyTuple_New(length);
    if (!v)
	goto onError;
    
    i = 0;
    if (path[i] == '/')
	i++;
    start = i;
    for (; i < len; i++) {
	if (path[i] == '/') {
	    PyObject *w;
	    
	    w = PyString_FromStringAndSize(&path[start],i - start);
	    if (!w) 
		goto onError;
	    PyTuple_SET_ITEM(v,index,w);
	    index++;
	    start = i + 1;
	}
    }
    if (start < len) {
	PyObject *w;

	w = PyString_FromStringAndSize(&path[start],i - start);
	if (!w) 
	    goto onError;
	PyTuple_SET_ITEM(v,index,w);
	index++;
    }
    Py_Assert(index == length,
	      mxURL_Error,
	      "internal error in mxURL_PathTuple");

    return v;

 onError:
    Py_XDECREF(v);
    return NULL;
}

/* Return the extension part of the URL as Python string (the first
   suffix starting with a '.'; the dot itself is not included in the
   string, e.g. 'abc.tar.gz' will yield 'gz'). The string is always
   converted to all lowercase letters. */

static
PyObject *mxURL_Extension(mxURLObject *self)
{
    register Py_ssize_t i = self->path_len;
    char *path = &PyString_AS_STRING(self->url)[self->path];
    Py_ssize_t ext_len;
    char ext[256];
	
    if (i == 0 || path[i] == '.')
	return PyString_FromStringAndSize("",0);
    /* Extensions must have at least one character */
    i--;
    for (;i >= 0; i--) {
	if (path[i] == '.')
	    break;
	if (path[i] == '/')
	    i = 0;
    }
    if (i < 0)
	return PyString_FromStringAndSize("",0);
    /* We don't include the '.' */
    i++;

    /* Make all lower case ... */
    ext_len = self->path_len - i;
    if (ext_len > 256)
	Py_Error(PyExc_SystemError,
		 "extension too long to process");
    memcpy(ext,&path[i],ext_len);
    for (i = 0; i < ext_len; i++)
	if (isupper(ext[i]))
	    ext[i] = tolower(ext[i]);
    return PyString_FromStringAndSize(ext,ext_len);

 onError:
    return NULL;
}

/* Return the MIME type of the URL deducting it from the URL's file
   extension as string (format "major/minor") or "* / *" (without the
   spaces) in case it cannot be determined.
*/

static
PyObject *mxURL_MIMEType(mxURLObject *self)
{
    register Py_ssize_t i = self->path_len;
    char *path = &PyString_AS_STRING(self->url)[self->path];
    Py_ssize_t ext_len;
    char ext[256];
    static PyObject *unknown;
    PyObject *v,*mime;
    
    if (i == 0 || path[i] == '.' || mxURL_MIMEDict == NULL)
	goto unknownType;
    
    /* Extensions must have at least one character */
    i--;
    for (;i >= 0; i--) {
	if (path[i] == '.')
	    break;
	if (path[i] == '/')
	    i = 0;
    }
    if (i < 0)
	goto unknownType;

    /* Make all lower case ... */
    ext_len = self->path_len - i;
    if (ext_len > 256)
	Py_Error(PyExc_SystemError,
		 "extension too long to process");
    memcpy(ext,&path[i],ext_len);
    for (i = 1; i < ext_len; i++)
	if (isupper(ext[i]))
	    ext[i] = tolower(ext[i]);
    
    /* Find the extension in the MIME map */
    v = PyString_FromStringAndSize(ext,ext_len);
    if (!v)
	goto onError;
    PyString_InternInPlace(&v);
    mime = PyDict_GetItem(mxURL_MIMEDict,v);
    Py_DECREF(v);
    if (!mime)
	goto unknownType;
    Py_INCREF(mime);
    return mime;

 unknownType:
    if (!unknown) {
	unknown = PyString_FromString("*/*");
	if (!unknown)
	    goto onError;
	PyString_InternInPlace(&unknown);
    }
    Py_INCREF(unknown);
    return unknown;

 onError:
    return NULL;
}

/* Return the file part of the URL's path as Python string. */

static
PyObject *mxURL_File(mxURLObject *self)
{
    register Py_ssize_t i = self->path_len;
    char *path = &PyString_AS_STRING(self->url)[self->path];
	
    if (i == 0)
	return PyString_FromStringAndSize("",0);
    for (;i >= 0; i--) {
	if (path[i] == '/')
	    break;
    }
    i++;
    return PyString_FromStringAndSize(&path[i],
				      self->path_len - i);
}

/* Return the hostname part of the URL's netloc
   (//<user>:<password>@<host>:<port>/) as Python string.  If not
   given, an empty string is returned.*/

static
PyObject *mxURL_Hostname(mxURLObject *self)
{
    register Py_ssize_t i;
    Py_ssize_t host;
    Py_ssize_t netloc_len = self->netloc_len;
    char *netloc = &PyString_AS_STRING(self->url)[self->netloc];
	
    if (netloc_len == 0)
	return PyString_FromStringAndSize("",0);
    /* Find start of host part */
    for (i = 0;i < netloc_len; i++) {
	if (netloc[i] == '@')
	    break;
    }
    if (i == netloc_len)
	host = 0;
    else
	host = i + 1;
    /* Find end of host part */
    for (i = host;i < netloc_len; i++) {
	if (netloc[i] == ':')
	    break;
    }
    return PyString_FromStringAndSize(&netloc[host],i - host);
}

/* Return the username part of the URL's netloc as Python string. If
   not given, an empty string is returned. */

static
PyObject *mxURL_Username(mxURLObject *self)
{
    register Py_ssize_t i;
    Py_ssize_t netloc_len = self->netloc_len;
    char *netloc = &PyString_AS_STRING(self->url)[self->netloc];
	
    if (netloc_len == 0)
	return PyString_FromStringAndSize("",0);
    /* Do we have a user part ? */
    for (i = 0;i < netloc_len; i++) {
	if (netloc[i] == '@')
	    break;
    }
    if (i == netloc_len)
	return PyString_FromStringAndSize("",0);
    netloc_len = i;
    /* Find end of user */
    for (i = 0;i < netloc_len; i++) {
	if (netloc[i] == ':')
	    break;
    }
    return PyString_FromStringAndSize(netloc,i);
}

/* Return the username part of the URL's netloc as Python string. If
   not given, an empty string is returned. */

static
PyObject *mxURL_Password(mxURLObject *self)
{
    register Py_ssize_t i;
    Py_ssize_t netloc_len = self->netloc_len;
    char *netloc = &PyString_AS_STRING(self->url)[self->netloc];
	
    if (netloc_len == 0)
	return PyString_FromStringAndSize("",0);
    /* Do we have a user part ? */
    for (i = 0;i < netloc_len; i++) {
	if (netloc[i] == '@')
	    break;
    }
    if (i == netloc_len)
	return PyString_FromStringAndSize("",0);
    netloc_len = i;
    /* Find passwd start */
    for (i = 0;i < netloc_len; i++) {
	if (netloc[i] == ':')
	    break;
    }
    if (i == netloc_len)
	return PyString_FromStringAndSize("",0);
    i++;
    return PyString_FromStringAndSize(&netloc[i],netloc_len - i);
}

/* Return the username part of the URL's netloc as Python integer. If
   not given, an empty string is returned. */

static
PyObject *mxURL_Port(mxURLObject *self)
{
    register Py_ssize_t i;
    int port = 80;
    Py_ssize_t netloc_len = self->netloc_len;
    char *netloc = &PyString_AS_STRING(self->url)[self->netloc];
	
    if (netloc_len == 0)
	return PyString_FromStringAndSize("",0);
    /* Find port start */
    for (i = netloc_len - 1;i >= 0; i--) {
	if (netloc[i] == ':')
	    break;
	if (netloc[i] == '@')
	    /* Hit a user part... cop out */
	    return PyString_FromStringAndSize("",0);
    }
    i++;
    /* Convert to an integer. Errors are NOT reported. */
    port = atoi(&netloc[i]);
    return PyInt_FromLong(port);
}

/* Return the depth of an absolute URL. Toplevel has depth 0. -1 is
   returned in case of error and a Python exception set. */

static
Py_ssize_t mxURL_Depth(mxURLObject *self)
{
    register Py_ssize_t i = self->path_len - 1;
    register Py_ssize_t depth = 0;
    char *path = &PyString_AS_STRING(self->url)[self->path];
	
    for (;i >= 0; i--) {
	if (path[i] == '/')
	    depth++;
#if 0
	else if (path[i] == '.' && depth > 0) {
	    Py_Error(mxURL_Error,
		     "depth not defined: path has relative components");
	}
#endif
    }
    if (depth == 0 || path[0] != '/')
	Py_Error(mxURL_Error,
		 "depth not defined: path is relative or empty");
    return depth - 1;

 onError:
    return -1;
}

/* Return a tuple (scheme, netloc, path, params, query, fragment) just
   as urlparse.urlparse() does. */

static
PyObject *mxURL_Parsed(mxURLObject *self)
{
    char *url = PyString_AS_STRING(self->url);
    char *scheme;
    
    if (!self->scheme)
	scheme = "";
    else
	scheme = PyString_AS_STRING(self->scheme);

    return Py_BuildValue("ss#s#s#s#s#",
			 scheme,
			 &url[self->netloc], (Py_ssize_t)self->netloc_len,
			 &url[self->path], (Py_ssize_t)self->path_len,
			 &url[self->params], (Py_ssize_t)self->params_len,
			 &url[self->query], (Py_ssize_t)self->query_len,
			 &url[self->fragment], (Py_ssize_t)self->fragment_len
			 );
}

static
mxURLObject *mxURL_FromString(char *str,
			      int normalize)
{
    mxURLObject *url = 0;
    
    url = mxURL_New();
    if (!url)
	return NULL;

    /* Init vars */
    if (mxURL_SetFromString(url,str,normalize) < 0)
	goto onError;
    return url;

 onError:
    Py_XDECREF(url);
    return NULL;
}

static
mxURLObject *mxURL_FromBrokenDown(char *scheme,
				  char *netloc,
				  char *path,
				  char *params,
				  char *query,
				  char *fragment,
				  int normalize)
{
    mxURLObject *url = 0;
    
    url = mxURL_New();
    if (!url)
	return NULL;

    /* Init vars */
    if (mxURL_SetFromBrokenDown(url,
				scheme,
				strlen(scheme),
				netloc,
				strlen(netloc),
				path,
				strlen(path),
				params,
				strlen(params),
				query,
				strlen(query),
				fragment,
				strlen(fragment),
				normalize) < 0)
	goto onError;
    return url;

 onError:
    Py_XDECREF(url);
    return NULL;
}

/* Reconstruct a new URL object from a given one by replacing only a
   few parts. Entries that are not supposed to be changed must be set
   to NULL. */

static
mxURLObject *mxURL_RebuildFromBrokenDown(mxURLObject *url,
					 char *scheme,
					 char *netloc,
					 char *path,
					 char *params,
					 char *query,
					 char *fragment,
					 int normalize)
{
    mxURLObject *newurl = 0;
    char *str = PyString_AS_STRING(url->url);
    Py_ssize_t scheme_len,netloc_len,path_len,params_len,query_len,fragment_len;

    newurl = mxURL_New();
    if (!newurl)
	return NULL;
    
    if (!scheme && url->scheme) {
	scheme = PyString_AS_STRING(url->scheme);
	scheme_len = PyString_GET_SIZE(url->scheme);
    }
    else
	scheme_len = scheme ? strlen(scheme) : 0;
    
    if (!netloc) {
	netloc_len = url->netloc_len;
	netloc = netloc_len ? &str[url->netloc] : NULL;
    }
    else
	netloc_len = strlen(netloc);
    
    if (!path) {
	path_len = url->path_len;
	path = path_len ? &str[url->path] : NULL;
    }
    else
	path_len = strlen(path);
    
    if (!params) {
	params_len = url->params_len;
	params = params_len ? &str[url->params] : NULL;
    }
    else
	params_len = strlen(params);
    
    if (!query) {
	query_len = url->query_len;
	query = query_len ? &str[url->query] : NULL;
    }
    else
	query_len = strlen(query);
    
    if (!fragment) {
	fragment_len = url->fragment_len;
	fragment = fragment_len ? &str[url->fragment] : NULL;
    }
    else
	fragment_len = strlen(fragment);

    /* Init vars */
    if (mxURL_SetFromBrokenDown(newurl,
				scheme,
				scheme_len,
				netloc,
				netloc_len,
				path,
				path_len,
				params,
				params_len,
				query,
				query_len,
				fragment,
				fragment_len,
				normalize) < 0)
	goto onError;
    return newurl;

 onError:
    Py_XDECREF(newurl);
    return NULL;
}

static
char *mxURL_AsString(mxURLObject *url)
{
    return PyString_AS_STRING(url->url);
}


/* Return 1/0 depending on whether url points to an absolute path or
   not. */

static
int mxURL_AbsolutePath(mxURLObject *url)
{
    if (url->path_len && 
	PyString_AS_STRING(url->url)[url->path] == '/')
	return 1;
    return 0;
}

/* Return a new URL object that points to the same URL as url but
   with a normalized URL. */

static
mxURLObject *mxURL_NormalizedFromURL(mxURLObject *url)
{
    mxURLObject *normurl = 0;
    char *str = PyString_AS_STRING(url->url);

    /* Shortcut */
    if (url->path_normalized) {
	Py_INCREF(url);
	return url;
    }

    normurl = mxURL_New();
    if (!normurl)
	return NULL;

    /* Init vars */
    if (mxURL_SetFromBrokenDown(normurl,
				url->scheme ?
				PyString_AS_STRING(url->scheme) : 0,
				url->scheme ?
				PyString_GET_SIZE(url->scheme) : 0,
				&str[url->netloc],
				url->netloc_len,
				&str[url->path],
				url->path_len,
				&str[url->params],
				url->params_len,
				&str[url->query],
				url->query_len,
				&str[url->fragment],
				url->fragment_len,
				NORMALIZE_URL))
	goto onError;
    return normurl;

 onError:
    Py_XDECREF(normurl);
    return NULL;
}

/* Return a (new) URL object that points to the same base URL as url
   but without any of the special parts fragment, query or params
   set. If url does not contain those parts, a new reference to it is
   returned. */

static
mxURLObject *mxURL_BasicFromURL(mxURLObject *url)
{
    mxURLObject *basicurl = 0;
    char *str = PyString_AS_STRING(url->url);

    /* Shortcut */
    if (url->params_len == 0 &&
	url->query_len == 0 &&
	url->fragment_len == 0) {
	Py_INCREF(url);
	return url;
    }

    basicurl = mxURL_New();
    if (!basicurl)
	return NULL;

    /* Init vars */
    if (mxURL_SetFromBrokenDown(basicurl,
				url->scheme ?
				PyString_AS_STRING(url->scheme) : 0,
				url->scheme ?
				PyString_GET_SIZE(url->scheme) : 0,
				&str[url->netloc],
				url->netloc_len,
				&str[url->path],
				url->path_len,
				NULL,0,
				NULL,0,
				NULL,0,
				NORMALIZE_URL))
	goto onError;
    return basicurl;

 onError:
    Py_XDECREF(basicurl);
    return NULL;
}

/* Return a new URL object that joined with the base URL points to the
   same URL as url. The base URL should provide scheme and netloc,
   because otherwise joining might result in lossage of scheme or
   netloc. */

static
mxURLObject *mxURL_RelativeFromURL(mxURLObject *url,
				   mxURLObject *base)
{
    mxURLObject *relurl = 0;
    char *str = PyString_AS_STRING(url->url);
    char *urlpath = &str[url->path];
    char *basepath = &(PyString_AS_STRING(base->url)[base->path]);
    Py_ssize_t mismatch, mismatchlen, urldepth, basedepth, diffdepth, prefixlen;
    char *relpath = 0;
    register Py_ssize_t i;

    Py_Assert(url->path_normalized && base->path_normalized &&
	      mxURL_AbsolutePath(url) && mxURL_AbsolutePath(base),
	      mxURL_Error,
	      "URL's path must be absolute and normalized");

    /* Special case: both schemes are given, but they differ. Since
       the base's scheme will be overridden during joining, we simply
       return a new reference to url. */
    if (url->scheme && base->scheme &&
	url->scheme != base->scheme &&
	(strcmp(PyString_AS_STRING(url->scheme),
		PyString_AS_STRING(base->scheme)) != 0)) {
	Py_INCREF(url);
	return url;
    }

    /* Special case: both netlocs are given but differ. We want our
       host, so completely ignore the base URL and return a new
       reference to url. */
    if (url->netloc_len && base->netloc_len &&
	(url->netloc_len != base->netloc_len ||
	 (strncmp(&str[url->netloc],
		  &str[base->netloc],url->netloc_len) != 0))) {
	Py_INCREF(url);
	return url;
    }

    /* Special case: scheme does not support relative URLs. In that
       case we simply return a new reference to url. */
    if (url->scheme) {
	int rc = mxURL_SchemeUsesRelativePaths(url->scheme);
	if (rc < 0)
	    goto onError;
	if (!rc) {
	    Py_INCREF(url);
	    return url;
	}
    }

    /* Get depth of both URLs */
    urldepth = mxURL_Depth(url);
    if (urldepth < 0)
	goto onError;
    basedepth = mxURL_Depth(base);
    if (basedepth < 0)
	goto onError;

    /* Compare the prefixes of both URLs and determine the match depth */
    diffdepth = basedepth;
    mismatch = 1;
    for (i = 1; i < min(url->path_len, base->path_len); i++) {
	if (urlpath[i] != basepath[i])
	    break;
	if (urlpath[i] == '/') {
	    diffdepth--;
	    mismatch = i + 1;
	}
    }

    /* Create the relative URL */
    if (diffdepth == 0)
	prefixlen = 2;
    else
	prefixlen = diffdepth * 3;
    mismatchlen = url->path_len - mismatch;

    relpath = new(char,prefixlen + mismatchlen);
    if (!relpath)
	goto onError;

    if (diffdepth > 0)
	for (i = 0; i < prefixlen;) {
	    relpath[i++] = '.';
	    relpath[i++] = '.';
	    relpath[i++] = '/';
	}
    else {
	relpath[0] = '.';
	relpath[1] = '/';
	i = 2;
    }
    memcpy(&relpath[i],&urlpath[mismatch],mismatchlen);

    relurl = mxURL_New();
    if (!relurl)
	goto onError;

    /* Init vars */
    if (mxURL_SetFromBrokenDown(relurl,
				(url->scheme && !base->scheme) ?
				PyString_AS_STRING(url->scheme) : 0,
				(url->scheme && !base->scheme) ?
				PyString_GET_SIZE(url->scheme) : 0,
				NULL,
				0,
				relpath,
				prefixlen + mismatchlen,
				&str[url->params],
				url->params_len,
				&str[url->query],
				url->query_len,
				&str[url->fragment],
				url->fragment_len,
				NORMALIZE_URL))
	goto onError;
    
    if (relpath)
	free(relpath);
    return relurl;

 onError:
    if (relpath)
	free(relpath);
    Py_XDECREF(relurl);
    return NULL;
}

static
mxURLObject *mxURL_FromJoiningURLs(mxURLObject *base,
				   mxURLObject *other)
{
    mxURLObject *url = 0;
    char *baseurl, *otherurl;
    char *scheme, *netloc, *path, *params, *query, *fragment;
    Py_ssize_t scheme_len, netloc_len, path_len, params_len, 
	query_len, fragment_len;
    char buffer[256];		/* Stack buffer */
    char *workspace = buffer;	/* Work buffer */
    register char *w;		/* Pointer into workspace */
    register Py_ssize_t len;
    int uses_relative;
    int inherit_query = 0, inherit_params = 0;

    DPRINTF("mxURL_FromJoiningURLs('%s','%s')\n",
	    PyString_AS_STRING(base->url),
	    PyString_AS_STRING(other->url));

#if 0
    Py_Assert(base->scheme == NULL || other->scheme == NULL || 
	      strcmp(PyString_AS_STRING(base->scheme),
		     PyString_AS_STRING(other->scheme)) == 0,
	      PyExc_ValueError,
	      "schemes must match when joining URLs");
#else
    /* Handle special case where both schemes are given but differ.
       Joining the URL will simply return the other URL. */
    if (other->scheme && base->scheme &&
	other->scheme != base->scheme &&
	strcmp(PyString_AS_STRING(other->scheme),
		 PyString_AS_STRING(base->scheme)) != 0) {
	Py_INCREF(other);
	url = other;
	goto finished;
    }
#endif

    /* Create a big enough buffer to hold the result; we use the C
       stack buffer if possible or allocate a chunk of heap memory
       otherwise */
    len = PyString_GET_SIZE(base->url) + PyString_GET_SIZE(other->url) + 10;
    if (len > sizeof(buffer)) {
	workspace = new(char,len);
	if (!workspace) {
	    PyErr_NoMemory();
	    goto onError;
	}
    }
    w = workspace;
    
    /* Create the 0-terminated strings needed for setting from broken down
       values */

    baseurl = PyString_AS_STRING(base->url);
    otherurl = PyString_AS_STRING(other->url);

    /* Choose the scheme: only some of these cases will be true due to
       the above test. The case where the base URL provides the scheme
       and the other URL is relative should be the more common one. */
    scheme = w;
    if (base->scheme) {
	len = PyString_GET_SIZE(base->scheme);
	memcpy(scheme,
	       PyString_AS_STRING(base->scheme),
	       len);
	uses_relative = mxURL_SchemeUsesRelativePaths(base->scheme);
	if (uses_relative < 0)
	    goto onError;
    }
    else if (other->scheme) {
	len = PyString_GET_SIZE(other->scheme);
	memcpy(scheme,
	       PyString_AS_STRING(other->scheme),
	       len);
	uses_relative = mxURL_SchemeUsesRelativePaths(other->scheme);
	if (uses_relative < 0)
	    goto onError;
    }
    else {
	/* not given */
	len = 0;
	uses_relative = 1;
    }
    scheme_len = len;
    scheme[len] = '\0';
    DPRINTF(" using scheme '%s'\n",scheme);
    w += len + 1;

    /* Decide whether or not to inherit the params and query settings
       from the base URL in case these are not given in the other URL.

       One special case for which this is needed is where the other
       URL is empty or only contains a fragment; in this case the base
       URL entries for params and query preserved.

    */
    if (PyString_GET_SIZE(other->url) == 0 ||
	(!other->scheme && !other->netloc_len && !other->path_len && 
	 !other->query_len && !other->params_len)) {
	inherit_params = 1;
	inherit_query = 1;
    }
    else {
	inherit_params = 0;
	inherit_query = 0;
    }
    
    /* Netloc from other URL overrides base netloc */
    netloc = w;
    if (other->netloc_len) {
	len = other->netloc_len;
	memcpy(netloc,&otherurl[other->netloc],len);
    }
    else {
	len = base->netloc_len;
	memcpy(netloc,&baseurl[base->netloc],len);
    }
    netloc_len = len;
    netloc[len] = '\0';
    DPRINTF(" using netloc '%s'\n",netloc);
    w += len + 1;

    /* Join paths */
    path = w;
    if (other->path_len) {
	if (!uses_relative || 
	    base->path_len == 0 || 
	    otherurl[other->path] == '/') {
	    /* other path overrides base path */
	    len = other->path_len;
	    memcpy(path,&otherurl[other->path],len);
	}
	else {
	    Py_ssize_t i;
	    
	    /* join the path in base with the relative one in other */
	    len = base->path_len;
	    memcpy(path,&baseurl[base->path],len);
	    
	    /* look for last '/' in base path */
	    i = len - 1;
	    if (i > 0 && path[i-1] == '.' && path[i] == '.') {
		/* path ends in '..' -- append '/' */
		i++;
		path[i] = '/';
	    }
	    else {
		for (; i >= 0; i--)
		    if (path[i] == '/')
			break;
	    }
	    path[i] = '/';
	    i++;
	    
	    /* now paste the other URL right behind it */
	    len = i + other->path_len;
	    memcpy(&path[i],&otherurl[other->path],other->path_len);
	}
    }
    else if (!other->netloc_len) {
	/* use the base URL's path */
	len = base->path_len;
	memcpy(path,&baseurl[base->path],len);
    }
    else
	/* not given */
	len = 0;
#if 0
    DPRINTF(" unnormalized path '%.*s'\n",len,path);
    /* Normalize path */
    len = mxURL_NormalizePath(path,len);
#endif
    path_len = len;
    path[len] = '\0';
    DPRINTF(" using path '%s'\n",path);
    w += len + 1;

    /* Parameters from other URL */
    params = w;
    if (other->params_len) {
	len = other->params_len;
	memcpy(params,&otherurl[other->params],len);
    }
    else if (base->params_len && inherit_params) {
	len = base->params_len;
	memcpy(params,&baseurl[base->params],len);
    }
    else
	/* not given */
	len = 0;
    params_len = len;
    params[len] = '\0';
    DPRINTF(" using params '%s'\n",params);
    w += len + 1;

    /* Query from other URL */
    query = w;
    if (other->query_len) {
	len = other->query_len;
	memcpy(query,&otherurl[other->query],len);
    }
    else if (base->query_len && inherit_query) {
	len = base->query_len;
	memcpy(query,&baseurl[base->query],len);
    }
    else
	/* not given */
	len = 0;
    query_len = len;
    query[len] = '\0';
    DPRINTF(" using query '%s'\n",query);
    w += len + 1;

    /* Fragment from other URL */
    fragment = w;
    if (other->fragment_len) {
	len = other->fragment_len;
	memcpy(fragment,&otherurl[other->fragment],len);
    }
    else
	/* not given */
	len = 0;
    fragment_len = len;
    fragment[len] = '\0';
    DPRINTF(" using fragment '%s'\n",fragment);
    w += len + 1;

    /* Create a new URL object */
    url = mxURL_New();
    if (!url)
	goto onError;

    /* Set variables */
    if (mxURL_SetFromBrokenDown(url,
				scheme,
				scheme_len,
				netloc,
				netloc_len,
				path,
				path_len,
				params,
				params_len,
				query,
				query_len,
				fragment,
				fragment_len,
				NORMALIZE_URL) < 0)
	goto onError;

 finished:
    DPRINTF(" returning '%s'\n",PyString_AS_STRING(url->url));

    if (workspace != buffer)
	free(workspace);
    return url;

 onError:
    if (workspace != buffer && workspace)
	free(workspace);
    Py_XDECREF(url);
    return NULL;
}

/* --- methods --- (should have lowercase extension) */

#define url ((mxURLObject*)self)

Py_C_Function( mxURL_normalized,
	       "normalized()\n\n"
	       "Return a new URL object pointing to the same URL\n"
	       "but normalized."
	       )
{
    PyObject *v;

    Py_NoArgsCheck();

    v = (PyObject *)mxURL_NormalizedFromURL(url);
    if (v == NULL)
	goto onError;
    return v;

 onError:
    return NULL;
}

Py_C_Function( mxURL_parsed,
	       "parsed()\n\n"
	       "Return a tuple (scheme, netloc, path, params, query, fragment)\n"
	       "just as urlparse.urlparse() does."
	       )
{
    Py_NoArgsCheck();

    return mxURL_Parsed(url);

 onError:
    return NULL;
}

Py_C_Function( mxURL_basic,
	       "basic()\n\n"
	       "Return a new URL object pointing to the same base URL,\n"
	       "but without the parts params, query and fragment. In case\n"
	       "the url already forfills this requirement, a new reference\n"
	       "to it is returned."
	       )
{
    Py_NoArgsCheck();

    return (PyObject *)mxURL_BasicFromURL(url);

 onError:
    return NULL;
}

Py_C_Function( mxURL_depth,
	       "depth()\n\n"
	       "Return the depth of the URL. Depth is only defined if\n"
	       "the URL is normalized and absolute. Otherwise an error\n"
	       "will be raised. Toplevel has depth 0."
	       )
{
    Py_ssize_t depth;

    Py_NoArgsCheck();

    depth = mxURL_Depth(url);
    if (depth < 0)
	goto onError;
    return PyInt_FromSsize_t(depth);
 onError:
    return NULL;
}

Py_C_Function( mxURL_relative,
	       "relative(baseURL)\n\n"
	       "Return a new URL object that when joined with baseURL\n"
	       "results in the same basic URL as the object itself.\n"
	       "URL and baseURL must both be absolute URLs for this to\n"
	       "work. An exception is raised otherwise."
	       )
{
    PyObject *base = 0;
    PyObject *relurl;

    Py_GetArg("O",base);

    if (PyString_Check(base)) {
	base = (PyObject *)mxURL_FromString(PyString_AS_STRING(base),
					    NORMALIZE_URL);
	if (!base)
	    goto onError;
    }
    else
	Py_INCREF(base);
    Py_Assert(_mxURL_Check(base),
	      PyExc_TypeError,
	      "argument must be a URL or a string");

    relurl = (PyObject *)mxURL_RelativeFromURL(url,(mxURLObject *)base);
    if (!relurl)
	goto onError;

    Py_DECREF(base);
    return relurl;

 onError:
    Py_XDECREF(base);
    return NULL;
}

Py_C_Function_WithKeywords(
	       mxURL_rebuild,
	       "rebuild(scheme='',netloc='',path='',params='',query='',fragment='')\n\n"
	       "Return a new URL object created from the given parameters and\n"
	       "the URL object. This method can handle keywords."
	       )
{
    char *scheme = NULL,*netloc = NULL,*path = NULL;
    char *params = NULL,*query = NULL,*fragment = NULL;

    Py_KeywordsGet6Args("|ssssss",
			scheme,netloc,path,params,query,fragment);

    return (PyObject *)mxURL_RebuildFromBrokenDown(url,
						   scheme,
						   netloc,
						   path,
						   params,
						   query,
						   fragment,
						   NORMALIZE_URL);
 onError:
    return NULL;
}

Py_C_Function( mxURL_pathentry,
	       "pathentry(index)\n\n"
	       "Return the path entry index. index may also be negative to\n"
	       "indicate an entry counted from the right. An IndexError is\n"
	       "raised in case the index lies out of range. Leading and\n"
	       "trailing '/' are not counted."
	       )
{
    Py_ssize_t index;

    Py_GetArg(Py_SSIZE_T_PARSERMARKER, index);

    return mxURL_PathEntry(url,index);

 onError:
    return NULL;
}

Py_C_Function( mxURL_pathlen,
	       "pathlen()\n\n"
	       "Returns path length defined by the semantics of .pathentry()"
	       )
{
    Py_ssize_t length;

    Py_NoArgsCheck();

    length = mxURL_PathLength(url);
    if (length < 0)
	goto onError;
    return PyInt_FromSsize_t(length);
 onError:
    return NULL;
}

Py_C_Function( mxURL_pathtuple,
	       "pathtuple()\n\n"
	       "Returns the path as tuple using the semantics of .pathentry()"
	       )
{
    Py_NoArgsCheck();

    return mxURL_PathTuple(url);

 onError:
    return NULL;
}

#undef url

/* --- slots --- */

static
int mxURL_Print(PyObject *obj,
		FILE *fp, 
		int flags)
{
    mxURLObject *self = (mxURLObject *)obj;

    fprintf(fp, "<URL:%s>", PyString_AS_STRING(self->url));
    return 0;
}

static
PyObject *mxURL_Repr(PyObject *obj)
{
    mxURLObject *self = (mxURLObject *)obj;
    char s[256];

    if (PyString_GET_SIZE(self->url) > 150)
	sprintf(s,"<URL object at %lx>",(long)self);
    else
	sprintf(s,"<URL object for '%s' at %lx>",
		PyString_AS_STRING(self->url),(long)self);
    return PyString_FromString(s);
}

static
PyObject *mxURL_Str(PyObject *obj)
{
    mxURLObject *self = (mxURLObject *)obj;

    Py_INCREF(self->url);
    return self->url;
}

static
int mxURL_Compare(PyObject *v,
		  PyObject *w)
{
    mxURLObject *left = (mxURLObject *)v;
    mxURLObject *right = (mxURLObject *)w;

    return PyObject_Compare(left->url, right->url);
}

static
long mxURL_Hash(PyObject *obj)
{
    mxURLObject *self = (mxURLObject *)obj;

    return PyObject_Hash(self->url);
}

#if 0
static
PyObject *mxURL_Add(PyObject *v,
		    PyObject *w)
{
    mxURLObject *self = (mxURLObject *)v;
    mxURLObject *other = (mxURLObject *)w;

    return (PyObject *)mxURL_FromJoiningURLs(self, other);
}
#endif

static
PyObject *mxURL_Getattr(PyObject *obj,
			char *name)
{
    mxURLObject *self = (mxURLObject *)obj;

    if (Py_WantAttr(name,"url") || Py_WantAttr(name,"string")) {
	Py_INCREF(self->url);
	return self->url;
    }

    else if (Py_WantAttr(name,"scheme")) {
	if (self->scheme) {
	    Py_INCREF(self->scheme);
	    return self->scheme;
	}
	else
	    return PyString_FromStringAndSize("",0);
    }

    else if (Py_WantAttr(name,"netloc"))
	return PyString_FromStringAndSize(
			&PyString_AS_STRING(self->url)[self->netloc],
			self->netloc_len);

    else if (Py_WantAttr(name,"path"))
	return PyString_FromStringAndSize(
			&PyString_AS_STRING(self->url)[self->path],
			self->path_len);

    else if (Py_WantAttr(name,"normal"))
	return PyInt_FromLong((long)self->path_normalized);
    
    else if (Py_WantAttr(name,"absolute")) {
	if (mxURL_AbsolutePath(self))
	    Py_ReturnTrue();
	Py_ReturnFalse();
    }
    
    else if (Py_WantAttr(name,"params"))
	return PyString_FromStringAndSize(
			&PyString_AS_STRING(self->url)[self->params],
			self->params_len);

    else if (Py_WantAttr(name,"query"))
	return PyString_FromStringAndSize(
			&PyString_AS_STRING(self->url)[self->query],
			self->query_len);

    else if (Py_WantAttr(name,"fragment"))
	return PyString_FromStringAndSize(
			&PyString_AS_STRING(self->url)[self->fragment],
			self->fragment_len);

    else if (Py_WantAttr(name,"mimetype"))
	return mxURL_MIMEType(self);

    else if (Py_WantAttr(name,"ext"))
	return mxURL_Extension(self);

    else if (Py_WantAttr(name,"base"))
	return mxURL_Base(self);

    else if (Py_WantAttr(name,"file"))
	return mxURL_File(self);

    else if (Py_WantAttr(name,"host"))
	return mxURL_Hostname(self);

    else if (Py_WantAttr(name,"user"))
	return mxURL_Username(self);

    else if (Py_WantAttr(name,"passwd"))
	return mxURL_Password(self);

    else if (Py_WantAttr(name,"port"))
	return mxURL_Port(self);

    else if (Py_WantAttr(name,"__members__"))
	return Py_BuildValue("[ssssssssssssssssss]",
			     "url", "scheme", "netloc",
			     "path", "params", "query", "mimetype",
			     "fragment", "ext", "base",
			     "file", "string", "absolute",
			     "normal","host","user",
			     "passwd","port"
			     );

    return Py_FindMethod(mxURL_Methods,
			 (PyObject *)self,name);
}

#ifdef Py_NEWSTYLENUMBER
static
int mxURL_NonZero(PyObject *obj)
{
    mxURLObject *self = (mxURLObject *)obj;

    return PyString_GET_SIZE(self->url) > 0;
}
#endif

static
Py_ssize_t mxURL_Length(PyObject *obj)
{
    mxURLObject *self = (mxURLObject *)obj;

    return PyString_GET_SIZE(self->url);
}

/* URL-join two URLs giving a new one, or a URL and a string giving a
   new URL. string + URL only works with New Style Numbers and then
   also results in a URL object. */

static
PyObject *mxURL_Concat(PyObject *self,
		       PyObject *other)
{
    mxURLObject *new_url;
    
    if (_mxURL_Check(self) && _mxURL_Check(other))
	return (PyObject *)mxURL_FromJoiningURLs((mxURLObject *)self,
						 (mxURLObject *)other);

    /* Be prepared for the new style number mechanism... */
    new_url = 0;
    if (_mxURL_Check(self)) {
	Py_Assert(PyString_Check(other),
		  PyExc_TypeError,
		  "can't concat URL and other object");
    
	other = (PyObject *)mxURL_FromString(PyString_AS_STRING(other),
					     RAW_URL);
	if (!other)
	    goto onError;
	new_url = mxURL_FromJoiningURLs((mxURLObject *)self,
					(mxURLObject *)other);
	Py_DECREF(other);
    }
    else if (_mxURL_Check(other) ) {
	Py_Assert(PyString_Check(self),
		  PyExc_TypeError,
		  "can't concat other object and URL");
    
	self = (PyObject *)mxURL_FromString(PyString_AS_STRING(self),
					    RAW_URL);
	if (!self)
	    goto onError;
	new_url = mxURL_FromJoiningURLs((mxURLObject *)self,
					(mxURLObject *)other);
	Py_DECREF(self);
    }
    else
	PyErr_BadInternalCall();
    if (!new_url)
	goto onError;
    return (PyObject *)new_url;

 onError:
    return NULL;
}

/* Make url[i] work like string[i] */

static
PyObject *mxURL_Item(PyObject *obj,
		     Py_ssize_t index)
{
    mxURLObject *self = (mxURLObject *)obj;

    char *url = PyString_AS_STRING(self->url);
    
    if (index < 0 || index >= PyString_GET_SIZE(self->url))
	Py_Error(PyExc_IndexError,
		 "index out of range");
    return PyString_FromStringAndSize(&url[index],1);

 onError:
    return NULL;
}

static 
PyObject *mxURL_Slice(PyObject *obj,
		      Py_ssize_t left,
		      Py_ssize_t right)
{
    mxURLObject *self = (mxURLObject *)obj;

    char *url = PyString_AS_STRING(self->url);
    
    Py_CheckStringSlice(self->url,left,right);
    if (left == 0 && right == PyString_GET_SIZE(self->url)) {
	Py_INCREF(self->url);
	return self->url;
    }
    return PyString_FromStringAndSize(&url[left],right-left);
}

static
PyObject *mxURL_Repeat(PyObject *obj,
		       Py_ssize_t index)
{
    /* mxURLObject *self = (mxURLObject *)obj; */

    Py_Error(mxURL_Error,
	     "URL*int not implemented");

 onError:
    return NULL;
}

/* Python Type Tables */

/* We play new style number to be able to implement
   string + URL == URL */

#ifdef Py_NEWSTYLENUMBER
static
PyNumberMethods mxURL_TypeAsNumber = {

    /* These slots are not NULL-checked, so we must provide dummy functions */
    mxURL_Concat,			/*nb_add*/
    notimplemented2,			/*nb_subtract*/
    notimplemented2,			/*nb_multiply*/
    notimplemented2,			/*nb_divide*/
    notimplemented2,			/*nb_remainder*/
    notimplemented2,			/*nb_divmod*/
    notimplemented3,			/*nb_power*/
    notimplemented1,			/*nb_negative*/
    notimplemented1,			/*nb_positive*/

    /* Everything below this line EXCEPT nb_nonzero (!) is NULL checked */
    0,					/*nb_absolute*/
    mxURL_NonZero,			/*nb_nonzero*/
    0,					/*nb_invert*/
    notimplemented2,			/*nb_lshift*/
    notimplemented2,			/*nb_rshift*/
    notimplemented2,			/*nb_and*/
    notimplemented2,			/*nb_xor*/
    notimplemented2,			/*nb_or*/
    Py_NEWSTYLENUMBER,			/*nb_coerce*/
    0,					/*nb_int*/
    0,					/*nb_long*/
    0,					/*nb_float*/
    0,					/*nb_oct*/
    0,					/*nb_hex*/
};
#endif

static
PySequenceMethods mxURL_TypeAsSequence = {
    /* These slots are not NULL-checked */
    mxURL_Length,			/*sq_length*/
    mxURL_Concat,			/*sq_concat*/
    mxURL_Repeat,			/*sq_repeat*/
    mxURL_Item,				/*sq_item*/
    /* Everything below is NULL checked. */
    mxURL_Slice,			/*sq_slice*/
    0,					/*sq_ass_item*/
    0,					/*sq_ass_slice*/
};

statichere
PyTypeObject mxURL_Type = {
    PyObject_HEAD_INIT(0)		/* init at startup ! */
    0,			  		/*ob_size*/
    "URL",	  			/*tp_name*/
    sizeof(mxURLObject),	      	/*tp_basicsize*/
    0,			  		/*tp_itemsize*/
    /* slots */
    (destructor)mxURL_Free,		/*tp_dealloc*/
    mxURL_Print,	  		/*tp_print*/
    mxURL_Getattr,  			/*tp_getattr*/
    0,		  			/*tp_setattr*/
    mxURL_Compare,  			/*tp_compare*/
    mxURL_Repr,	  			/*tp_repr*/
#ifdef Py_NEWSTYLENUMBER
    &mxURL_TypeAsNumber, 		/*tp_as_number*/
#else
    0,			 		/*tp_as_number*/
#endif
    &mxURL_TypeAsSequence,		/*tp_as_sequence*/
    0,					/*tp_as_mapping*/
    mxURL_Hash,				/*tp_hash*/
    0,					/*tp_call*/
    mxURL_Str,				/*tp_str*/
    0, 					/*tp_getattro*/
    0, 					/*tp_setattro*/
    0,					/*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT,			/*tp_flags*/
    0,					/* tp_doc */
    0,					/* tp_traverse */
    0,					/* tp_clear */
    0,					/* tp_richcompare */
    0,					/* tp_weaklistoffset */
    0,					/* tp_iter */
    0,					/* tp_iternext */
    mxURL_Methods,			/* tp_methods */
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
PyMethodDef mxURL_Methods[] =
{   
    Py_MethodListEntryNoArgs("depth",mxURL_depth),
    Py_MethodListEntry("pathentry",mxURL_pathentry),
    Py_MethodListEntryNoArgs("pathlen",mxURL_pathlen),
    Py_MethodListEntryNoArgs("parsed",mxURL_parsed),
    Py_MethodListEntryNoArgs("basic",mxURL_basic),
    Py_MethodListEntry("relative",mxURL_relative),
    Py_MethodWithKeywordsListEntry("rebuild",mxURL_rebuild),
    Py_MethodListEntryNoArgs("normalized",mxURL_normalized),
    Py_MethodListEntryNoArgs("pathtuple",mxURL_pathtuple),
    {NULL,NULL} /* end of list */
};

/* --- Other functions ----------------------------------------------------- */

Py_C_Function( mxURL_URL,
	       "URL(url)\n\n"
	       "Create a new URL object from url. Takes either a string\n"
	       "or another URL as argument. The url is stored normalized."
	       )
{
    PyObject *v;

    Py_GetArgObject(v);

    if (_mxURL_Check(v))
	return (PyObject *)mxURL_NormalizedFromURL((mxURLObject*)v);

    Py_Assert(PyString_Check(v),
	      PyExc_TypeError,
	      "argument must be a string or a URL");

    v = (PyObject *)mxURL_FromString(PyString_AS_STRING(v),
				     NORMALIZE_URL);
    if (v == NULL)
	goto onError;
    return v;
 onError:
    return NULL;
}

Py_C_Function_WithKeywords(
	       mxURL_BuildURL,
	       "BuildURL(scheme='',netloc='',path='',params='',query='',fragment='')\n\n"
	       "Create a new URL object from the given parameters.\n"
	       "The url is stored normalized. This constructor can handle\n"
	       "keywords."
	       )
{
    char *scheme = "",*netloc = "",*path = "";
    char *params = "",*query = "",*fragment = "";

    Py_KeywordsGet6Args("|ssssss",
			scheme,netloc,path,params,query,fragment);

    return (PyObject *)mxURL_FromBrokenDown(scheme,
					    netloc,
					    path,
					    params,
					    query,
					    fragment,
					    NORMALIZE_URL);
 onError:
    return NULL;
}

Py_C_Function( mxURL_RawURL,
	       "RawURL(url)\n\n"
	       "Create a new URL object from url. Takes either a string\n"
	       "or another URL as argument. The url is not normalized but\n"
	       "stored as-is."
	       )
{
    PyObject *v;

    Py_GetArgObject(v);

    if (_mxURL_Check(v)) {
	Py_INCREF(v);
	return v;
    }

    Py_Assert(PyString_Check(v),
	      PyExc_TypeError,
	      "argument must be a string or a URL");

    v = (PyObject *)mxURL_FromString(PyString_AS_STRING(v),
				     RAW_URL);
    if (v == NULL)
	goto onError;
    return v;
 onError:
    return NULL;
}

Py_C_Function( mxURL_urljoin,
	       "urljoin(u,v)\n\n"
	       "Takes two URLs or strings, joins them and returns the\n"
	       "result as URL object")
{
    mxURLObject *a = 0,*b = 0;
    PyObject *u,*v;
    PyObject *url;

    Py_Get2Args("OO",u,v);

    if (_mxURL_Check(u)) {
	a = (mxURLObject *)u;
	Py_INCREF(u);
    }
    else if (PyString_Check(u)) {
	a = mxURL_FromString(PyString_AS_STRING(u),
			     RAW_URL);
	if (!a)
	    goto onError;
    }
    else
	Py_Error(PyExc_TypeError,
		 "arguments must be URLs or strings");

    if (_mxURL_Check(v)) {
	b = (mxURLObject *)v;
	Py_INCREF(v);
    }
    else if (PyString_Check(v)) {
	b = mxURL_FromString(PyString_AS_STRING(v),
			     RAW_URL);
	if (!b)
	    goto onError;
    }
    else
	Py_Error(PyExc_TypeError,
		 "arguments must be URLs or strings");

    url = (PyObject *)mxURL_FromJoiningURLs((mxURLObject*)a,
					    (mxURLObject*)b);
    if (!url)
	goto onError;

    DPRINTF(" urljoin() returning '%s'\n",
	    PyString_AS_STRING(((mxURLObject *)url)->url));
    Py_DECREF(a);
    Py_DECREF(b);
    Py_PRINT_REFCOUNT(url);
    return url;

 onError:
    Py_XDECREF(a);
    Py_XDECREF(b);
    return NULL;
}

Py_C_Function( mxURL_setmimedict,
	       "setmimedict(dict)\n\n"
	       "Sets the MIME type map dictionary used by the package."
	       )
{
    PyObject *v;

    Py_GetArgObject(v);

    Py_Assert(PyDict_Check(v),
	      PyExc_TypeError,
	      "argument must be a dictionary");

    Py_INCREF(v);
    mxURL_MIMEDict = v;

    Py_ReturnNone();
 onError:
    return NULL;
}

/* --- module init --------------------------------------------------------- */

/* Python Method Table */

static 
PyMethodDef Module_methods[] =
{   
    Py_MethodListEntrySingleArg("URL",mxURL_URL),
    Py_MethodListEntrySingleArg("RawURL",mxURL_RawURL),
    Py_MethodListEntry("urljoin",mxURL_urljoin),
    Py_MethodWithKeywordsListEntry("BuildURL",mxURL_BuildURL),
    Py_MethodListEntrySingleArg("setmimedict",mxURL_setmimedict),
    {NULL,NULL} /* end of list */
};

/* C API table */
static
mxURLModule_APIObject mxURLModuleAPI =
{
    &mxURL_Type,
    mxURL_FromString,
    mxURL_AsString,
    mxURL_FromBrokenDown,
    mxURL_NormalizedFromURL,
};

/* Cleanup function */
static 
void mxURLModule_Cleanup(void)
{
#ifdef MXURL_FREELIST
    {
	mxURLObject *d = mxURL_FreeList;
	while (d != NULL) {
	    mxURLObject *v = d;
	    d = *(mxURLObject **)d;
	    PyObject_Del(v);
	}
	mxURL_FreeList = NULL;
    }
#endif
    if (!mxPy_DECREF_Unsafe_AtExit()) {
	/* We keep a weakref global mxURL_MIMEDict, but this lives in the
	   mimetools module dictionary and gets GCed there. */
    }

    /* Clear weakref globals */
    mxURL_MIMEDict = NULL;

    /* Reset mxURL_Initialized flag */
    mxURL_Initialized = 0;
}

/* create PyMethodObjects and register them in the module's dict */
MX_EXPORT(void) 
     initmxURL(void)
{
    PyObject *module, *moddict, *api;
    int i;

    if (mxURL_Initialized)
	Py_Error(PyExc_SystemError,
		 "can't initialize "MXURL_MODULE" more than once");

    /* Create module */
    module = Py_InitModule4(MXURL_MODULE, /* Module name */
			    Module_methods, /* Method list */
			    Module_docstring, /* Module doc-string */
			    (PyObject *)NULL, /* always pass this as *self */
			    PYTHON_API_VERSION); /* API Version */
    if (module == NULL)
	goto onError;

    /* Init type objects */
    PyType_Init(mxURL_Type);

    /* Init globals */
#ifdef MXURL_FREELIST
    mxURL_FreeList = NULL;
#endif

    /* Add some constants to the module's dict */
    moddict = PyModule_GetDict(module);
    PyDict_SetItemString(moddict, 
			 "__version__",
			 PyString_FromString(MXURL_VERSION));

    /* Init the scheme dict */
    mxURL_SchemeDict = PyDict_New();
    if (mxURL_SchemeDict == NULL)
	goto onError;
    for (i = 0; i < sizeof(mxURL_SchemeFeatures) /
	            sizeof(mxURL_SchemeFeature); i++) {
	mxURL_SchemeFeature *s = &mxURL_SchemeFeatures[i];
	PyObject *t;
	t = Py_BuildValue("(iiiii)",
			  s->uses_netloc,
			  s->uses_params, 
			  s->uses_query, 
			  s->uses_fragment, 
			  s->uses_relative);
	if (!t)
	    goto onError;
	if (PyDict_SetItemString(mxURL_SchemeDict,
				 s->scheme,
				 t))
	    goto onError;
    }
    if (PyDict_SetItemString(moddict,"schemes",mxURL_SchemeDict))
	goto onError;

    /* URL unsafe characters */
    mxURL_URLUnsafeCharacters = PyString_FromString(URL_unsafe_chars);
    if (mxURL_URLUnsafeCharacters == NULL)
	goto onError;
    if (PyDict_SetItemString(moddict,"url_unsafe_charset",
			     mxURL_URLUnsafeCharacters))
	goto onError;

    /* Errors */
    if (!(mxURL_Error = insexc(moddict,"Error",PyExc_StandardError)))
	goto onError;

    /* Type objects */
    Py_INCREF(&mxURL_Type);
    PyDict_SetItemString(moddict, "URLType",
			 (PyObject *)&mxURL_Type);

    /* Register cleanup function */
    if (Py_AtExit(mxURLModule_Cleanup)) {
	/* XXX what to do if we can't register that function ??? */
	DPRINTF("* Failed to register mxURL cleanup function\n");
    }

    /* Export C API */
    api = PyCObject_FromVoidPtr((void *)&mxURLModuleAPI, NULL);
    if (api == NULL)
	goto onError;
    PyDict_SetItemString(moddict,MXURL_MODULE"API",api);
    Py_DECREF(api);

    /* We are now initialized */
    mxURL_Initialized = 1;

 onError:
    /* Check for errors and report them */
    if (PyErr_Occurred())
	Py_ReportModuleInitError(MXURL_MODULE);
    return;
}
