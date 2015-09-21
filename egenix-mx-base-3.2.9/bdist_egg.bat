@echo off

set ORIGPATH=%PATH%

rem Disable creating debug builds ?
rem set NODEBUG=1

rem ===============================================================
rem Python 2.4-2.5 need VisualStudio 7.1

set PATH=%ORIGPATH%
call d:\VisualStudio7.1\Bin\vsvars32.bat
set VSPATH=%PATH%

echo --------------------------------------------------------------
echo Building for Python 2.4...
set PATH=d:\Python24;%ORIGPATH%
python setup.py clean --all
python setup.py bdist_egg

if #%NODEBUG%# == #1# goto skip_python24_debug
echo --------------------------------------------------------------
echo Building for Python 2.4 (debug version)...
set PATH=d:\Python24;%ORIGPATH%
python setup.py clean --all
python setup.py mx_autoconf --enable-debug bdist_egg --dist-dir=dist/debug/
:skip_python24_debug

echo --------------------------------------------------------------
echo Building for Python 2.5...
set PATH=d:\Python25;%VSPATH%
python setup.py clean --all
python setup.py bdist_egg 

if #%NODEBUG%# == #1# goto skip_python25_debug
echo --------------------------------------------------------------
echo Building for Python 2.5 (debug version)...
set PATH=d:\Python25;%VSPATH%
python setup.py clean --all
python setup.py mx_autoconf --enable-debug bdist_egg --dist-dir=dist/debug/
:skip_python25_debug

rem ===============================================================
rem Python 2.6 and up need VisualStudio 9.0

:python26

set PATH=%ORIGPATH%
call d:\VisualStudio9.0\Common7\Tools\vsvars32.bat
set VSPATH=%PATH%

echo --------------------------------------------------------------
echo Building for Python 2.6...
set PATH=d:\Python26;%VSPATH%
python setup.py clean --all
python setup.py bdist_egg 

if #%NODEBUG%# == #1# goto skip_python26_debug
echo --------------------------------------------------------------
echo Building for Python 2.6 (debug version)...
set PATH=d:\Python26;%VSPATH%
python setup.py clean --all
python setup.py mx_autoconf --enable-debug bdist_egg --dist-dir=dist/debug/
:skip_python26_debug

echo --------------------------------------------------------------
echo Building for Python 2.7...
set PATH=d:\Python27;%VSPATH%
python setup.py clean --all
python setup.py bdist_egg 

if #%NODEBUG%# == #1# goto skip_python27_debug
echo --------------------------------------------------------------
echo Building for Python 2.7 (debug version)...
set PATH=d:\Python27;%VSPATH%
python setup.py clean --all
python setup.py mx_autoconf --enable-debug bdist_egg --dist-dir=dist/debug/
:skip_python27_debug
