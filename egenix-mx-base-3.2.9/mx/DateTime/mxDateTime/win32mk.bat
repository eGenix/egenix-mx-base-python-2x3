@echo off
@cls
@echo.
@echo.

@if %1!==! echo Call win32mk (python directoty) as in: win32mk d:\programme\python
@echo.
@if %1!==! goto EndBat

REM This was tested on Windows95 using VC50 and NMAKE
@if %OS%!==Windows_NT! goto NTSet

@goto Win95Set


:NTSet
@if exist .\release deltree/y .\release
@set PYTHON_DIR=%1
@nmake mxdatetime.mak
@if exist %1\dlls copy release\*.dll %1\dlls\*.pyd
@goto EndBat


:Win95Set
@command /e:1024 /cW95.bat %1

:EndBat
