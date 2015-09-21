@echo off

set ORIGPATH=%PATH%

rem Disable creating debug builds ?
rem set NODEBUG=1

rem Note: We no longer support Python 1.5.2 - 2.3 !
goto python24

rem ===============================================================
rem Python 1.5-2.3 need VisualStudio 6

:python23

call D:\VisualStudio6\Bin\vcvars32.bat
set VSPATH=%PATH%

echo --------------------------------------------------------------
echo Building for Python 1.5.2...
set PATH=d:\Python152;%VSPATH%
python setup.py clean --all
python setup.py bdist_wininst -c -o 

echo --------------------------------------------------------------
echo Building for Python 2.0...
set PATH=d:\Python20;%VSPATH%
python setup.py clean --all
python setup.py bdist_wininst -c -o 

echo --------------------------------------------------------------
echo Building for Python 2.1...
set PATH=d:\Python21;%VSPATH%
python setup.py clean --all
python setup.py bdist_wininst -c -o 

echo --------------------------------------------------------------
echo Building for Python 2.2...
set PATH=d:\Python22;%VSPATH%
python setup.py clean --all
python setup.py bdist_wininst -c -o 

echo --------------------------------------------------------------
echo Building for Python 2.3...
set PATH=d:\Python23;%VSPATH%
python setup.py clean --all
python setup.py bdist_wininst -c -o 
python setup.py bdist_prebuilt

rem ===============================================================
rem Python 2.4 and up need VisualStudio 7.1

:python24

set PATH=%ORIGPATH%
call d:\VisualStudio7.1\Common7\Tools\vsvars32.bat
set VSPATH=%PATH%

echo --------------------------------------------------------------
echo Building for Python 2.4...
set PATH=d:\Python24;%VSPATH%
python setup.py clean --all
python setup.py bdist_wininst -c -o 
python setup.py bdist_prebuilt

if #%NODEBUG%# == #1# goto skip_python24_debug
echo --------------------------------------------------------------
echo Building for Python 2.4 (debug version)...
set PATH=d:\Python24;%VSPATH%
python setup.py clean --all
python setup.py mx_autoconf --enable-debug bdist_wininst -c -o --dist-dir=dist/debug/
:skip_python24_debug

echo --------------------------------------------------------------
echo Building for Python 2.5...
set PATH=d:\Python25;%VSPATH%
python setup.py clean --all
python setup.py bdist_msi -c -o 
python setup.py bdist_prebuilt

if #%NODEBUG%# == #1# goto skip_python25_debug
echo --------------------------------------------------------------
echo Building for Python 2.5 (debug version)...
set PATH=d:\Python25;%VSPATH%
python setup.py clean --all
python setup.py mx_autoconf --enable-debug bdist_msi -c -o --dist-dir=dist/debug/
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
