$ cc/prefix=all/incl=(sys$disk:[], python_include) MXDATETIME.C
$ @ PYTHON_VMS:SETUP
$ dbg = f$trnlnm("PYTHON_CFG_DEBUG")
$ thrd = f$trnlnm("PYTHON_CFG_THREADS")
$ def lnk$library sys$library:sql$user
$ libr/repl python_olb:MODULES_D'dbg''thrd'.OLB mxdatetime.obj
$ python PYTHON_TOOLS:REGISTER_CONFIG.PY  R  VMS_CONFIG_MXDATETIME.TXT
$ @ PYTHON_TOOLS:CVT_CONFIG_MODULES.COM
$ @ PYTHON_TOOLS:CVT_CONFIG_OLB.COM
$ @ PYTHON_VMS:CONFIG_INITTAB2MAR.COM  "CONFIG.DAT"  "D"
$ @ PYTHON_VMS:BLDRUN VMS_MACRO CONFIG_INITTAB
$ @ PYTHON_VMS:LINK_PY
