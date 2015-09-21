/* 
  mxte_impl -- A table driven Tagging Engine for Python (Version 0.9)

  This is the Tagging Engine implementation. It can be compiled for
  8-bit strings and Unicode by setting the TE_* defines appropriately.

  Copyright (c) 2000, Marc-Andre Lemburg; mailto:mal@lemburg.com
  Copyright (c) 2000-2015, eGenix.com Software GmbH; mailto:info@egenix.com
*/


#ifndef TE_STRING_CHECK 
# define TE_STRING_CHECK(obj) PyString_Check(obj)
#endif
#ifndef TE_STRING_AS_STRING
# define TE_STRING_AS_STRING(obj) PyString_AS_STRING(obj)
#endif
#ifndef TE_STRING_GET_SIZE
# define TE_STRING_GET_SIZE(obj) PyString_GET_SIZE(obj)
#endif
#ifndef TE_STRING_FROM_STRING
# define TE_STRING_FROM_STRING(str, size) PyString_FromStringAndSize(str, size)
#endif
#ifndef TE_CHAR
# define TE_CHAR char
#endif
#ifndef TE_HANDLE_MATCH
# define TE_HANDLE_MATCH string_match_append
#endif
#ifndef TE_ENGINE_API
# define TE_ENGINE_API mxTextTools_TaggingEngine
#endif


/* --- Tagging Engine ----------------------------------------------------- */

/* Forwards */
staticforward
int TE_HANDLE_MATCH(int flags,
		    PyObject *textobj,
		    PyObject *taglist,
		    PyObject *tagobj,
		    Py_ssize_t match_left,
		    Py_ssize_t match_right,
		    PyObject *subtags,
		    PyObject *context);

/* mxTextTools_TaggingEngine(): a table driven parser engine

   Parameters:
    textobj          - text object to work on
    text_start       - left text slice index
    text_stop        - right text slice index
    table            - tag table object defining the parser
    taglist          - tag list to append matches to
    context          - optional context object; may be NULL
    *next            - output parameter: set to the next index in text
    level            - stack level; should be 0 on the first level
  
   Return codes:
    rc = 2: match ok
    rc = 1: match failed
    rc = 0: error

   Notes:
   - doesn't check type of passed arguments !
   - doesn't increment reference counts of passed objects !

*/

int TE_ENGINE_API(PyObject *textobj,
		  Py_ssize_t sliceleft,	
		  Py_ssize_t sliceright,	
		  mxTagTableObject *table,
		  PyObject *taglist,
		  PyObject *context,
		  Py_ssize_t *next,
                  int level)
{
    register Py_ssize_t x;		/* current (head) position in text */
    Py_ssize_t i = 0; 			/* index of current table entry */
    TE_CHAR *text;			/* Pointer to the parsed text data */
    Py_ssize_t start = sliceleft;	/* start position for current tag */
    Py_ssize_t table_len = table->ob_size; /* table length */
    int rc = -1;			/* return code: -1 not set, 0 error, 1
					   not ok, 2 ok */
    Py_ssize_t loopcount = -1; 		/* loop counter */
    Py_ssize_t loopstart = start;	/* loop start position */
    int flags;				/* flags set in command */
    int cmd;				/* command */
    Py_ssize_t jne;			/* rel. jump distance on 'not
					   matched' */
    Py_ssize_t je;			/* dito on 'matched' */
    PyObject *match;			/* matching parameter */

    /* Init */
    Py_AssertWithArg(TE_STRING_CHECK(textobj),
		     PyExc_TypeError,
		     "expected a string or unicode to parse: found %.50s",
		     textobj->ob_type->tp_name);
    text = TE_STRING_AS_STRING(textobj);
    x = start;

    Py_AssertWithArg(mxTagTable_Check(table),
		     PyExc_TypeError,
		     "expected a TagTable: found %.50s",
		     table->ob_type->tp_name);

    if (sliceleft == sliceright) {
	/* Matching on an empty string will always fail */
	rc = 1;
	goto finished;
    }
    Py_AssertWith2Args(sliceleft < sliceright,
		       PyExc_ValueError,
		       "invalid slice indexes: [%ld:%ld]",
		       (long)sliceleft, (long)sliceright);

    /* Protect against stack related segfaults */
    Py_AssertWithArg(level < Py_GetRecursionLimit(),
		     PyExc_RuntimeError,
		     "maximum recursion depth exceeded: %i",
		     level);

    /* Main loop */
    for (i = 0, je = 0;;) {
	mxTagTableEntry *entry;

    next_entry:
	/* Get next entry */
	i += je;
	if (i >= table_len || i < 0 || x > sliceright)
	    /* Out of bounds: we're finished */
	    goto finished;

	/* Load entry */
	entry = &table->entry[i];
	cmd = entry->cmd;
	flags = entry->flags;
	match = entry->args;
	jne = entry->jne;
	je = entry->je;

	IF_DEBUGGING {
	    PyObject *v;
	    
	    mxDebugPrintf("\n# Tag Table entry %ld: ",
			  (long)i);
	    v = PyObject_Repr(entry->tagobj);
	    if (!v) {
		v = entry->tagobj;
		Py_INCREF(v);
	    }
	    if (PyString_Check(v))
		mxDebugPrintf("tagobj=%s cmd=%i flags=%i position=%ld\n",
			      PyString_AsString(v),cmd,flags,(long)x);
	    else
		mxDebugPrintf("tagobj at 0x%lx cmd=%i flags=%i position=%ld\n",
			      (long)v,cmd,flags,(long)x);
	    Py_DECREF(v);
	}
	
	/* Low-level matching commands */

	if (cmd < MATCH_MAX_LOWLEVEL) {
	    TE_CHAR *m = TE_STRING_AS_STRING(match);

	    /* save starting position */
	    start = x;

	    /* Low-level commands never match the EOF */
	    if (x == sliceright)
		goto low_level_done;

	    /* Note that the Tag Table Compiler assures that the match
	       string for all of these low-level commands (except
	       AllInCharSet and IsInCharSet) are non-empty. */

	    switch (cmd) {

	    case MATCH_ALLIN:

		{
		    register Py_ssize_t ml = TE_STRING_GET_SIZE(match);
		    register TE_CHAR *tx = &text[x];

		    DPRINTF("\nAllIn :\n"
			    " looking for   = '%.40s'\n"
			    " in string     = '%.40s'\n",m,tx);
	    
		    if (ml > 1)
			for (; x < sliceright; tx++, x++) {
			    register Py_ssize_t j;
			    register TE_CHAR *mj = m;
			    register TE_CHAR ctx = *tx;
			    for (j=0; j < ml && ctx != *mj; mj++, j++) ;
			    if (j == ml) 
				break;
			}
		    else
			/* one char only: use faster variant: */
			for (; x < sliceright && *tx == *m; tx++, x++) ;

		    break;
		}

	    case MATCH_ALLNOTIN:

		{
		    register Py_ssize_t ml = TE_STRING_GET_SIZE(match);
		    register TE_CHAR *tx = &text[x];

		    DPRINTF("\nAllNotIn :\n"
			    " looking for   = '%.40s'\n"
			    " not in string = '%.40s'\n",m,tx);
	    
		    if (ml > 1)
			for (; x < sliceright; tx++, x++) {
			    register Py_ssize_t j;
			    register TE_CHAR *mj = m;
			    register TE_CHAR ctx = *tx;
			    for (j=0; j < ml && ctx != *mj; mj++, j++) ;
			    if (j != ml) 
				break;
			}
		    else
			/* one char only: use faster variant: */
			for (; x < sliceright && *tx != *m; tx++, x++) ;
		    break;
		}

	    case MATCH_IS: 
		    
		{
		    DPRINTF("\nIs :\n"
			    " looking for   = '%.40s'\n"
			    " in string     = '%.40s'\n",m,text+x);
	    
		    if (*(&text[x]) == *m)
			x++;
		    break;
		}

	    case MATCH_ISNOT: 
		    
		{
		    DPRINTF("\nIsNot :\n"
			    " looking for   = '%.40s'\n"
			    " in string     = '%.40s'\n",m,text+x);
	    
		    if (*(&text[x]) != *m)
			x++;
		    break;
		}

	    case MATCH_ISIN:

		{
		    register Py_ssize_t ml = TE_STRING_GET_SIZE(match);
		    register TE_CHAR ctx = text[x];
		    register Py_ssize_t j;
		    register TE_CHAR *mj = m;

		    DPRINTF("\nIsIn :\n"
			    " looking for   = '%.40s'\n"
			    " in string     = '%.40s'\n",m,text+x);
	    
		    for (j=0; j < ml && ctx != *mj; mj++, j++) ;
		    if (j != ml) 
			x++;
		    break;
		}

	    case MATCH_ISNOTIN:

		{
		    register Py_ssize_t ml = TE_STRING_GET_SIZE(match);
		    register TE_CHAR ctx = text[x];
		    register Py_ssize_t j;
		    register TE_CHAR *mj = m;

		    DPRINTF("\nIsNotIn :\n"
			    " looking for   = '%.40s'\n"
			    " not in string = '%.40s'\n",m,text+x);
	    
		    for (j=0; j < ml && ctx != *mj; mj++, j++) ;
		    if (j == ml) 
			x++;
		    break;
		}

	    case MATCH_WORD:

		{
		    Py_ssize_t ml1 = TE_STRING_GET_SIZE(match) - 1;
		    register TE_CHAR *tx = &text[x + ml1];
		    register Py_ssize_t j = ml1;
		    register TE_CHAR *mj = &m[j];

		    DPRINTF("\nWord :\n"
			    " looking for   = '%.40s'\n"
			    " in string     = '%.40s'\n",m,&text[x]);
	    
		    if (x+ml1 >= sliceright) 
			break;
		    
		    /* compare from right to left */
		    for (; j >= 0 && *tx == *mj;
			 tx--, mj--, j--) ;

		    if (j >= 0) /* not matched */
			x = start; /* reset */
		    else
			x += ml1 + 1;
		    break;
		}

	    case MATCH_WORDSTART:
	    case MATCH_WORDEND:

		{
		    Py_ssize_t ml1 = TE_STRING_GET_SIZE(match) - 1;
		    register TE_CHAR *tx = &text[x];
			    
		    DPRINTF("\nWordStart/End :\n"
			    " looking for   = '%.40s'\n"
			    " in string     = '%.40s'\n",m,tx);

		    /* Brute-force method; from right to left */
		    for (;;) {
			register Py_ssize_t j = ml1;
			register TE_CHAR *mj = &m[j];

			if (x+j >= sliceright) {
			    /* reached eof: no match, rewind */
			    x = start;
			    break;
			}

			/* scan from right to left */
			for (tx += j; j >= 0 && *tx == *mj; 
			     tx--, mj--, j--) ;
			/*
			DPRINTF("match text[%ld+%ld]: %c == %c\n",
				x,j,*tx,*mj);
			*/

			if (j < 0) {
			    /* found */
			    if (cmd == MATCH_WORDEND) 
				x += ml1 + 1;
			    break;
			}
			/* not found: rewind and advance one char */
			tx -= j - 1;
			x++;
		    }
		    break;
		}

#if (TE_TABLETYPE == MXTAGTABLE_STRINGTYPE)

	    /* Note: These two only work for 8-bit set strings. */
	    case MATCH_ALLINSET:

		{
		    register TE_CHAR *tx = &text[x];
		    unsigned char *m = (unsigned char *)PyString_AS_STRING(match);

		    DPRINTF("\nAllInSet :\n"
			    " looking for   = set at 0x%lx\n"
			    " in string     = '%.40s'\n",(long)match,tx);
	    
		    for (;
			 x < sliceright &&
			 (m[((unsigned char)*tx) >> 3] & 
			  (1 << (*tx & 7))) > 0;
			 tx++, x++) ;
		
		    break;
		}

	    case MATCH_ISINSET:

		{
		    register TE_CHAR *tx = &text[x];
		    unsigned char *m = (unsigned char *)PyString_AS_STRING(match);

		    DPRINTF("\nIsInSet :\n"
			    " looking for   = set at 0x%lx\n"
			    " in string     = '%.40s'\n",(long)match,tx);
	    
		    if (x < sliceright &&
			(m[((unsigned char)*tx) >> 3] & 
			 (1 << (*tx & 7))) > 0)
			x++;
		
		    break;
		}

#endif

	    case MATCH_ALLINCHARSET:

		{
		    Py_ssize_t matching;

		    DPRINTF("\nAllInCharSet :\n"
			    " looking for   = CharSet at 0x%lx\n"
			    " in string     = '%.40s'\n",
			    (long)match, &text[x]);
		    
		    matching = mxCharSet_Match(match,
					       textobj,
					       x,
					       sliceright,
					       1);
		    if (matching < 0)
			goto onError;
		    x += matching;
		    break;
		}

	    case MATCH_ISINCHARSET:

		{
		    int test;

		    DPRINTF("\nIsInCharSet :\n"
			    " looking for   = CharSet at 0x%lx\n"
			    " in string     = '%.40s'\n",
			    (long)match, &text[x]);

#if (TE_TABLETYPE == MXTAGTABLE_STRINGTYPE)
		    test = mxCharSet_ContainsChar(match, text[x]);
#else
		    test = mxCharSet_ContainsUnicodeChar(match, text[x]);
#endif
		    if (test < 0)
			goto onError;
		    if (test)
			x++;
		    break;
		}

	    }

	low_level_done:
	    
	    /* Not matched */
	    if (x == start) { 
		DPRINTF(" (no success)\n");
		if (jne == 0) { 
		    /* failed */
		    rc = 1; 
		    goto finished; 
		}
		else 
		    je = jne;
		goto next_entry;
	    }

	    /* Matched */
	    if (entry->tagobj) { 
		if (TE_HANDLE_MATCH(flags,
				    textobj,
				    taglist,
				    entry->tagobj,
				    start,x,
				    NULL,
				    context) < 0)
		    goto onError;
		DPRINTF(" [%ld:%ld] (matched and remembered this slice)\n",
			(long)start, (long)x);
	    }
	    else
		DPRINTF(" [%ld:%ld] (matched but not saved)\n",
			(long)start, (long)x);

	    if (flags & MATCH_LOOKAHEAD) {
		x = start;
		DPRINTF(" LOOKAHEAD flag set: reseting position to %ld\n",
			(long)x);
	    }

	    goto next_entry;
	}
	
	/* Jumps & low-level special commands */

	if (cmd < MATCH_MAX_SPECIALS) {
	
	    switch (cmd) {

	    case MATCH_FAIL: /* == MATCH_JUMP */

		if (jne == 0) { /* match failed */
		    rc = 1;
		    goto finished; 
		}
		else 
		    je = jne;
		goto next_entry;
	    
	    case MATCH_SKIP:

		DPRINTF("\nSkip %ld characters\n"
			" in string    = '%.40s'\n",
			PyInt_AS_LONG(match),text+x);

		start = x;
		x += PyInt_AS_LONG(match);
		break;

	    case MATCH_MOVE:

		start = x;
		x = PyInt_AS_LONG(match);
		if (x < 0)
		    /* Relative to end of the slice */
		    x += sliceright + 1;
		else
		    /* Relative to beginning of the slice */
		    x += sliceleft;

		DPRINTF("\nMove to position %ld \n"
			" string       = '%.40s'\n",
			(long)x, text+x);
		break;
		
	    case MATCH_EOF:

		DPRINTF("\nEOF at position %ld ? \n"
			" string       = '%.40s'\n",
			(long)x, text+x);

		if (x < sliceright) { /* not matched */
		    DPRINTF(" (no success)\n");
		    if (jne == 0) { /* match failed */
			rc = 1;
			goto finished; 
		    }
		    else 
			je = jne;
		    goto next_entry;
		}

		/* Matched & finished */
		x = sliceright;
		if (entry->tagobj) {
		    if (TE_HANDLE_MATCH(flags,
					textobj,
					taglist,
					entry->tagobj,
					x, x,
					NULL,
					context) < 0)
			goto onError;
		    DPRINTF(" [%ld:%ld] (matched and remembered this slice)\n",
			    (long)x, (long)x);
		}
		else
		    DPRINTF(" [%ld:%ld] (matched but not saved)\n",
			    (long)x, (long)x);
		rc = 2;
		goto finished;

	    case MATCH_JUMPTARGET:

		DPRINTF("\nJumpTarget '%.40s'\n",
			PyString_AsString(match));

		if (entry->tagobj) {
		    if (TE_HANDLE_MATCH(flags,
					textobj,
					taglist,
					entry->tagobj,
					x, x,
					NULL,
					context) < 0)
			goto onError;
		    DPRINTF(" [%ld:%ld] (matched and remembered this slice)\n",
			    (long)x, (long)x);
		}
		else
		    DPRINTF(" [%ld:%ld] (matched but not saved)\n",
			    (long)x, (long)x);
		goto next_entry;
	    
	    }

	    /* Matched */
	    if (x < 0)
		Py_ErrorWithArg(PyExc_TypeError,
				"Tag Table entry %ld: "
				"moved/skipped beyond start of text",
				(long)i);
	    
	    if (entry->tagobj) {
		if (TE_HANDLE_MATCH(flags,
				    textobj,
				    taglist,
				    entry->tagobj,
				    start, x,
				    NULL,
				    context) < 0)
		    goto onError;
		DPRINTF(" [%ld:%ld] (matched and remembered this slice)\n",
			(long)start, (long)x);
	    }
	    else
		DPRINTF(" [%ld:%ld] (matched but not saved)\n",
			(long)start, (long)x);

	    if (flags & MATCH_LOOKAHEAD) {
		x = start;
		DPRINTF(" LOOKAHEAD flag set: reseting position to %ld\n",
			(long)x);
	    }

	    goto next_entry;
	}

	/* Higher level matching and special commands */

	switch (cmd) {

	case MATCH_SWORDSTART:
	case MATCH_SWORDEND:
	case MATCH_SFINDWORD:
	    {
		Py_ssize_t wordstart, wordend;
		int rc;

		DPRINTF("\nsWordStart/End/sFindWord :\n"
			" in string   = '%.40s'\n",text+x);
		start = x;
		rc = TE_SEARCHAPI(match,
				  text,
				  start,
				  sliceright,
				  &wordstart,
				  &wordend);
		if (rc < 0)
		    goto onError;
		if (rc == 0) { 
		    /* not matched */
		    DPRINTF(" (no success)\n");
		    if (jne == 0) { 
			/* match failed */
			rc = 1; 
			goto finished; 
		    }
		    else 
			je = jne;
		} 
		else { 
		    /* matched */
		    if (cmd == MATCH_SWORDSTART)
			x = wordstart;
		    else {
			x = wordend;
			if (cmd == MATCH_SFINDWORD)
			    start = wordstart;
		    }
		    if (entry->tagobj) {
			if (TE_HANDLE_MATCH(flags,
					    textobj,
					    taglist,
					    entry->tagobj,
					    start, x,
					    NULL,
					    context) < 0)
			    goto onError;
			DPRINTF(" [%ld:%ld] (matched and remembered this slice)\n",
				(long)start, (long)x);
		    }
		    else
			DPRINTF(" [%ld:%ld] (matched but not saved)\n",
				(long)start, (long)x);

		    if (flags & MATCH_LOOKAHEAD) {
			x = start;
			DPRINTF(" LOOKAHEAD option set: reseting position to %ld\n",
				(long)x);
		    }
		}
		
		goto next_entry;
	    }

	case MATCH_TABLE:
	case MATCH_SUBTABLE:

	    if (PyInt_Check(match) && 
		PyInt_AS_LONG(match) == MATCH_THISTABLE)
		match = (PyObject *)table;

	    /* XXX Fix to auto-compile that match argument */
	    if (PyTuple_Check(match)) { 
		Py_Error(PyExc_TypeError,
			 "Match argument must be compiled TagTable");
	    }

	    else  { 
		PyObject *subtags;
		Py_ssize_t y = x;
		int newrc = 0;
		Py_ssize_t taglist_len;

		if (taglist != Py_None && cmd != MATCH_SUBTABLE) {
		    /* Create a new list for use as subtaglist */
		    subtags = PyList_New(0);
		    if (subtags == NULL) 
			goto onError;
		    taglist_len = 0;
		}
		else {
		    /* Use taglist as subtaglist */
		    subtags = taglist;
		    Py_INCREF(subtags);
		    if (taglist != Py_None) {
			taglist_len = PyList_Size(taglist);
			if (taglist_len < 0)
			    goto onError;
		    }
		    else
			taglist_len = 0;
		}

		DPRINTF("\n[Sub]Table : using table at 0x%lx\n",(long)match);

		start = x;

		/* match other table */
		newrc = TE_ENGINE_API(textobj, start, sliceright,
				      (mxTagTableObject *)match, 
				      subtags, context, &y,
				      level + 1);
		if (newrc == 0) {
		    Py_DECREF(subtags);
		    goto onError;
		}

		if (newrc == 1) {
		    /* not matched */
		    DPRINTF(" (no success)\n");
		    /* Undo changes to taglist in case of SUBTABLE match */
		    if (cmd == MATCH_SUBTABLE && taglist != Py_None) {
			DPRINTF("  undoing changes: del taglist[%ld:%ld]\n",
				(long)taglist_len, (long)PyList_Size(taglist));
			if (PyList_SetSlice(taglist, 
					    taglist_len, 
					    PyList_Size(taglist), 
					    NULL))
			    goto onError;
		    }
		    if (jne == 0) {
			/* match failed */
			rc = 1; 
			Py_DECREF(subtags);
			goto finished;
		    }
		    else 
			je = jne;
		} 
		else { 
		    /* matched */

		    /* move x to new position */
		    x = y;

		    /* Use None as subtaglist for the match entry for SUBTABLE */
		    if (cmd == MATCH_SUBTABLE) {
			Py_DECREF(subtags);
			Py_INCREF(Py_None);
			subtags = Py_None;
		    }

		    if (entry->tagobj) {
			if (TE_HANDLE_MATCH(flags,
					    textobj,
					    taglist,
					    entry->tagobj,
					    start, x,
					    subtags,
					    context) < 0) {
			    /* append failed */
			    Py_DECREF(subtags);
			    goto onError;
			}
			DPRINTF(" [%ld:%ld] (matched and remembered this slice)\n",
				(long)start, (long)x);
		    }
		    else
			DPRINTF(" [%ld:%ld] (matched but not saved)\n",
				(long)start, (long)x);

		    if (flags & MATCH_LOOKAHEAD) {
			x = start;
			DPRINTF(" LOOKAHEAD option set: reseting position to %ld\n",
				(long)x);
		    }
		}
		Py_DECREF(subtags);
	    }
	    goto next_entry;
	    
	case MATCH_TABLEINLIST:
	case MATCH_SUBTABLEINLIST:
	    
	    {
		PyObject *subtags;
		Py_ssize_t y = x;
		int newrc = 0;
		Py_ssize_t taglist_len;

		/* Get matching table from (list, index_integer) */
		match = PyList_GetItem(PyTuple_GET_ITEM(match, 0),
				       PyInt_AS_LONG(
				       PyTuple_GET_ITEM(match, 1))
				       );
		if (match == NULL)
		    Py_ErrorWithArg(PyExc_TypeError,
				    "Tag Table entry %ld: "
				    "matching table not found "
				    "in list of tables", 
				    (long)i);
		if (mxTagTable_Check(match))
		    Py_INCREF(match);
		else {
		    /* These tables are considered to be
		       cacheable. */
		    match = mxTagTable_New(match,
					   table->tabletype,
					   1);
		    if (match == NULL)
			goto onError;
		}

		if (taglist != Py_None && cmd != MATCH_SUBTABLEINLIST) {
		    /* Create a new list for use as subtaglist */
		    subtags = PyList_New(0);
		    if (subtags == NULL) {
			Py_DECREF(match);
			goto onError;
		    }
		    taglist_len = 0;
		}
		else {
		    /* Use taglist as subtaglist */
		    subtags = taglist;
		    Py_INCREF(subtags);
		    if (taglist != Py_None) {
			taglist_len = PyList_Size(taglist);
			if (taglist_len < 0)
			    goto onError;
		    }
		    else
			taglist_len = 0;
		}

		DPRINTF("\n[Sub]TableInList : using table at 0x%lx\n",
			(long)match);
	    
		/* XXX This part is almost identical to the above
		       MATCH_TABLE code -- should do something about
		       this... 

		*/

		start = x;

		/* match other table */
		newrc = TE_ENGINE_API(textobj, start, sliceright,
				      (mxTagTableObject *)match, 
				      subtags, context, &y,
		                      level + 1);
		if (newrc == 0) {
		    Py_DECREF(subtags);
		    Py_DECREF(match);
		    goto onError;
		}

		if (newrc == 1) {
		    /* not matched */
		    DPRINTF(" (no success)\n");
		    /* Undo changes to taglist in case of SUBTABLE match */
		    if (cmd == MATCH_SUBTABLEINLIST && taglist != Py_None) {
			DPRINTF("  undoing changes: del taglist[%ld:%ld]\n",
				(long)taglist_len, (long)PyList_Size(taglist));
			if (PyList_SetSlice(taglist, 
					    taglist_len, 
					    PyList_Size(taglist), 
					    NULL))
			    goto onError;
		    }
		    if (jne == 0) {
			/* match failed */
			rc = 1;
			Py_DECREF(subtags);
			Py_DECREF(match);
			goto finished;
		    }
		    else 
			je = jne;
		} 
		else { 
		    /* matched */

		    /* move x to new position */
		    x = y;

		    /* Use None as subtaglist for the match entry for SUBTABLEINLIST */
		    if (cmd == MATCH_SUBTABLEINLIST) {
			Py_DECREF(subtags);
			Py_INCREF(Py_None);
			subtags = Py_None;
		    }

		    if (entry->tagobj) {
			if (TE_HANDLE_MATCH(flags,
					    textobj,
					    taglist,
					    entry->tagobj,
					    start, x,
					    subtags,
					    context) < 0) {
			    /* append failed */
			    Py_DECREF(subtags);
			    Py_DECREF(match);
			    goto onError;
			}
			DPRINTF(" [%ld:%ld] (matched and remembered this slice)\n",
				(long)start, (long)x);
		    }
		    else {
			DPRINTF(" [%ld:%ld] (matched but not saved)\n",
				(long)start, (long)x);
		    }

		    if (flags & MATCH_LOOKAHEAD) {
			x = start;
			DPRINTF(" LOOKAHEAD option set: reseting position to %ld\n",
				(long)x);
		    }
		}
		Py_DECREF(subtags);
		Py_DECREF(match);
		goto next_entry;
	    }
	    
	case MATCH_LOOP:

	    DPRINTF("\nLoop: pre loop counter = %ld\n",
		    (long)loopcount);
	    
	    if (loopcount > 0)
		/* we are inside a loop */
		loopcount--;

	    else if (loopcount < 0) {
		/* starting a new loop */
		if (PyInt_Check(match)) {
		    loopcount = PyInt_AS_LONG(match);
		    loopstart = x;
		}
		else
		    Py_ErrorWithArg(PyExc_TypeError,
				    "Tag Table entry %ld: "
				    "expected an integer (cmd=Loop)",
				    (long)i);
	    }

	    if (loopcount == 0) {
		/* finished loop */
		loopcount = -1;
		if (loopstart == x) {
		    /* not matched */
		    DPRINTF(" (no success)\n");
	    
		}
		else if (entry->tagobj) {
		    /* matched */
		    if (TE_HANDLE_MATCH(flags,
					textobj,
					taglist,
					entry->tagobj,
					loopstart, x,
					NULL,
					context) < 0)
			goto onError;
		    DPRINTF(" [%ld:%ld] (matched and remembered this slice)\n",
			    (long)loopstart, (long)x);
		}
		else {
		    DPRINTF(" [%ld:%ld] (matched but not saved)\n",
			    (long)start, (long)x);
		}

		if (flags & MATCH_LOOKAHEAD) {
		    x = start;
		    DPRINTF(" LOOKAHEAD option set: reseting position to %ld\n",
			    (long)x);
		}

		/* skip loop body */
		je = jne; 
	    }
	    DPRINTF("\nloop: post loop counter = %ld\n",
		    (long)loopcount);
	    goto next_entry;

	case MATCH_LOOPCONTROL:

	    DPRINTF("\nLoopControl: loop counter = %ld, "
		    "setting it to = %ld\n",
		    (long)loopcount, (long)PyInt_AS_LONG(match));

	    loopcount = PyInt_AS_LONG(match);
	    goto next_entry;

	case MATCH_CALL:
	case MATCH_CALLARG:

	    {
		PyObject *fct;
		Py_ssize_t argc;
		register PyObject *w;
	    
		if (!PyTuple_Check(match)) {
		    argc = 0;
		    fct = match;
		}
		else {
		    argc = PyTuple_GET_SIZE(match) - 1;
		    Py_AssertWithArg(argc >= 0,
				     PyExc_TypeError,
				    "Tag Table entry %ld: "
				    "expected a tuple (fct,arg0,arg1,...)"
				    "(cmd=CallArg)",
				     (long)i);
		    fct = PyTuple_GET_ITEM(match,0);
		}
	    
		DPRINTF("\nCall[Arg] :\n");

		start = x;

		/* Build args =
		   (textobj,start,sliceright[,arg0,arg1,...][,context]) */
		{
		    PyObject *args;
		    register Py_ssize_t i;

        	    args = PyTuple_New(3 + argc + (context ? 1 : 0));
		    if (!args)
			goto onError;
		    Py_INCREF(textobj);
		    PyTuple_SET_ITEM(args,0,textobj);
		    w = PyInt_FromSsize_t(start);
		    if (!w)
			goto onError;
		    PyTuple_SET_ITEM(args,1,w);
		    w = PyInt_FromSsize_t(sliceright);
		    if (!w)
			goto onError;
		    PyTuple_SET_ITEM(args,2,w);
		    for (i = 0; i < argc; i++) {
			w = PyTuple_GET_ITEM(match,i + 1);
			Py_INCREF(w);
			PyTuple_SET_ITEM(args,3 + i,w);
		    }
		    if (context) {
			Py_INCREF(context);
			PyTuple_SET_ITEM(args, 3 + argc, context);
		    }
		
		    w = PyEval_CallObject(fct,args);
		    Py_DECREF(args);
		    if (w == NULL) 
			goto onError;
		
		}
		
		Py_AssertWithArg(PyInt_Check(w),
				 PyExc_TypeError,
				 "Tag Table entry %ld: "
				 "matching fct has to return an integer",
				 (long)i);
		x = PyInt_AS_LONG(w);
		Py_DECREF(w);

		if (start == x) { 
		    /* not matched */
		    DPRINTF(" (no success)\n");
		    if (jne == 0) { 
			/* match failed */
			rc = 1; 
			goto finished; 
		    }
		    else 
			je = jne;
		} 
		else if (entry->tagobj) { 
		    /* matched */
		    if (TE_HANDLE_MATCH(flags,
					textobj,
					taglist,
					entry->tagobj,
					start, x,
					NULL,
					context) < 0)
			goto onError;
		    DPRINTF(" [%ld:%ld] (matched and remembered this slice)\n",
			    (long)start, (long)x);
		}
		else {
		    DPRINTF(" [%ld:%ld] (matched but not saved)\n",
			    (long)start, (long)x);
		}

		if (flags & MATCH_LOOKAHEAD) {
		    x = start;
		    DPRINTF(" LOOKAHEAD option set: reseting position to %ld\n",
			    (long)x);
		}

		goto next_entry;
	    }
	}

    } /* for-loop */

 finished:
    /* In case no specific return code was set, check if we have
       matched successfully (table index beyond the end of the table)
       or failed to match (table index negative or scanned beyond the
       end of the slice) */
    if (rc < 0) {
	if (i >= table_len)
	    rc = 2; /* Ok */
	else if (i < 0)
	    rc = 1; /* Not Ok */
	else if (x > sliceright)
	    rc = 1; /* Not Ok */
	else
	    Py_ErrorWithArg(PyExc_StandardError,
			    "Internal Error: "
			    "tagging engine finished with no proper result "
			    "at position %ld in table",
			    (long)i);
    }

    DPRINTF("\nTag Engine finished: %s; Tag Table entry %ld; position %ld\n",
	    rc==1?"failed":"success",
	    (long)i, (long)x);

    /* Record the current head position */
    *next = x;

    return rc;

 onError:
    rc = 0;
    return rc;
}

/*
  What to do with a successful match depends on the value of flags:

  flags mod x == 1:
  -----------------
  MATCH_CALLTAG: 
  	call the tagobj with (taglist,textobj,match_left,r,subtags) and
	let it decide what to do; if context is given, the tagobj
	is called with (taglist,textobj,match_left,r,subtags,context)
	instead
  MATCH_APPENDTAG:
  	do a tagobj.append((None,match_left,r,subtags))
  MATCH_APPENDTAGOBJ:
  	do a taglist.append(tagobj)
  MATCH_APPENDMATCH:
  	do a taglist.append(textobj[match_left:r])
  default:
  	do a taglist.append((tagobj,match_left,r,subtags)) 
 
 - subtags is made to reference Py_None, if subtags == NULL
 - returns -1 if not successful, 0 on success
 - refcounts: all reference counts are incremented upon success only

*/
statichere
int TE_HANDLE_MATCH(int flags,
		    PyObject *textobj,
		    PyObject *taglist,
		    PyObject *tagobj,
		    Py_ssize_t match_left,
		    Py_ssize_t match_right,
		    PyObject *subtags,
		    PyObject *context)
{
    register PyObject *w;

    if (subtags == NULL)
	subtags = Py_None;
    if (tagobj == NULL)
	tagobj = Py_None;

    /* Default mechanism: */

    if (flags == 0 || flags == MATCH_LOOKAHEAD) {
	/* append result to taglist */

	if (!taglist || taglist == Py_None) 
	    return 0; /* nothing to be done */

	/* Build w = (tagobj,match_left,match_right,subtags) */
	w = PyTuple_New(4);
	if (!w)
	    goto onError;

	Py_INCREF(tagobj);
	PyTuple_SET_ITEM(w,0,tagobj);
	PyTuple_SET_ITEM(w,1,PyInt_FromSsize_t(match_left));
	PyTuple_SET_ITEM(w,2,PyInt_FromSsize_t(match_right));
	Py_INCREF(subtags);
	PyTuple_SET_ITEM(w,3,subtags);

	if (PyList_Append(taglist,w))
	    goto onError;
	Py_DECREF(w);
	return 0;
    }

    /* Flags are set: */
    
    if (flags & MATCH_APPENDTAGOBJ) {
	/* append the tagobj to the taglist */
	if (taglist == Py_None) 
	    return 0; /* nothing to be done */
	return PyList_Append(taglist,tagobj);
    }

    if (flags & MATCH_APPENDMATCH) {
	/* append the match to the taglist */
	register PyObject *v;
	
	if (taglist == Py_None) 
	    return 0; /* nothing to be done */
	v = TE_STRING_FROM_STRING(TE_STRING_AS_STRING(textobj) + match_left, 
				  match_right - match_left);
	if (!v)
	    goto onError;
	if (PyList_Append(taglist,v))
	    goto onError;
	Py_DECREF(v);
	return 0;
    }

    if (flags & MATCH_CALLTAG) { 
	/* call tagobj */
	register PyObject *args;
	Py_ssize_t nargs = 5;
	
	if (context)
	    nargs++;

	/* Build args = (taglist,textobj,
	                 match_left,match_right,
                         subtags[,context]) */
	args = PyTuple_New(nargs);
	if (!args)
	    goto onError;

	Py_INCREF(taglist);
	PyTuple_SET_ITEM(args,0,taglist);
	Py_INCREF(textobj);
	PyTuple_SET_ITEM(args,1,textobj);
	PyTuple_SET_ITEM(args,2,PyInt_FromSsize_t(match_left));
	PyTuple_SET_ITEM(args,3,PyInt_FromSsize_t(match_right));
	Py_INCREF(subtags);
	PyTuple_SET_ITEM(args,4,subtags);
	if (context) {
	    Py_INCREF(context);
	    PyTuple_SET_ITEM(args,5,context);
	}

	w = PyEval_CallObject(tagobj,args);
	Py_DECREF(args);
	if (w == NULL)
	    goto onError;
	Py_DECREF(w);
	return 0;
    }

    if (flags & MATCH_APPENDTAG) { 
	/* append to tagobj */

	/* Build w = (None,match_left,match_right,subtags) */
	w = PyTuple_New(4);
	if (!w)
	    goto onError;

	Py_INCREF(Py_None);
	PyTuple_SET_ITEM(w,0,Py_None);
	PyTuple_SET_ITEM(w,1,PyInt_FromSsize_t(match_left));
	PyTuple_SET_ITEM(w,2,PyInt_FromSsize_t(match_right));
	Py_INCREF(subtags);
	PyTuple_SET_ITEM(w,3,subtags);

	if (PyList_Check(tagobj)) {
	    if (PyList_Append(tagobj, w)) {
		Py_DECREF(w);
		goto onError;
	    }
	    Py_DECREF(w);
	}
	else {
	    PyObject *result;
	    result = PyEval_CallMethod(tagobj, "append", "(O)", w);
	    Py_DECREF(w);
	    if (result == NULL)
		goto onError;
	    Py_DECREF(result);
	}
	return 0;
    }

    if (flags & MATCH_LOOKAHEAD)
	return 0;

    Py_Error(PyExc_TypeError,
	     "Tag Table: unknown flag in command");
 onError:
    return -1;
}
