@echo off

rem ===============================================================
rem Python 1.5-2.3 needs VisualStudio 6

call D:\VisualStudio6\Bin\vcvars32.bat
set ORIGPATH=%PATH%

rem echo --------------------------------------------------------------
rem echo Building for Zope 2.3 (Python 1.5.2)...
rem set PATH=d:\Python152;%ORIGPATH%
rem python setup.py clean --all
rem python setup.py bdist_zope --format zip

rem echo --------------------------------------------------------------
rem echo Building for Zope 2.4-2.6 (Python 2.1)...
rem set PATH=d:\Python21;%ORIGPATH%
rem python setup.py clean --all
rem python setup.py bdist_zope --format zip

echo --------------------------------------------------------------
echo Building for Zope 2.7 (Python 2.3)...
set PATH=d:\Python23;%ORIGPATH%
python setup.py clean --all
python setup.py bdist_zope --format zip

rem ===============================================================
rem Python 2.4 and up needs VisualStudio 7.1

set PATH=%ORIGPATH%
call d:\VisualStudio7.1\Bin\vsvars32.bat
set ORIGPATH=%PATH%

echo --------------------------------------------------------------
echo Building for Zope 2.8 (Python 2.4)...
set PATH=d:\Python24;%ORIGPATH%
python setup.py clean --all
python setup.py bdist_zope --format zip

rem echo --------------------------------------------------------------
rem echo Building for Zope 2.8 (Python 2.4; debug version)...
rem set PATH=d:\Python24;%ORIGPATH%
rem python setup.py clean --all
rem python setup.py mx_autoconf --enable-debug bdist_zope --format zip --dist-dir=dist/debug/

