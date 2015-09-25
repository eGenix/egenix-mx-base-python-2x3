#!/bin/bash
SCRIPT=bin/python-modernize
OPTIONS=	-f lib2to3.fixes.fix_apply \
		-f lib2to3.fixes.fix_has_key \
		-f lib2to3.fixes.fix_idioms \
		-f lib2to3.fixes.fix_paren \
		-f lib2to3.fixes.fix_sys_exc \
		-f lib2to3.fixes.fix_except \
		-f libmodernize.fixes.fix_print \
		-f libmodernize.fixes.fix_raise \

# Run modernize	
$SCRIPT $OPTIONS $*
