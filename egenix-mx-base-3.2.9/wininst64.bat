@echo off

set ORIGPATH=%PATH%

rem Disable creating debug builds ?
rem set NODEBUG=1

rem NOTE: We only support Python 2.6 and later on Windows x64 platforms !

rem ===============================================================
rem Python 2.6 and up need VisualStudio 9.0

:python26

set PATH=%ORIGPATH%
call d:\VisualStudio9.0\VC\vcvarsall.bat amd64
set VSPATH=%PATH%

echo --------------------------------------------------------------
echo Building for Python 2.6...
set PATH=d:\Python26;%VSPATH%
python setup.py clean --all
python setup.py bdist_msi -c -o 
python setup.py bdist_prebuilt

if #%NODEBUG%# == #1# goto skip_python26_debug
echo --------------------------------------------------------------
echo Building for Python 2.6 (debug version)...
set PATH=d:\Python26;%VSPATH%
python setup.py clean --all
python setup.py mx_autoconf --enable-debug bdist_msi -c -o --dist-dir=dist/debug/
:skip_python26_debug

echo --------------------------------------------------------------
echo Building for Python 2.7...
set PATH=d:\Python27;%VSPATH%
python setup.py clean --all
python setup.py bdist_msi -c -o 
python setup.py bdist_prebuilt

if #%NODEBUG%# == #1# goto skip_python27_debug
echo --------------------------------------------------------------
echo Building for Python 2.7 (debug version)...
set PATH=d:\Python27;%VSPATH%
python setup.py clean --all
python setup.py mx_autoconf --enable-debug bdist_msi -c -o --dist-dir=dist/debug/
:skip_python27_debug
