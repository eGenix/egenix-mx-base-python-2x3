@echo off

@if exist release\NUL deltree/y release
@set PYTHON_DIR=%1
@nmake mxdatetime.mak
@if exist %1\dlls\NUL copy release\*.dll %1\dlls\*.pyd

