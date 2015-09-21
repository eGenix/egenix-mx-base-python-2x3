/* 
  mxStack -- A stack implementation

  Copyright (c) 1997-2000, Marc-Andre Lemburg; mailto:mal@lemburg.com
  Copyright (c) 2000-2015, eGenix.com Software GmbH; mailto:info@egenix.com
  See the documentation for further copyright information or contact
  the author (mailto:mal@lemburg.com).

*/

/* Define this to aid in finding memory leaks */
/*#define MAL_MEM_DEBUG*/
/*#define MAL_DEBUG*/

/* Logging file used by debugging facility */
#ifndef MAL_DEBUG_OUTPUTFILE
# define MAL_DEBUG_OUTPUTFILE "mxStack.log"
#endif

/* We want all our symbols to be exported */
#define MX_BUILDING_MXSTACK

/* Mark the module as Py_ssize_t clean. */
#define PY_SSIZE_T_CLEAN 1

/* Setup mxstdlib memory management */
#if 1
# define MAL_USE_PYMALLOC
#else
# define MAL_USE_C_MALLOC
#endif

#include "mx.h"
#include "mxStack.h"

#define MXSTACK_VERSION "3.2.9"

/* The minimal size of stacks; must be > 1 */
#define MINIMAL_STACKSIZE	4

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

 MXSTACK_MODULE" -- A stack implementation. Version "MXSTACK_VERSION"\n\n"

 "Copyright (c) 1997-2000, Marc-Andre Lemburg; mailto:mal@lemburg.com\n"
 "Copyright (c) 2000-2015, eGenix.com Software GmbH; mailto:info@egenix.com\n\n"
 "                 All Rights Reserved\n\n"
 "See the documentation for further information on copyrights,\n"
 "or contact the author."
;

/* --- module globals ----------------------------------------------------- */

static PyObject *mxStack_Error;			/* Error Exception
						   object */
static PyObject *mxStack_EmptyError;		/* EmptyError Exception
						   object */

/* Flag telling us whether the module was initialized or not. */
static int mxStack_Initialized = 0;

/* --- forward declarations ----------------------------------------------- */

staticforward PyTypeObject mxStack_Type;
staticforward PyMethodDef mxStack_Methods[];

/* --- internal macros ---------------------------------------------------- */

#define _mxStack_Check(v) \
        (((mxStackObject *)(v))->ob_type == &mxStack_Type)


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
	modname = MXSTACK_MODULE;
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

/* --- Stack Object -------------------------------------------------*/

/* --- allocation --- */

static
mxStackObject *mxStack_New(Py_ssize_t size)
{
    mxStackObject *stack;
    PyObject **w;

    stack = PyObject_NEW(mxStackObject,&mxStack_Type);
    if (stack == NULL)
	return NULL;

    /* Init vars */
    if (size < MINIMAL_STACKSIZE)
	size = MINIMAL_STACKSIZE;
    stack->array = NULL;
    w = new(PyObject*,size);
    if (w == NULL) {
	Py_DECREF(stack);
	PyErr_NoMemory();
	return NULL;
    }
    stack->array = w;
    stack->size = size;
    stack->top = -1;

    return stack;
}

/* --- deallocation --- */

static
void mxStack_Free(register mxStackObject *stack)
{
    if (stack->array) {
	Py_ssize_t i;
	
	for (i = 0; i <= stack->top; i++)
	    Py_DECREF(stack->array[i]);
	free(stack->array);
    }
    PyObject_Del(stack);
}

/* --- internal functions --- */

/* --- API functions --- */

static
int mxStack_Push(register mxStackObject *stack,
		 PyObject *v)
{
    Py_ssize_t top;
    
    if (stack == NULL) {
	PyErr_BadInternalCall();
	goto onError;
    }

    top = stack->top + 1;

    /* Grow the stack, if we hit the array boundary */
    if (top == stack->size) {
	register PyObject **w;
	register Py_ssize_t size;

	size = stack->size;
	GROW(size);
	w = resize(stack->array,PyObject*,size);
	if (w == NULL) {
	    PyErr_NoMemory();
	    goto onError;
	}
	stack->array = w;
	stack->size = size;
    }
    
    Py_INCREF(v);
    stack->array[top] = v;
    stack->top = top;
    return 0;
 onError:
    return -1;
}

static
int mxStack_PushMany(register mxStackObject *stack,
		     PyObject *seq)
{
    Py_ssize_t top;
    register Py_ssize_t i;
    Py_ssize_t length;

    if (stack == NULL) {
	PyErr_BadInternalCall();
	goto onError;
    }

    length = PySequence_Length(seq);
    if (length < 0)
	goto onError;

    top = stack->top;

    /* Grow the stack, if we hit the array boundary */
    if (top + length >= stack->size) {
	register PyObject **w;
	register Py_ssize_t size;

	size = stack->size;
	while (top + length >= size)
	    GROW(size);
	w = resize(stack->array,PyObject*,size);
	if (w == NULL) {
	    PyErr_NoMemory();
	    goto onError;
	}
	stack->array = w;
	stack->size = size;
    }

    /* Push the entries from left to right onto the stack */
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
		    Py_DECREF(stack->array[top]);
		    top--;
		    i--;
		}
		stack->top = top;
		goto onError;
	    }
	}
	stack->array[++top] = v;
    }
    stack->top = top;
    return 0;

 onError:
    return -1;
}

static
PyObject *mxStack_Pop(register mxStackObject *stack)
{
    if (stack == NULL) {
	PyErr_BadInternalCall();
	goto onError;
    }
    Py_Assert(stack->top >= 0,
	      mxStack_EmptyError,
	      "stack is empty");
    return stack->array[stack->top--];
 onError:
    return NULL;
}

/* Pop up to n entries into a Python tuple from the top of the stack;
   order is top to bottom. */

static
PyObject *mxStack_PopMany(register mxStackObject *stack,
			  Py_ssize_t n)
{
    PyObject *t;
    register Py_ssize_t i;
    
    if (stack == NULL) {
	PyErr_BadInternalCall();
	goto onError;
    }
    n = min(stack->top + 1, n);
    t = PyTuple_New(n);
    if (!t)
	goto onError;
    for (i = 0; i < n; i++)
	PyTuple_SET_ITEM(t,i,stack->array[stack->top--]);
    return t;
 onError:
    return NULL;
}

static
int mxStack_Clear(register mxStackObject *stack)
{
    register Py_ssize_t i;
	
    for (i = 0; i <= stack->top; i++)
	Py_DECREF(stack->array[i]);
    stack->top = -1;
    return 0;
}

static
mxStackObject *mxStack_FromSequence(PyObject *seq)
{
    mxStackObject *stack = 0;
    
    /* Create an "empty" stack */
    stack = mxStack_New(0);
    if (stack == NULL)
	return NULL;

    /* Insert items */
    if (mxStack_PushMany(stack,seq))
	goto onError;

    return stack;

 onError:
    Py_DECREF(stack);
    return NULL;
}

static
PyObject *mxStack_AsTuple(register mxStackObject *stack)
{
    PyObject *t = 0;
    Py_ssize_t i,len;
    
    if (stack == NULL) {
	PyErr_BadInternalCall();
	goto onError;
    }
    
    len = stack->top + 1;
    t = PyTuple_New(len);
    if (t == NULL)
	goto onError;
    for (i = 0; i < len; i++) {
	PyObject *v;
	v = stack->array[i];
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
PyObject *mxStack_AsList(register mxStackObject *stack)
{
    PyObject *l = 0;
    Py_ssize_t i,len;
    
    if (stack == NULL) {
	PyErr_BadInternalCall();
	goto onError;
    }
    
    len = stack->top + 1;
    l = PyList_New(len);
    if (l == NULL)
	goto onError;
    for (i = 0; i < len; i++) {
	PyObject *v;
	v = stack->array[i];
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

static
Py_ssize_t _mxStack_Length(register mxStackObject *self)
{
    return self->top + 1;
}

static
Py_ssize_t mxStack_Length(register PyObject *self)
{
    return _mxStack_Length((mxStackObject *)self);
}

static
int mxStack_Resize(register mxStackObject *self,
		   Py_ssize_t size)
{
    register PyObject **w;

    if (self == NULL) {
	PyErr_BadInternalCall();
	goto onError;
    }
    
    if (size < self->top)
	size = self->top + 1;
    if (size < MINIMAL_STACKSIZE)
	size = MINIMAL_STACKSIZE;
    
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

static
PyObject *_mxStack_GetItem(register mxStackObject *self,
			   Py_ssize_t index)
{
    PyObject *v;
    Py_ssize_t len = self->top + 1;

    if (index < 0)
	index += len;
    Py_Assert((index >= 0) && (index < len),
	      PyExc_IndexError,
	      "index out of range");
    v = self->array[index];
    Py_INCREF(v);
    return v;

 onError:
    return NULL;
}

static
PyObject *mxStack_GetItem(register PyObject *obj,
			  Py_ssize_t index)
{
    return _mxStack_GetItem((mxStackObject *)obj, index);
}


/* --- methods --- (should have lowercase extension) */

#define stack ((mxStackObject*)self)

Py_C_Function( mxStack_new,
	       "Stack([intialsize])")
{
    PyObject *v;
    Py_ssize_t size = 0;

    Py_GetArg("|"Py_SSIZE_T_PARSERMARKER, size);

    v = (PyObject *)mxStack_New(size);
    if (v == NULL)
	goto onError;
    return v;

 onError:
    return NULL;
}

Py_C_Function( mxStack_push,
	       "push(o)")
{
    PyObject *v;

    Py_GetArgObject(v);
    
    if (mxStack_Push(stack,v))
	goto onError;
    Py_ReturnNone();

 onError:
    return NULL;
}

Py_C_Function( mxStack_push_many,
	       "push_many(sequence)")
{
    PyObject *seq;

    Py_GetSingleArg("O",seq);
    
    if (mxStack_PushMany(stack,seq))
	goto onError;
    Py_ReturnNone();

 onError:
    return NULL;
}

Py_C_Function( mxStack_pop,
	       "pop()")
{
    register PyObject *v;

    Py_NoArgsCheck();
    
    v = mxStack_Pop(stack);
    if (v == NULL)
	goto onError;
    return v;

 onError:
    return NULL;
}

Py_C_Function( mxStack_pop_many,
	       "pop_many(n)")
{
    register PyObject *v;
    Py_ssize_t n;

    Py_GetSingleArg(Py_SSIZE_T_PARSERMARKER, n);
    
    v = mxStack_PopMany(stack,n);
    if (v == NULL)
	goto onError;
    return v;

 onError:
    return NULL;
}

Py_C_Function( mxStack_clear,
	       "clear()")
{
    Py_NoArgsCheck();
    
    if (mxStack_Clear(stack))
	goto onError;
    Py_ReturnNone();

 onError:
    return NULL;
}

Py_C_Function( mxStack_as_tuple,
	       "as_tuple()")
{
    register PyObject *v;

    Py_NoArgsCheck();
    
    v = mxStack_AsTuple(stack);
    if (v == NULL)
	goto onError;
    return v;

 onError:
    return NULL;
}

Py_C_Function( mxStack_as_list,
	       "as_list()")
{
    register PyObject *v;

    Py_NoArgsCheck();
    
    v = mxStack_AsList(stack);
    if (v == NULL)
	goto onError;
    return v;

 onError:
    return NULL;
}

Py_C_Function( mxStack_resize,
	       "resize([size=len(stack)])")
{
    Py_ssize_t size = _mxStack_Length(stack);

    Py_GetArg("|"Py_SSIZE_T_PARSERMARKER, size);
    
    if (mxStack_Resize(stack,size))
	goto onError;

    Py_ReturnNone();

 onError:
    return NULL;
}

#undef stack

/* --- slots --- */

static
int mxStack_Print(PyObject *obj,
		  FILE *fp, 
		  int flags)
{
    mxStackObject *self = (mxStackObject *)obj;
    Py_ssize_t i,top;
    
    top = self->top;
    fprintf(fp, "Stack[");
    for (i = 0; i <= top; i++) {
	if (i > 0)
	    fprintf(fp, ", ");
	if (PyObject_Print(self->array[i], fp, flags))
	    goto onError;
    }
    fprintf(fp, "]");
    return 0;
 onError:
    return -1;
}

static
PyObject *mxStack_Repr(PyObject *obj)
{
    mxStackObject *self = (mxStackObject *)obj;
    char s[256];

    sprintf(s,"<Stack object at %lx>",(long)self);
    return PyString_FromString(s);
}

static
int mxStack_Compare(PyObject *left,
		    PyObject *right)
{
    mxStackObject *v = (mxStackObject *)left;
    mxStackObject *w = (mxStackObject *)right;
    Py_ssize_t len = min(v->top,w->top) + 1;
    Py_ssize_t i;

    for (i = 0; i < len; i++) {
	int cmp = PyObject_Compare(v->array[i],w->array[i]);
	if (cmp != 0)
	    return cmp;
	}
    if (v->top < w->top)
	return -1;
    else if (v->top == w->top)
	return 0;
    else
	return 1;
}

static
PyObject *mxStack_Getattr(PyObject *obj,
			  char *name)
{
    return Py_FindMethod(mxStack_Methods, obj, name);
}

static
int mxStack_NonZero(PyObject *obj)
{
    mxStackObject *self = (mxStackObject *)obj;

    return self->top >= 0;
}

/* Undocumented feature:
      stack << x	does stack.push(x) and returns stack
      stack >> 1	return stack.pop()
      stack >> n	returns stack.pop_many(n)

   To make this work, we need a few hacks... :-(

   We make anything coerce and then check the arguments to the number
   slot functions. Unfortunately this only works if the left hand
   argument is a Stack. If the left hand argument is something else,
   you better press your thumbs and duck cover... (Na, it's not that
   dangerous, but the results are pretty much undefined.)

*/

static 
int mxStack_Coerce(PyObject **pv,
		   PyObject **pw)
{
    if (_mxStack_Check(*pv)) {
	/* Anything goes... */
	Py_INCREF(*pv);
	Py_INCREF(*pw);
	DPRINTF("Stack coerced ok\n");
	return 0;
    }
    DPRINTF("Stack coerce failed\n");
    return 1;
}

static
PyObject *mxStack_LeftShift(PyObject *left,
			    PyObject *right)
{
    mxStackObject *self = (mxStackObject *)left;

    /* Stack << Object -> (Stack.push(Object),Stack) */
    if (!_mxStack_Check(left)) {
	PyErr_BadInternalCall();
	return NULL;
    }
    mxStack_Push(self, right);
    Py_INCREF(self);
    return left;
}

static
PyObject *mxStack_RightShift(PyObject *left,
			     PyObject *right)
{
    mxStackObject *self = (mxStackObject *)left;
    long n;
    
    /* Stack >> Integer -> Stack.pop_many(Integer) */
    if (!_mxStack_Check(left)) {
	PyErr_BadInternalCall();
	return NULL;
    }
    if (!PyInt_Check(right))
	Py_Error(PyExc_TypeError,
		 "right side of >> must an integer");
    n = PyInt_AS_LONG(right);
    Py_Assert(n > 0,
	      PyExc_TypeError,
	      "you can only pop 1 or more entries");
    if (n == 1)
	return mxStack_Pop(self);
    else
	return mxStack_PopMany(self, n);
 onError:
    return NULL;
}

/* Python Type Tables */

static
PySequenceMethods mxStack_TypeAsSequence = {
    mxStack_Length,			/*sq_length*/
    0,					/*sq_concat*/
    0,					/*sq_repeat*/
    mxStack_GetItem,			/*sq_item*/
    0,					/*sq_slice*/
    0,					/*sq_ass_item*/
    0,					/*sq_ass_slice*/
};

static
PyNumberMethods mxStack_TypeAsNumber = {

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
    mxStack_NonZero,			/*nb_nonzero*/
    0,					/*nb_invert*/
    mxStack_LeftShift,			/*nb_lshift*/
    mxStack_RightShift,			/*nb_rshift*/
    notimplemented2,			/*nb_and*/
    notimplemented2,			/*nb_xor*/
    notimplemented2,			/*nb_or*/
    mxStack_Coerce,			/*nb_coerce*/
    0,					/*nb_int*/
    0,					/*nb_long*/
    0,					/*nb_float*/
    0,					/*nb_oct*/
    0,					/*nb_hex*/
};

statichere
PyTypeObject mxStack_Type = {
    PyObject_HEAD_INIT(0)		/* init at startup ! */
    0,			  		/*ob_size*/
    "Stack",	  			/*tp_name*/
    sizeof(mxStackObject),      	/*tp_basicsize*/
    0,			  		/*tp_itemsize*/
    /* slots */
    (destructor)mxStack_Free,		/*tp_dealloc*/
    mxStack_Print,	  		/*tp_print*/
    mxStack_Getattr,  			/*tp_getattr*/
    0,		  			/*tp_setattr*/
    mxStack_Compare,  			/*tp_compare*/
    mxStack_Repr,	  		/*tp_repr*/
    &mxStack_TypeAsNumber, 		/*tp_as_number*/
    &mxStack_TypeAsSequence,		/*tp_as_sequence*/
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
    mxStack_Methods,			/* tp_methods */
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
PyMethodDef mxStack_Methods[] =
{   
    Py_MethodListEntrySingleArg("push",mxStack_push),
    Py_MethodListEntryNoArgs("pop",mxStack_pop),
    Py_MethodListEntrySingleArg("push_many",mxStack_push_many),
    Py_MethodListEntrySingleArg("pop_many",mxStack_pop_many),
    Py_MethodListEntryNoArgs("as_tuple",mxStack_as_tuple),
    Py_MethodListEntryNoArgs("as_list",mxStack_as_list),
    Py_MethodListEntryNoArgs("clear",mxStack_clear),
    Py_MethodListEntry("resize",mxStack_resize),
    {NULL,NULL} /* end of list */
};

/* --- Other functions ----------------------------------------------------- */

Py_C_Function( mxStack_StackFromSequence,
	       "StackFromSequence(seq)")
{
    PyObject *v;
    PyObject *stack;

    Py_GetArg("O",v);

    Py_Assert(PySequence_Check(v),
	      PyExc_TypeError,
	      "argument must be a sequence");
    stack = (PyObject *)mxStack_FromSequence(v);
    if (stack == NULL)
	goto onError;
    return stack;
 onError:
    return NULL;
}

/* --- module init --------------------------------------------------------- */

/* Python Method Table */

static 
PyMethodDef Module_methods[] =
{   
    Py_MethodListEntry("Stack",mxStack_new),
    Py_MethodListEntry("StackFromSequence",mxStack_StackFromSequence),
    {NULL,NULL} /* end of list */
};

/* Cleanup function */
static 
void mxStackModule_Cleanup(void)
{
    /* Reset mxStack_Initialized flag */
    mxStack_Initialized = 0;
}

/* C API table */
static
mxStackModule_APIObject mxStackModuleAPI =
{
    &mxStack_Type,
    mxStack_New,
    mxStack_FromSequence,
    mxStack_Push,
    mxStack_Pop,
    mxStack_AsTuple,
    mxStack_AsList,
    mxStack_PopMany,
    mxStack_PushMany,
    mxStack_Clear,
    _mxStack_Length,
    _mxStack_GetItem
};

/* create PyMethodObjects and register them in the module's dict */
MX_EXPORT(void) 
     initmxStack(void)
{
    PyObject *module, *moddict, *api;

    if (mxStack_Initialized)
	Py_Error(PyExc_SystemError,
		 "can't initialize "MXSTACK_MODULE" more than once");

    /* Init type objects */
    PyType_Init(mxStack_Type);

    /* create module */
    module = Py_InitModule4(MXSTACK_MODULE, /* Module name */
			    Module_methods, /* Method list */
			    Module_docstring, /* Module doc-string */
			    (PyObject *)NULL, /* always pass this as *self */
			    PYTHON_API_VERSION); /* API Version */
    if (module == NULL)
	goto onError;

    /* Register cleanup function */
    if (Py_AtExit(mxStackModule_Cleanup)) {
	/* XXX what to do if we can't register that function ??? */
	DPRINTF("* Failed to register mxStack cleanup function\n");
    }

    /* Add some constants to the module's dict */
    moddict = PyModule_GetDict(module);
    PyDict_SetItemString(moddict, 
			 "__version__",
			 PyString_FromString(MXSTACK_VERSION));

    /* Errors */
    if (!(mxStack_Error = insexc(moddict,"Error",PyExc_IndexError)))
	goto onError;
    if (!(mxStack_EmptyError = insexc(moddict,"EmptyError",mxStack_Error)))
	goto onError;

    /* Type objects */
    Py_INCREF(&mxStack_Type);
    PyDict_SetItemString(moddict, "StackType",
			 (PyObject *)&mxStack_Type);

    /* Export C API */
    api = PyCObject_FromVoidPtr((void *)&mxStackModuleAPI, NULL);
    if (api == NULL)
	goto onError;
    PyDict_SetItemString(moddict,MXSTACK_MODULE"API",api);
    Py_DECREF(api);

    /* We are now initialized */
    mxStack_Initialized = 1;

 onError:
    /* Check for errors and report them */
    if (PyErr_Occurred())
	Py_ReportModuleInitError(MXSTACK_MODULE);
    return;
}
