/*
   xmapmodule.c - Implementation of xmap function/type by
                  Christopher Tavares (mailto:tavares@connix.com).

   Version: 0.3

   Based on version 0.2 contributed by Christopher Tavares.

*/

#include "Python.h"
#include "assert.h"

#include "mx.h"
#include "mxTools.h"

/*
// Bogus sun headers don't have prototypes for printf or fprintf.
// Add them
*/
#if defined(__STDC__) && !defined(STDC_HEADERS)
int printf(const char *, ...);
int fprintf(FILE *, const char *, ...);
#endif

#if 0
/*
// This is not needed for Python 1.5. If you're running 1.4,
// enable this
*/

/*
// Bug fix: workaround for bug in PySequence_GetItem when
// accessing sequences that don't have __len__ defined.
// Thanks to J. Fulton.
*/

/*
Subject: Re: Help for extending newbie
From: jim@digicool.com (Jim Fulton)
Newsgroups: comp.lang.python

  In article <MPG.cedc3757c747a9b989685@news.connix.com>
  tavares@connix.com (Christopher Tavares) writes: 
  
	Hi all! I'm working on my first C extension module (a "lazy" version of 
	map) and I've run into a snag that I was hoping to get some advice on.
	
	  You see, I'm trying to support passing class instances that have 
	  __getitem__ defined but not __len__. (For testing, I've been using the 
	  File class on page 32 of Lutz.)  My Python prototype handled this just 
	  fine. However, when I call PySequence_GetItem for such a class instance, 
	  I get "AttributeError: __len__."  Is there any way around this without 
	  acres of coding, or will I just have to require that sequences I work 
	  with have __len__ defined?
	  
		Yipes, this is a bug in PySequence_GetItem!  :-(
		
		  I'll submit a fix to Guido, but in the mean time, you might want to
		  simply include this replacement in your module:
*/

static PyObject * 
PySequence_GetItem_fix(PyObject *s,
		       int i)
{
    PySequenceMethods *m;
    int l;
    
    if(! s)
    {
		if(!PyErr_Occurred())
		{
			PyErr_SetString(PyExc_SystemError, "null argument to internal routine");
		}
		return NULL;
    }
    
    if(! ((m=s->ob_type->tp_as_sequence) && m->sq_item))
    {
		PyErr_SetString(PyExc_AttributeError, "__getitem__");
		return NULL;
    }
    
    if(i < 0)
    {
		if(! m->sq_length || 0 > (l=m->sq_length(s))) return NULL;
		i += l;
    }
    
    return m->sq_item(s,i);
}

#define PySequence_GetItem PySequence_GetItem_fix
#endif

/* ----------------------------------------------------- */

/* Declarations for objects of type xmaptype */

typedef struct {
	PyObject_HEAD
	/* XXXX Add your own stuff here */
	PyObject *func;		/* Function object */
	unsigned nseqs;		/* Number of sequences */
	PyObject **seqs;	/* List of sequences */
} xmaptobject;

staticforward PyTypeObject Xmapttype;

#define is_xmapobject(s) ((s)->ob_type == &Xmapttype)

/* prototypes for functions we need later */

static PyObject *xmapt_item(xmaptobject *self, int i);
static int xmapt_length(xmaptobject *self);
static PyObject *xmapt_tolist(PyObject *self, PyObject *args);

/* ---------------------------------------------------------------- */

static struct PyMethodDef xmapt_methods[] = {
	{"tolist", xmapt_tolist, 1},
	{NULL, NULL}		/* sentinel */
};

/* ---------- */

/*
// "constructor" for xmap objects. This is called by the xmap.xmap
// function. It is passed the argument tuple passed to xmap.xmap.
// The arguments have already been validated for correct number,
// types, etc.
*/

static xmaptobject *
newxmaptobject(PyObject *args)
{
    xmaptobject *self;
    PyObject **seqs;
    unsigned nseqs;
    unsigned seq;
    
    /*
    // Grab memory to store sequence lists. This
    // is done before allocating self to make cleanup
    // a little easier for novices like me.
    */
    nseqs = PyObject_Length(args) - 1;
    assert(nseqs > 0);
    
    seqs = (PyObject **)malloc(sizeof(PyObject *) * nseqs);
    if(seqs == NULL)
    {
		PyErr_SetString(PyExc_MemoryError,
			"Could not allocate space for sequence list");
		return NULL;
    }
	
    self = PyObject_NEW(xmaptobject, &Xmapttype);
    if (self == NULL)
    {
		free(seqs);
		return NULL;
    }
    
    self->func = PySequence_GetItem(args, 0);
    self->nseqs = nseqs;
    self->seqs = seqs;
    
    for(seq = 0; seq < nseqs; seq++)
    {
		seqs[seq] = PySequence_GetItem(args, seq + 1);
		assert(seqs[seq] != NULL);
    }
	
    return self;
}


static void
xmapt_dealloc(xmaptobject *self)
{
    unsigned seq;
    
    Py_DECREF(self->func);
    for(seq = 0; seq < self->nseqs; seq++)
    {
		Py_DECREF(self->seqs[seq]);
    }
    free(self->seqs);
    PyMem_DEL(self);
}

/*
// standard getattr function
*/

static PyObject *xmapt_getattr(xmaptobject *self,
			       char *name)
{
    return Py_FindMethod(xmapt_methods, (PyObject *)self, name);
}

/* Print object to fp - print it like a tuple */
static int xmapt_print(xmaptobject *self,
		       FILE *fp,
		       int flags)
{
    PyObject *element;
    int i = 0, printcomma = 0;
	
    fprintf(fp, "(xmap: ");
	
    do
    {
		element = xmapt_item(self, i);
		if(element != NULL)
		{
			if(printcomma)
			{
				fprintf(fp, ", ");
			}		
			PyObject_Print(element, fp, 0);
			Py_DECREF(element);
		}
		i++;
		printcomma = 1;
    } while(element != NULL);
    fprintf(fp, ")");
    if(PyErr_Occurred() == PyExc_IndexError) 
    {
		PyErr_Clear();
		return 0;
    }
    return -1;
}

/*
// CUSTOM METHOD: tolist()
// Calculates all the values of the mapping and returns them as a
// list. Effectively the same as just doing a map.
*/

static PyObject *xmapt_tolist(PyObject *self, 
			      PyObject *args)
{
    PyObject *templist, *element;
    int len, i;
    
    if(self == NULL || !is_xmapobject(self))
    {
		PyErr_SetString(PyExc_SystemError, "bad self pointer to xmap tolist member");
		return NULL;
    }
    
    if(!PyArg_ParseTuple(args, ""))
    {
		return NULL;
    }
    
    /* 
    // If we have a length, we can preallocate the list. Otherwise,
    // we have to append to it.
    */
    len = xmapt_length((xmaptobject *)self);
    if(len != -1)
    {
		templist = PyList_New(len);
		if(templist == NULL)
		{
			return NULL;
		}
		for(i = 0; i < len; i++)
		{
			element = xmapt_item((xmaptobject *)self, i);
			if(element == NULL)
			{
				goto bailout;
			}
			if(PyList_SetItem(templist, i, element) == -1)
			{
				goto bailout;
			}
		}
		return templist;
    }
    else
    {
		templist = PyList_New(0);
		if(templist == NULL)
		{
			return NULL;
		}
		for(i = 0; ; i++)
		{
			element = xmapt_item((xmaptobject *)self, i);
			if(element != NULL)
			{
				if(PyList_Append(templist, element) == -1)
				{
					goto bailout;
				}
			}
			else
			{
				if(PyErr_Occurred() == PyExc_IndexError)
				{
					PyErr_Clear();
					return templist;
				}
				goto bailout;
			}
		}
    }
bailout:
    assert(templist != NULL);
    Py_DECREF(templist);
    return NULL;
}

/* Code to handle accessing xmaptype objects as sequence objects */

/*
// Len is slightly strange because we need to handle
// "generator" types that may not have a length themselves.
// What we do is we get the length of each of our sequences,
// and if any of them fail, we return -1 (failure), otherwise
// we return the longest length.
*/

static int
xmapt_length(xmaptobject *self)
{
    unsigned seq;
    int len, curlen;
    
    for(len = 0, seq = 0; seq < self->nseqs; seq++)
    {
		curlen = PyObject_Length(self->seqs[seq]);
		if(curlen == -1)
		{
			return -1;
		}
		if(len < curlen)
		{
			len = curlen;
		}
    }
    return len;
}

static PyObject *
xmapt_concat(xmaptobject *self,
	     PyObject *bb)
{
    /* XXXX Return the concatenation of self and bb */
    PyErr_SetString(PyExc_TypeError, "cannot concatenate xmap objects");
    return NULL;
}

static PyObject *
xmapt_repeat(xmaptobject *self,
	     int n)
{
    PyErr_SetString(PyExc_TypeError, "Cannot repeat xmap objects");
    return NULL;
}

static PyObject *
xmapt_item(xmaptobject *self,
	   int i)
{
    unsigned seq;
    unsigned errcount = self->nseqs;
    PyObject *arg_list;
    PyObject *item;
    PyObject *result;
    
    /* Create argument tuple */
    arg_list = PyTuple_New(self->nseqs);
    if(arg_list == NULL)
    {
		return NULL;
    }
    
    /* Pull out items from each sequence */
    for(seq = 0; seq < self->nseqs; seq++)
    {
		item = PySequence_GetItem(self->seqs[seq], i);
		if(item != NULL)
		{
			PyTuple_SET_ITEM(arg_list, seq, item);
		}
		else
		{
			if(PyErr_Occurred() == PyExc_IndexError)
			{
				PyErr_Clear();
				Py_INCREF(Py_None);
				PyTuple_SET_ITEM(arg_list, seq, Py_None);
				errcount--;
			}
			else
			{
				Py_DECREF(arg_list);
				return NULL;
			}
		}
    }
    
    /*
    // If we got here and errcount == 0, we got IndexError for
    // every sequence. Therefore, we bail, returning IndexError.
    */
    if(errcount == 0)
    {
		PyErr_SetString(PyExc_IndexError, "index out of range");
		Py_DECREF(arg_list);
		return NULL;
    }
    
    /*
    // If function is None, return arg_list tuple, with one exception.
    // If there's only one element in the argument list, just return
    // that element.
    */
    if(self->func == Py_None)
    {
		if(self->nseqs == 1)
		{
			result = PySequence_GetItem(arg_list, 0);
		}
		else
		{
			result = arg_list;
			Py_INCREF(result);
		}
    }
    else
    {
	/*
	// Function is NOT null, so we call it and get the result
		*/
		result = PyObject_CallObject(self->func, arg_list);
    }
    
    /* Clean up argument list, return result */
    Py_DECREF(arg_list);
    return result;
}

/*
// get slice method. We do this by grabbing the indicated slice
// from each input sequence and then creating a new xmap object
// with the same function.
*/

static PyObject *
xmapt_slice(xmaptobject *self,
	    int ilow, 
	    int ihigh)
{
    PyObject *args;		/* arguments to create new xmap object */
    PyObject *slice;		/* Slice of input sequence */
    xmaptobject *new_xmap;	/* New xmap object to be created */
    unsigned int i;
    
    /* Create argument tuple */
    args = PyTuple_New(self->nseqs + 1); /* func + sequences */
    if(args == NULL)
		return NULL;
    
    Py_INCREF(self->func);
    PyTuple_SET_ITEM(args, 0, self->func);
    for(i = 0; i < self->nseqs; i++)
    {
		slice = PySequence_GetSlice(self->seqs[i], ilow, ihigh);
		if(slice == NULL)
		{
			Py_DECREF(args);
			return NULL;
		}
		PyTuple_SET_ITEM(args, i + 1, slice);
    }
    
    new_xmap = newxmaptobject(args);
    Py_DECREF(args);
    return (PyObject *)new_xmap;
}

static PySequenceMethods xmapt_as_sequence = {
    (inquiry)xmapt_length,	/*sq_length*/
		(binaryfunc)xmapt_concat,	/*sq_concat*/
		(intargfunc)xmapt_repeat,	/*sq_repeat*/
		(intargfunc)xmapt_item,	/*sq_item*/
		(intintargfunc)xmapt_slice,	/*sq_slice*/
		(intobjargproc)0,		/*sq_ass_item*/
		(intintobjargproc)0,	/*sq_ass_slice*/
};

/* -------------------------------------------------------------- */

static char Xmapttype__doc__[] = 
""
;

statichere PyTypeObject Xmapttype = {
		PyObject_HEAD_INIT(0)		/* init at startup ! */
		0,				/*ob_size*/
		"xmaptype",			/*tp_name*/
		sizeof(xmaptobject),	/*tp_basicsize*/
		0,				/*tp_itemsize*/
		/* methods */
		(destructor)xmapt_dealloc,	/*tp_dealloc*/
		(printfunc)xmapt_print,	/*tp_print*/
		(getattrfunc)xmapt_getattr,	/*tp_getattr*/
		(setattrfunc)0,		/*tp_setattr*/
		(cmpfunc)0,			/*tp_compare*/
		(reprfunc)0,		/*tp_repr*/
		0,				/*tp_as_number*/
		&xmapt_as_sequence,		/*tp_as_sequence*/
		0,				/*tp_as_mapping*/
		(hashfunc)0,		/*tp_hash*/
		(ternaryfunc)0,		/*tp_call*/
		(reprfunc)0,		/*tp_str*/
		
		/* Space for future expansion */
		0L,0L,0L,0L,
		Xmapttype__doc__ /* Documentation string */
};

/* End of code for xmaptype objects */
/* -------------------------------------------------------- */


static char xmap_xmap__doc__[] =
""
;

/*
// Verify arguments, then create a new xmap object with
// the same arguments
*/

static PyObject *
xmap_xmap(PyObject *self,
	  PyObject *args)
{
    PyObject *func;
    PyObject *seq;
    PyObject *result = NULL;
    int arg, len;
    
    /* Check we've got at least 2 arguments */
    len = PyObject_Length(args);
    if(len < 2)
    {
		PyErr_SetString(PyExc_TypeError, "must have at least two arguments");
		return NULL;
    }
    
    func = PySequence_GetItem(args, 0);
    if(func != Py_None && !PyCallable_Check(func))
    {
		PyErr_SetString(PyExc_TypeError, "function argument must be callable");
		goto done;
    }
    
    for(arg = 1; arg < len; arg++)
    {
		seq = PySequence_GetItem(args, arg);
		if(seq == NULL)
		{
			goto done;
		}
		if(!PySequence_Check(seq))
		{
			PyErr_SetString(PyExc_TypeError, "arguments must be sequences");
			Py_DECREF(seq);
			goto done;
		}
		Py_DECREF(seq);
    }
    
    /* If we're here, arguments are OK */
    result = (PyObject *)newxmaptobject(args);
done:
    /* Clean up and return whatever happened */
    Py_DECREF(func);
    return result;
}

/* List of methods defined in the module */

static struct PyMethodDef xmap_methods[] = {
	{"xmap",	(PyCFunction)xmap_xmap,	METH_VARARGS,	xmap_xmap__doc__},
		
	{NULL,	 (PyCFunction)NULL, 0, NULL}		/* sentinel */
};


/* Initialization function for the module (*must* be called initxmap) */

static char xmap_module_documentation[] = 
"xmap: \"Lazy\" implementation of map\n"
"Xmap implements an object type that has the same relationship to map\n"
"that xrange does to range. map produces a list and calculates all the\n"
"values up front. xmap produces an object that generates the values\n"
"as they are indexed.\n"
"\nUsage:\n"
"\txmap(func, seq, [seq, seq, ...])\n"
"xmap object support indexing (obviously) and slicing (by forming slices of\n"
"the input sequences and creating a new xmap object).\n"
"\nGetting the length of an xmap object is a special case. Unlike map, xmap\n"
"can handle being given a sequence that does not have a __len__ method.\n"
"However, if any of the input sequences to xmap do not have __len__ defined,\n"
"then the resulting xmap object will not have __len__ defined either.\n"
"\nxmap objects do not support repetition or concatenation.\n"
"\nxmap objects also support one method: x.tolist(). This calculates all the\n"
"values and returns them as a list.\n"
;

MX_EXPORT(void)
    initxmap(void)
{
	PyObject *m;
	
	/* Init type objects */
	PyType_Init(Xmapttype);

	/* Create the module and add the functions */
	m = Py_InitModule4("xmap", xmap_methods,
		xmap_module_documentation,
		(PyObject*)NULL,PYTHON_API_VERSION);
	
 onError:
	/* Check for errors and report them */
	if (PyErr_Occurred())
	    Py_ReportModuleInitError("xmap");
}


