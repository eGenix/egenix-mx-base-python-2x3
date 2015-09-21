/* 
  mxQueue -- A queue implementation

  Copyright (c) 1999-2000, Marc-Andre Lemburg; mailto:mal@lemburg.com
  Copyright (c) 2000-2015, eGenix.com Software GmbH; mailto:info@egenix.com
  See the documentation for further copyright information or contact
  the author (mailto:mal@lemburg.com).

*/

/* Define this to aid in finding memory leaks */
/*#define MAL_MEM_DEBUG*/
/*#define MAL_DEBUG*/

/* Logging file used by debugging facility */
#ifndef MAL_DEBUG_OUTPUTFILE
# define MAL_DEBUG_OUTPUTFILE "mxQueue.log"
#endif

/* We want all our symbols to be exported */
#define MX_BUILDING_MXQUEUE

/* Mark the module as Py_ssize_t clean. */
#define PY_SSIZE_T_CLEAN 1

/* Setup mxstdlib memory management */
#if 1
# define MAL_USE_PYMALLOC
#else
# define MAL_USE_C_MALLOC
#endif

#include "mx.h"
#include "mxQueue.h"

#define MXQUEUE_VERSION "3.2.9"

/* The minimal size of queues; must be > 1 */
#define MINIMAL_QUEUESIZE	4

/* Grow strategy to be used: */
#if 1
/* Fibonacci-like */
# define GROW(size) size += size >> 1
#else
/* Double */
# define GROW(size) size <<= 1
#endif

/* --- module doc-string -------------------------------------------------- */

static char *Module_docstring = 

 MXQUEUE_MODULE" -- A queue implementation. Version "MXQUEUE_VERSION"\n\n"

 "Copyright (c) 1999-2000, Marc-Andre Lemburg; mailto:mal@lemburg.com\n"
 "Copyright (c) 2000-2015, eGenix.com Software GmbH; mailto:info@egenix.com\n\n"
 "                 All Rights Reserved\n\n"
 "See the documentation for further information on copyrights,\n"
 "or contact the author."
;

/* --- module globals ----------------------------------------------------- */

static PyObject *mxQueue_Error;			/* Error Exception
						   object */
static PyObject *mxQueue_EmptyError;		/* EmptyError Exception
						   object */

/* Flag telling us whether the module was initialized or not. */
static int mxQueue_Initialized = 0;

/* --- forward declarations ----------------------------------------------- */

staticforward PyTypeObject mxQueue_Type;
staticforward PyMethodDef mxQueue_Methods[];

/* --- internal macros ---------------------------------------------------- */

#define _mxQueue_Check(v) \
        (((mxQueueObject *)(v))->ob_type == &mxQueue_Type)


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
	modname = MXQUEUE_MODULE;
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

/* --- Queue Object -------------------------------------------------*/

/* --- allocation --- */

static
mxQueueObject *mxQueue_New(Py_ssize_t size)
{
    mxQueueObject *queue;
    PyObject **w;

    queue = PyObject_NEW(mxQueueObject,&mxQueue_Type);
    if (queue == NULL)
	return NULL;

    /* Init vars */
    if (size < MINIMAL_QUEUESIZE)
	size = MINIMAL_QUEUESIZE;
    queue->array = NULL;
    w = new(PyObject*,size);
    if (w == NULL) {
	Py_DECREF(queue);
	PyErr_NoMemory();
	return NULL;
    }
    queue->array = w;
    queue->size = size;
    queue->tail = queue->head = size - 1;
    DPRINTF("Created Queue object at 0x%0lx with size %i\n",
	    (long)queue,size);

    return queue;
}

/* --- deallocation --- */

static
void mxQueue_Free(register mxQueueObject *queue)
{
    if (queue->array) {
	register Py_ssize_t i;
	Py_ssize_t head = queue->head,
	    size = queue->size;
	
	for (i = queue->tail; i != head; i = (i+1) % size)
	    Py_DECREF(queue->array[i]);
	free(queue->array);
    }
    PyObject_Del(queue);
}

/* --- internal functions --- */

/* --- API functions --- */

static
Py_ssize_t _mxQueue_Length(register mxQueueObject *queue)
{
    register Py_ssize_t len = queue->head - queue->tail;
    if (len < 0)
	len += queue->size;
    return len;
}

static
Py_ssize_t mxQueue_Length(register PyObject *queue)
{
    return _mxQueue_Length((mxQueueObject *)queue);
}

static
int mxQueue_Push(register mxQueueObject *queue,
		 PyObject *v)
{
    Py_ssize_t tail,size = queue->size;
    
    if (queue == NULL) {
	PyErr_BadInternalCall();
	goto onError;
    }

    tail = queue->tail - 1;
    if (tail < 0)
	tail += size;
    DPRINTF("Old tail=%i, New tail=%i\n",queue->tail,tail);

    /* Grow the queue first, if we have touching ends */
    if (queue->head == tail) {
	PyObject **w;
	Py_ssize_t oldsize,oldtail;

	oldsize = size;
	oldtail = queue->tail;
	GROW(size);
	w = resize(queue->array,PyObject*,size);
	if (w == NULL) {
	    PyErr_NoMemory();
	    goto onError;
	}
	queue->array = w;
	queue->size = size;

	/* Adjust the pointers and copy the right part of the queue to
           the end of the array */
	queue->tail += size - oldsize;
	if (oldtail < queue->head)
	    queue->head += size - oldsize;
	memmove(&w[queue->tail], 
		&w[oldtail], 
		(oldsize - oldtail) * sizeof(PyObject *));

	/* Recalculate the new tail position */
	tail = queue->tail - 1;
    }
    
    Py_INCREF(v);
    queue->array[tail] = v;
    queue->tail = tail;
    return 0;

 onError:
    return -1;
}

#if 0
static
int mxQueue_PushMany(register mxQueueObject *queue,
		     PyObject *seq)
{
    Py_ssize_t tail;
    register Py_ssize_t i;
    Py_ssize_t length;

    if (queue == NULL) {
	PyErr_BadInternalCall();
	goto onError;
    }

    length = PySequence_Length(seq);
    if (length < 0)
	goto onError;

    tail = queue->tail;

    /* Grow the queue, if we hit the array boundary */
    if (tail + length >= queue->size) {
	register PyObject **w;
	register Py_ssize_t size;

	size = queue->size;
	while (tail + length >= size)
	    GROW(size);
	w = resize(queue->array,PyObject*,size);
	if (w == NULL) {
	    PyErr_NoMemory();
	    goto onError;
	}
	queue->array = w;
	queue->size = size;
    }

    /* Push the entries from left to right onto the queue */
    for (i = 0; i < length; i++) {
	register PyObject *v;
	
	if (PyTuple_Check(seq)) {
	    v = PyTuple_GET_ITEM(seq,i);
	    Py_INCREF(v);
	}
	else if (PyList_Check(seq)) {
	    v = PyList_GET_ITEM(seq,i);
	    Py_INCREF(v);
	}
	else {
	    v = PySequence_GetItem(seq,i);
	    if (!v) {
		/* Rollback */
		while (i > 0) {
		    Py_DECREF(queue->array[tail--]);
		    i--;
		}
		queue->tail = tail;
		goto onError;
	    }
	}
	queue->array[++tail] = v;
    }
    queue->tail = tail;
    return 0;

 onError:
    return -1;
}
#endif

static
PyObject *mxQueue_Pop(register mxQueueObject *queue)
{
    PyObject *v;
    Py_ssize_t head = queue->head;
    
    if (queue == NULL) {
	PyErr_BadInternalCall();
	goto onError;
    }
    Py_Assert(head != queue->tail,
	      mxQueue_EmptyError,
	      "queue is empty");

    head--;
    if (head < 0)
	head += queue->size;
    v = queue->array[head];
    queue->head = head;
    return v;

 onError:
    return NULL;
}

#if 0
/* Pop up to n entries into a Python tuple from the tail of the queue;
   order is tail to bottom. */

static
PyObject *mxQueue_PopMany(register mxQueueObject *queue,
			  Py_ssize_t n)
{
    PyObject *t;
    register Py_ssize_t i;
    
    if (queue == NULL) {
	PyErr_BadInternalCall();
	goto onError;
    }
    n = min(queue->tail + 1, n);
    t = PyTuple_New(n);
    if (!t)
	goto onError;
    for (i = 0; i < n; i++)
	PyTuple_SET_ITEM(t,i,queue->array[queue->tail--]);
    return t;
 onError:
    return NULL;
}
#endif

static
int mxQueue_Clear(register mxQueueObject *queue)
{
    register Py_ssize_t i;
    Py_ssize_t size = queue->size,
	head = queue->head;
	
    if (head != queue->tail)
	for (i = queue->tail;; i = (i+1) % size) {
	    Py_DECREF(queue->array[i]);
	    if (i == head)
		break;
	}
    queue->tail = queue->head = queue->size - 1;
    return 0;
}

#if 0
static
mxQueueObject *mxQueue_FromSequence(PyObject *seq)
{
    mxQueueObject *queue;
    
    /* Create an "empty" queue */
    queue = mxQueue_New(0);
    if (queue == NULL)
	return NULL;

    /* Insert items */
    if (mxQueue_PushMany(queue,seq))
	goto onError;

    return queue;

 onError:
    Py_DECREF(queue);
    return NULL;
}

static
PyObject *mxQueue_AsTuple(register mxQueueObject *queue)
{
    PyObject *t = 0;
    Py_ssize_t i,len;
    
    if (queue == NULL) {
	PyErr_BadInternalCall();
	goto onError;
    }
    
    len = queue->tail + 1;
    t = PyTuple_New(len);
    if (t == NULL)
	goto onError;
    for (i = 0; i < len; i++) {
	PyObject *v;
	v = queue->array[i];
	Py_INCREF(v);
	PyTuple_SET_ITEM(t,i,v);
    }
    return t;
 onError:
    if (t) {
	Py_DECREF(t);
    }
    return NULL;
}

static
PyObject *mxQueue_AsList(register mxQueueObject *queue)
{
    PyObject *l = 0;
    Py_ssize_t i,len;
    
    if (queue == NULL) {
	PyErr_BadInternalCall();
	goto onError;
    }
    
    len = queue->tail + 1;
    l = PyList_New(len);
    if (l == NULL)
	goto onError;
    for (i = 0; i < len; i++) {
	PyObject *v;
	v = queue->array[i];
	Py_INCREF(v);
	PyList_SET_ITEM(l,i,v);
    }
    return l;
 onError:
    if (l) {
	Py_DECREF(l);
    }
    return NULL;
}
#endif

#if 0
static
int mxQueue_Resize(register mxQueueObject *self,
		   Py_ssize_t size)
{
    register PyObject **w;

    if (self == NULL) {
	PyErr_BadInternalCall();
	goto onError;
    }
    
    if (size < self->tail)
	size = self->tail + 1;
    if (size < MINIMAL_QUEUESIZE)
	size = MINIMAL_QUEUESIZE;
    
    GROW(size);
    w = resize(self->array,PyObject*,size);
    if (w == NULL) {
	PyErr_NoMemory();
	goto onError;
    }
    self->array = w;
    self->size = size;

    return 0;

 onError:
    return -1;
}
#endif

/* --- methods --- (should have lowercase extension) */

#define queue ((mxQueueObject*)self)

Py_C_Function( mxQueue_new,
	       "Queue([intialsize])")
{
    PyObject *v;
    Py_ssize_t size = 0;

    Py_GetArg("|"Py_SSIZE_T_PARSERMARKER, size);

    v = (PyObject *)mxQueue_New(size);
    if (v == NULL)
	goto onError;
    return v;

 onError:
    return NULL;
}

Py_C_Function( mxQueue_push,
	       "push(o)")
{
    PyObject *v;

    Py_GetArgObject(v);
    
    if (mxQueue_Push(queue,v))
	goto onError;
    Py_ReturnNone();

 onError:
    return NULL;
}

#if 0
Py_C_Function( mxQueue_push_many,
	       "push_many(sequence)")
{
    PyObject *seq;

    Py_GetSingleArg("O",seq);
    
    if (mxQueue_PushMany(queue,seq))
	goto onError;
    Py_ReturnNone();

 onError:
    return NULL;
}
#endif

Py_C_Function( mxQueue_pop,
	       "pop()")
{
    register PyObject *v;

    Py_NoArgsCheck();
    
    v = mxQueue_Pop(queue);
    if (v == NULL)
	goto onError;
    return v;

 onError:
    return NULL;
}

#if 0
Py_C_Function( mxQueue_pop_many,
	       "pop_many(n)")
{
    register PyObject *v;
    Py_ssize_t n;

    Py_GetSingleArg(Py_SSIZE_T_PARSERMARKER, n);
    
    v = mxQueue_PopMany(queue,n);
    if (v == NULL)
	goto onError;
    return v;

 onError:
    return NULL;
}
#endif

Py_C_Function( mxQueue_clear,
	       "clear()")
{
    Py_NoArgsCheck();
    
    if (mxQueue_Clear(queue))
	goto onError;
    Py_ReturnNone();

 onError:
    return NULL;
}

#if 0
Py_C_Function( mxQueue_as_tuple,
	       "as_tuple()")
{
    register PyObject *v;

    Py_NoArgsCheck();
    
    v = mxQueue_AsTuple(queue);
    if (v == NULL)
	goto onError;
    return v;

 onError:
    return NULL;
}

Py_C_Function( mxQueue_as_list,
	       "as_list()")
{
    register PyObject *v;

    Py_NoArgsCheck();
    
    v = mxQueue_AsList(queue);
    if (v == NULL)
	goto onError;
    return v;

 onError:
    return NULL;
}

Py_C_Function( mxQueue_resize,
	       "resize([size=len(queue)])")
{
    Py_ssize_t size = mxQueue_Length(queue);

    Py_GetArg("|"Py_SSIZE_T_PARSERMARKER,size);
    
    if (mxQueue_Resize(queue,size))
	goto onError;

    Py_ReturnNone();

 onError:
    return NULL;
}
#endif

#undef queue

/* --- slots --- */

static
int mxQueue_Print(PyObject *obj,
		  FILE *fp, 
		  int flags)
{
    mxQueueObject *self = (mxQueueObject *)obj;
    Py_ssize_t i,
	head = self->head,
	tail = self->tail,
	size = self->size;
    
    fprintf(fp, "Queue[");
#if 1
    for (i = tail; i != head; i = (i+1) % size) {
	if (i != tail)
	    fprintf(fp, ", ");
	if (PyObject_Print(self->array[i], fp, flags))
	    goto onError;
    }
#else
    /* To ease debugging... */
    for (i = 0; i < size; i++) {
	if (i > 0)
	    fprintf(fp, ", ");
	fprintf(fp, "0x%lx", (long)self->array[i]);
    }
#endif
    fprintf(fp, "]");
    return 0;
 onError:
    return -1;
}

static
PyObject *mxQueue_Repr(PyObject *obj)
{
    mxQueueObject *self = (mxQueueObject *)obj;
    char s[256];

#if 1
    sprintf(s,"<Queue object at %lx>", (long)self);
#else
    /* To ease debugging... */
    sprintf(s,"<Queue object at %lx, head=%i, tail=%i, size=%i, len=%i>",
	    (long)self,
	    self->head,self->tail,self->size,mxQueue_Length(self));
#endif
    return PyString_FromString(s);
}

static
int mxQueue_Compare(PyObject *left,
		    PyObject *right)
{
    mxQueueObject *v = (mxQueueObject *)left;
    mxQueueObject *w = (mxQueueObject *)right;
    Py_ssize_t v_len = _mxQueue_Length(v);
    Py_ssize_t w_len = _mxQueue_Length(w);
    Py_ssize_t len = min(v_len,w_len);
    Py_ssize_t i,j,k;

    for (i = v->tail, j = w->tail, k = 0; 
	 k < len; 
	 k++, i = (i+1) % v->size, j = (j+1) % w->size) {
	int cmp = PyObject_Compare(v->array[i],w->array[j]);
	if (cmp != 0)
	    return cmp;
    }
    if (v_len < w_len)
	return -1;
    else if (v_len == w_len)
	return 0;
    else
	return 1;
}

static
PyObject *mxQueue_Getattr(PyObject *obj,
			  char *name)
{
    return Py_FindMethod(mxQueue_Methods, obj, name);
}

static
int mxQueue_NonZero(PyObject *obj)
{
    mxQueueObject *self = (mxQueueObject *)obj;

    return self->head != self->tail;
}

/* Undocumented feature:
      queue << x	does queue.push(x) and returns queue
      queue >> 1	return queue.pop()
      queue >> n	returns queue.pop_many(n)

   To make this work, we need a few hacks... :-(

   We make anything coerce and then check the arguments to the number
   slot functions. Unfortunately this only works if the left hand
   argument is a Queue. If the left hand argument is something else,
   you better press your thumbs and duck cover... (Na, it's not that
   dangerous, but the results are pretty much undefined.)
*/

static 
int mxQueue_Coerce(PyObject **pv,
		   PyObject **pw)
{
    if (_mxQueue_Check(*pv)) {
	/* Anything goes... */
	Py_INCREF(*pv);
	Py_INCREF(*pw);
	DPRINTF("Queue coerced ok\n");
	return 0;
    }
    DPRINTF("Queue coerce failed\n");
    return 1;
}

static
PyObject *mxQueue_LeftShift(PyObject *left,
			    PyObject *right)
{
    mxQueueObject *self = (mxQueueObject *)left;

    /* Queue << Object -> (Queue.push(Object),Queue) */
    mxQueue_Push(self, right);
    Py_INCREF(self);
    return left;
}

static
PyObject *mxQueue_RightShift(PyObject *left,
			     PyObject *right)
{
    mxQueueObject *self = (mxQueueObject *)left;
    long n;
    
    /* Queue >> Integer -> Queue.pop_many(Integer) */
    if (!PyInt_Check(right))
	Py_Error(PyExc_TypeError,
		 "right side of >> must an integer");
    n = PyInt_AS_LONG(right);

    /* XXX Doesn't work yet... */
    n = 1;
#if 0    
    Py_Assert(n > 0,
	      PyExc_TypeError,
	      "you can only pop 1 or more entries");
    if (n == 1)
	return mxQueue_Pop(self);
    else
	return mxQueue_PopMany(self, n);
#else
    return mxQueue_Pop(self);
#endif

 onError:
    return NULL;
}

/* Python Type Tables */

static
PySequenceMethods mxQueue_TypeAsSequence = {
    mxQueue_Length,			/*sq_length*/
    0,					/*sq_concat*/
    0,					/*sq_repeat*/
    0,					/*sq_item*/
    0,					/*sq_slice*/
    0,					/*sq_ass_item*/
    0,					/*sq_ass_slice*/
};

static
PyNumberMethods mxQueue_TypeAsNumber = {

    /* These slots are not NULL-checked, so we must provide dummy functions */
    notimplemented2,			/*nb_add*/
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
    mxQueue_NonZero,			/*nb_nonzero*/
    0,					/*nb_invert*/
    mxQueue_LeftShift,			/*nb_lshift*/
    mxQueue_RightShift,			/*nb_rshift*/
    notimplemented2,			/*nb_and*/
    notimplemented2,			/*nb_xor*/
    notimplemented2,			/*nb_or*/
    mxQueue_Coerce,			/*nb_coerce*/
    0,					/*nb_int*/
    0,					/*nb_long*/
    0,					/*nb_float*/
    0,					/*nb_oct*/
    0,					/*nb_hex*/
};

statichere
PyTypeObject mxQueue_Type = {
    PyObject_HEAD_INIT(0)		/* init at startup ! */
    0,			  		/*ob_size*/
    "Queue",	  			/*tp_name*/
    sizeof(mxQueueObject),      	/*tp_basicsize*/
    0,			  		/*tp_itemsize*/
    /* slots */
    (destructor)mxQueue_Free,		/*tp_dealloc*/
    mxQueue_Print,	  	       	/*tp_print*/
    mxQueue_Getattr,  			/*tp_getattr*/
    0,		  			/*tp_setattr*/
    mxQueue_Compare,  		       	/*tp_compare*/
    mxQueue_Repr,	  	       	/*tp_repr*/
    &mxQueue_TypeAsNumber, 		/*tp_as_number*/
    &mxQueue_TypeAsSequence,		/*tp_as_sequence*/
    0,					/*tp_as_mapping*/
    0,					/*tp_hash*/
    0,					/*tp_call*/
    0,					/*tp_str*/
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
    mxQueue_Methods,			/* tp_methods */
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
PyMethodDef mxQueue_Methods[] =
{   
    Py_MethodListEntrySingleArg("push",mxQueue_push),
    Py_MethodListEntryNoArgs("pop",mxQueue_pop),
    Py_MethodListEntryNoArgs("clear",mxQueue_clear),
#if 0
    Py_MethodListEntrySingleArg("push_many",mxQueue_push_many),
    Py_MethodListEntrySingleArg("pop_many",mxQueue_pop_many),
    Py_MethodListEntryNoArgs("as_tuple",mxQueue_as_tuple),
    Py_MethodListEntryNoArgs("as_list",mxQueue_as_list),
    Py_MethodListEntry("resize",mxQueue_resize),
#endif
    {NULL,NULL} /* end of list */
};

/* --- Other functions ----------------------------------------------------- */

#if 0
Py_C_Function( mxQueue_QueueFromSequence,
	       "QueueFromSequence(seq)")
{
    PyObject *v;
    PyObject *queue;

    Py_GetArg("O",v);

    Py_Assert(PySequence_Check(v),
	      PyExc_TypeError,
	      "argument must be a sequence");
    queue = (PyObject *)mxQueue_FromSequence(v);
    if (queue == NULL)
	goto onError;
    return queue;
 onError:
    return NULL;
}
#endif

/* --- module init --------------------------------------------------------- */

/* Python Method Table */

static 
PyMethodDef Module_methods[] =
{   
    Py_MethodListEntry("Queue",mxQueue_new),
#if 0
    Py_MethodListEntry("QueueFromSequence",mxQueue_QueueFromSequence),
#endif
    {NULL,NULL} /* end of list */
};

/* C API table */
static
mxQueueModule_APIObject mxQueueModuleAPI =
{
    &mxQueue_Type,
    mxQueue_New,
    mxQueue_Push,
    mxQueue_Pop,
    mxQueue_Clear,
    _mxQueue_Length
#if 0
    ,mxQueue_FromSequence,
    mxQueue_AsTuple,
    mxQueue_AsList,
    mxQueue_PopMany,
    mxQueue_PushMany
#endif
};

/* Cleanup function */
static 
void mxQueueModule_Cleanup(void)
{
    /* Reset mxQueue_Initialized flag */
    mxQueue_Initialized = 0;
}

/* create PyMethodObjects and register them in the module's dict */
MX_EXPORT(void) 
     initmxQueue(void)
{
    PyObject *module, *moddict, *api;

    if (mxQueue_Initialized)
	Py_Error(PyExc_SystemError,
		 "can't initialize "MXQUEUE_MODULE" more than once");

    /* Init type objects */
    PyType_Init(mxQueue_Type);

    /* create module */
    module = Py_InitModule4(MXQUEUE_MODULE, /* Module name */
			    Module_methods, /* Method list */
			    Module_docstring, /* Module doc-string */
			    (PyObject *)NULL, /* always pass this as *self */
			    PYTHON_API_VERSION); /* API Version */
    if (module == NULL)
	goto onError;

    /* Register cleanup function */
    if (Py_AtExit(mxQueueModule_Cleanup)) {
	/* XXX what to do if we can't register that function ??? */
	DPRINTF("* Failed to register mxQueue cleanup function\n");
    }

    /* Add some constants to the module's dict */
    moddict = PyModule_GetDict(module);
    PyDict_SetItemString(moddict, 
			 "__version__",
			 PyString_FromString(MXQUEUE_VERSION));

    /* Errors */
    if (!(mxQueue_Error = insexc(moddict,"Error",PyExc_IndexError)))
	goto onError;
    if (!(mxQueue_EmptyError = insexc(moddict,"EmptyError",mxQueue_Error)))
	goto onError;

    /* Type objects */
    Py_INCREF(&mxQueue_Type);
    PyDict_SetItemString(moddict, "QueueType",
			 (PyObject *)&mxQueue_Type);

    /* Export C API */
    api = PyCObject_FromVoidPtr((void *)&mxQueueModuleAPI, NULL);
    if (api == NULL)
	goto onError;
    PyDict_SetItemString(moddict,MXQUEUE_MODULE"API",api);
    Py_DECREF(api);

    /* We are now initialized */
    mxQueue_Initialized = 1;

 onError:
    /* Check for errors and report them */
    if (PyErr_Occurred())
	Py_ReportModuleInitError(MXQUEUE_MODULE);
    return;
}
