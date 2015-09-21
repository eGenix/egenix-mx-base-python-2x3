/* SWIG interface for the mxBeeBase wrapper. */

%module mxBeeBase

%{
#include "mx.h"
#include "btr.h"
%}

/* My typemaps and the like... */
%include mxSWIG.i

/* Now get the interface file... */
%include mxBeeBase.swig
