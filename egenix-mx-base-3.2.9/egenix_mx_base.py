#!/usr/local/bin/python

""" Configuration for the eGenix mx Base Distribution

    Copyright (c) 1997-2000, Marc-Andre Lemburg; mailto:mal@lemburg.com
    Copyright (c) 2000-2015, eGenix.com Software GmbH; mailto:info@egenix.com
    See the documentation for further information on copyrights,
    or contact the author. All Rights Reserved.
"""
import sys
from mxSetup import mx_Extension, mx_version

#
# Package version
#
version = mx_version(3, 2, 9,
                     #snapshot=1
                     )

#
# Setup information
#
name = "egenix-mx-base"

#
# Meta-Data
#
description = "eGenix mx Base Distribution for Python - mxDateTime, mxTextTools, mxProxy, mxTools, mxBeeBase, mxStack, mxQueue, mxURL, mxUID"
long_description = """\
eGenix mx Base Distribution for Python
--------------------------------------

The eGenix mx Extension Series are a collection of Python extensions
written in ANSI C and Python which provide a large spectrum of useful
additions to everyday Python programming.

We are using the distribution on a daily basis on our own servers and
for client installations. Many large corporations are building their
Python applications on parts of the eGenix.com mx Base Distribution.
It is also included in popular Linux distributions, such
as RedHat, OpenSUSE, Debian, Ubuntu, etc.


Contents
--------

The Base Distribution includes the Open Source subpackages of the
series and is needed by all other add-on packages of the series:

**mxDateTime - Date/Time Library for Python**

    mxDateTime implements three new object types, DateTime,
    DateTimeDelta and RelativeDateTime and many tools based on these
    for doing easy conversion between and parsing of various date/time
    formats.

    http://www.egenix.com/products/python/mxBase/mxDateTime/

**mxTextTools - Fast Text Parsing and Processing Tools for Python**

    mxTextTools provides several useful functions and types that
    implement high-performance text parsing, processing and search
    algorithms.

    http://www.egenix.com/products/python/mxBase/mxTextTools/

**mxProxy - Object Access Control for Python**

    mxProxy implements a new proxy type to provide low-level object
    access control, weak referencing and a cleanup protocol. It's
    ideal for use in restricted execution environments.

    http://www.egenix.com/products/python/mxBase/mxProxy/

**mxBeeBase - On-disk B+Tree Based Database Kit for Python**

    mxBeeBase is a high performance construction kit for disk based
    indexed databases. It offers components which you can plug
    together to easily build your own custom mid-sized databases.

    http://www.egenix.com/products/python/mxBase/mxBeeBase/

**mxURL - Flexible URL Data-Type for Python**

    mxURL provides a new datatype for storing and manipulating URL
    values as well as a few helpers related to URL building, encoding
    and decoding.

    http://www.egenix.com/products/python/mxBase/mxURL/

**mxUID - Fast Universal Identifiers for Python**

    mxUID implements a fast mechanism for generating universal
    identification strings (UIDs).

    http://www.egenix.com/products/python/mxBase/mxUID/

**mxStack - Fast and Memory-Efficient Stack Type for Python**

    mxStack implements a fast and memory efficient stack object type.

    http://www.egenix.com/products/python/mxBase/mxStack/

**mxQueue - Fast and Memory-Efficient Queue Type for Python**

    mxQueue implements a fast and memory efficient queue object type.

    http://www.egenix.com/products/python/mxBase/mxQueue/

**mxTools - Fast Everyday Helpers for Python**

    mxTools provides a collection of handy functions and objects for
    every day Python programming. It includes many functions that
    you've often missed in Python.

    http://www.egenix.com/products/python/mxBase/mxTools/


Downloads
---------

For downloads, documentation, installation instructions and
changelogs, please visit the product page at:

    http://www.egenix.com/products/python/mxBase/


License
-------

This open source software is brought to you by eGenix.com and
distributed under the eGenix.com Public License 1.1.0.
"""
license = (
"eGenix.com Public License 1.1.0; "
"Copyright (c) 1997-2000, Marc-Andre Lemburg, All Rights Reserved; "
"Copyright (c) 2000-2015, eGenix.com Software GmbH, All Rights Reserved"
)
author = "eGenix.com Software GmbH"
author_email = "info@egenix.com"
maintainer = "eGenix.com Software GmbH"
maintainer_email = "info@egenix.com"
url = "http://www.egenix.com/products/python/mxBase/"
download_url = url
platforms = [
    'Windows',
    'Linux',
    'FreeBSD',
    'Solaris',
    'Mac OS X',
    'AIX',
    ]
classifiers = [
    "Environment :: Console",
    "Environment :: No Input/Output (Daemon)",
    "Intended Audience :: Developers",
    "License :: OSI Approved :: Python License (CNRI Python License)",
    "License :: Freely Distributable",
    "License :: Other/Proprietary License",
    "Natural Language :: English",
    "Operating System :: OS Independent",
    "Operating System :: Microsoft :: Windows",
    "Operating System :: POSIX",
    "Operating System :: Unix",
    "Operating System :: BeOS",
    "Operating System :: MacOS",
    "Operating System :: OS/2",
    "Operating System :: Other OS",
    "Programming Language :: C",
    "Programming Language :: Python",
    "Programming Language :: Python :: 2",
    "Programming Language :: Python :: 2.4",
    "Programming Language :: Python :: 2.5",
    "Programming Language :: Python :: 2.6",
    "Programming Language :: Python :: 2.7",
    "Topic :: Communications",
    "Topic :: Database",
    "Topic :: Documentation",
    "Topic :: Internet",
    "Topic :: Internet :: WWW/HTTP",
    "Topic :: Internet :: WWW/HTTP :: Dynamic Content",
    "Topic :: Internet :: WWW/HTTP :: Dynamic Content :: CGI Tools/Libraries",
    "Topic :: Internet :: WWW/HTTP :: Site Management :: Link Checking",
    "Topic :: Scientific/Engineering",
    "Topic :: Scientific/Engineering :: Interface Engine/Protocol Translator",
    "Topic :: Software Development",
    "Topic :: Software Development :: Libraries",
    "Topic :: Software Development :: Libraries :: Application Frameworks",
    "Topic :: Software Development :: Libraries :: Python Modules",
    "Topic :: Text Processing",
    "Topic :: Text Processing :: Filters",
    "Topic :: Text Processing :: Markup",
    "Topic :: Utilities ",
    ]
if 'a' in version:
    classifiers.append("Development Status :: 3 - Alpha")
elif 'b' in version:
    classifiers.append("Development Status :: 4 - Beta")
else:
    classifiers.append("Development Status :: 5 - Production/Stable")
    classifiers.append("Development Status :: 6 - Mature")
classifiers.sort()

#
# Python packages
#
packages = [

    # mx Extensions Base Package
    'mx',

    # mxDateTime
    'mx.DateTime',
    'mx.DateTime.mxDateTime',
    'mx.DateTime.Examples',

    # mxProxy
    'mx.Proxy',
    'mx.Proxy.mxProxy',

    # mxQueue
    'mx.Queue',
    'mx.Queue.mxQueue',

    # mxStack
    'mx.Stack',
    'mx.Stack.mxStack',

    # mxTextTools
    'mx.TextTools',
    'mx.TextTools.mxTextTools',
    'mx.TextTools.Constants',
    'mx.TextTools.Examples',

    # mxTools
    'mx.Tools',
    'mx.Tools.mxTools',
    'mx.Tools.Examples',

    # mxBeeBase
    'mx.BeeBase',
    'mx.BeeBase.mxBeeBase',

    # mxURL
    'mx.URL',
    'mx.URL.mxURL',
    
    # mxUID
    'mx.UID',
    'mx.UID.mxUID',

    # Misc. other modules
    'mx.Misc',

    ]

#
# C Extensions
#

# Determine optional platform-dependent features
if sys.platform[:3] != 'win':
    # Unix-like platforms
    _mxDateTime_optional_libraries = [
        # mxDateTime needs floor() and ceil() which are sometimes
        # defined in libm. Python normally already references this
        # library if necessary, so not finding the library is not
        # necessarily a reason to fail building mxDateTime.
        ('m', ['math.h']),
        # mxDateTime can use the API clock_gettime() if available,
        # but this sometimes needs the librt to be available.
        ('rt', ['time.h']),
        ]
    _mxTools_optional_libraries = [
        # The optional mx.Tools.dlopen() function needs the dl lib in
        # order to load dynamic libaries on Unix platforms.
        ('dl', ['dlfcn.h']),
        ]
else:
    # On Windows, the extra libs are not needed (or even available)
    _mxDateTime_optional_libraries = []
    _mxTools_optional_libraries = []
    
# Extension definitions
ext_modules = [

    # mxDateTime
    mx_Extension('mx.DateTime.mxDateTime.mxDateTime',
                 ['mx/DateTime/mxDateTime/mxDateTime.c'],
                 # If mxDateTime doesn't compile, try removing the next line.
                 define_macros=[('USE_FAST_GETCURRENTTIME', None)],
                 #
                 include_dirs=['mx/DateTime/mxDateTime'],
                 optional_libraries=_mxDateTime_optional_libraries,
                 ),

    # mxProxy
    mx_Extension('mx.Proxy.mxProxy.mxProxy',
                 ['mx/Proxy/mxProxy/mxProxy.c'],
                 include_dirs=['mx/Proxy/mxProxy']),

    # mxQueue
    mx_Extension('mx.Queue.mxQueue.mxQueue',
                 ['mx/Queue/mxQueue/mxQueue.c'],
                 include_dirs=['mx/Queue/mxQueue']),

    # mxStack
    mx_Extension('mx.Stack.mxStack.mxStack',
                 ['mx/Stack/mxStack/mxStack.c'],
                 include_dirs=['mx/Stack/mxStack']),

    # mxTextTools
    mx_Extension('mx.TextTools.mxTextTools.mxTextTools',
                 ['mx/TextTools/mxTextTools/mxTextTools.c',
                  'mx/TextTools/mxTextTools/mxte.c',
                  'mx/TextTools/mxTextTools/mxbmse.c'],
                 define_macros=[('MX_BUILDING_MXTEXTTOOLS', None)],
                 include_dirs=['mx/TextTools/mxTextTools']),

    # mxTools
    mx_Extension('mx.Tools.mxTools.mxTools',
                 ['mx/Tools/mxTools/mxTools.c'],
                 define_macros=[

                     # To enable mx.Tools.setproctitle(), you have to enable
                     # the following line. Note that not all Python versions
                     # expose the required Py_GetArgcArgv() API.
                     # ('HAVE_PY_GETARGCARGV', None),
                     
                     # If you want to use the experimental mx.Tools.safecall()
                     # API, you have to enable the following line.
                     # ('MXTOOLS_ENABLE_SAFECALL', None),
                     
                 ],
                 include_dirs=['mx/Tools/mxTools'],
                 optional_libraries=_mxTools_optional_libraries,
                 ),

    # xmap is no longer supported
    #mx_Extension('mx.Tools.mxTools.xmap',
    #             ['mx/Tools/mxTools/xmap.c'],
    #             include_dirs=['mx/Tools/mxTools']),

    # mxBeeBase
    mx_Extension('mx.BeeBase.mxBeeBase.mxBeeBase',
                 ['mx/BeeBase/mxBeeBase/mxBeeBase.c',
                  'mx/BeeBase/mxBeeBase/btr.c'],
                 include_dirs=['mx/BeeBase/mxBeeBase']),

    # mxURL
    mx_Extension('mx.URL.mxURL.mxURL',
                 ['mx/URL/mxURL/mxURL.c'],
                 include_dirs=['mx/URL/mxURL']),

    # mxUID
    mx_Extension('mx.UID.mxUID.mxUID',
                 ['mx/UID/mxUID/mxUID.c'],
                 include_dirs=['mx/UID/mxUID']),

    ]

#
# Data files
#
data_files = [

    # Copyright, licenses, READMEs
    'mx/COPYRIGHT',
    'mx/LICENSE',
    'mx/README',

    # Misc
    'mx/Misc/LICENSE',
    'mx/Misc/COPYRIGHT',

    # mxDateTime
    'mx/DateTime/Doc/mxDateTime.pdf',
    'mx/DateTime/COPYRIGHT',
    'mx/DateTime/LICENSE',
    'mx/DateTime/README',
    'mx/DateTime/mxDateTime/mxDateTime.h',
    'mx/DateTime/mxDateTime/mxh.h',

    # mxProxy
    'mx/Proxy/Doc/mxProxy.pdf',
    'mx/Proxy/COPYRIGHT',
    'mx/Proxy/LICENSE',
    'mx/Proxy/README',
    'mx/Proxy/mxProxy/mxProxy.h',
    'mx/Proxy/mxProxy/mxh.h',

    # mxQueue
    'mx/Queue/Doc/mxQueue.pdf',
    'mx/Queue/COPYRIGHT',
    'mx/Queue/LICENSE',
    'mx/Queue/README',
    'mx/Queue/mxQueue/mxQueue.h',
    'mx/Queue/mxQueue/mxh.h',

    # mxStack
    'mx/Stack/Doc/mxStack.pdf',
    'mx/Stack/COPYRIGHT',
    'mx/Stack/LICENSE',
    'mx/Stack/README',
    'mx/Stack/mxStack/mxStack.h',
    'mx/Stack/mxStack/mxh.h',

    # mxTextTools
    'mx/TextTools/Doc/mxTextTools.pdf',
    'mx/TextTools/COPYRIGHT',
    'mx/TextTools/LICENSE',
    'mx/TextTools/README',
    'mx/TextTools/mxTextTools/mxTextTools.h',
    'mx/TextTools/mxTextTools/mxh.h',
    'mx/TextTools/mxTextTools/mxbmse.h',
    
    # mxTools
    'mx/Tools/Doc/mxTools.pdf',
    'mx/Tools/COPYRIGHT',
    'mx/Tools/LICENSE',
    'mx/Tools/README',
    'mx/Tools/mxTools/mxTools.h',
    'mx/Tools/mxTools/mxh.h',

    # mxBeeBase
    'mx/BeeBase/Doc/mxBeeBase.pdf',
    'mx/BeeBase/COPYRIGHT',
    'mx/BeeBase/LICENSE',
    'mx/BeeBase/README',
    'mx/BeeBase/mxBeeBase/mxBeeBase.h',
    'mx/BeeBase/mxBeeBase/mxh.h',
    'mx/BeeBase/mxBeeBase/btr.h',

    # mxURL
    'mx/URL/Doc/mxURL.pdf',
    'mx/URL/COPYRIGHT',
    'mx/URL/LICENSE',
    'mx/URL/README',
    'mx/URL/mxURL/mxURL.h',
    'mx/URL/mxURL/mxh.h',

    # mxUID
    'mx/UID/Doc/mxUID.pdf',
    'mx/UID/COPYRIGHT',
    'mx/UID/LICENSE',
    'mx/UID/README',
    'mx/UID/mxUID/mxUID.h',
    'mx/UID/mxUID/mxh.h',

    ]

# Declare namespace packages (for building eggs)
namespace_packages = [
    'mx',
    ]
