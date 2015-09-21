#!/usr/local/bin/python

""" Distutils Extensions needed for the mx Extensions.

    Copyright (c) 1997-2000, Marc-Andre Lemburg; mailto:mal@lemburg.com
    Copyright (c) 2000-2015, eGenix.com Software GmbH; mailto:info@egenix.com
    See the documentation for further information on copyrights,
    or contact the author. All Rights Reserved.

    License: eGenix.com Public License. See LICENSE or LICENSE.mxSetup
    or http://www.egenix.com/products/python/mxBase/ for details.

"""
#
# History:
# 3.9.5: Add fix for bdist_wheel to make it work with mxSetup
# 3.9.4: Add canonical OS and platform APIs; use these for prebuilts
# 3.9.3: Enhanced mx_set_platform() monkey-patch of distutils.
# 3.9.2: Added mx_bdist_egg_setuptools
# 3.9.1: Added mx_upload which allows uploading single dist files
# 3.9.0: Added sdist_web format for web installers
# 3.8.0: Changed to prebuilt filename version 2
# 3.7.0: Added mx_register command with option to add a hash tag
#        to the download_url (--with-hash-tag)
# 3.6.8: Removed mx_customize_compiler(). We now always use distutils'
#        version. See #943.
# 3.6.7: Fix for clang override needed for Mac OS X Lion (see #943)
# 3.6.6: Fix for moved customize_compiler() function (see #861)
# 3.6.5: added install option --force-non-pure
# 3.6.4: import commands and base classes directly from setuptools,
#        if present to fixes compatibility with pip (see #741)
# 3.6.3: added verify_package_version()
# 3.6.2: added fix-up for distutils.util.change_root()
# 3.6.1: added --exclude-files to bdist_prebuilt
# 3.6.0: added support for Python 2.7's distutils version
# 3.5.1: added support to have prebuilt archives detect version and
#        and platform mismatches before installing a package
# 3.5.0: added own non-setuptools version of bdist_egg to build eggs
#        straight from the available packages rather than relying on
#        setuptools to do this
# 3.4.2: added support for using embedded .dylibs
# 3.4.1: only include top-level documentation directories in prebuilt archives
# 3.4.0: made the inclusion of the data source files optional in bdist_prebuilt,
#        fixed the prebuilt file search mechanism to always convert to platform
#        convention
# 3.3.1: have mx_version()'s snapshot default to true if prerelease is 'dev'
# 3.3.0: use our own mx_get_platform() version in order to support prebuilt
#        installers on platforms that have slightly different platform strings
# 3.2.1: py_version() will no longer add the Unicode info on Windows
# 3.2.0: Add support for Python 2.6 Windows builds
# 3.1.1: Allow Unicode values for meta data
# 3.1.0: changed the build pickle name used by prebuilt archives
# 3.0.0: added mx_build_data, removed the need to use "build --skip"
#        when installing prebuilt archives, uninstall now also works
#        for prebuilt archives
# 2.9.0: added support for (orig, dest) data_files definitions
# 2.8.0: added optional setuptools support
# 2.7.0: added mx_bdist_prebuilt
# 2.6.0: added mx_bdist_msi that allows setting the product name
# 2.5.1: add support for distutils 2.5.0 which broke the MSVCCompiler
#        compile options hack
# 2.5.0: patch install_lib to not produce weird byte-code filenames;
#        add work-around for change in bdist_wininst introduced in
#        Python 2.4 that broke the py_version() approach
# 2.4.0: remove monkey patch and make use of py_version() instead
# 2.3.0: monkey patch get_platform() to include the Unicode width info
# 2.2.0: replaced .announce() and .warn() usage with log object
# 2.1.2: added wide Unicode support
# 2.1.1: added support for Python 2.3 way of finding MSVC paths
# 2.1.0: added bdist_zope, support for classifiers
# 2.0.0: reworked the include and lib path logic; factored out the
#        compiler preparation
# 1.9.0: added new include and lib path logic; added bdist_zope command
# Older revisions are not documented.
#
import types, glob, os, sys, re, cPickle, copy, imp, shutil
import urllib2, urlparse

### Globals

# Module version
__version__ = '3.9.5'

# Generate debug output for mxSetup.py ?
_debug = int(os.environ.get('EGENIX_MXSETUP_DEBUG', 0))

# Python version running this module
python_version = '%i.%i' % sys.version_info[:2]

# Prebuilt archive marker file
PREBUILT_MARKER = 'PREBUILT'

# Allowed configuration value types; all other configuration entries
# are removed by run_setup()
ALLOWED_SETUP_TYPES = (types.StringType,
                       types.ListType,
                       types.TupleType,
                       types.IntType,
                       types.FloatType,
                       types.DictType)
if python_version >= '2.6':
    ALLOWED_SETUP_TYPES += (types.UnicodeType,)

# Some Python distutils versions don't support all setup keywords we
# use
UNSUPPORTED_SETUP_KEYWORDS = ()
if python_version < '2.3':
    UNSUPPORTED_SETUP_KEYWORDS = UNSUPPORTED_SETUP_KEYWORDS + (
        'classifiers',
        'download_url',
        )

# Filename suffixes used for Python modules on this platform
PY_SUFFIXES = [suffix for (suffix, mode, order) in imp.get_suffixes()]
if '.pyc' not in PY_SUFFIXES:
    PY_SUFFIXES.append('.pyc')
if '.pyo' not in PY_SUFFIXES:
    PY_SUFFIXES.append('.pyo')

# Namespace __init__.py file content required by setuptools namespace
# packages
SETUPTOOLS_NAMESPACE_INIT = """\
__import__('pkg_resources').declare_namespace(__name__)
"""
    
### Python compatibility support

if 1:
    # Patch True/False into builtins for those versions of Python that
    # don't support it
    try:
        True
    except NameError:
        __builtins__['True'] = 1
        __builtins__['False'] = 0

    # StringTypes into types module for those versions of Python that
    # don't support it
    try:
        types.StringTypes
    except AttributeError:
        types.StringTypes = (types.StringType, types.UnicodeType)

    # Patch isinstance() to support tuple arguments
    try:
        isinstance('', types.StringTypes)
    except TypeError:
        def py22_isinstance(obj, classes,
                            orig_isinstance=isinstance):
            if type(classes) is not types.TupleType:
                return orig_isinstance(obj, classes)
            for classobj in classes:
                if orig_isinstance(obj, classobj):
                    return True
            return False
        __builtins__['isinstance'] = py22_isinstance

    # UnicodeDecodeError is new in Python 2.3
    try:
        UnicodeDecodeError
    except NameError:
        UnicodeDecodeError = UnicodeError

if python_version < '2.2':
    def module_loaded(name):
        return sys.modules.has_key(name)
    def has_substring(substr, text):
        return text.find(substr) >= 0
else:
    def module_loaded(name):
        return name in sys.modules
    def has_substring(substr, text):
        return substr in text

### Errors

class TimeoutError(IOError):

    """ An I/O timeout was hit.

    """
    pass

class NotSupportedError(TypeError):

    """ The requested functionality is not supported on the
        platform.

    """
    pass

### distutils fix-ups

# distutils.util.change_root() has a bug on nt and os2: it fails with
# an IndexError in case pathname is empty. We fix this by
# monkey-patching distutils.
        
import distutils.util

orig_change_root = distutils.util.change_root

def change_root(new_root, pathname):
    if os.name == 'nt':
        (drive, path) = os.path.splitdrive(pathname)
        if path and path[0] == '\\':
            path = path[1:]
        return os.path.join(new_root, path)
    elif os.name == 'os2':
        (drive, path) = os.path.splitdrive(pathname)
        if path and path[0] == os.sep:
            path = path[1:]
        return os.path.join(new_root, path)
    else:
        return orig_change_root(new_root, pathname)

distutils.util.change_root = change_root

### Setuptools support

# Let setuptools monkey-patch distutils, if a command-line option
# --use-setuptools is given and enable the setuptools work-arounds
# if it was already loaded (see ticket #547).
if module_loaded('setuptools'):
    import setuptools
elif '--use-setuptools' in sys.argv:
    sys.argv.remove('--use-setuptools')
    try:
        import setuptools
        print 'running mxSetup.py with setuptools patched distutils'
    except ImportError:
        print 'could not import setuptools; ignoring --use-setuptools'
        setuptools = None
else:
    setuptools = None

### Distutils platform support

import distutils.util

if not hasattr(distutils.util, 'set_platform'):
    # Replace the distutils get_platform() function with our own, since we
    # will in some cases need to adjust its return value, e.g. for
    # pre-built archives.

    # Safe the original get_platform() function as copy in
    # orig_get_platform()
    #
    # We have to use this approach, since copying the function object
    # will only do a shallow copy of the code object
    import types
    orig_get_platform = types.FunctionType(
        distutils.util.get_platform.func_code,
        distutils.util.get_platform.func_globals,
        distutils.util.get_platform.func_name)

    # Global platform string
    PLATFORM = orig_get_platform()

    def mx_get_platform():

        """ Return the platform string that distutils uses through-out
            the system.

        """
        return PLATFORM

    # Replace the code object in distutils get_platform function so
    # that all existing imports get changed as well
    distutils.util.get_platform.func_code = mx_get_platform.func_code
    assert (distutils.util.get_platform.func_code !=
            orig_get_platform.func_code)
    distutils.util.PLATFORM = PLATFORM

    # Replace distutils' own get_platform() function with ours
    distutils.util.get_platform = mx_get_platform

    def mx_set_platform(platform):

        """ Adjust the platform string that distutils uses to platform.

            This is needed e.g. when installing pre-built setups, since
            the target system platform string may well be different from
            the build system one, e.g. due to OS version differences or
            Mac OS X fat binaries that get installed on i386/ppc systems.

        """
        global PLATFORM
        if PLATFORM != platform:
            log.info('adjusting distutils platform string from %r to %r' %
                     (PLATFORM, platform))
            PLATFORM = platform
            distutils.util.PLATFORM = platform

else:
    # For Python 2.7+ and 3.2+ we don't need to monkey-patch
    # distutils, since it now has a set_platform() API.
    #
    # XXX Turns out that this useful functionality was removed again,
    #     before 2.7 and 3.2 were released. See the discussion on
    #     http://bugs.python.org/issue13994 and #861. Leaving the code
    #     here in case it gets added again.

    def mx_get_platform():

        """ Return the platform string that distutils uses through-out
            the system.

        """
        return distutils.util.get_platform()

    def mx_set_platform(platform):

        """ Adjust the platform string that distutils uses to platform.

            This is needed e.g. when installing pre-built setups, since
            the target system platform string may well be different from
            the build system one, e.g. due to OS version differences or
            Mac OS X fat binaries that get installed on i386/ppc systems.

        """
        if platform != mx_get_platform():
            log.info('adjusting distutils platform string to %r' % platform)
            distutils.util.set_platform(platform)

### Load distutils

# This has to be done after importing setuptools, since it heavily
# monkey-patches distutils with its black magic...

from distutils.errors import \
     DistutilsError, DistutilsExecError, CompileError, CCompilerError, \
     DistutilsSetupError
if setuptools is not None:
    from setuptools import setup, Extension, Command
    from setuptools import Distribution
    from setuptools.command.install import install
    from setuptools.command.bdist_egg import bdist_egg
else:
    from distutils.core import setup, Extension, Command
    from distutils.dist import Distribution
    from distutils.command.install import install
from distutils.msvccompiler import MSVCCompiler
from distutils.util import execute
from distutils.version import StrictVersion
from distutils.dir_util import remove_tree, mkpath, create_tree
from distutils.spawn import spawn, find_executable
from distutils.command.config import config
from distutils.command.build import build
from distutils.command.build_ext import build_ext
from distutils.command.build_clib import build_clib
from distutils.command.build_py import build_py
from distutils.command.bdist import bdist
from distutils.command.bdist_rpm import bdist_rpm
from distutils.command.bdist_dumb import bdist_dumb
from distutils.command.bdist_wininst import bdist_wininst
from distutils.command.install_data import install_data
from distutils.command.install_lib import install_lib
from distutils.command.sdist import sdist
from distutils.command.register import register
from distutils.command.clean import clean
import distutils.archive_util

# Upload command is only available in Python 2.5+
if python_version >= '2.5':
    from distutils.command.upload import upload
else:
    upload = None

# distutils changed a lot in Python 2.7/3.2 due to many
# distutils.sysconfig APIs having been moved to the new (top-level)
# sysconfig module.
if (python_version < '2.7' or
    (python_version >= '3.0' and python_version < '3.2')):
    # Older Python versions (<=2.6 and <=3.1):
    from distutils.sysconfig import \
         get_config_h_filename, parse_config_h, customize_compiler, \
         get_config_var, get_config_vars, get_python_version
    from distutils.util import get_platform

else:
    # More recent Python versions (2.7 and 3.2+):
    from sysconfig import \
         get_config_h_filename, parse_config_h, get_path, \
         get_config_var, get_config_vars, get_python_version, get_platform

    # This API was moved from distutils.sysconfig to
    # distutils.ccompiler in Python 2.7... and then back again in
    # 2.7.3 (see #861); since the sysconfig version is deemed the
    # correct one and 3.x only has it there, we first try sysconfig
    # now and then revert to ccompiler in case it's not found
    try:
        from distutils.sysconfig import customize_compiler
    except ImportError:
        from distutils.ccompiler import customize_compiler

### Optional distutils support

# Load the MSI bdist command (new in Python 2.5, only on Windows)
try:
    from distutils.command.bdist_msi import bdist_msi
    import msilib
except ImportError:
    bdist_msi = None

# The log object was added to distutils in Python 2.3; we provide a
# compatile emulation for earlier Python versions
try:
    from distutils import log
except ImportError:
    class Log:
        def log(self, level, msg, *args):
            print (msg % args)
            sys.stdout.flush()
        def debug(self, msg, *args):
            if _debug:
                self.log(1, msg, args)
        def info(self, msg, *args):
            if _debug:
                self.log(2, msg, args)
        def warn(self, msg, *args):
            self.log(3, msg, args)
        def error(self, msg, *args):
            self.log(4, msg, args)
        def fatal(self, msg, *args):
            self.log(5, msg, args)
    log = Log()

### Third-party distutils extensions

# This is bdist_ppm from ActiveState
if python_version >= '2.0':
    try:
        from distutils.command import bdist_ppm
    except ImportError:
        bdist_ppm = None
    try:
        from distutils.command import GenPPD
    except ImportError:
        GenPPD = None
else:
    bdist_ppm = None
    GenPPD = None

# bdist_wheel support

try:
    from wheel.bdist_wheel import bdist_wheel
except ImportError:
    bdist_wheel = None

###

#
# Helpers
#

def get_python_include_dir():

    """ Return the path to the Python include dir.

        This is the location of the Python.h and other files.

    """
    # Can't use get_path('include') here, because Debian/Ubuntu
    # hack those paths to point to different installation paths
    # rather than the Python's own paths. See #762.
    try:
        config_h = get_config_h_filename()
        include_dir = os.path.split(config_h)[0]
        if not os.path.exists(os.path.join(include_dir, 'Python.h')):
            raise IOError('Python.h not found in include dir %s' %
                          include_dir)
        return include_dir

    except IOError, reason:
        if _debug:
            print ('get_config_h_filename: %s' % reason)
        # Ok, we've hit a problem with the Python installation or
        # virtualenv setup, so let's try a common locations:
        pydir = 'python%i.%i' % sys.version_info[:2]
        for dir in (
            os.path.join(sys.prefix, 'include', pydir),
            os.path.join(sys.prefix, 'include', 'python'),
            os.path.join(sys.prefix, 'include'),
            os.path.join(sys.exec_prefix, 'include', pydir),
            os.path.join(sys.exec_prefix, 'include', 'python'),
            os.path.join(sys.exec_prefix, 'include'),
            ):
            if _debug > 1:
                print ('get_config_h_filename: trying include dir: %s' %
                       dir)
            if os.path.exists(os.path.join(dir, 'Python.h')):
                if _debug:
                    print ('get_config_h_filename: found include dir: %s' %
                           dir)
                return dir
        # Nothing much we can do...
        raise

# prerelease parsers used for conversion to PEP 386 format
PRERELEASE_RX = re.compile(
    '('
     '(?P<alpha>alpha|a)|'
     '(?P<beta>beta|b)|'
     '(?P<rc>rc|rc|c)|'
     '(?P<dev>dev)'
    ')'
    '[-_ ./#]*'
    '(?P<version>[0-9]+)?'
    )

def mx_version(major=1, minor=0, patch=0, prerelease='', snapshot=None,
               sub_version=None, pep_compatible=True):

    """ Format the package version number.

        If sub_version is given as tuple of integers, it is appended
        to the major.minor.patch version string, with dots as
        delimiters.

        If prerelease is given, this is appended to the version
        string. The string should use the format
        '(alpha/beta/rc/dev)_123', e.g. 'beta_1'.

        If snapshot is true, the current date is added to the version
        string. snapshot defaults to true, if prerelease is set to
        'dev'.

        When using pep_compatible=True, the functions returns a version
        string that conforms to PEP 386/440.  It's turned off by default
        for now to keep the function backwards compatible.

    """
    s = '%i.%i.%i' % (major, minor, patch)
    if sub_version:
        # Add sub_version tuple
        s += ''.join(['.%i' % x for x in sub_version])
    if prerelease:
        pr = PRERELEASE_RX.match(prerelease)
        if pep_compatible:
            if pr is None:
                raise TypeError('unsupported prerelease string: %r' %
                                prerelease)
            if pr.group('version'):
                pr_version = int(pr.group('version'))
            else:
                pr_version = 0
            if pr.group('alpha'):
                s += 'a%i' % pr_version
            elif pr.group('beta'):
                s += 'b%i' % pr_version
            elif pr.group('rc'):
                s += 'rc%i' % pr_version
            elif pr.group('dev'):
                if pr_version:
                    if pep_compatible:
                        s += '.dev%i' % pr_version
                    else:
                        # Old style format
                        s += '_dev_%i' % pr_version
                else:
                    # If no version is given, use the current date
                    snapshot = 1
        else:
            # Old style format
            s += '_' + prerelease
            if prerelease == 'dev' and snapshot is None:
                snapshot = 1
    if snapshot:
        import time
        now = time.gmtime(time.time())
        date = time.strftime('%Y%m%d', now)
        if pep_compatible:
            # Create a dev release
            s += '.dev' + date
        else:
            # Old style format
            s += '_' + date
    return s

MX_VERSION_RE = re.compile('(\d+)\.(\d+)\.(\d+)')

def parse_mx_version(version):

    """ Convert a version string created with mx_version() back to
        its components.

        Returns a tuple (major, minor, patch, prerelease, snapshot).

        prerelease and snapshot are currently not supported and always
        set to '' and None.

        Raises a ValueError in case the version string cannot be
        parsed.

        Note: This function (currently) does not support pre-release
        or snapshot versions.

    """
    m = MX_VERSION_RE.match(version)
    if m is None:
        raise ValueError('incompatible version string format: %r' %
                         version)
    major, minor, patch = m.groups()
    major = int(major)
    minor = int(minor)
    patch = int(patch)
    prerelease = ''
    snapshot = None
    return major, minor, patch, prerelease, snapshot

# Tests

def _test_mx_version():

    import time
    now = time.gmtime(time.time())
    date = time.strftime('%Y%m%d', now)

    assert mx_version(1,2,3) == '1.2.3'
    assert mx_version(1,2,3, 'alpa') == '1.2.3a0'
    assert mx_version(1,2,3, 'beta') == '1.2.3b0'
    assert mx_version(1,2,3, 'rc') == '1.2.3rc0'
    assert mx_version(1,2,3, 'a1') == '1.2.3a1'
    assert mx_version(1,2,3, 'b2') == '1.2.3b2'
    assert mx_version(1,2,3, 'rc1') == '1.2.3rc1'
    assert mx_version(1,2,3, 'alpha-1') == '1.2.3a1'
    assert mx_version(1,2,3, 'beta-2') == '1.2.3b2'
    assert mx_version(1,2,3, 'rc-1') == '1.2.3rc1'
    assert mx_version(1,1,2, prerelease='dev-1') == '1.1.2.dev1'
    assert mx_version(1,1,2, prerelease='dev-2') == '1.1.2.dev2'
    assert mx_version(1,1,2, prerelease='dev') == '1.1.2.dev%s' % date
    assert mx_version(1,1,2, prerelease='dev-45') == '1.1.2.dev45'
    assert mx_version(1,1,2, prerelease='dev45') == '1.1.2.dev45'
    assert mx_version(1,1,2, prerelease='dev.45') == '1.1.2.dev45'
    assert mx_version(1,1,2, prerelease='dev_45') == '1.1.2.dev45'
    assert mx_version(1,1,2, prerelease='dev 45') == '1.1.2.dev45'
    assert mx_version(1,1,2, prerelease='dev/45') == '1.1.2.dev45'
    assert mx_version(1,1,2, prerelease='dev#45') == '1.1.2.dev45'
    assert mx_version(1,1,2, prerelease='dev 45', snapshot=1) == '1.1.2.dev45.dev%s' % date
    assert mx_version(1,1,2, prerelease='dev', snapshot=1) == '1.1.2.dev%s' % date
    assert mx_version(1,1,2, snapshot=1) == '1.1.2.dev%s' % date

if 0:
    _test_mx_version()

### User interaction support

def write_console(text, linefeed=False, force=False):

    """ Write text to the console.

        If linefeed is true (default is false), a line feed is
        appended to text.

        If force is true, the original stdout of the process is used,
        in case the regular sys.stdout is redirected. If it is false,
        an IOError is raised in case sys.stdout is redirected in some
        way.

    """
    stdout = sys.stdout
    if sys.__stdout__ is not sys.stdout:
        # stdout was redirected
        if force:
            stdout = sys.__stdout__
        else:
            raise IOError('cannot write to console - redirected ?')
    if linefeed:
        text += os.linesep
    stdout.write(text)
    stdout.flush()

def read_console(prompt='', timeout=0, force=False, timeout_optional=False):

    """ Read input from the console.

        prompt may be given to write a prompt to the console before
        waiting for the input.

        timeout defines the time to wait for input. If set to 0
        (default), no timeout is used. In case of a timeout, an
        TimeoutError is raised.

        On some platforms timeout is not supported.  A
        NotSupportedError is raised in case timeout is used on such a
        platform and timeout_optional is not set (default). With
        timeout_optional set to true, the function ignores the timeout
        on such platforms.

        force has the same meaning as for write_console(), but for
        stdin. If a prompt is to be written, force is also passed to
        the write_console() call to print the prompt.

    """
    stdin = sys.stdin
    if sys.__stdin__ is not sys.stdin:
        # stdin was redirected
        if force:
            stdin = sys.__stdin__
        else:
            raise IOError('cannot read from console - redirected ?')
    if prompt:
        write_console(prompt, linefeed=False, force=force)
    if timeout:
        try:
            import select
            readable, writeable, exceptional = select.select(
                [stdin], [], [], timeout)
        except ImportError:
            # No select support on this platform
            if not timeout_optional:
                raise NotSupportedError('read_console() timeout not supported')
            else:
                readable = True
        except select.error, reason:
            # Select did not work; this is most likely a platform
            # which doesn't allow reading from stdin or a stdin
            # emulation which is not supported by select()
            if not timeout_optional:
                raise NotSupportedError('read_console() timeout not supported')
            else:
                readable = True
        if readable:
            data = stdin.readline()
        else:
            raise TimeoutError('timeout reading from console')
    else:
        data = stdin.readline()
    return data.rstrip()

### Canonical OS and platform names support

def canonical_os_name(sys_platform=None):

    """ Return a canonical OS name string for sys_platform.

        sys_platform defaults to sys.platform if not given.

        The canonical name of sys_platform can be used to match
        sys.platform on a target system against the one included in
        e.g. a prebuilt archive.

        The canonical name is only used for systems which maintain
        platform binary compatibility between versions, otherwise
        platform is returned as-is.

        Note that this does not mean that the system libraries are the
        same on all versions of the OS, it also doesn't mean that
        binaries are compatible between processor architectures. It
        just means that the binaries compiled for previous OS versions
        on the same processor architecture can be loaded by the newer
        OS version.
        
    """
    if sys_platform is None:
        sys_platform = sys.platform
    platform = sys_platform.strip().lower()
    
    # Linux
    if platform.startswith('linux'):
        # Linux versions are binary compatible. See TBD.
        return 'linux'

    # Windows
    elif platform.startswith('win32') or platform.startswith('windows'):
        # Windows versions are (mostly) binary compatibile. See TBD.
        return 'win32'

    # Mac OS
    elif (platform.startswith('darwin')
          or platform.startswith('mac os')
          or platform.startswith('macos')):
        # Mac OS X versions are binary compatibile. See TBD.
        return 'darwin'

    # FreeBSD
    elif platform.startswith('freebsd'):
        # FreeBSD versions are binary compatible. See TBD.
        return 'freebsd'

    # OpenBSD
    elif platform.startswith('openbsd'):
        # OpenBSD versions are binary compatible. See TBD.
        return 'openbsd'

    # NetBSD
    elif platform.startswith('netbsd'):
        # NetBSD versions are binary compatible. See TBD.
        return 'netbsd'

    # AIX
    elif platform.startswith('aix'):
        # AIX 5, 6 and 7 are binary compatible.  See
        # http://www-03.ibm.com/systems/power/software/aix/compatibility/
        return 'aix'

    # HP-UX
    elif platform.startswith('hp-ux'):
        # HP-UX versions are binary compatible. See
        # https://h21007.www2.hp.com/portal/download/files/unprot/hpux/5981-7108en.pdf
        return 'hp-ux'

    # SunOS (Solaris)
    elif platform.startswith('sunos') or platform.startswith('solaris'):
        # Solaris versions are binary compatible. See
        # https://lildude.co.uk/oracle-solaris-11-compatibility-checker-tool
        # and
        # http://www.oracle.com/us/products/servers-storage/solaris/binary-app-guarantee-080255.pdf
        return 'sunos'

    # Cygwin (Unix for Windows)
    elif platform.startswith('cygwin'):
        # Current cygwin versions are binary compatibile. See TBD.
        return 'cygwin'

    # OS/2 EMX
    elif platform.startswith('os2emx'):
        # OS/2 versions are binary compatibile. See TBD.
        return 'os2emx'

    # For ones not listed here, leave the version specifier as-is,
    # since we don't know about binary compatibility.
    return platform

def compatible_os(ref_platform, this_platform=None):

    """ Return True if ref_platform is OS compatible to
        this_platform.

        ref_platform and this_platform should be standard Python
        sys.platform values.

        If this_platform is not given, it defaults to sys.platform on
        the currenlty running system.

        See canonical_os_name() for details on what this means.

    """
    return (canonical_os_name(ref_platform) ==
            canonical_os_name(this_platform))

def _test_compatible_os():

    assert compatible_os(sys.platform, sys.platform)
    assert compatible_os(sys.platform)
    assert compatible_os('linux3', 'linux2')
    assert compatible_os('linux2', 'linux3')
    assert compatible_os('linux3', 'linux4')
    assert compatible_os('freebsd8', 'freebsd9')
    assert compatible_os('aix5', 'aix6')
    assert not compatible_os('win32', 'linux3')
    # XXX There is a Linux compat layer in many BSDs, but still...
    assert not compatible_os('linux3', 'freebsd8')
    assert not compatible_os('sunos5', 'linux3')
    assert not compatible_os('aix6', 'win32')
    print ('compatible_os() works.')

if 0:
    _test_compatible_os()

def canonical_platform_name(platform=None, return_re=False):

    """ Return a canonical platform name for platform.

        platform defaults to distutils.util.get_platform() if not
        given.

        The canonical name of platform can be used to check whether a
        target system can potentially run the binaries included in
        e.g. a prebuilt archive. It includes the OS name and the
        processor name for this reason.

        If return_re is true (default is false), the function will
        return a regular expression, so you can use re.match() to
        extend the matching to multiple possible variants. This is
        useful for e.g. fat builds on Mac OS X.

        Note that the canonical platform name is just a guess whether
        there is a chance of two systems being compatible. It is still
        possible that the binaries won't run due to e.g. library
        incompatibilities or missing APIs.

    """
    if platform is None:
        platform = mx_get_platform()

    platform = platform.strip().lower()

    # Common REs
    # See http://en.wikipedia.org/wiki/Uname as reference
    processor_re = ('(i686|x86_64|x64|amd64|ia64|arm.+|sparc.+|ppc.+|'
                    'k1om|mips|i386|sun4u|i86pc|unknown|[^-]+)')

    # distutil's get_platform() uses the format osname-release-machine
    # on most platforms, but not all of them

    # Linux
    if platform.startswith('linux'):
        m = re.match('(linux).*-%s$' % processor_re, platform)
        name, processor = m.groups()
        return '%s-%s' % (name, processor)

    # Windows
    elif platform.startswith('win'):
        # distutils already returns a canonical name for Windows
        return platform

    # AIX
    elif platform.startswith('aix'):
        # AIX doesn't return a processor type; it's always powerpc
        # these days
        return 'aix'

    # Mac OS
    elif platform.startswith('mac'):
        m = re.match('(macosx)-(.+)-%s$' % processor_re, platform)
        name, version, processor = m.groups()
        # Special case for fat builds (we use our setup here, which is
        # to only do fat builds for PPC and i386): match all possible
        # processor strings
        if return_re and processor == 'fat':
            processor = '(ppc|i386|fat)'
        return '%s-%s' % (name, processor)

    # SunOS (Solaris)
    elif platform.startswith('sunos') or platform.startswith('solaris'):
        # Solaris versions are binary compatible. See
        # https://lildude.co.uk/oracle-solaris-11-compatibility-checker-tool
        # and
        # http://www.oracle.com/us/products/servers-storage/solaris/binary-app-guarantee-080255.pdf
        m = re.match('(sunos|solaris)-(.+)-%s$' % processor_re, platform)
        name, version, processor = m.groups()
        return '%s-%s' % (name, processor)

    # BSDs
    elif (platform.startswith('freebsd')
          or platform.startswith('openbsd')
          or platform.startswith('netbsd')):
        m = re.match('(freebsd|openbsd|netbsd)-(.+)-%s$' % processor_re, platform)
        name, version, processor = m.groups()
        return '%s-%s' % (name, processor)

    # HP-UX
    elif platform.startswith('hp-ux'):
        # Format unkown
        m = re.match('(hp-ux)-(.+)-%s$' % processor_re, platform)
        name, version, processor = m.groups()
        return '%s-%s' % (name, processor)
    
    # Cygwin (Unix for Windows)
    elif platform.startswith('cygwin'):
        m = re.match('(cygwin)-(.+)-%s$' % processor_re, platform)
        name, version, processor = m.groups()
        return '%s-%s' % (name, processor)
    
    # Try the generic format; assume they don't use hyphens in the OS
    # name
    m = re.match('([^-]+)-(.+)-%s$' % processor_re, platform)
    if m is not None:
        name, version, processor = m.groups()
        return '%s-%s' % (name, processor)

    # No canonical name available
    return platform

def compatible_platform(ref_platform, this_platform=None):

    """ Check whether ref_platform is OS and processor compatible with
        this_platform. The function is essential the same as
        compatible_os(), but based on the distutils
        util.get_platform() return values.

        ref_platform and this_platform should be standard Python
        distutils.util.get_platform() values.

        If this_platform is not given, it defaults to
        distutils.util.get_platform() on the currently running
        system.

        The order of the arguments is important: ref_platform may be
        compatible with multiple platforms. The function uses the RE
        variant of canonical_platform_name() to match against the
        canonical name of this_platform.

    """
    # We use the RE version for ref_platform, since this may be
    # compatible with multiple target platforms.
    m = re.match(canonical_platform_name(ref_platform, return_re=True),
                 canonical_platform_name(this_platform))
    if m is not None:
        return True
    else:
        return False

def _test_compatible_platform():
    
    for platform in (
        'linux-i686',
        'linux-x86_64',
        'linux-armv6l',
        'linux-armv7l',
        'win32',
        'win-amd64',
        'freebsd-8.3-RELEASE-p3-amd64',
        'freebsd-8.3-RELEASE-p3-i386',
        'cygwin-1.7.16-i686',
        'cygwin-1.7.16-x86_64',
        'macosx-10.4-ppc',
        'macosx-10.4-fat',
        'macosx-10.5-x86_64',
        'aix-6.1',
        'solaris-2.9-sun4u',
        ):
        assert compatible_platform(platform, platform)
    assert compatible_platform(mx_get_platform(), mx_get_platform())
    assert compatible_platform(mx_get_platform())
    assert not compatible_platform('linux-i686', 'linux-armv6l')
    assert not compatible_platform('linux-armv6l', 'linux-armv7l')
    assert not compatible_platform('linux-i686', 'linux-x86_64')
    assert not compatible_platform('win32', 'linux-i686')
    assert not compatible_platform('win32', 'win-amd64')
    assert not compatible_platform('win32', 'win-ia64')
    assert compatible_platform('macosx-10.4-fat', 'macosx-10.4-ppc')
    assert compatible_platform('macosx-10.4-fat', 'macosx-10.4-i386')
    assert not compatible_platform('macosx-10.4-fat', 'macosx-10.4-x86_64')
    assert not compatible_platform('macosx-10.4-ppc', 'macosx-10.4-x86_64')
    print ('compatible_platform() works.')

if 0:
    _test_compatible_platform()

#

def get_env_var(name, default=None, integer_value=0, yesno_value=0):

    value = os.environ.get(name, None)
    if value is None:
        return default

    # Try to convert to an integer, if possible
    if integer_value:
        try:
            return int(value)
        except ValueError:
            return default

    # Try to convert a yes/no value, if possible
    if yesno_value:
        value = value.strip().lower()
        if not value:
            return default
        if value[0] in ('y', '1'):
            return 1
        elif value == 'on':
            return 1
        else:
            return 0
        
    # Return the string value
    return value

def convert_to_platform_path(distutils_path):

    """ Convert a distutils Unix path distutils_path to the platform
        format.

    """
    if os.sep == '/':
        return distutils_path
    return os.sep.join(distutils_path.split('/'))

def convert_to_distutils_path(platform_path):

    """ Convert a platform path to the distutils Unix format.

    """
    if os.sep == '/':
        return platform_path
    return '/'.join(platform_path.split(os.sep))

def remove_path_prefix(pathname, prefix):

    """ Return a relative path by removing prefix from pathname.

        If pathname doesn't begin with prefix, no change is applied
        and pathname returned as-is.

    """
    prefix_len = len(prefix)
    if pathname[:prefix_len] != prefix:
        # pathname doesn't begin with prefix
        return pathname
    if prefix[-1] != os.sep:
        # Remove leading separator as well
        prefix_len += 1
    return pathname[prefix_len:]

def find_file(filename, paths, pattern=None):

    """ Look for a file in the directories defined in the list
        paths.

        If pattern is given, the found files are additionally checked
        to include the given RE search pattern. Pattern matching is
        done case-insensitive per default.

        Returns the directory where the file can be found or None in
        case it was not found.

        filename may include path components, e.g. if a particular
        file in a subdirectory is used as token to match the
        subdirectory.

    """
    if _debug:
        print 'looking for %s ...' % filename
        if pattern:
            print ' applying pattern check using %r' % pattern
    for dir in paths:
        pathname = os.path.join(dir, filename)
        if os.path.exists(pathname):
            if pattern:
                data = open(pathname, 'rb').read()
                if re.search(pattern, data, re.I) is None:
                    data = None
                    if _debug:
                        print ' %s: found, but not matched' % dir
                    continue
                data = None
                if _debug:
                    print ' %s: found and matched' % dir
            else:
                if _debug:
                    print ' %s: found' % dir
            return dir
        elif _debug:
            print ' %s: not found' % dir
    if _debug:
        print 'not found'
    return None

def is_python_package(path):

    """ Return 1/0 depending on whether path points to a Python
        package directory or not.

    """
    marker = '__init__' + os.extsep
    for filename in os.listdir(path):
        if filename.startswith(marker):
            return True
    return False

def python_module_name(path):

    """ Return the Python module name for the Python module path.

        Returns None if path does not point to a (potential) Python
        module.

    """
    for suffix in PY_SUFFIXES:
        if path.endswith(suffix):
            return os.path.basename(path[:-len(suffix)])
    return None

def find_python_modules(path):

    """ Find Python modules/packages available in path.

        Returns a dictionary mapping the Python module/package name
        (without extension) to either 'package' or 'module'.

    """
    d = {}
    for filename in os.listdir(path):
        pathname = os.path.join(path, filename)
        if os.path.isdir(pathname) and is_python_package(pathname):
            d[os.path.basename(filename)] = 'package'
        else:
            module_name = python_module_name(pathname)
            if module_name:
                d[module_name] = 'module'
    return d

def add_dir(dir, pathlist, index=-1):

    if dir not in pathlist and \
       os.path.isdir(dir):
        if index < 0:
            index = index + len(pathlist) + 1
        pathlist.insert(index, dir)

def py_unicode_build():

    """ Return the Python Unicode version.

        Possible values:
         'ucs2' - UCS2 build (standard Python source build)
         'ucs4' - UCS4 build (used on most recent Linux distros)
         ''     - No Unicode support

    """
    if python_version >= '2.1':
        # UCS4 builds were introduced in Python 2.1; Note: RPM doesn't
        # like hyphens to be used in the Python version string which is
        # why we append the UCS information using an underscore.
        try:
            unichr(100000)
        except NameError:
            # No Unicode support
            return ''
        except ValueError:
            # UCS2 build (standard)
            return 'ucs2'
        else:
            # UCS4 build (most recent Linux distros)
            return 'ucs4'
    else:
        return ''


def py_version(unicode_aware=None, include_patchlevel=0):

    """ Return the Python version as short string.

        If unicode_aware is true (default on all platforms except
        win32, win16, os2 and dos), the function also tests whether a
        UCS2 or UCS4 built is running and modifies the version
        accordingly.

        If include_patchlevel is true (default is false), the patch
        level is also included in the version string.

    """
    if include_patchlevel:
        version = '%i.%i.%i' % sys.version_info[:3]
    else:
        version = '%i.%i' % sys.version_info[:2]
    if unicode_aware is None:
        # Chose default for unicode_aware based on platform
        if sys.platform in ('win32', 'dos', 'win16', 'os2'):
            # These platforms always use UCS2 builds (at least for all
            # versions up until Python 2.6)
            unicode_aware = 0
        else:
            unicode_aware = 1
    if unicode_aware and version >= '2.1':
        # UCS4 builds were introduced in Python 2.1; Note: RPM doesn't
        # like hyphens to be used in the Python version string which is
        # why we append the UCS information using an underscore.
        try:
            unichr(100000)
        except ValueError:
            # UCS2 build (standard)
            version = version + '_ucs2'
        else:
            # UCS4 build (most recent Linux distros)
            version = version + '_ucs4'
    return version

def check_zope_product_version(version, version_txt):

    """ Check whether the version string version matches the
        version data in the Zope product version.txt file
        version_txt.

    """
    data = open(version_txt, 'r').read().strip()
    return data[-len(version):] == version

def verify_package_version(package, version):

    """ Check whether the Python package's __version__ matches
        the given version string.

        The first 3 version components must match,
        i.e. major.minor.patch. parse_mx_version() is used to extract
        this information from both the version string and the packages
        __version__ attribute.

        Raises a ValueError in case the versions do not match.

    """
    package_path = package.replace('.', os.sep)
    try:
        m = __import__(package_path, None, None, ['*'])
    except ImportError, reason:
        raise ImportError('Cannot find %s package: %s' % (package, reason))
    dist_version = parse_mx_version(version)
    package_version = parse_mx_version(m.__version__)
    if dist_version[:3] != package_version[:3]:
        raise ValueError('%s.__version__ mismatch: expected %s, found %s' %
                         (package, version, m.__version__))

def find_code_version(filename):

    """ Tries to extract the code version information from the file
        filename and returns it.

        The following formats are recognized (first match wins):
        - __version__ = '1.2.3'
        - #define *MX*_VERSION* "1.2.3"
        - #define *VERSION* "1.2.3"

        Raises a ValueError in case the version cannot be parsed.

    """
    file = open(filename)
    try:
        code = file.read()
    finally:
        file.close()

    # Find version string
    for version_rx in (
        "__version__ *= *'([0-9]\.[0-9][^']*)'",
        '__version__ *= *"([0-9]\.[0-9][^"]*)"',
        '#define .*MX.*_VERSION.*"([0-9]\.[0-9]\.[0-9][^"]*)"',
        '#define .*VERSION.*"([0-9]\.[0-9]\.[0-9][^"]*)"',
        ):
        match = re.search(version_rx, code)
        if match is not None:
            return match.group(1)
    else:
        raise ValueError('%s code version could not be parsed' %
                         filename)

def verify_code_version(filename, version):

    """ Check whether the file filename's version string matches
        the given version string.

        The first 3 version components must match,
        i.e. major.minor.patch. parse_mx_version() is used to extract
        this information from both the version string and the file's
        version definition.

        The following find_code_version() formats are recognized.

        Raises a ValueError in case the versions do not match.

    """
    # Find code version
    code_version_string = find_code_version(filename)

    # Compare versions
    code_version = parse_mx_version(code_version_string)
    verify_version = parse_mx_version(version)
    if code_version[:3] != verify_version[:3]:
        raise ValueError('%s version mismatch: expected %s, found %s' %
                         (filename, version, code_version_string))

def quote_string_define_value(value):

    """ Quote the value used in a -D name=value compiler define in a
        command line safe way.

        This function makes sure that the string value is quoted in
        way that survives the command line interface used for talking
        to compilers.

    """
    # Remove any existing quotes
    if ((value[:1] == '"' and value[-1:] == '"') or
        (value[:1] == "'" and value[-1:] == "'")):
        value = value[1:-1]
    elif ((value[:2] == '\\"' and value[-2:] == '\\"') or
          (value[:2] == "\\'" and value[-2:] == "\\'")):
        value = value[2:-2]

    # Setup the value formatting template
    if sys.platform[:3] == 'win':
        # Use escaped quotes that survive the VC++ command line
        # interface
        value_template = '\\"%s\\"'
    else:
        # Regular quotes for all other platforms
        value_template = '"%s"'

    return value_template % value

# Keep the symbol around for backwards compatibility, but don't use it
# anymore.  See #943.
mx_customize_compiler = customize_compiler

compression_programs = {
    'gzip': ('.gz', '-f9'),
    'bzip2': ('.bz2', 'f9'),
    'compress': ('.Z', '-f'),
    }

def mx_make_tarball(base_name, base_dir, compression='gzip', verbose=0,
                    dry_run=0, owner=None, group=None,
                    tar_options='-h', **kws):

    # Much like archive_util.make_tarball, except that this version
    # dereferences symbolic links.
    tar_archive = base_name + '.tar'

    # Create the directory for the archive
    mkpath(os.path.dirname(tar_archive), dry_run=dry_run)

    # Create the archive
    if owner:
        tar_options += ' --owner="%s"' % owner
    if group:
        tar_options += ' --group="%s"' % group
    cmd = ['tar', '-c', tar_options, '-f', tar_archive, base_dir]
    spawn(cmd, verbose=verbose, dry_run=dry_run)

    # Compress if that's needed
    if compression:
        try:
            ext, options = compression_programs[compression]
        except KeyError:
            raise ValueError('unknown compression program: %s' % compression)
        cmd = [compression, options, tar_archive]
        spawn(cmd, verbose=verbose, dry_run=dry_run)
        tar_archive = tar_archive + ext
        
    return tar_archive

# Register our version of make_tarball()
register_archive_formats = {
    'gztar': (mx_make_tarball, [('compression', 'gzip')], 'gzipped tar-file'),
    'bztar': (mx_make_tarball, [('compression', 'bzip2')], 'bzip2ed tar-file'),
    'ztar':  (mx_make_tarball, [('compression', 'compress')], 'compressed tar file'),
    'tar':   (mx_make_tarball, [('compression', None)], 'tar file'),
    }
if not hasattr(shutil, 'register_format'):
    # In Python <2.7, 3.0 and 3.1, we have to register straight with
    # the distutils ARCHIVE_FORMATS dictionary
    distutils.archive_util.ARCHIVE_FORMATS.update(register_archive_formats)
else:
    # Python 2.7+ and 3.2+ use the new shutil archive functions instead
    # of the ones from distutils, so register our archives there
    for archive_name, archive_params in register_archive_formats.items():
        shutil.register_format(archive_name, *archive_params)

def build_path(dirs):

    """ Builds a path list from a list of directories/paths.

        The dirs list may contain shell variable references and user
        dir references. These will get expanded
        automatically. Non-existing shell variables are replaced with
        an empty string. Path entries will get expanded to single
        directory entries.  Empty string entries are removed from the
        list.

    """
    try:
        expandvars = os.path.expandvars
    except AttributeError:
        expandvars = None
    try:
        expanduser = os.path.expanduser
    except AttributeError:
        expanduser = None
    path = []
    for i in range(len(dirs)):
        dir = dirs[i]
        if expanduser is not None:
            dir = expanduser(dir)
        if expandvars is not None:
            dir = expandvars(dir)
            if '$' in dir:
                dir = ''.join(re.split(r'\$\w+|\{[^}]*\}', dir))
        dir = dir.strip()
        if os.pathsep in dir:
            path.extend(dir.split(os.pathsep))
        elif dir:
            path.append(dir)
        # empty entries are omitted
    return path

def verify_path(path):

    """ Verify the directories in path for existence and their
        directory nature.

        Also removes duplicates from the list.

    """
    d = {}
    l = []
    for dir in path:
        if os.path.exists(dir) and \
           os.path.isdir(dir):
            if not d.has_key(dir):
                d[dir] = 1
                l.append(dir)
    path[:] = l

def get_msvc_paths():

    """ Return a tuple (libpath, inclpath) defining the search
        paths for library files and include files that the MS VC++
        compiler uses per default.

        Both entries are lists of directories.

        Only available on Windows platforms with installed compiler.

    """
    if python_version >= '2.6':
        # Python 2.6 distutils

        # If possible, assume that the environment is already
        # properly setup and read the setting from there - this is
        # important since we could be cross-compiling
        if os.environ.has_key('lib') and os.environ.has_key('include'):
            libpath = os.environ['lib'].split(os.pathsep)
            inclpath = os.environ['include'].split(os.pathsep)
        else:
            # VC 9.0 and later no longer appear to set the registry
            # entries used by .get_msvc_paths(). See #1619.
            log.info('no compiler environment found')
            libpath = []
            inclpath = []

    elif python_version >= '2.3':
        # Python 2.3 - 2.5 distutils
        try:
            msvccompiler = MSVCCompiler()
            inclpath = msvccompiler.get_msvc_paths('include')
            libpath = msvccompiler.get_msvc_paths('library')
        except Exception, why:
            log.error('*** Problem: %s' % why)
            import traceback
            traceback.print_exc()
            libpath = []
            inclpath = []
        msvccompiler = None

    else:
        # distutils versions prior to the one that came with Python 2.3
        from distutils.msvccompiler import get_devstudio_versions, get_msvc_paths
        msvc_versions = get_devstudio_versions()
        if msvc_versions:
            msvc_version = msvc_versions[0] # choose most recent one
            inclpath = get_msvc_paths('include', msvc_version)
            libpath = get_msvc_paths('lib', msvc_version)
        else:
            libpath = []
            inclpath = []

    return libpath, inclpath

#
# Search paths
#
        
# Standard system directories which are automatically scanned by the
# compiler and linker for include files and libraries. LIB and INCLUDE
# are environment variables used on Windows platforms, other platforms
# may have different names.
STDLIBPATH = build_path(['/usr/lib', '/opt/lib', '$LIB'])
STDINCLPATH = build_path(['/usr/include', '/opt/include', '$INCLUDE'])

# Add additional dirs from Windows registry if available
if sys.platform[:3] == 'win':
    libpath, inclpath = get_msvc_paths()
    STDLIBPATH.extend(libpath)
    STDINCLPATH.extend(inclpath)

# Default paths for additional library and include file search (in
# addition to the standard system directories above); these are always
# added to the compile and link commands by mxSetup per default.
LIBPATH = build_path(['/usr/local/lib',
                      '/opt/local/lib',
                      os.path.join(sys.prefix, 'lib')])
INCLPATH = build_path(['/usr/local/include',
                       '/opt/local/include',
                       os.path.join(sys.prefix, 'include')])

# Add 32- or 64-bit dirs if needed by the Python version
if sys.maxint > 2147483647L:
    # 64-bit build
    STDLIBPATH.extend(['/usr/lib64', '/opt/lib64'])
    LIBPATH.extend(['/usr/local/lib64', '/opt/local/lib64'])
else:
    # 32-bit build
    STDLIBPATH.extend(['/usr/lib32', '/opt/lib32'])
    LIBPATH.extend(['/usr/local/lib32', '/opt/local/lib32'])

# Additional paths to scan in order to find third party libs and
# headers; these are used by mx_autoconf.find_*_file() APIs.
FINDLIBPATH = build_path([])
FINDINCLPATH = build_path([])

verify_path(STDLIBPATH)
verify_path(STDINCLPATH)
verify_path(LIBPATH)
verify_path(INCLPATH)
verify_path(FINDLIBPATH)
verify_path(FINDINCLPATH)

if 0:
    # Work-around for quirk on Solaris which happens to be a common
    # problem when compiling things with GCC: there's a non-GCC stdarg.h
    # header file in /usr/include which gets picked up by GCC instead of
    # its own compiler specific one, so we remove /usr/include from
    # INCLPATH in that situation.
    if sys.platform == 'sunos5' and \
       sys.version.find('GCC') >= 0:
        if os.path.exists('/usr/include/stdarg.h'):
            INCLPATH.remove('/usr/include')

#
# File extensions
#

# Library types to search for (see distutils.ccompiler)
if sys.platform == 'darwin':
    # Mac OS X uses .dylibs in addition to .so files, so we need to
    # search for those as well
    #
    # Note that only the distutils unixcompiler supports searching for
    # dylib, other compiler classes will simply exist with an
    # AttributeError
    LIB_TYPES = ('dylib', 'shared', 'static')
    
else:
    # These types are supported by all compiler classes
    LIB_TYPES = ('shared', 'static')


if _debug > 1:
    # Note that printing these lines to stdout can cause scripts that
    # use mxSetup for extracting information from the setup module to
    # fail, since they don't expect the extra output. This is why we
    # only show this information for higher _debug levels.
    print 'mxSetup will be using these search paths:'
    print ' std lib path:', STDLIBPATH
    print ' std include path:', STDINCLPATH
    print ' additional lib path:', LIBPATH
    print ' additional include path:', INCLPATH
    print ' additional autoconf lib path:', FINDLIBPATH
    print ' additional autoconf include path:', FINDINCLPATH
    print ' library types:', LIB_TYPES
    print ' Python include path: %r' % get_python_include_dir()

### Download tools

def fetch_url(url, timeout=30):

    """ Fetch the given url and return the data as file-like object.

        The function can raise urllib2.URLError exceptions in case of
        an error.

    """
    import urllib2
    if python_version >= '2.6':
        data_file = urllib2.urlopen(url, None, timeout)
    elif python_version >= '2.3':
        import socket
        socket.setdefaulttimeout(timeout)
        data_file = urllib2.urlopen(url)
    else:
        # Ignore timeout
        data_file = urllib2.urlopen(url)
    return data_file

def read_url(url, timeout=30):

    """ Fetch the given url and return the data as string.

        The function can raise urllib2.URLError exceptions in case of
        an error.

    """
    data_file = fetch_url(url, timeout)
    data = data_file.read()
    data_file.close()
    return data

def copy_file(source, target, chunksize=65000, close_files=True):

    """ Copy data from source to target.

        chunksize may be given to determine the chunk size used for
        the copy operation.

        If close_files is true (default), close the files after the
        copying operation.

    """
    try:
        while True:
            data = source.read(chunksize)
            if not data:
                break
            target.write(data)
    finally:
        if close_files:
            source.close()
            target.close()

def download_url(url, timeout=30, target=None, path=None,
                 chunksize=65000, index_name='index.html'):

    """ Fetch the given url and write the data to target.

        Returns the target pathname.

        target defaults to the filename given in the url, if not
        provided.

        If path is given, the path is joined with the target filename
        to build the output filename.

        chunksize may be given to determine the chunk size used for
        downloading the file.

        index_name is used as target filename for directory downloads
        (defaults to 'index.html').

    """
    # Determine target filename, if not given
    if not target:
        import urlparse, posixpath
        parsed_url = urlparse.urlparse(url)
        dirs, target = posixpath.split(parsed_url.path)
    if path is not None:
        target = os.path.join(path, target)

    # Make sure that the parent dirs exist
    target_dir = os.path.dirname(target)
    if target_dir and not os.path.exists(target_dir):
        os.makedirs(target_dir)

    # If target is a directory, use the index_name as target name
    if os.path.isdir(target):
        target = os.path.join(target, index_name)

    # Fetch the data in chunks and write it to target
    data_file = fetch_url(url, timeout)
    target_file = open(target, 'wb')
    copy_file(data_file, target_file, chunksize)
    data_file.close()
    target_file.close()
    return target

def hashed_download_url(download_url, mode='simple', data=None, download=None):

    """ Add a hash marker to the download_url.

        This is done by fetching the URL, creating
        one or more hash/size entries and appending these
        as fragment to the download_url.

        mode determines how the fragment is formatted. Possible
        values:
        
        * 'simple': add #md5=...
        
          This is compatible with setuptools/easy_install/pip.

        * 'pip': add #sha256=...

          This is compatible with pip only. setuptools/easy_install
          don't support sha256 hashes.

        * 'extended': add #md5=...&sha1=...&sha256=...&size=...

          This is not yet supported by the installers tools, but
          provides the most advanced hash tag format. sha256 is only
          included if supported by the Python version.

        data or download may be given to avoid having the function
        fetch the URL. data must be the raw data chunk, download a
        filename to read the data from, if given. If both are given,
        download takes precedence.

    """
    # Determine hash functions to use
    if python_version >= '2.5':
        import hashlib
        md5 = hashlib.md5
        sha1 = hashlib.sha1
        sha256 = hashlib.sha256
    else:
        import md5, sha
        md5 = md5.md5
        sha1 = sha.sha
        sha256 = None

    # Strip existing hash tags
    if '#' in download_url:
        download_url = download_url.split('#')[0]

    # Get data to hash
    if download is not None:
        file = open(download, 'rb')
        data = file.read()
        file.close()
    elif data is None:
        data = read_url(download_url)

    # Format hashed URL
    if mode == 'simple':
        return '%s#md5=%s' % (
            download_url,
            md5(data).hexdigest())
    elif mode == 'pip':
        if sha256 is None:
            raise TypeError('Python version does not support SHA-256')
        return '%s#sha256=%s' % (
            download_url,
            sha256(data).hexdigest())
    elif mode == 'extended':
        if sha256 is not None:
            return '%s#md5=%s&sha1=%s&sha256=%s&size=%i' % (
                download_url,
                md5(data).hexdigest(),
                sha1(data).hexdigest(),
                sha256(data).hexdigest(),
                len(data))
        else:
            return '%s#md5=%s&sha1=%s&size=%i' % (
                download_url,
                md5(data).hexdigest(),
                sha1(data).hexdigest(),
                len(data))

def verify_hashed_url_download(url, download=None, data=None):

    """ Verify a hashed URL url based on the downloaded content.

        Either download or data must be given. download must point to
        a downloaded file, data to the raw data block to verify.

        Raises a ValueError in case the content does not verify, no
        hash tag can be found or the hash tag uses an unknown format.

    """
    if download is not None:
        file = open(download, 'rb')
        data = file.read()
        file.close()
    elif data is None:
        raise TypeError('Either download or data must be given')
    if python_version >= '2.5':
        import hashlib
        md5 = hashlib.md5
        sha1 = hashlib.sha1
        sha256 = hashlib.sha256
    else:
        import md5, sha
        md5 = md5.md5
        sha1 = sha.sha
        sha256 = None
    l = url.rsplit('#')
    if len(l) < 2:
        raise ValueError('No URL hashtag found')
    hashtag = l[1]
    components = hashtag.split('&')
    for component in hashtag.split('&'):
        try:
            function, value = component.split('=')
        except ValueError:
            raise ValueError('Hash tag component %r has illegal format' %
                             component)
        if function == 'md5':
            if md5(data).hexdigest() != value:
                raise ValueError('MD5 hash tag does not match')
        elif function == 'sha1':
            if sha1(data).hexdigest() != value:
                raise ValueError('SHA1 hash tag does not match')
        elif function == 'sha256' and sha256 is not None:
            if sha256(data).hexdigest() != value:
                raise ValueError('SHA256 hash tag does not match')
        elif function == 'size':
            if len(data) != int(value):
                raise ValueError('Size does not match')
        else:
            raise ValueError('Unknown function in component %r' %
                             component)

def build_archive_name_map(names, target_path, strip_path=0):

    """ Return a dictionary mapping archive name entries from names to
        local file names based at target_path.

        The function applies security checks to make sure that the
        local names do not map to directories outside the target_path.

        strip_path may be given as integer to indicate how many
        leading path entries from the archive names to strip when
        building the local names.

    """
    name_map = {}
    pathsep = os.path.sep
    for name in names:
        if strip_path:
            local_name = pathsep.join(name.split('/')[strip_path:])
        else:
            local_name = name.replace('/', pathsep)
        if not local_name:
            continue
        target_name = os.path.normpath(os.path.join(target_path, local_name))
        if not target_name.startswith(target_path):
            raise ValueError(
                'Illegal archive entry %r - would map to local %r' %
                (name, target_name))
        name_map[name] = target_name
    return name_map

def unzip_file(file, path=None, strip_path=0):

    """ Unzip the given file to the path directory.

        If path is not given, the current work directory is used.

    """
    import zipfile

    # Determine target dir
    if path is None:
        path = os.getcwd()
    target_path = os.path.abspath(path)

    # Open ZIP file and extract names
    zf = zipfile.ZipFile(file, 'r')
    names = zf.namelist()

    # Build name map
    name_map = build_archive_name_map(names,
                                      target_path,
                                      strip_path=strip_path)

    # Extract files (Python 2.4 compatible, 2.6+ could use .open()
    # and .extractall(), but they don't exist in Python 2.4 and 2.5)
    for name in names:
        target_name = name_map.get(name, None)
        if target_name is None:
            continue
        # Make sure that the parent dirs exist
        target_dir = os.path.dirname(target_name)
        if target_dir and not os.path.exists(target_dir):
            os.makedirs(target_dir)
        target_file = open(target_name, 'wb')
        target_file.write(zf.read(name))
        target_file.close()
    zf.close()

def untar_file(file, path=None, strip_path=0):

    """ Untar the given file to the path directory.

        If path is not given, the current work directory is used.

    """
    import tarfile

    # Determine target dir
    if path is None:
        path = os.getcwd()
    target_path = os.path.abspath(path)

    # Open tar file and extract names
    tf = tarfile.open(file, 'r')
    names = tf.getnames()

    # Build name map
    name_map = build_archive_name_map(names,
                                      target_path,
                                      strip_path=strip_path)

    # Extract files (Python 2.4 compatible, 2.6+ could use .open()
    # and .extractall(), but they don't exist in Python 2.4 and 2.5)
    for name in names:
        target_name = name_map.get(name, None)
        if target_name is None:
            continue
        # Make sure that the parent dirs exist
        target_dir = os.path.dirname(target_name)
        if target_dir and not os.path.exists(target_dir):
            os.makedirs(target_dir)
        data_file = tf.extractfile(name)
        if data_file is None:
            # Probably a directory entry
            continue
        data = data_file.read()
        data_file.close()
        target_file = open(target_name, 'wb')
        target_file.write(data)
        target_file.close()
    tf.close()

def install_web_package(url, destdir):

    # Strip hash tags from url to not confuse Python 2.6
    if '#' in url:
        verify = True
        file_url = url.split('#', 1)[0]
    else:
        verify = False
        file_url = url

    # Download the package into the destdir
    log.info('downloading web package %s' % file_url)
    pkg_file = download_url(file_url, path=destdir)

    # Verify the download
    if verify:
        log.info('verifying web package %s' % pkg_file)
        try:
            verify_hashed_url_download(url, download=pkg_file)
        except ValueError:
            raise DistutilsError('downloaded package file does not verify: '
                                 '%s' % pkg_file)
        if _debug:
            print ('downloaded package %s verifies ok' % file_url)

    # Unzip the package into the destdir (in place)
    log.info('extracting web package %s into %s' % (pkg_file, destdir))
    import zipfile, tarfile
    if zipfile.is_zipfile(pkg_file):
        if _debug:
            print ('unzipping downloaded package %s' % pkg_file)
        unzip_file(pkg_file, destdir, strip_path=1)
    elif tarfile.is_tarfile(pkg_file):
        if _debug:
            print ('untaring downloaded package %s' % pkg_file)
        untar_file(pkg_file, destdir, strip_path=1)
    else:
        raise DistutilsError('unsupported package file format: %r' %
                             pkg_file)
    if _debug:
        print ('extracted package into %s' % destdir)

    # Remove the downloaded ZIP file
    os.remove(pkg_file)

# Tests

def _test_hashed_download_url():
    for url in (
        'file:///home/lemburg/projects/distribution/index/ucs2/egenix-pyopenssl/0.13.0_1.0.1c_1/index.html',
        'http://downloads.egenix.com/python/index/ucs2/egenix-pyopenssl/0.13.0_1.0.1c_1/',
        'http://downloads.egenix.com/python/index/ucs2/egenix-pyopenssl/0.13.0_1.0.1c_1/index.html',
        'https://downloads.egenix.com/python/index/ucs2/egenix-pyopenssl/0.13.0_1.0.1c_1/',
        'https://downloads.egenix.com/python/index/ucs2/egenix-pyopenssl/0.13.0_1.0.1c_1/index.html',
        ):
        for mode in (
            'simple',
            'pip',
            'extended'):
            hurl = hashed_download_url(url, mode)
            print (hurl)
            target = download_url(url, path='test-hashed-download')
            verify_hashed_url_download(hurl, download=target)
            assert hurl == hashed_download_url(url, mode, download=target)

def _test_download_url():

    for url in (
        'https://downloads.egenix.com/python/egenix-mx-base-3.2.7-py2.7_ucs2-linux-x86_64-prebuilt.zip',
        ):
        target = download_url(url)
        unzip_file(target, 'test-zip-file/')
        unzip_file(target, 'test-stripped-zip-file/', strip_path=1)

if 0:    
    # Tests
    _test_hashed_download_url()
    #_test_download_url()

### Package platform matching

def normalize_tags(tags):

    """ Normalize tags and return the normalized set.

        Removes empty and duplicate entries, makes sure that all tags
        are lower case.

    """
    # Create tag set, strip and convert to lower case
    tag_set = set(tag.strip().lower()
                  for tag in tags)

    # Remove empty tag
    tag_set.discard('')

    # Return set as list
    return tags

def write_package_data(datafile, data, base_url=None, replace=False,
                       add_hash_tag=False, hash_tag_mode='extended',
                       base_url_mode='join'):

    """ Write the entries url: tags from the data dictionary to the
        datafile, possibly replacing already existing entries.

        For each data entry url: tags, url has to be a URL or filename
        which refers to the package file and tags has to be a set or
        list of string tags associated with the package.
        
        base_url may be given to set a base URL. base_url_mode defines
        how the base_url is combined with the URLs in the data
        dictionary.  Default is to 'join' the the base_url with these
        urls. In this mode, the parameter has no effect if the URLs in
        the data dictionary already are already absolute URLs. In
        'prepend' mode, the base_url is prepended to the urls using
        base_url + url. In 'template' more, the base_url defines a
        template which is applied to the urls using base_url % url.

        replace may be set to True to have the function
        unconditionally overwrite a possibly existing datafile with
        the given new data, removing any old data entries.

        If add_hash_tag is true (default is false), the function will
        add hash tags to the urls. hash_tag_mode defines the hash tag
        mode to use for this. Note that failing downloads will result
        in DistutilsError to be raised.
        
    """
    if os.path.exists(datafile) and not replace:
        package_data = read_package_data(datafile)
    else:
        package_data = {}
    for url, tags in data.iteritems():
        if base_url is not None:
            if base_url_mode == 'join':
                url = urlparse.urljoin(base_url, url)
            elif base_url_mode == 'prepend':
                url = base_url + url
            elif base_url_mode == 'template':
                try:
                    url = base_url % url
                except TypeError, reason:
                    raise TypeError('problem formatting url from %r: %s' %
                                    (base_url, reason))
            else:
                raise TypeError('unknown base_url_mode: %r' % base_url_mode)
        if add_hash_tag:
            try:
                url = hashed_download_url(url, hash_tag_mode)
            except urllib2.URLError, reason:
                raise DistutilsError(
                    'could not generate hash for download URL %s' %
                    url)
        package_data[url] = normalize_tags(tags)

    # Write data to datafile
    f = open(datafile, 'w')
    items = package_data.items()
    items.sort()
    for urlkey, urltags in items:
        # When making changes to the format, also update
        # read_package_data()
        f.write('%s -> %s\n' % (urlkey, ', '.join(urltags)))
    f.close()

def write_package_tags(package_file, tags):

    """ Write the tag file for package package_file.

        Returns the tag file name.

    """
    tag_file = package_file + '.tags'
    url = os.path.split(package_file)[1]
    log.info('writing package tag file %s' % tag_file)
    write_package_data(tag_file, {url: tags})
    return tag_file

def read_package_data(datafile, remove_base_url=''):

    """ Open datafile, read the data dictionary and return it.

        The dictionary entries will have the format url: tags, mapping
        URLs to sets of tag strings. See write_package_data() for
        details.

        If remove_base_url is given, prefixes of this form ar removed
        from the urls in the data dictionary.

    """
    f = open(datafile, 'r')
    chunk = f.read()
    f.close()
    data = {}
    len_remove_base_url = len(remove_base_url)
    for line in chunk.splitlines():
        line = line.strip()
        if not line or line[0] == '#':
            continue
        url, tagstr = line.split(' -> ')
        tags = normalize_tags(tagstr.split(', '))
        if len_remove_base_url and url.startswith(remove_base_url):
            url = url[len_remove_base_url:]
        data[url] = tags
    return data

def read_package_tags(package_file, remove_base_url=''):

    """ Read the tag file for package package_file.

        See read_package_data() for details on the returned format and
        parameters.

    """
    tag_file = package_file + '.tags'
    return read_package_data(tag_file, remove_base_url=remove_base_url)

def _filter_packages(package_data, tag):

    """ Return a package_data dictionary filtered by tag.

        Only packages which have the tag set are included in the
        returned dictionary.

    """
    return dict(
        (url, tags)
        for (url, tags) in package_data.iteritems()
        if tag in tags)

def _subset_packages(package_data, tags):

    """ Return a package_data dictionary subset with packages that
        have at least one of the given tags.

    """
    if not isinstance(tags, set):
        tags_set = set(tags)
    return dict(
        (url, tags)
        for (url, tags) in package_data.iteritems()
        if tags_set.intersection(tags))

def _setminus_packages(package_data, tags):

    """ Return a package_data dictionary subset with packages that
        have none of the given tags.

    """
    if not isinstance(tags, set):
        tags_set = set(tags)
    return dict(
        (url, tags)
        for (url, tags) in package_data.iteritems()
        if not tags_set.intersection(tags))

def _find_source_package(package_data):

    """ Find a suitable source package and return its URL.

        Raises a KeyError if no package can be found.
    
    """
    source_packages = _filter_packages(package_data, 'source')
    if _debug:
        print ('filtered to source packages: %r' % source_packages)
    if not source_packages:
        raise KeyError(
            'No suitable package found for the current platform')

    if len(source_packages) > 1:
        # Prefer compiled faster code, if available
        compiler_packages = _filter_packages(source_packages,
                                             'compiler')
        if compiler_packages:
            source_packages = compiler_packages
            if _debug:
                print ('filtered to compiler packages: %r' %
                       compiler_packages)

    # Choose one of the available source packages (by sorting and
    # picking the last one to make the choice deterministic; this will
    # prefer zip files over tar files)
    package_list = sorted(source_packages.iteritems())
    package = package_list[-1]
    if _debug:
        print ('found source package: %r' % (package,))
    return package[0]

## Hardware information helpers

# Example cpuinfo output:
#
# processor       : 0
# model name      : ARMv6-compatible processor rev 7 (v6l)
# BogoMIPS        : 2.00
# Features        : half thumb fastmult vfp edsp java tls
# CPU implementer : 0x41
# CPU architecture: 7
# CPU variant     : 0x0
# CPU part        : 0xb76
# CPU revision    : 7
#
# Hardware        : BCM2708
# Revision        : 000f
# Serial          : 00000000c8cc66df
#
# There is one processor block per core.
#

def parse_cpuinfo(default=None, cpuinfo_text=''):

    """ Return the parsed /proc/cpuinfo output or default (None) in
        case the data is not available or cannot be parsed.

        The cpuinfo is parsed into a list of block dictionaries, each
        mapping key to value.

        cpuinfo may also be passed in as text via the cpuinfo_text
        parameter. This may be useful on systems which keep the
        information elsewhere or for testing.

    """
    if not cpuinfo_text:
        try:
            f = open('/proc/cpuinfo', 'rb')
            data = f.read()
            f.close()
        except IOError:
            return default
    else:
        data = cpuinfo_text

    # Parse the file and look for field
    blocks = []
    block = {}
    for line in data.splitlines():
        line = line.strip()
        if not line:
            # New block
            block = {}
            blocks.append(block)
            continue
        if _debug > 2:
            print ('parsing line %r' % line)
        try:
            field_name, field_value = line.split(':', 1)
        except ValueError:
            # Ignore the line
            continue
        field_name = field_name.strip()
        field_value = field_value.strip()
        block[field_name] = field_value
    if not block:
        # Remove trailing empty block
        del blocks[-1]
    return blocks

def cpuinfo_hardware(default=None):

    """ Try to identify the hardware by reading /proc/cpuinfo

        Returns the Hardware or model name field entry, or return
        default (None) in case the information is not available or
        cannot be parsed.

        Note: The Hardware field only appears to be available on ARM
        or other SoC systems. Perhaps it's even an addition only
        available on Raspbian, the Raspi Linux OS.
    
    """
    cpuinfo = parse_cpuinfo(default)
    if cpuinfo is default:
        return cpuinfo

    # First look for 'Hardware' key
    for block in cpuinfo:
        hardware = block.get('Hardware', None)
        if hardware is not None:
            return hardware

    # Fall back to the 'model name' key if no Hardware key is found.
    for block in cpuinfo:
        hardware = block.get('model name', None)
        if hardware is not None:
            return hardware

    return default

def raspi_version():

    """ Return the Raspberry Pi version or None, in case
        this cannot be identified or this system is not a
        Raspberry Pi.

        Currently Rapis version 1 and 2 are supported.

    """
    # Check CPU hardware
    hardware = cpuinfo_hardware()
    if hardware is None:
        return None

    # Check CPU type
    import platform
    machine = platform.machine()

    # See #1615 for details
    if _debug > 2:
        print ('machine = %r, hardware = %r' % (machine, hardware))
    if machine == 'armv6l' and hardware == 'BCM2708':
        return 1
    elif machine == 'armv7l' and hardware == 'BCM2709':
        return 2
    return None

def _test_raspi_version():
    global parse_cpuinfo

    # Mock a Raspi 1
    orig_parse_cpuinfo = parse_cpuinfo
    def parse_cpuinfo(default=None, cpuinfo_text=''):
        return orig_parse_cpuinfo(
            default=default,
            cpuinfo_text="""
processor       : 0
model name      : ARMv6-compatible processor rev 7 (v6l)
BogoMIPS        : 2.00
Features        : half thumb fastmult vfp edsp java tls
CPU implementer : 0x41
CPU architecture: 7
CPU variant     : 0x0
CPU part        : 0xb76
CPU revision    : 7

Hardware        : BCM2708
Revision        : 000f
Serial          : 00000000c8cc66df
            """.strip())

    import platform
    orig_platform_machine = platform.machine
    def machine():
        return 'armv6l'
    platform.machine = machine
    
    assert raspi_version() == 1

    # Mock Raspi2
    def parse_cpuinfo(default=None, cpuinfo_text=''):
        return orig_parse_cpuinfo(
            default=default,
            cpuinfo_text="""
processor       : 0
model name      : ARMv7 Processor rev 5 (v7l)
BogoMIPS        : 38.40
Features        : half thumb fastmult vfp edsp neon vfpv3 tls vfpv4 idiva idivt vfpd32 lpae evtstrm
CPU implementer : 0x41
CPU architecture: 7
CPU variant     : 0x0
CPU part        : 0xc07
CPU revision    : 5

processor       : 1
model name      : ARMv7 Processor rev 5 (v7l)
BogoMIPS        : 38.40
Features        : half thumb fastmult vfp edsp neon vfpv3 tls vfpv4 idiva idivt vfpd32 lpae evtstrm
CPU implementer : 0x41
CPU architecture: 7
CPU variant     : 0x0
CPU part        : 0xc07
CPU revision    : 5

processor       : 2
model name      : ARMv7 Processor rev 5 (v7l)
BogoMIPS        : 38.40
Features        : half thumb fastmult vfp edsp neon vfpv3 tls vfpv4 idiva idivt vfpd32 lpae evtstrm
CPU implementer : 0x41
CPU architecture: 7
CPU variant     : 0x0
CPU part        : 0xc07
CPU revision    : 5

processor       : 3
model name      : ARMv7 Processor rev 5 (v7l)
BogoMIPS        : 38.40
Features        : half thumb fastmult vfp edsp neon vfpv3 tls vfpv4 idiva idivt vfpd32 lpae evtstrm
CPU implementer : 0x41
CPU architecture: 7
CPU variant     : 0x0
CPU part        : 0xc07
CPU revision    : 5

Hardware        : BCM2709
Revision        : 1a01040
            """.strip())
    def machine():
        return 'armv7l'
    platform.machine = machine

    assert raspi_version() == 2

    # Undo mocking
    parse_cpuinfo = orig_parse_cpuinfo
    platform.machine = orig_platform_machine

if 0:
    _test_raspi_version()

## Platform tags support
    
def platform_tags():

    """ Determine the platform parameters of the currently running
        Python script.

        Returns a tuple (os_name, code_type, alt_code_types,
        python_version_tag, ucs_version_tag) with the found information.

        os_name and code_types are set to 'unknown-os' and
        'unknown-code-type' in case the details cannot be determined.
        ucs_version_tag is only set for Python versions where this
        matters and left set to '' otherwise.  alt_code_types is a
        tuple of additional code types supported by packages compiled
        on this system, e.g. if the system supports fat builds.
        
    """
    # Determine bitsize used by Python (not necessarily the same as
    # the one used by the platform)
    import struct
    bits = struct.calcsize('P') * 8

    # Determine OS name and code type
    import platform
    machine = platform.machine()
    os_name = 'unknown-os'
    code_type = 'unknown-code-type'
    alt_code_types = []
    if sys.platform.startswith('linux'):
        os_name = 'linux'
        if machine in ('x86_64', 'i686', 'i586', 'i386'):
            if bits == 32:
                code_type = 'x86'
            elif bits == 64:
                code_type = 'x64'
        elif machine.startswith('arm'):
            raspi = raspi_version()
            if raspi is None:
                # Not a Raspi
                pass
            elif raspi == 1:
                code_type = 'raspi'
                # Raspi1 code also runs on Raspi2
                alt_code_types.append('raspi2')
            elif raspi == 2:
                # Rapis2 code only runs on Rapis2; since this code
                # will only be included in newer web installers,
                # restricting the platforms should not affect older
                # releases.
                code_type = 'raspi2'

    elif sys.platform.startswith('freebsd'):
        os_name = 'freebsd'
        if machine == 'arm':
            # Most likely running on Raspi
            code_type = 'raspi'
        elif machine in ('amd64', 'i386'):
            # Intel processor
            if bits == 32:
                code_type = 'x86'
            elif bits == 64:
                code_type = 'x64'

    elif sys.platform.startswith('win32'):
        os_name = 'windows'
        # XXX I guess we should also include ARM platforms here
        if bits == 32:
            code_type = 'x86'
        elif bits == 64:
            code_type = 'x64'
           
    elif sys.platform.startswith('darwin'):
        os_name = 'macosx'
        if machine.startswith('Power'):
            if bits == 32:
                code_type = 'ppc'
            elif bits == 64:
                code_type = 'ppc64'
        elif machine == 'i386':
            if bits == 32:
                code_type = 'x86'
            elif bits == 64:
                code_type = 'x64'
        # For fat Python builds on Mac OS X, add additional code_types
        # to alt_code_types
        cflags = get_config_var('CFLAGS')
        if cflags is not None:
            # Should always be defined
            if '-arch ppc' in cflags and code_type != 'ppc':
                alt_code_types.append('ppc')
            if '-arch ppc64' in cflags and code_type != 'ppc64':
                alt_code_types.append('ppc64')
            if '-arch i386' in cflags and code_type != 'i386':
                alt_code_types.append('x86')
            # XXX What about x64 builds ?
    alt_code_types = tuple(alt_code_types)

    # Determine Python version
    python_version_tag = 'py%i.%i' % sys.version_info[:2]
    if sys.version_info < (3, 3):
        if sys.maxunicode > 66000:
            ucs_version_tag = 'ucs4'
        else:
            ucs_version_tag = 'ucs2'
    else:
        ucs_version_tag = ''

    return (os_name, code_type, alt_code_types, python_version_tag, ucs_version_tag)

## Tag matching support

def find_matching_package(package_data, required_tags=()):

    """ Find a package matching the current platform and return the
        URL for this.

        package_data has to be dictionary mapping URLs to sets of
        tags identifying the platform for which the URLs are best
        suited.

        required_tags may be given to define a list/set of supported
        tags, one of which packages must include. The available
        packages are subset filtered using this list/set.

        Valid tags:

         * 'prebuilt' - package is a prebuilt archive
         * 'sdist' - package is a source distribution
         * 'msi' - package is an MSI installer
         * 'egg' - package is an egg package
         * 'webinstaller' - package is a web installer

         * 'linux' - package works on Linux
         * 'windows' - package works on Windows
         * 'freebsd' - package works on FreeBSD
         * 'macosx' - package works on Mac OS X
         * 'anyos' - package works on any OS

         * 'x86' - Intel 32-bit code
         * 'x64' - Intel 64-bit code
         * 'ppc' - Power-PC 32-bit code
         * 'ppc64' - Power-PC 64-bit code
         * 'raspi' - Raspberry Pi 1 (ARMv6) 32-bit code
         * 'raspi2' - Raspberry Pi 2 (ARMv7) 32-bit code
         * 'purepython' - Pure Python code

         * 'pyX.X' - Python X.X compatible
         * 'ucs2' - Python UCS2 build compatible
         * 'ucs4' - Python UCS4 build compatible

         * 'source' - Source code
         * 'compiler' - C compiler needed

         * 'tar' - package is a tar file (may be compressed)
         * 'zip' - package is a zip file

        Raises a KeyError if no suitable package can be found.

    """
    # Determine platform parameters of the Python version that's
    # currently running
    (os_name,
     code_type,
     alt_code_types,
     python_version_tag,
     ucs_version_tag) = platform_tags()
    if _debug:
        print ('Running package matching algorithm on %r' % package_data)

    # Init: filter by required tags
    if required_tags:
        package_data = _subset_packages(package_data,
                                        required_tags)
        if _debug > 1:
            print ('filtered by required tags (%s): %r' %
                   (', '.join(required_tags), package_data))
        
    # First step: filter by OS
    os_packages = _filter_packages(package_data, os_name)
    if _debug > 1:
        print ('filtered to OS (%s): %r' %
               (os_name, os_packages))
    if not os_packages:
        os_packages = _subset_packages(package_data, ('anyos', 'purepython'))
        if _debug > 1:
            print ('filtered to OS (anyos, purepython): %r' %
                   (os_packages,))
    if not os_packages:
        if _debug > 1:
            print ('no suitable OS package found, '
                   'trying source code')
        return _find_source_package(package_data)

    # Next: filter by code type
    code_packages = _filter_packages(os_packages, code_type)
    if _debug > 1:
        print ('filtered to code type (%s): %r' %
               (code_type, code_packages))
    if not code_packages:
        code_packages = _filter_packages(os_packages, 'purepython')
        if _debug > 1:
            print ('filtered to code type (purepython): %r' %
                   (code_packages,))
    if not code_packages:
        if _debug > 1:
            print ('no suitable code type package found, '
                   'trying source code')
        return _find_source_package(package_data)

    # Next: filter by Python version
    python_packages = _filter_packages(code_packages, python_version_tag)
    if _debug > 1:
        print ('filtered to Python version (%s): %r' %
               (python_version_tag, python_packages))
    if not python_packages:
        if _debug > 1:
            print ('no suitable Python version package found, '
                   'trying source code')
        return _find_source_package(package_data)

    # Next: filter by UCS version, if needed
    if ucs_version_tag:
        ucs2_python_packages = _filter_packages(python_packages, 'ucs2')
        ucs4_python_packages = _filter_packages(python_packages, 'ucs4')
        if not ucs2_python_packages and not ucs4_python_packages:
            # No restriction
            pass
        else:
            if ucs_version_tag == 'ucs4':
                python_packages = ucs4_python_packages
            else:
                python_packages = ucs2_python_packages
            if _debug > 1:
                print ('filtered to UCS version (%s): %r' %
                       (ucs_version_tag, python_packages))
            if not python_packages:
                if _debug > 1:
                    print ('no suitable Python version package found, '
                           'trying source code')
                return _find_source_package(package_data)

    if not python_packages:
        if _debug:
            print ('no matching package found')
        raise KeyError(
            'No suitable package found for the current platform')

    # Choose one of the available packages (by sorting and picking the
    # last one to make the choice deterministic)
    package_list = sorted(python_packages.iteritems())
    package = package_list[-1]
    if _debug:
        print ('found binary package: %r' % (package,))
    return package[0]

def _test_package_data_files():
    
    example_package_data = {
        'egenix-mx-base-3.2.7-py2.7_ucs2-linux-x86_64-prebuilt.zip': [
            'linux', 'x64', 'py2.7', 'ucs2'],
        'egenix-mx-base-3.2.7-py2.7_ucs4-linux-x86_64-prebuilt.zip': [
            'linux', 'x64', 'py2.7', 'ucs4'],
        'egenix-mx-base-3.2.7-py2.6_ucs2-linux-x86_64-prebuilt.zip': [
            'linux', 'x64', 'py2.6', 'ucs2'],
        'egenix-mx-base-3.2.7-py2.6_ucs4-linux-x86_64-prebuilt.zip': [
            'linux', 'x64', 'py2.6', 'ucs4'],
        'egenix-mx-base-3.2.7-py2.7_ucs2-linux-i686-prebuilt.zip': [
            'linux', 'x86', 'py2.7', 'ucs2'],
        'egenix-mx-base-3.2.7-py2.7_ucs4-linux-i686-prebuilt.zip': [
            'linux', 'x86', 'py2.7', 'ucs4'],
        'egenix-mx-base-3.2.7-py2.6_ucs2-linux-i686-prebuilt.zip': [
            'linux', 'x86', 'py2.6', 'ucs2'],
        'egenix-mx-base-3.2.7-py2.6_ucs4-linux-i686-prebuilt.zip': [
            'linux', 'x86', 'py2.6', 'ucs4'],
        'egenix-mx-base-3.2.7.zip': [
            'source', 'compiler'],
        }

    test_file = 'test-package-data.tags'
    if os.path.exists(test_file):
        os.remove(test_file)
    
    write_package_data(test_file, example_package_data)
    data = read_package_data(test_file)
    assert data == example_package_data, (data, example_package_data)
    os.remove(test_file)

    data['addthis'] = ['linux', 'x86']
    write_package_data(test_file, data)
    read_data = read_package_data(test_file)
    assert read_data == data, (read_data, data)
    os.remove(test_file)

    data = {}
    data['addmore'] = ['linux', 'x86']
    write_package_data(test_file, data)
    read_data = read_package_data(test_file)
    assert read_data == data, (read_data, data)
    os.remove(test_file)

    write_package_data(
        test_file, example_package_data,
        base_url='https://downloads.egenix.com/python/')
    data = read_package_data(
        test_file,
        remove_base_url='https://downloads.egenix.com/python/')
    assert data == example_package_data, (data, example_package_data)
    os.remove(test_file)

# Tests
if 0:
    _test_package_data_files()
    sys.exit(0)

#
# Mixin helpers
#

class CompilerSupportMixin:

    """ Compiler support mixin which makes sure that the .compiler
        attribute is properly setup.
    
    """
    # Internal flag
    prepared_compiler = 0

    def prepare_compiler(self):

        # Setup .compiler instance
        compiler = self._get_compiler_object()
        if compiler is None:
            if hasattr(self, '_check_compiler'):
                # The config command has this method to setup the
                # compiler
                self._check_compiler()
            else:
                raise CCompilerError('no C compiler setup; cannot continue')
            compiler = self._get_compiler_object()
            
        elif self.prepared_compiler:
            # Return the already prepared compiler
            return compiler
        
        # Work around a bug in distutils <= 1.0.3
        if compiler.exe_extension is None:
            compiler.exe_extension = ''

        # Make sure we have a typical setup for directory
        # searches
        for dir in LIBPATH:
            add_dir(dir, compiler.library_dirs)
        for dir in INCLPATH:
            add_dir(dir, compiler.include_dirs)

        # Add the Python include dir
        add_dir(get_python_include_dir(), compiler.include_dirs)

        # Customize the compiler according to system settings
        customize_compiler(compiler)

        log.info('configured compiler')
        self.prepared_compiler = 1

        return compiler

    def reset_compiler(self):

        """ Reset a possibly already prepared compiler.

            The compiler will have to be prepared again using
            .prepared_compiler().

        """
        self._set_compiler_object(None)
        self.prepared_compiler = 0

    def _get_compiler_object(self):

        """ Get the command's compiler object.

        """
        # See the change and discussion for
        # http://bugs.python.org/issue6377, new in Python 2.7
        # The change was later reverted, so this probably never
        # triggers. See http://bugs.python.org/issue13994
        if hasattr(self, 'compiler_obj'):
            return self.compiler_obj
        else:
            return self.compiler

    def _set_compiler_object(self, compiler):

        """ Set the command's compiler object.

        """
        # See the change and discussion for
        # http://bugs.python.org/issue6377, new in Python 2.7
        # The change was later reverted, so this probably never
        # triggers. See http://bugs.python.org/issue13994
        if hasattr(self, 'compiler_obj'):
            self.compiler_obj = compiler
        else:
            self.compiler = compiler

#
# mx Auto-Configuration command
#

class mx_autoconf(CompilerSupportMixin,
                  config):

    """ Auto-configuration class which adds some extra configuration
        settings to the packages.

    """
    # Command description
    description = "auto-configuration build step (for internal use only)"

    # Command line options
    user_options = config.user_options + [
        ('enable-debugging', None,
         'compile with debugging support (env var: MX_ENABLE_DEBUGGING)'),
        ('platform-id', None,
         'set MX_PLATFORM_ID (env var: MX_PLATFORM_ID)'),
        ('defines=', None,
         'define C macros (example: A=1,B=2,C,D)'),
        ('undefs=', None,
         'define C macros (example: A,B,C)'),
        ]

    # User option defaults
    enable_debugging = 0
    platform_id = None
    defines = None
    undefs = None

    # C APIs to check: (C API name, list of header files to include)
    api_checks = (
        ('strftime', ['time.h']),
        ('strptime', ['time.h']),
        ('timegm', ['time.h']),
        ('clock_gettime', ['time.h']),
        ('clock_getres', ['time.h']),
        #('this_always_fails', []), # For testing the detection mechanism
        )

    def initialize_options(self):

        config.initialize_options(self)
        self.noisy = 0
        self.dump_source = 0

        if not self.enable_debugging:
            # Enable debugging for dev snapshots
            version = self.distribution.metadata.get_version()
            if has_substring('dev', version):
                self.enable_debugging = 1
                log.warn('debugging enabled for development snapshots')

        if not self.enable_debugging:
            # Enable debugging via env variable MX_ENABLE_DEBUGGING
            if get_env_var('MX_ENABLE_DEBUGGING', default=0, yesno_value=1):
                self.enable_debugging = 1
                log.warn('debugging enabled via '
                         'MX_ENABLE_DEBUGGING environment variable')

        if not self.platform_id:
            # Set platform id via env variable MX_PLATFORM_ID
            platform_id = get_env_var('MX_PLATFORM_ID', default=None)
            if platform_id is not None:
                self.platform_id = platform_id
                log.warn('MX_PLATFORM_ID set to %r (via env var)' %
                         platform_id)

    def finalize_options(self):

        config.finalize_options(self)
        
        if self.defines:
            defines = []
            for defstr in self.defines.split(','):
                defstr = defstr.strip()
                if '=' in defstr:
                    defname, defvalue = defstr.split('=')
                    defname = defname.strip()
                    defvalue = defvalue.strip()
                else:
                    defname = defstr
                    defvalue = '1'
                defines.append((defname, defvalue))
            self.defines = defines
        else:
            self.defines = []

        if self.undefs:
            undefs = []
            for undefstr in self.undefs.split(','):
                undefname = undefstr.strip()
                undefs.append(undefname)
            self.undefs = undefs
        else:
            self.undefs = []

    def get_outputs(self):

        """ We don't generate any output.

        """
        return []

    def run(self):

        # Setup compiler
        compiler = self.prepare_compiler()        

        # Check compiler setup
        log.info('checking compiler setup')
        if not self.compiler_available():
            if sys.platform == 'darwin':
                # On Mac OS X, Apple removed the GCC compiler from
                # Xcode 4.1, but the Python installers are still
                # compiled with GCC, so distutils will default to
                # looking for GCC (see Python issue #13590). We'll try
                # clang as fallback solution.
                cc, cxx, ldshared = get_config_vars('CC', 'CXX', 'LDSHARED')
                log.info('compiler problem: "%s" not available, trying '
                         '"clang" instead' % cc)
                if 'CC' not in os.environ:
                    os.environ['CC'] = 'clang'
                if 'CXX' not in os.environ:
                    os.environ['CXX'] = 'clang'
                if 'LDSHARED' not in os.environ:
                    if ldshared.startswith(cc):
                        ldshared = 'clang ' + ldshared[len(cc):]
                    os.environ['LDSHARED'] = ldshared
                self.reset_compiler()
                compiler = self.prepare_compiler()
                if self.compiler_available():
                    log.info('using "clang" instead of "%s"' % cc)
                else:
                    log.info('no working compiler found; '
                             'please install Xcode first')
                    raise CCompilerError('no compiler available')
            else:
                log.error('compiler setup does not work or no compiler found; '
                          'try adjusting the CC/LDSHARED environemnt variables '
                          'to point to the installed compiler')
                raise CCompilerError('no compiler available')
        log.info('compiler setup works')

        # Add some static #defines which should not hurt
        compiler.define_macro('_GNU_SOURCE', '1')

        # Parse [py]config.h
        config_h = get_config_h_filename()
        try:
            configfile = open(config_h)
        except IOError,why:
            log.warn('could not open %s file' % config_h)
            configuration = {}
        else:
            configuration = parse_config_h(configfile)
            configfile.close()

        # Tweak configuration a little for some platforms
        if sys.platform[:5] == 'win32':
            configuration['HAVE_STRFTIME'] = 1

        # Build lists of #defines and #undefs
        define = []
        undef = []

        # Check APIs
        for apiname, includefiles in self.api_checks:
            macro_name = 'HAVE_' + apiname.upper()
            if not configuration.has_key(macro_name):
                log.info('checking for availability of %s() '
                         '(errors from this can safely be ignored)' % apiname)
                if self.check_function(apiname, includefiles):
                    log.info('%s() is available on this platform' %
                             apiname)
                    define.append((macro_name, '1'))
                else:
                    log.info('%s() is not available on this platform' %
                             apiname)
                    undef.append(macro_name)

        # Compiler tests
        if not configuration.has_key('BAD_STATIC_FORWARD'):
            log.info('checking compiler for bad static forward handling '
                     '(errors from this can safely be ignored)')
            if self.check_bad_staticforward():
                log.info('compiler has problems with static forwards '
                         '- enabling work-around')
                define.append(('BAD_STATIC_FORWARD', '1'))

        # Enable debugging support
        if self.enable_debugging:
            log.info('enabling mx debug support')
            define.append(('MAL_DEBUG', None))

        # Set platform id
        if self.platform_id:
            log.info('setting mx platform id to %r' % self.platform_id)
            define.append(('MX_PLATFORM_ID',
                           quote_string_define_value(self.platform_id)))

        # Add extra #defines and #undefs
        if self.defines:
            define.extend(self.defines)
        if self.undefs:
            undef.extend(self.undefs)

        log.info('macros to define: %s' % define)
        log.info('macros to undefine: %s' % undef)

        # Reinitialize build_ext command with extra defines
        build_ext = self.distribution.reinitialize_command('build_ext')
        build_ext.ensure_finalized()
        # We set these here, since distutils 1.0.2 introduced a
        # new incompatible way to process .define and .undef
        if build_ext.define:
            #print repr(build_ext.define)
            if type(build_ext.define) is types.StringType:
                # distutils < 1.0.2 needs this:
                l = build_ext.define.split(',')
                build_ext.define = map(lambda symbol: (symbol, '1'), l)
            build_ext.define = build_ext.define + define
        else:
            build_ext.define = define
        if build_ext.undef:
            #print repr(build_ext.undef)
            if type(build_ext.undef) is types.StringType:
                # distutils < 1.0.2 needs this:
                build_ext.undef = build_ext.undef.split(',')
            build_ext.undef = build_ext.undef + undef
        else:
            build_ext.undef = undef
        log.debug('updated build_ext with autoconf setup')

    def compiler_available(self):

        """ Check whether the compiler works.

            Return 1/0 depending on whether the compiler can produce
            code or not.
        
        """
        body = "int main (void) { return 0; }"
        return self.check_compiler(body)

    def check_compiler(self, sourcecode, headers=None, include_dirs=None,
                       libraries=None, library_dirs=None):

        """ Check whether sourcecode compiles and links with the current
            compiler and link environment.

            For documentation of the other arguments see the base
            class' .try_link().
        
        """
        self.prepare_compiler()
        return self.try_link(sourcecode, headers, include_dirs,
                             libraries, library_dirs)

    def check_bad_staticforward(self):

        """ Check whether the compiler does not supports forward declaring
            static arrays.

            For documentation of the other arguments see the base
            class' .try_link().
        
        """
        body = """
        typedef struct _mxstruct {int a; int b;} mxstruct;
        staticforward mxstruct mxarray[];
        statichere mxstruct mxarray[] = {{0,2},{3,4},};
        int main(void) {return mxarray[0].a;}
        """
        self.prepare_compiler()
        return not self.try_compile(body,
                                    headers=('Python.h',),
                                    include_dirs=[get_python_include_dir()])

    def check_function(self, function, headers=None, include_dirs=None,
                       libraries=None, library_dirs=None,
                       prototype=0, call=0):

        """ Check whether function is available in the given
            compile and link environment.

            If prototype is true, a function prototype is included in
            the test. If call is true, a function call is generated
            (rather than just a reference of the function symbol).

            For documentation of the other arguments see the base
            class' .try_link().
        
        """
        body = []
        if prototype:
            body.append("int %s (void);" % function)
        body.append("int main (void) {\n"
                    "  void *ptr = 0; ")
        if call:
            body.append("  %s();" % function)
        else:
            body.append("  ptr = &%s;" % function)
        body.append("return 0; }")
        body = "\n".join(body) + "\n"
        return self.check_compiler(body, headers, include_dirs,
                                   libraries, library_dirs)

    def check_library(self, library, library_dirs=None,
                      headers=None, include_dirs=None, other_libraries=[]):

        """ Check whether we can link against the given library.

            For documentation of the other arguments see the base
            class' .try_link().
        
        """
        body = "int main (void) { return 0; }"
        return self.check_compiler(body, headers, include_dirs,
                                   [library]+other_libraries, library_dirs)

    def find_include_file(self, filename, paths, pattern=None):

        """ Find an include file of the given name.

            Returns a tuple (directory, filename) or (None, None) in
            case no library can be found.

            The search path is determined by the paths parameter, the
            compiler's .include_dirs attribute and the STDINCLPATH and
            FINDINCLPATH globals. The search is done in this order.

        """
        compiler = self.prepare_compiler()
        paths = (paths
                 + compiler.include_dirs
                 + STDINCLPATH
                 + FINDINCLPATH)
        verify_path(paths)
        if _debug:
            print 'INCLPATH', paths
        dir = find_file(filename, paths, pattern)
        if dir is None:
            return (None, None)
        return (dir, os.path.join(dir, filename))

    def find_library_file(self, libname, paths, pattern=None,
                          lib_types=LIB_TYPES):

        """ Find a library of the given name.

            Returns a tuple (directory, filename) or (None, None) in
            case no library can be found.

            The search path is determined by the paths parameter, the
            compiler's .library_dirs attribute and the STDLIBPATH and
            FINDLIBPATH globals. The search is done in this order.

            Shared libraries are prefered over static ones if both
            types are given in lib_types.

        """
        compiler = self.prepare_compiler()
        paths = (paths
                 + compiler.library_dirs
                 + STDLIBPATH
                 + FINDLIBPATH)
        verify_path(paths)
        if _debug:
            print 'LIBPATH: %r' % paths
            print 'Library types: %r' % lib_types

        # Try to first find a shared library to use and revert
        # to a static one, if no shared lib can be found
        for lib_type in lib_types:
            filename = compiler.library_filename(libname,
                                                 lib_type=lib_type)
            if _debug:
                print 'looking for library file %s' % filename
            dir = find_file(filename, paths, pattern)
            if dir is not None:
                return (dir, os.path.join(dir, filename))
            
        return (None, None)

#
# mx MSVC Compiler extension
#
# We want some extra options for the MSVCCompiler, so we add them
# here. This is an awful hack, but there seems to be no other way to
# subclass a standard distutils C compiler class...

if python_version < '2.4':
    # VC6
    MSVC_COMPILER_FLAGS = ['/O2', '/Gf', '/GB', '/GD', '/Ob2']
elif python_version < '2.6':
    # VC7.1
    MSVC_COMPILER_FLAGS = ['/O2', '/GF', '/GB', '/Ob2']
else:
    # VC9
    MSVC_COMPILER_FLAGS = ['/O2', '/GF', '/Ob2']

if hasattr(MSVCCompiler, 'initialize'):
    # distutils 2.5.0 separates the initialization of the
    # .compile_options out into a new method .initialize()

    # remember old _initialize
    old_MSVCCompiler_initialize = MSVCCompiler.initialize

    def mx_msvccompiler_initialize(self, *args, **kws):

        apply(old_MSVCCompiler_initialize, (self,) + args, kws)

        # Add our extra options
        self.compile_options.extend(MSVC_COMPILER_FLAGS)

    # "Install" new initialize
    MSVCCompiler.initialize = mx_msvccompiler_initialize

else:
    # distutils prior to 2.5.0 only allow to hook into the .__init__()
    # method

    # remember old __init__
    old_MSVCCompiler__init__ = MSVCCompiler.__init__

    def mx_msvccompiler__init__(self, *args, **kws):

        apply(old_MSVCCompiler__init__, (self,) + args, kws)

        # distutils 2.5.0 separates the initialization of the
        # .compile_options out into a new method
        if hasattr(self, 'initialized') and not self.initialized:
            self.initialize()

        # Add out extra options
        self.compile_options.extend(MSVC_COMPILER_FLAGS)

    # "Install" new __init__
    MSVCCompiler.__init__ = mx_msvccompiler__init__

#
# mx Distribution class
#

class mx_Distribution(Distribution):

    """ Distribution class which knows about our distutils extensions.
        
    """
    # List of UnixLibrary instances
    unixlibs = None

    # Option to override the package version number
    set_version = None

    # List of setuptools namespace package names
    namespace_packages = None

    # List of setuptools dependency links
    dependency_links = None

    # Add classifiers dummy option if needed
    display_options = Distribution.display_options[:]
    display_option_names = Distribution.display_option_names[:]
    if 'classifiers' not in display_options:
        display_options = display_options + [
            ('classifiers', None,
             "print the list of classifiers (not yet supported)"),
        ]
        display_option_names = display_option_names + [
            'classifiers'
            ]

    # Add set-version option
    global_options = Distribution.global_options[:]
    global_options = global_options + [
        ('set-version=', None, "override the package version number"),
        ]

    def finalize_options(self):

        if self.namespace_packages is None:
            self.namespace_packages = []
        if self.dependency_links is None:
            self.dependency_links = []

        # Call base method
        Distribution.finalize_options(self)

    def parse_command_line(self):

        if not Distribution.parse_command_line(self):
            return

        # Override the version information from the package with a
        # command-line given version string
        if self.set_version is not None:
            self.metadata.version = self.set_version
            self.version = self.set_version

        return 1

    def has_unixlibs(self):
        return self.unixlibs and len(self.unixlibs) > 0

    def get_namespace_packages(self):
        return self.namespace_packages or []

    def get_dependency_links(self):
        return self.dependency_links or []

#
# mx Extension class
#

class mx_Extension(Extension):

    """ Extension class which allows specifying whether the extension
        is required to build or optional.
        
    """
    # Is this Extension required to build or can we issue a Warning in
    # case it fails to build and continue with the remaining build
    # process ?
    required = 1

    # Warn, if an optional extension cannot be built ?
    warn = 1

    # List of optional libaries (libname, list of header files to
    # check) to include in the link step; the availability of these is
    # tested prior to compiling the extension. If found, the symbol
    # HAVE_<upper(libname)>_LIB is defined and the library included in
    # the list of libraries to link against.
    optional_libraries = ()

    # List of needed include files in form of tuples (filename,
    # [dir1, dir2,...], pattern); see mx_autoconf.find_file()
    # for details
    needed_includes = ()

    # List of needed include files in form of tuples (libname,
    # [dir1, dir2,...], pattern); see mx_autoconf.find_library_file()
    # for details
    needed_libraries = ()

    # Include the found library files in the extension output ?  This
    # causes the files to be copied into the same location as the
    # extension itself.
    include_needed_libraries = 0

    # Library types to check (for needed libraries)
    lib_types = LIB_TYPES

    # Data files needed by this extension (these are only
    # installed if building the extension succeeded)
    data_files = ()

    # Python packages needed by this extension (these are only
    # installed if building the extension succeeded)
    packages = ()

    # Building success marker
    successfully_built = 0

    # NOTE: If you add new features to this class, also adjust
    # rebase_extensions()

    def __init__(self, *args, **kws):
        for attr in ('required',
                     'warn',
                     'lib_types',
                     'optional_libraries',
                     'needed_includes',
                     'needed_libraries',
                     'include_needed_libraries',
                     'data_files',
                     'packages'):
            if kws.has_key(attr):
                setattr(self, attr, kws[attr])
                del kws[attr]
            else:
                value = getattr(self, attr)
                if type(value) == type(()):
                    setattr(self, attr, list(value))
        apply(Extension.__init__, (self,) + args, kws)

#
# mx Build command
#

class mx_build(build):

    """ build command which knows about our distutils extensions.

        This build command builds extensions in properly separated
        directories (which includes building different Unicode
        variants in different directories).
        
    """
    # Skip the build process ?
    skip = None

    # Assume prebuilt archive ?
    prebuilt = None
   
    user_options = build.user_options + [
            ('skip', None,
             'skip build and reuse the existing build files'),
            ('prebuilt', None,
             'assume we have a prebuilt archive (even without %s file)' %
             PREBUILT_MARKER),
            ]

    def finalize_options(self):

        # Make sure different Python versions are built in separate
        # directories
        python_platform = '.%s-%s' % (mx_get_platform(), py_version())
        if self.build_platlib is None:
            self.build_platlib = os.path.join(
                self.build_base,
                'lib' + python_platform)
        if self.build_purelib is None:
            self.build_purelib = os.path.join(
                self.build_base,
                'lib.' + py_version(unicode_aware=0))
        if self.build_temp is None:
            self.build_temp = os.path.join(
                self.build_base,
                'temp' + python_platform)

        # Call the base method
        build.finalize_options(self)

        if self.skip is None:
            self.skip = 0
            
        # Handle prebuilt archives
        if os.path.exists(PREBUILT_MARKER):
            self.prebuilt = 1
        elif self.prebuilt is None:
            self.prebuilt = 0

        if self.prebuilt:
            if not self.have_build_pickle():
                # Either the build pickle is missing or not compatible
                # with the currently running Python interpreter.  Read
                # setup information from the build pickle; the path to
                # this file is stored in the PREBUILT_MARKER file.
                if os.path.exists(PREBUILT_MARKER):
                    build_pickle_pathname = (
                        open(PREBUILT_MARKER, 'rb').read().strip())
                    build_pickle = self.read_build_pickle(
                        build_pickle_pathname)
                    mxSetup = build_pickle.get('mxSetup', {})
                else:
                    mxSetup = {}
                    
                print """
--------------------------------------------------------------------

ERROR: Cannot find the build information needed for your platform.
                
Please check that you have downloaded the right prebuilt archive for
your platform and Python version.

Product name:        %s
Product version:     %s
mxSetup version:     %s
    
Your Python installation uses these settings:

    Python version:  %s
    Platform:        %s
    sys.platform:    %s

The prebuilt archive was built for:

    Python version:  %s
    Platform:        %s
    sys.platform:    %s
    mxSetup version: %s

--------------------------------------------------------------------
                """.strip() % (
                self.distribution.metadata.get_name(),
                self.distribution.metadata.get_version(),
                __version__,
                py_version(unicode_aware=1),
                mx_get_platform(),
                sys.platform,
                mxSetup.get('py_version', 'unknown'),
                mxSetup.get('get_platform', 'unknown'),
                mxSetup.get('sys_platform', 'unknown'),
                mxSetup.get('__version__', 'unknown'),
                )
                sys.exit(1)
            if self.force:
                log.info('prebuilt archive found: ignoring the --force option')
                self.force = 0
                
            # Override settings with data from a possibly existing
            # build pickle
            log.info('prebuilt archive found: skipping the build process and '
                     'loading the prebuilt archive')
            self.load_build_pickle()

            # Make sure loading the pickle does not overwrite the
            # .prebuilt flag
            self.prebuilt = 1
            self.skip = 1

        # Use the build pickle, in case we are supposed to skip the
        # build
        elif self.skip and self.have_build_pickle():
            log.info('skipping the build process and '
                     'loading the build pickle instead')
            self.load_build_pickle()

            # Make sure loading the pickle did not overwrite the .skip
            # flag
            self.skip = 1

    def get_outputs(self):

        """ Collect the outputs of all sub-commands (this knows about
            the extra commands we added).

            This is needed by mx_bdist_prebuilt and mx_uninstall.

        """
        outputs = {}
        for sub_command in self.get_sub_commands():
            cmd = self.get_finalized_command(sub_command)
            if not hasattr(cmd, 'get_outputs'):
                log.error('problem: missing .get_outputs() ... %r' % cmd)
                raise ValueError('missing .get_outputs() implementation '
                                 'for command %r' % cmd)
            for filename in cmd.get_outputs():
                outputs[filename] = 1

        # Add pickle, if available
        pickle_filename = self.get_build_pickle_pathname()
        if os.path.exists(pickle_filename):
            if _debug:
                print 'added pickle:', pickle_filename
            outputs[pickle_filename] = 1

        # Return a sorted list
        outputs = outputs.keys()
        outputs.sort()
        return outputs

    def pure_python_build(self):

        """ Return 1/0 depending on whether this is a pure Python
            build or not.

            Pure Python builds do not include extensions.

        """
        return not self.distribution.ext_modules

    def get_build_pickle_pathname(self):

        """ Return the path name for the build pickle file.

            Note that the target system loading these pickles may well
            return different values for get_platform() than the system
            used to compile the build.

            We therefore do not include the platform in the pathname,
            only the Python version (to prevent obvious user errors
            related to downloading the wrong prebuilt archive for
            their Python version).

            For pure Python distributions (ones without extensions),
            we also don't include the Unicode tag in the pickle name.

        """
        unicode_aware = not self.pure_python_build()
        return os.path.join(
            self.build_base,
            'build-py%s.pck' % (py_version(unicode_aware=unicode_aware)))

    def write_build_pickle(self, pathname=None):

        """ Write the current state of the distribution, the
            build command and all sub-commands to a Python
            pickle in the build directory.

            If pathname is not given, .get_build_pickle_pathname() is
            used as default.

        """
        if pathname is None:
            pathname = self.get_build_pickle_pathname()

        # Prepare the sub commands for pickle'ing
        self.get_outputs()

        # Remove data that would cause conflicts when restoring the
        # build pickle
        data = self.__dict__.copy()
        if data.has_key('distribution'):
            del data['distribution']
        if data.has_key('compiler'):
            del data['compiler']
        state = {'build': data}
        for sub_command, needed in self.sub_commands:
            cmd = self.distribution.get_command_obj(sub_command)
            data = cmd.__dict__.copy()
            if data.has_key('distribution'):
                del data['distribution']
            if data.has_key('compiler'):
                del data['compiler']
            if data.has_key('extensions') and data['extensions']:
                # Convert mx_Extension instances into instance dicts;
                # this is needed to make easy_install happy
                extensions = data['extensions']
                data['extensions'] = [
                    ext.__dict__.copy()
                    for ext in extensions]
            if data.has_key('autoconf'):
                del data['autoconf']
            state[sub_command] = data
        data = {'have_run': self.distribution.have_run,
                'data_files': self.distribution.data_files,
                }
        state['distribution'] = data
        if 0:
            data = self.distribution.__dict__.copy()
            if data.has_key('distribution'):
                del data['distribution']
            if data.has_key('metadata'):
                del data['metadata']
            if data.has_key('ext_modules'):
                del data['ext_modules']
            if data.has_key('command_obj'):
                del data['command_obj']
            if data.has_key('cmdclass'):
                del data['cmdclass']
            for key, value in data.items():
                if type(value) is type(self.distribution.get_url):
                    # Bound method
                    del data[key]
                elif type(value) is type(self.distribution):
                    # Instance
                    del data[key]
            state['distribution'] = data


        # Save additional meta-data
        pure_python_build = self.pure_python_build()
        unicode_aware = not pure_python_build
        state['mxSetup'] = {
            '__version__': __version__,
            'unicode_aware': unicode_aware, 
            'py_version': py_version(unicode_aware=unicode_aware),
            'sys_platform': sys.platform,
            'get_name': self.distribution.metadata.get_name(),
            'get_version': self.distribution.metadata.get_version(),
            'get_platform': mx_get_platform(),
            'pure_python_build': pure_python_build,
            }

        # Save pickle
        if _debug:
            print ('saving build pickle: %s' % repr(state))
        pickle_file = open(self.get_build_pickle_pathname(),
                           'wb')
        cPickle.dump(state, pickle_file)
        pickle_file.close()

    def have_build_pickle(self, pathname=None,
                          ignore_distutils_platform=True,
                          ignore_distribution_version=True,
                          ignore_sys_platform=None):

        """ Return 1/0 depending on whether there is a build pickle
            that could be used or not.

            If pathname is not given, .get_build_pickle_pathname() is
            used as default.

            If ignore_platform is set (default), the platform
            information stored in the pickle is ignored in the check.
            This is useful, since the value of the build system may
            differ from the target system (e.g. for fat builds on Mac
            OS X that get installed on Intel Macs).

            If ignore_distribution_version is set (default), the
            distribution version information in the pickle is ignored.
            This is useful for cases where the version determined at
            build time can be different than at installation time,
            e.g. due to a timestamp being created dynamically and
            added to the version.

            If ignore_sys_platform is set (default is false for builds
            with C extensions and true for pure Python builds), the
            sys.platform version information in the pickle is ignored.

        """
        if pathname is None:
            pathname = self.get_build_pickle_pathname()
        try:
            pickle_file = open(pathname, 'rb')
        except IOError:
            log.info('no build data file %r found' % pathname)
            return 0
        state = cPickle.load(pickle_file)
        pickle_file.close()

        # Check whether this is a valid build file for this Python
        # version
        mxSetup = state.get('mxSetup', None)
        if mxSetup is None:
            return 0
        unicode_aware = mxSetup.get('unicode_aware', 1)
        pure_python_build = mxSetup.get('pure_python_build', 0)
        if ignore_sys_platform is None:
            if pure_python_build:
                if mxSetup['sys_platform'].startswith('win'):
                    # Prebuilt archives built on win32 can only be
                    # installed there, since the os.sep does not
                    # correspond to the distutils standard of '/'.
                    ignore_sys_platform = False
                else:
                    ignore_sys_platform = True
        if mxSetup['__version__'] != __version__:
            log.info('build data file %r found, '
                     'but mxSetup version does not match; not using it' %
                     pathname)
            return 0
        if mxSetup['py_version'] != py_version(unicode_aware=unicode_aware):
            log.info('build data file %r found, '
                     'but Python version does not match; not using it' %
                     pathname)
            return 0
        if ((not ignore_sys_platform) and
            not compatible_os(mxSetup['sys_platform'], sys.platform)):
            log.info('build data file %r found, '
                     'but sys.platform does not match; not using it' %
                     pathname)
            return 0
        if mxSetup['get_name'] != self.distribution.metadata.get_name():
            log.info('build data file %r found, '
                     'but distribution name does not match; not using it' %
                     pathname)
            return 0
        if ((not ignore_distribution_version) and
            mxSetup['get_version'] != self.distribution.metadata.get_version()):
            log.info('build data file %r found, '
                     'but distribution version does not match; not using it' %
                     pathname)
            return 0
        if ((not ignore_distutils_platform) and
            not compatible_platform(mxSetup['get_platform'],
                                    mx_get_platform())):
            log.info('build data file %r found, '
                     'but distutils platform does not match; not using it' %
                     pathname)
            return 0

        log.info('found usable build data file %r' % pathname)
        return 1

    def read_build_pickle(self, pathname=None):

        """ Read the pickle written by the .write_build_pickle() method.

            If pathname is not given, .get_build_pickle_pathname() is
            used as default.

        """
        if pathname is None:
            pathname = self.get_build_pickle_pathname()
        pickle_file = open(pathname, 'rb')
        state = cPickle.load(pickle_file)
        pickle_file.close()
        if _debug:
            print 'read build pickle:'
            import pprint
            pprint.pprint(state)
        return state

    def load_build_pickle(self):

        """ Restore the state written by the .write_build_pickle() method.

        """
        # Read pickle file
        state = self.read_build_pickle()

        # Adjust distutils platform string, if needed
        platform = state['mxSetup'].get('get_platform', None)
        if platform is not None:
            log.info('setting platform to %r' % platform)
            mx_set_platform(platform)
        
        log.info('restoring build data from a previous build run')
        distribution_data = None
        for sub_command, data in state.items():
            if _debug:
                print 'restoring build data for command %r' % sub_command
            if sub_command == 'mxSetup':
                self.distribution.metadata.version = data['get_version']
                self.distribution.metadata.name = data['get_name']
            elif sub_command == 'build':
                for key, value in data.items():
                    self.__dict__[key] = value
            elif sub_command == 'distribution':
                distribution_data = data
            else:
                # This call will create the command object in case it
                # doesn't exist yet. Note: It also resets the
                # distribution.have_run entry for the command. See
                # #1583.
                cmd = self.distribution.get_command_obj(sub_command)
                for key, value in data.items():
                    cmd.__dict__[key] = value
                if sub_command == 'build_ext':
                    if cmd.extensions:
                        # Convert Extension dicts back into mx_Extension
                        # instances
                        extensions_dicts = cmd.extensions
                        cmd.extensions = []
                        class Ext: pass
                        for ext_dict in extensions_dicts:
                            ext = Ext()
                            ext.__dict__.update(ext_dict)
                            ext.__class__ = mx_Extension
                            cmd.extensions.append(ext)

        # Finally, restore the distribution data (the command
        # restoration would otherwise overwrite parts like
        # e.g. distribution.have_run. See #1583.
        if distribution_data is None:
            raise DistutilsError('missing distribution data in build pickle')
        for key, value in distribution_data.items():
            self.distribution.__dict__[key] = value

        # Report successful load
        log.info('loaded build data for platform %r' % self.plat_name)

    def run(self):

        # Check whether we need to skip the build process
        if self.skip:
            log.info('skipping the build process and '
                     'reusing existing build files')
            return

        # Run the build command
        build.run(self)

        # Save the build data in a build pickle for later reuse,
        # unless this is a prebuilt initiated run
        if not self.prebuilt:
            # Make sure we save the run state of the build command,
            # since this is normaly set *after* the call to .run()
            self.distribution.have_run['build'] = 1
            self.write_build_pickle()

    def has_unixlibs(self):
        return self.distribution.has_unixlibs()

    def has_data_files(self):
        return self.distribution.has_data_files()

    # Add new sub-commands:
    if len(build.sub_commands) > 4:
        raise DistutilsError, 'incompatible distutils version !'
    sub_commands = [('build_clib',    build.has_c_libraries),
                    ('build_unixlib', has_unixlibs),
                    ('mx_autoconf',   build.has_ext_modules),
                    ('build_ext',     build.has_ext_modules),
                    ('build_py',      build.has_pure_modules),
                    ('build_scripts', build.has_scripts),
                    ('build_data',    has_data_files),
                   ]

#
# mx Build C Lib command
#

class mx_build_clib(CompilerSupportMixin,
                    build_clib):

    """ build_clib command which builds the libs using
        separate temp dirs
        
    """
    # Lib of output files
    outfiles = None

    def finalize_options(self):
        
        build_clib.finalize_options(self)
        self.outfiles = []

    def build_library(self, lib_name, build_info):

        # Build each extension in its own subdir of build_temp (to
        # avoid accidental sharing of object files between extensions
        # having the same name, e.g. mxODBC.o).
        build_temp_base = self.build_temp
        self.build_temp = os.path.join(build_temp_base, lib_name)
        compiler = self.prepare_compiler()

        try:

            #
            # This is mostly identical to the original build_clib command.
            #
            sources = build_info.get('sources')
            if sources is None or \
               type(sources) not in (types.ListType, types.TupleType):
                raise DistutilsSetupError, \
                      ("in 'libraries' option (library '%s'), " +
                       "'sources' must be present and must be " +
                       "a list of source filenames") % lib_name
            sources = list(sources)

            log.info("building '%s' library" % lib_name)

            # First, compile the source code to object files in the
            # library directory.  (This should probably change to
            # putting object files in a temporary build directory.)
            macros = build_info.get('macros')
            include_dirs = build_info.get('include_dirs')
            objects = compiler.compile(sources,
                                       output_dir=self.build_temp,
                                       macros=macros,
                                       include_dirs=include_dirs,
                                       debug=self.debug)

            # Now "link" the object files together into a static library.
            # (On Unix at least, this isn't really linking -- it just
            # builds an archive.  Whatever.)
            compiler.create_static_lib(objects, lib_name,
                                       output_dir=self.build_clib,
                                       debug=self.debug)
            
            # Record the name of the library we just created
            self.outfiles.append(
                compiler.library_filename(lib_name,
                                          output_dir=self.build_clib))

        finally:
            # Cleanup local changes to the configuration
            self.build_temp = build_temp_base
        
    def build_libraries(self, libraries):

        for (lib_name, build_info) in libraries:
            self.build_library(lib_name, build_info)

    def get_outputs(self):

        """ Return a list of the created library files.

            This is needed by mx_bdist_prebuilt on all build commands
            and build_clib doesn't provide it.

        """
        return self.outfiles

#
# mx Build Extensions command
#

class mx_build_ext(CompilerSupportMixin,
                   build_ext):

    """ build_ext command which runs mx_autoconf command before
        trying to build anything.
        
    """
    user_options = build_ext.user_options + [
        ('disable-build=', None,
         'disable building an optional extensions (comma separated list of '
         'dotted package names); default is to try building all'),
        ('enable-build=', None,
         'if given, only these optional extensions are built (comma separated '
         'list of dotted package names)'),
        ]

    # mx_autoconf command object (finalized and run)
    autoconf = None

    # Default values for command line options
    disable_build = None
    enable_build = None

    # Extra output files
    extra_output = ()

    # Output files
    output_files = None
    
    def finalize_options(self):

        build_ext.finalize_options(self)
        if self.disable_build is None:
            self.disable_build = ()
        elif type(self.disable_build) is types.StringType:
            self.disable_build = [x.strip()
                                  for x in self.disable_build.split(',')]
        if type(self.enable_build) is types.StringType:
            self.enable_build = [x.strip()
                                 for x in self.enable_build.split(',')]
        self.extra_output = []

    def run(self):

        # Add unixlibs install-dirs to library_dirs, so that linking
        # against them becomes easy
        if self.distribution.has_unixlibs():
            build_unixlib = self.get_finalized_command('build_unixlib')
            paths, libs = build_unixlib.get_unixlib_lib_options()
            # Libraries have to be linked against by defining them
            # in the mx_Extension() setup, otherwise, all extensions
            # get linked against all Unix libs that were built...
            #self.libraries[:0] = libs
            self.library_dirs[:0] = paths
            
        # Assure that mx_autoconf has been run and store a reference
        # in .autoconf
        self.run_command('mx_autoconf')
        self.autoconf = self.get_finalized_command('mx_autoconf')

        # Now, continue with the standard build process
        build_ext.run(self)

    def build_extensions(self):

        # Make sure the compiler is setup correctly
        compiler = self.prepare_compiler()
        
        # Make sure that any autoconf actions use the same compiler
        # settings as we do (the .compiler is set up in build_ext.run()
        # just before calling .build_extensions())
        self.autoconf._set_compiler_object(compiler)

        # Build the extensions
        self.check_extensions_list(self.extensions)
        for ext in self.extensions:
            self.build_extension(ext)

        # Cleanup .extensions list (remove entries which did not build correctly)
        l = []
        for ext in self.extensions:
            if not isinstance(ext, mx_Extension):
                l.append(ext)
            else:
                if ext.successfully_built:
                    l.append(ext)
        self.extensions = l
        if _debug:
            print ('built these extensions: %s' % (
                ', '.join(ext.name for ext in self.extensions)))
        log.info('')
         
    def build_extension(self, ext):

        # Prevent duplicate builds of the same extension
        if ext.successfully_built and not self.force:
            log.info('reusing existing build of extension "%s"' % ext.name)
            return

        # Prepare compilation
        required = not hasattr(ext, 'required') or ext.required
        warn = not hasattr(ext, 'warn') or ext.warn
        log.info('')
        log.info('building extension "%s" %s' %
                 (ext.name,
                  required * '(required)' or '(optional)'))
        compiler = self.prepare_compiler()

        # Optional extension building can be adjusted via command line options
        if not required:
            if self.enable_build is not None and \
               ext.name not in self.enable_build:
                log.info('skipped -- build not enabled by command line option')
                return
            elif ext.name in self.disable_build:
                log.info('skipped -- build disabled by command line option')
                return

        # Search for include files
        if (isinstance(ext, mx_Extension) and \
            ext.needed_includes):
            log.info('looking for header files needed by extension '
                     '"%s"' % ext.name)
            for filename, dirs, pattern in ext.needed_includes:
                (dir, pathname) = self.autoconf.find_include_file(
                    filename,
                    dirs,
                    pattern)
                if dir is not None:
                    log.info('found needed include file "%s" '
                             'in directory %s' % (filename, dir))
                    if dir not in ext.include_dirs and \
                       dir not in STDINCLPATH and \
                       dir not in INCLPATH:
                        ext.include_dirs.append(dir)
                else:
                    if required:
                        raise CompileError(
                              'could not find needed header file "%s"' %
                              filename)
                    else:
                        if warn:
                            log.warn(
                                '*** WARNING: Building of extension '
                                '"%s" failed: needed include file "%s" '
                                'not found' %
                                (ext.name, filename))
                        else:
                            log.warn(
                                'optional extension "%s" could not be built: '
                                'needed include file "%s" not found' %
                                (ext.name, filename))
                        return
                    
        # Search for libraries
        if hasattr(ext, 'needed_libraries') and \
           ext.needed_libraries:
            log.info('looking for libraries needed by extension '
                     '"%s"' % ext.name)
            for libname, dirs, pattern in ext.needed_libraries:
                dir, pathname = self.autoconf.find_library_file(
                    libname,
                    dirs,
                    pattern=pattern,
                    lib_types=ext.lib_types)
                if dir is not None:
                    log.info('found needed library "%s" '
                             'in directory %s (%s)' % (libname,
                                                       dir,
                                                       pathname))
                    if 'shared' not in ext.lib_types:
                        # Force static linking and append the library
                        # pathname to the linker arguments
                        if libname in ext.libraries:
                            ext.libraries.remove(libname)
                        if not ext.extra_link_args:
                            ext.extra_link_args = []
                        ext.extra_link_args.append(
                            pathname)
                    else:
                        # Prefer dynamic linking
                        if dir not in ext.library_dirs and \
                           dir not in STDLIBPATH and \
                           dir not in LIBPATH:
                            ext.library_dirs.append(dir)
                        if libname not in ext.libraries:
                            ext.libraries.append(libname)
                    if ext.include_needed_libraries:
                        ext_package_dir = os.path.split(
                            self.get_ext_filename(
                            self.get_ext_fullname(ext.name)))[0]
                        # The linker will always link against the
                        # real name, not a symbolic name (XXX Hope this
                        # is true for all platforms)
                        realpathname = os.path.realpath(pathname)
                        realfilename = os.path.split(realpathname)[1]
                        # Copy the share lib to the package dir, using
                        # the real filename
                        data_entry = (realpathname,
                                      ext_package_dir + os.sep)
                        if data_entry not in ext.data_files:
                            if _debug:
                                print ('adding library data entry %r' %
                                       (data_entry,))
                            ext.data_files.append(data_entry)
                else:
                    if required:
                        raise CompileError(
                              'could not find needed library "%s"' %
                              libname)
                    else:
                        if warn:
                            log.warn(
                                '*** WARNING: Building of extension '
                                '"%s" failed: needed library "%s" '
                                'not found' %
                                (ext.name, libname))
                        else:
                            log.warn(
                                'optional extension "%s" could not be built: '
                                'needed library "%s" not found' %
                                (ext.name, libname))
                        return
                    
        # Build each extension in its own subdir of build_temp (to
        # avoid accidental sharing of object files between extensions
        # having the same name, e.g. mxODBC.o).
        build_temp_base = self.build_temp
        extpath = ext.name.replace('.', '-')
        self.build_temp = os.path.join(build_temp_base, extpath)

        # Check for availability of optional libs which can be used
        # by the extension; note: this step includes building small
        # object files to test for the availability of the libraries
        if isinstance(ext, mx_Extension) and \
           ext.optional_libraries:
            log.info("checking for optional libraries")
            for libname, headerfiles in ext.optional_libraries:
                if self.autoconf.check_library(libname, headers=headerfiles):
                    symbol = 'HAVE_%s_LIB' % libname.upper()
                    log.info("found optional library '%s'"
                             " -- defining %s" % (libname, symbol))
                    ext.libraries.append(libname)
                    ext.define_macros.append((symbol, '1'))
                else:
                    log.warn("could not find optional library '%s'"
                             " -- omitting it" % libname)

        if _debug:
            print 'Include dirs:', repr(ext.include_dirs +
                                        compiler.include_dirs)
            print 'Libary dirs:', repr(ext.library_dirs +
                                       compiler.library_dirs)
            print 'Libaries:', repr(ext.libraries)
            print 'Macros:', repr(ext.define_macros)

        # Build the extension
        successfully_built = 0
        try:
            
            # Skip extensions which cannot be built if the required
            # option is given and set to false.
            required = not hasattr(ext, 'required') or ext.required
            if required:
                build_ext.build_extension(self, ext)
                successfully_built = 1
            else:
                try:
                    build_ext.build_extension(self, ext)
                except (CCompilerError, DistutilsError), why:
                    if warn:
                        log.warn(
                            '*** WARNING: Building of extension "%s" '
                            'failed: %s' %
                            (ext.name, why))
                    else:
                        log.warn(
                            'optional extension "%s" '
                            'did not build correctly -- skipped' %
                            (ext.name,))
                else:
                    successfully_built = 1

        finally:
            # Cleanup local changes to the configuration
            self.build_temp = build_temp_base

        # Processing for successfully built extensions
        if successfully_built:

            if isinstance(ext, mx_Extension):
                # Add Python packages needed by this extension
                self.distribution.packages.extend(ext.packages)

                # Add data files needed by this extension
                self.distribution.data_files.extend(ext.data_files)

            # Mark as having been built successfully
            ext.successfully_built = 1

    def get_outputs(self):

        # Note: The cache is needed for mx_uninstall when used
        # together with mx_bdist_prebuilt. mx_build will run a
        # recursive .get_outputs() on all sub-commands and then store
        # the sub-command objects in the build pickle.  By using a
        # cache, we can avoid to have the command object to have to
        # rebuild the outputs (this may not be possible due to the
        # missing source files).
        if self.output_files is not None:
            return self.output_files
        else:
            files = build_ext.get_outputs(self)
            self.output_files = files
            return files

#
# mx Build Python command
#

class mx_build_py(build_py):

    """ build_py command which also allows removing Python source code
        after the byte-code compile process.
        
    """
    user_options = build_py.user_options + [
        ('without-source', None, "only include Python byte-code"),
        ]

    boolean_options = build_py.boolean_options + ['without-source']

    # Omit source files ?
    without_source = 0

    # Output cache
    output_files = None
    
    def run(self):

        if self.without_source:
            # --without-source implies byte-code --compile
            if (not self.compile and
                not self.optimize):
                self.compile = 1

        # Build the Python code
        build_py.run(self)

        # Optionally remove source code
        if self.without_source:
            log.info('removing Python source files (--without-source)')
            verbose = self.verbose
            dry_run = self.dry_run
            for file in build_py.get_outputs(self, include_bytecode=0):
                # Only process .py files
                if file[-3:] != '.py':
                    continue
                # Remove source code
                execute(os.remove, (file,), "removing %s" % file,
                        verbose=verbose, dry_run=dry_run)
                # Remove .pyc files (if not requested)
                if not self.compile:
                    filename = file + "c"
                    if os.path.isfile(filename):
                        execute(os.remove, (filename,),
                                "removing %s" % filename,
                                verbose=verbose, dry_run=dry_run)
                # Remove .pyo files (if not requested)
                if self.optimize == 0:
                    filename = file + "o"
                    if os.path.isfile(filename):
                        execute(os.remove, (filename,),
                                "removing %s" % filename,
                                verbose=verbose, dry_run=dry_run)

    def get_outputs(self, include_bytecode=1):

        # Note: The cache is needed for mx_uninstall when used
        # together with mx_bdist_prebuilt. See
        # mx_build_ext.get_outputs() for details.

        # Try cache first
        cache_key = include_bytecode
        if self.output_files is not None:
            files = self.output_files.get(cache_key, None)
            if files is not None:
                return files
        else:
            self.output_files = {}

        # Regular processing
        if (not self.without_source or
            not include_bytecode):
            files = build_py.get_outputs(self, include_bytecode)
            self.output_files[cache_key] = files
            return files

        # Remove source code files from outputs
        files = []
        for file in build_py.get_outputs(self, include_bytecode=1):
            if ((self.without_source and file[-3:] == '.py') or
                (not self.compile and file[-4:] == '.pyc') or
                (not self.optimize and file[-4:] == '.pyo')):
                continue
            files.append(file)
        self.output_files[cache_key] = files
        return files
        
#
# mx Build Data command
#

class mx_build_data(Command):

    """ build_data command which allows copying (external) data files
        into the build tree.
        
    """
    description = "build data files (copy them to the build directory)"

    user_options = [
        ('build-lib=', 'b',
         "directory to store built Unix libraries in"),
        ]
    
    boolean_options = []

    # Location of the build directory
    build_lib = None

    def initialize_options(self):

        self.build_lib = None
        self.outfiles = []

    def finalize_options(self):

        self.set_undefined_options('build',
                                   ('verbose', 'verbose'),
                                   ('build_lib', 'build_lib'),
                                   )
        if _debug:
            # For debugging we are always in very verbose mode...
            self.verbose = 2

    def get_outputs(self):

        return self.outfiles

    def build_data_files(self, data_files):

        """ Copy the data_files to the build_lib directory.

            For tuple entries, this updates the data_files list in
            place and adjusts it, so that the data files are picked
            up from the build directory rather than their original
            location.

        """
        build_lib = self.build_lib
        for i in range(len(data_files)):

            entry = data_files[i]
            copied_data_files = []
            
            if type(entry) == types.StringType:
                # Unix- to platform-convention conversion
                entry = convert_to_platform_path(entry)
                filenames = glob.glob(entry)
                for filename in filenames:
                    dst = os.path.join(build_lib, filename)
                    dstdir = os.path.split(dst)[0]
                    if not self.dry_run:
                        self.mkpath(dstdir)
                        outfile = self.copy_file(filename, dst)[0]
                    else:
                        outfile = dst
                    self.outfiles.append(outfile)
                    # Add to the copied_data_files list (using the
                    # distutils internal Unix path format)
                    copied_data_files.append(
                        convert_to_distutils_path(filename))
                    
            elif type(entry) == types.TupleType:
                origin, target = entry
                # Unix- to platform-convention conversion
                origin = convert_to_platform_path(origin)
                target = convert_to_platform_path(target)
                targetdir, targetname = os.path.split(target)
                origin_pathnames = glob.glob(origin)
                if targetname:
                    # Make sure that we don't copy multiple files to
                    # the same target filename
                    if len(origin_pathnames) > 1:
                        raise ValueError(
                            'cannot copy multiple files to %s' %
                            target)
                for pathname in origin_pathnames:
                    if targetname:
                        # Use the new targetname filename
                        filename = targetname
                    else:
                        # Use the original filename
                        filename = os.path.split(pathname)[1]
                    dst = os.path.join(build_lib,
                                       targetdir,
                                       filename)
                    dstdir = os.path.split(dst)[0]
                    if not self.dry_run:
                        self.mkpath(dstdir)
                        outfile = self.copy_file(pathname, dst)[0]
                    else:
                        outfile = dst
                    self.outfiles.append(outfile)
                    # Add to the copied_data_files list (using the
                    # distutils internal Unix path format)
                    copied_data_files.append(
                        convert_to_distutils_path(
                            os.path.join(targetdir,
                                         filename)))

            else:
                raise ValueError('unsupported data_files item format: %r' %
                                 entry)

            # Clear the data_files entry (we'll clean up the list
            # later on)
            data_files[i] = None

            # Add the new copied_data_files to the data_files, so
            # that install_data can pick up the build version of
            # the data file
            data_files.extend(copied_data_files)

        # Cleanup data_files (remove all None, empty and duplicate
        # entries)
        d = {}
        for filename in data_files:
            if not filename:
                continue
            d[filename] = 1
        data_files[:] = d.keys()

        if _debug:
            print ('After build_data: ')
            print (' data_files=%r' % data_files)
            print (' outfiles=%r' % self.outfiles)
        
    def run(self):

        if not self.distribution.data_files:
            return
        self.build_data_files(self.distribution.data_files)

#
# mx Build Unix Libs command
#
class UnixLibrary:

    """ Container for library configuration data.
    """
    # Name of the library
    libname = ''

    # Source tree where the library lives
    sourcetree = ''

    # List of library files and where to install them in the
    # build tree
    libfiles = None

    # Name of the configure script
    configure = 'configure'

    # Configure options
    configure_options = None

    # Make options
    make_options = None
    
    def __init__(self, libname, sourcetree, libfiles,
                 configure=None, configure_options=None,
                 make_options=None):

        self.libname = libname
        self.sourcetree = sourcetree
        # Check for 2-tuples...
        for libfile, targetdir in libfiles:
            pass
        self.libfiles = libfiles

        # Optional settings
        if configure:
            self.configure = configure
        if configure_options:
            self.configure_options = configure_options
        else:
            self.configure_options = []
        if make_options:
            self.make_options = make_options
        else:
            self.make_options = []
            
    def get(self, option, alternative=None):

        return getattr(self, option, alternative)

class mx_build_unixlib(Command):

    """ This command compiles external libs using the standard Unix
        procedure for this:
        
        ./configure
        make

    """
    description = "build Unix libraries used by Python extensions"

    # make program to use
    make = None
    
    user_options = [
        ('build-lib=', 'b',
         "directory to store built Unix libraries in"),
        ('build-temp=', 't',
         "directory to build Unix libraries to"),
        ('make=', None,
         "make program to use"),
        ('makefile=', None,
         "makefile to use"),
        ('force', 'f',
         "forcibly reconfigure"),
        ]
    
    boolean_options = ['force']

    def initialize_options(self):

        self.build_lib = None
        self.build_temp = None
        self.make = None
        self.makefile = None
        self.force = 0

    def finalize_options(self):

        self.set_undefined_options('build',
                                   ('verbose', 'verbose'),
                                   ('build_lib', 'build_lib'),
                                   ('build_temp', 'build_temp')
                                   )
        if self.make is None:
            self.make = 'make'
        if self.makefile is None:
            self.makefile = 'Makefile'

        if _debug:
            # For debugging we are always in very verbose mode...
            self.verbose = 2

    def run_script(self, script, options=[]):

        if options:
            l = []
            for k, v in options:
                if v is not None:
                    l.append('%s=%s' % (k, v))
                else:
                    l.append(k)
            script = script + ' ' + ' '.join(l)
        log.info('executing script %s' % repr(script))
        if self.dry_run:
            return 0
        try:
            rc = os.system(script)
        except DistutilsExecError, msg:
            raise CompileError, msg
        return rc
    
    def run_configure(self, options=[], dir=None, configure='configure'):

        """ Run the configure script using options is given.

            Options must be a list of tuples (optionname,
            optionvalue).  If an option should not have a value,
            passing None as optionvalue will have the effect of using
            the option without value.

            dir can be given to have the configure script execute in
            that directory instead of the current one.

        """
        cmd = './%s' % configure
        if dir:
            cmd = 'cd %s; ' % dir + cmd
        log.info('running %s in %s' % (configure, dir or '.'))
        rc = self.run_script(cmd, options)
        if rc != 0:
            raise CompileError, 'configure script failed'

    def run_make(self, targets=[], dir=None, make='make', options=[]):

        """ Run the make command for the given targets.

            Targets must be a list of valid Makefile targets.

            dir can be given to have the make program execute in that
            directory instead of the current one.

        """
        cmd = '%s' % make
        if targets:
            cmd = cmd + ' ' + ' '.join(targets)
        if dir:
            cmd = 'cd %s; ' % dir + cmd
        log.info('running %s in %s' % (make, dir or '.'))
        rc = self.run_script(cmd, options)
        if rc != 0:
            raise CompileError, 'make failed'

    def build_unixlib(self, unixlib):

        # Build each lib in its own subdir of build_temp (to
        # avoid accidental sharing of object files)
        build_temp_base = self.build_temp
        libpath = unixlib.libname
        self.build_temp = os.path.join(build_temp_base, libpath)

        try:

            # Get options
            configure = unixlib.configure
            configure_options = unixlib.configure_options
            make_options = unixlib.make_options
            sourcetree = unixlib.sourcetree
            buildtree = os.path.join(self.build_temp, sourcetree)
            libfiles = unixlib.libfiles
            if not libfiles:
                raise DistutilsError, \
                      'no libfiles defined for unixlib "%s"' % \
                      unixlib.name
            log.info('building C lib %s in %s' % (unixlib.libname,
                                                  buildtree))
            # Prepare build
            log.info('preparing build of %s' % unixlib.libname)
            self.mkpath(buildtree)
            self.copy_tree(sourcetree, buildtree)

            # Run configure to build the Makefile
            if not os.path.exists(os.path.join(buildtree, self.makefile)) or \
               self.force:
                self.run_configure(configure_options,
                                   dir=buildtree,
                                   configure=configure)
            else:
                log.info("skipping configure: "
                         "%s is already configured" %
                         unixlib.libname)

            # Run make
            self.run_make(dir=buildtree,
                          make=self.make,
                          options=make_options)

            # Copy libs to destinations
            for sourcefile, destination in libfiles:
                if not destination:
                    continue
                sourcefile = os.path.join(self.build_temp, sourcefile)
                destination = os.path.join(self.build_lib, destination)
                if not os.path.exists(sourcefile):
                    raise CompileError, \
                          'library "%s" failed to build' % sourcefile
                self.mkpath(destination)
                self.copy_file(sourcefile, destination)

        finally:
            # Cleanup local changes to the configuration
            self.build_temp = build_temp_base

    def build_unixlibs(self, unixlibs):

        for unixlib in unixlibs:
            self.build_unixlib(unixlib)

    def get_unixlib_lib_options(self):

        libs = []
        paths = []
        for unixlib in self.distribution.unixlibs:
            for sourcefile, destination in unixlib.libfiles:
                if not destination:
                    # direct linking
                    sourcefile = os.path.join(self.build_temp,
                                              sourcefile)
                    libs.append(sourcefile)
                else:
                    # linking via link path and lib name
                    sourcefile = os.path.basename(sourcefile)
                    libname = os.path.splitext(sourcefile)[0]
                    if libname[:3] == 'lib':
                        libname = libname[3:]
                    libs.append(libname)
                    destination = os.path.join(self.build_lib,
                                               destination)
                    paths.append(destination)
        #print paths, libs
        return paths, libs

    def run(self):

        if not self.distribution.unixlibs:
            return
        self.build_unixlibs(self.distribution.unixlibs)

#
# mx Install command
#

class mx_install(install):

    """ We want install_data to default to install_purelib, if it is
        not given.

    """
    # build_lib attribute copied to the install command from the
    # build command during .finalize_options()
    build_lib = None

    # Force installation into the platlib, even if the package is a
    # pure Python library
    force_non_pure = 0
    
    user_options = install.user_options + [
        ('force-non-pure', None,
         "force installation into the platform dependent directory"),
        ]

    def initialize_options(self):

        install.initialize_options(self)
        self.force_non_pure = 0
    
    def finalize_options(self):

        fix_install_data = (self.install_data is None)
            
        install.finalize_options(self)

        # Force installation into the platform dependent directories,
        # even if this package is a pure Python package
        if self.force_non_pure:
            self.install_purelib = self.install_platlib
            self.install_libbase = self.install_platlib
            self.install_lib = os.path.join(self.install_platlib,
                                            self.extra_dirs)

        # We want install_data to default to install_purelib, if it is
        # not given.
        if fix_install_data:
            # We want data to be installed alongside the Python
            # modules
            self.install_data = self.install_purelib

        # Undo the change introduced in Python 2.4 to bdist_wininst
        # which manipulates the build.build_lib path and adds
        # a target_version specific ending; we simply override
        # the value here (rather than in build), since all install_*
        # commands pick up the .build_lib value from this command
        # rather than build.
        if self.distribution.has_ext_modules():
            build = self.get_finalized_command('build')
            if self.build_lib != build.build_platlib:
                if _debug:
                    print ('resetting build_lib from "%s" to "%s"' %
                           (self.build_lib,
                            build.build_platlib))
                self.build_lib = build.build_platlib

        self.dump_dirs('after applying mx_install fixes')

    def ensure_finalized(self):

        install.ensure_finalized(self)

        # Hack needed for bdist_wininst
        if self.install_data[-5:] == '\\DATA':
            # Install data into the Python module part
            self.install_data = self.install_data[:-5] + '\\PURELIB'

    def run(self):

        install.run(self)
        if _debug:
            print ('install: outputs=%r' % self.get_outputs())

#
# mx Install Data command
#

class mx_install_data(install_data):

    """ Rework the install_data command to something more useful.

        Two data_files formats are supported:

        * string entries
        
            The files (which may include wildcards) are copied to the
            installation directory using the same relative path.

        * tuple entries of the form (orig, dest)

            The files given in orig (which may include wildcards) are
            copied to the dest directory relative to the installation
            directory.

            If dest includes a filename, the file orig is copied to
            the file dest. Otherwise, the original filename is used
            and the file copied to the dest directory.
    
    """

    user_options = install_data.user_options + [
        ('build-lib=', 'b',
         "directory to store built Unix libraries in"),
        ]

    def initialize_options(self):

        self.build_lib = None
        install_data.initialize_options(self)

    def finalize_options(self):

        if self.install_dir is None:
            installobj = self.distribution.get_command_obj('install')
            self.install_dir = installobj.install_data
        if _debug:
            print 'Installing data files to %s' % self.install_dir
        self.set_undefined_options('install',
                                   ('build_lib', 'build_lib'),
                                   )
        install_data.finalize_options(self)

    def run(self):

        if not self.dry_run:
            self.mkpath(self.install_dir)
        data_files = self.get_inputs()
        if _debug:
            print 'install_data: data_files=%r' % self.data_files
        for entry in data_files:

            if type(entry) == types.StringType:
                # Unix- to platform-convention conversion
                entry = convert_to_platform_path(entry)
                # Names in data_files are now relative to the build
                # directory, since mx_build_data has copied them there
                entry = os.path.join(self.build_lib, entry)
                filenames = glob.glob(entry)
                for filename in filenames:
                    relative_filename = remove_path_prefix(
                        filename, self.build_lib)
                    dst = os.path.join(self.install_dir, relative_filename)
                    dstdir = os.path.split(dst)[0]
                    if not self.dry_run:
                        self.mkpath(dstdir)
                        outfile = self.copy_file(filename, dst)[0]
                    else:
                        outfile = dst
                    self.outfiles.append(outfile)

            else:
                raise ValueError('unsupported data_files item format: %r' %
                                 (entry,))

        if _debug:
            print ('install_data: outfiles=%r' % self.outfiles)

#
# mx Install Lib command
#

class mx_install_lib(install_lib):

    """ Patch the install_lib to work around a problem where the
        .get_outputs() method would return filenames like '.pyoo',
        '.pyco', etc.
        
    """
    def _bytecode_filenames (self, filenames):

        """ Create a list of byte-code filenames from the list of
            filenames.

            Files in filenames that are not Python source code are
            ignored.

        """
        bytecode_filenames = []
        for py_file in filenames:
            if py_file[-3:] != '.py':
                continue
            if self.compile:
                bytecode_filenames.append(py_file + "c")
            if self.optimize > 0:
                bytecode_filenames.append(py_file + "o")
        return bytecode_filenames

    def run(self):

        install_lib.run(self)
        if _debug:
            print ('install_lib: outputs=%r' % self.get_outputs())

#
# mx Uninstall command
#
# Credits are due to Thomas Heller for the idea and the initial code
# base for this command (he posted a different version to
# distutils-sig@python.org) in 02/2001.
#

class mx_uninstall(Command):

    description = "uninstall the package files and directories"

    user_options = []

    def initialize_options(self):
        pass

    def finalize_options(self):
        pass
        
    def run(self):

        # Execute build
        log.info('determining installation files')
        log.info('(re)building package')
        savevalue = self.distribution.dry_run
        self.distribution.dry_run = 0
        self.run_command('build')

        # Execute install in dry-run mode
        log.debug('dry-run package install (to determine the installed files)')
        self.distribution.dry_run = 1
        self.run_command('install')
        self.distribution.dry_run = savevalue
        build = self.get_finalized_command('build')
        install = self.get_finalized_command('install')

        # Remove all installed files
        log.info('removing installed files')
        dirs = {}
        filenames = install.get_outputs()
        for filename in filenames:
            if not os.path.isabs(filename):
                raise DistutilsError,\
                      'filename %s from .get_output() not absolute' % \
                      filename

            if os.path.isfile(filename):
                log.info('removing %s' % filename)
                if not self.dry_run:
                    try:
                        os.remove(filename)
                    except OSError, details:
                        log.warn('could not remove file: %s' % details)
                    dir = os.path.split(filename)[0]
                    if not dirs.has_key(dir):
                        dirs[dir] = 1
                    if os.path.splitext(filename)[1] == '.py':
                        # Remove byte-code files as well
                        try:
                            os.remove(filename + 'c')
                        except OSError:
                            pass
                        try:
                            os.remove(filename + 'o')
                        except OSError:
                            pass

            elif os.path.isdir(filename):
                if not dirs.has_key(dir):
                    dirs[filename] = 1

            elif not os.path.splitext(filename)[1] in ('.pyo', '.pyc'):
                log.warn('skipping removal of %s (not found)' %
                         filename)

        # Remove the installation directories
        log.info('removing directories')
        dirs = dirs.keys()
        dirs.sort(); dirs.reverse() # sort descending
        for dir in dirs:
            # Don't remove directories which are listed on sys.path
            if dir in sys.path:
                continue
            # Check the the directory is empty
            dir_content = os.listdir(dir)
            if dir_content:
                log.info('directory %s is not empty - not removing it' %
                         dir)
                continue
            # Remove the directory and warn if this fails
            log.info('removing directory %s' % dir)
            if not self.dry_run:
                try:
                    os.rmdir(dir)
                except OSError, details:
                    log.warn('could not remove directory: %s' % details)

#
# mx register command
#

class mx_register(register):

    """ Register a package with PyPI.

        This version enhances the download_url by optionally adding a
        hash tag to it. The command fetches the current contents of
        the referenced URL and calculates the hash value from it.

    """
    # Add new option --add-hash-tag
    user_options = register.user_options + [
        ('add-hash-tag', None,
         'add a hash tag to the download_url'),
        ]
    boolean_options = register.boolean_options + [
        'add-hash-tag',
        ]

    def initialize_options(self):

        self.add_hash_tag = None
        register.initialize_options(self)

    def finalize_options(self):

        if self.add_hash_tag is None:
            self.add_hash_tag = False
        register.finalize_options(self)

    def run(self):

        if self.add_hash_tag:
            download_url = self.distribution.metadata.download_url
            if download_url:
                download_url = hashed_download_url(
                    download_url,
                    'simple')
                log.info('updating download_url to %r' % download_url)
                self.distribution.metadata.download_url = download_url
        register.run(self)

#
# mx clean command
#

class mx_clean(clean):

    """ Clean up the build directories.

        This version knows about the build pickle.

    """
    def run(self):

        if self.all:
            # Only remove the build pickle, if --all is requested
            build = self.get_finalized_command('build')
            build_pickle = build.get_build_pickle_pathname()
            if os.path.exists(build_pickle):
                log.info('removing %r' % build_pickle)
                try:
                    os.remove(build_pickle)
                except OSError, details:
                    log.warn('could not remove build pickle %s: %s' %
                             (build_pickle, details))

        clean.run(self)

#
# mx sdist command
#

class mx_sdist(sdist):

    """ Build a source distribution.

        This version does not use hard links when preparing the source
        archive - hard links don't match well with symlinks which we
        use in our source repositories.

    """
    # Tags for the build
    tags = None

    def finalize_options(self):

        # Determine tags
        if self.tags is None:
            self.tags = []
        if 'prebuilt' not in self.tags:
            self.tags.append('sdist')
            self.tags.append('source')
            build = self.get_finalized_command('build')
            if (build.has_ext_modules() or
                build.has_c_libraries() or
                build.has_unixlibs()):
                self.tags.append('compiler')

        # Call base method
        sdist.finalize_options(self)

    def make_release_tree(self, base_dir, files):

        # Prepare release dir
        self.mkpath(base_dir)
        self.distribution.metadata.write_pkg_info(base_dir)
        if not files:
            log.warn('no files found in release !')
            return

        # Create dir structure
        log.info('preparing release tree in %s...' % base_dir)
        create_tree(base_dir, files, dry_run=self.dry_run)
        for file in files:
            if not os.path.isfile(file):
                log.warn('%s is not a regular file -- skipping' % file)
            else:
                dest = os.path.join(base_dir, file)
                self.copy_file(file, dest, link=None)

    def make_distribution(self):

        # Call base method to have the distribution(s) built
        sdist.make_distribution(self)

        # Write tag files
        import tarfile, zipfile
        for archive in self.archive_files:
            if zipfile.is_zipfile(archive):
                format_tag = 'zip'
            elif tarfile.is_tarfile(archive):
                format_tag = 'tar'
            write_package_tags(archive, self.tags + [format_tag])

#
# mx sdist_web command
#

# Name of the tags file which hold the tagged download URLs of the web
# installer packages
SETUP_TAGS = 'setup.tags'

# Web installer supported distribution format tags. The web installer
# can only handle packages of these distribution formats.
WEB_INSTALLER_DIST_FORMS = ('prebuilt', 'sdist')

class mx_sdist_web(mx_sdist):

    """ Build a source distribution which works as web installer.

        The distribution only contains a minimal set of files and then
        downloads the remaining files when setup.py is used for the
        first time.

        Note that the corresponding setup.py will have to include the
        needed code to actually implement the web installation of the
        missing files.

    """
    # Files allowed in the web installer version of the sdist
    # distribution; these are re compatible regular expressions which
    # are matched against the package file paths; case does not matter
    allowed_files = [
        # Top-level notice/license/readme files
        r'[^/\\]*README.*',
        r'[^/\\]*LICENSE.*',
        r'[^/\\]*COPYRIGHT.*',
        r'[^/\\]*TERMS.*',
        r'[^/\\]*NOTICE.*',
        # Package info files
        'MANIFEST',
        'PKG-INFO',
        'setup\.cfg',
        # Top-level .py files
        '[\w-]+\.py',
        # SETUP_TAGS is always included
        ]

    user_options = mx_sdist.user_options + [
        ('package-tags=', None,
         'tags file with the available package files'),
        ('base-url=', None,
         'base URL to add to the tag file entries'),
        ('base-url-mode=', None,
         'method to use when adding base URLs to tag file entries'),
        ('exclude-tags=', None,
         'exclude package files which have these tags set '
         '(comma separated list of tags)'),
        ]

    # Defaults
    package_tags = None
    base_url = None
    base_url_mode = 'join'
    exclude_tags = None

    def initialize_options(self):

        # Call base method first
        mx_sdist.initialize_options(self)

        # Create .zip archives per default, instead of using a
        # platform-dependent default
        self.formats = ['zip']

    def finalize_options(self):

        mx_sdist.finalize_options(self)

        # Set tags for the web installer itself
        self.tags.append('webinstaller')

        # Set package tags file
        if self.package_tags is None:
            self.package_tags = SETUP_TAGS
        if _debug:
            print ('using package tags file: %s' % self.package_tags)

        # Set base URL
        if self.base_url is None:
            self.base_url = ''
        if _debug:
            print ('using base URL: %r' % self.base_url)

        # Set exclude tags
        if self.exclude_tags is None:
            self.exclude_tags = set()
        else:
            self.exclude_tags = normalize_tags(self.exclude_tags.split(','))
        if _debug:
            print ('excluding files with tags: %s' % self.exclude_tags)

        # Check setup.py for web installer
        self.check_setup_py()

        # Create setup.tags file
        self.create_setup_tags()

    def check_setup_py(self):

        """ Check setup.py for web installer code.

        """
        try:
            f = open('setup.py', 'r')
        except IOError:
            log.warn('WARNING: Could not open setup.py')
            return
        else:
            data = f.read()
            f.close()
        if 'run_web_installer(' not in data:
            log.warn('WARNING: No call to mxSetup.run_web_installer() found '
                     'in setup.py - missing web installer ?')
        log.info('found call to web installer in setup.py')

    def create_setup_tags(self):

        """ Create a new setup.tags file

        """
        log.info('reading package tags from %s' % self.package_tags)
        package_data = read_package_data(self.package_tags)

        # Filter out the packages that the web installer can actually
        # handle
        log.info('filtering packages to include only '
                 'compatible distribution formats')
        filtered_packages = _subset_packages(package_data,
                                             WEB_INSTALLER_DIST_FORMS)

        # Filter out the packages that have tags in .exclude_tags
        if self.exclude_tags:
            log.info('excluding packages to which have tags %s' %
                     self.exclude_tags)
            filtered_packages = _setminus_packages(filtered_packages,
                                                   self.exclude_tags)

        # Write package tags
        if not filtered_packages:
            raise DistutilsError(
                'no web installable packages found in %s' %
                self.package_tags)
        log.info('writing package tags (with hashes) to %s' % SETUP_TAGS)
        write_package_data(SETUP_TAGS,
                           filtered_packages,
                           base_url=self.base_url,
                           base_url_mode=self.base_url_mode,
                           replace=True,
                           add_hash_tag=True)

    def get_file_list(self):

        # Call base method to build the file list
        mx_sdist.get_file_list(self)

        files = self.filelist.files
        if _debug:
            print ('Original file list: %r' % files)

        # Strip all files which do not match the files in
        # .allowed_files
        pattern_re = re.compile('|'.join(self.allowed_files), re.I)
        for i in range(len(files)-1, -1, -1):
            if pattern_re.match(files[i]) is None:
                if _debug:
                    print (' removing %r' % files[i])
                del files[i]

        # Add setup.tags
        files.append(SETUP_TAGS)
        
        if _debug:
            print ('Updated file list: %r' % files)

        # Note: We do not write this as MANIFEST, since this should
        # contain the files of the web installed package, not those of
        # the web installer package.

# Web installer implementation

class WebInstaller:

    """ Web installer class which can be customized by setup.py
        package implementations as needed.

        The default implementation uses the setup.tags file to
        determine a suitable installation package and downloads this
        into the package installation directory.
        
    """
    # Package installation directory
    dir = None

    # List of landmark files and directories
    landmarks = None

    # Options dictionary (additional keyword parameters passed to the
    # constructor)
    options = None

    def __init__(self, dir, landmarks, **kwargs):

        self.dir = dir
        self.landmarks = list(landmarks)
        self.options = kwargs
        self.finalize_options()

    def finalize_options(self):

        """ Override to parse .options and set additional instance
            attributes.

        """
        pass

    def check_landmarks(self):

        """ Check for presence of a landmark file or directory based
            on the list .landmarks.

            Return the found landmark path or None in case no landmark
            was found in .dir.

        """
        for landmark in self.landmarks:
            landmark_path = os.path.join(self.dir, landmark)
            if os.path.exists(landmark_path):
                return landmark
        return None

    def find_matching_package(self):

        """ Find a package from the ones in the setup.tags file
            which matches the current platform.

            Return the URL in case a suitable package is found.
            Raises a DistutilsError in case no such package can be
            determined.

        """
        tags_file = os.path.join(self.dir, SETUP_TAGS)
        package_data = read_package_data(tags_file)
        log.info('trying to find suitable download package')
        try:
            url = find_matching_package(package_data,
                                        required_tags=WEB_INSTALLER_DIST_FORMS)
        except KeyError, reason:
            log.error('web installer could not find a suitable '
                      'download package')
            raise DistutilsError(
                'web installer could not find a suitable '
                'download package: %s' % reason)
        return url

    def confirm_package_download(self, url):

        """ Override to implement a user defined confirmation
            dialog.

            url lists the URL of the package that is about to be
            downloaded.

            The method has to return True for confirmation. False can
            be returned to stop the web installer. The .run() method
            will then return False, but no exception is raised. This
            can be useful to continue setup.py with only the
            information available in the web installer package,
            e.g. to determine meta information.

        """
        return True

    def install_package_from_url(self, url):

        """ Download the installer package from the given url and
            install it in .dir.

            Raises a DistutilsError in case the package could not be
            downloaded.

        """
        try:
            install_web_package(url, self.dir)
        except urllib2.URLError, reason:
            log.error('web installer could not download the package %s: %s' %
                      (url, reason))
            raise DistutilsError(
                'web installer could not download the package %s: %s' %
                (url, reason))

    def run(self):

        """ Run the web installer for the package unpacked in .dir.

            The installer function will only run the download in case
            none of the .landmarks file/dirs are not found in dir. The
            .landmarks should not be present in the web installer
            itself.

            Returns True/False depending on whether the web installer
            downloaded a package or not.

        """
        # Check whether the web installer already ran
        landmark = self.check_landmarks()
        if landmark:
            # Web installer already ran
            if _debug:
                print ('web installer found landmark %r: skipping download' %
                       landmark)
            return False
        log.info('web installer running')

        # Load package data from setup.tags file
        url = self.find_matching_package()
        log.info('found package URL %s' % url)

        # Confirm download
        if not self.confirm_package_download(url):
            return False

        # Download the installer package
        self.install_package_from_url(url)

        # Make sure that the landmark exists now
        landmark = self.check_landmarks()
        if not landmark:
            log.error('downloaded package is missing landmark files/dirs')
            raise DistutilsError(
                'downloaded package is missing landmark files/dirs')

        return True

class CryptoWebInstaller(WebInstaller):

    # Download terms text; this is formatted using a namespace which
    # includes the url and the optional additional keyword arguments
    # passed to the constructor (.options).
    terms = None

    # External confirmation string; if true, no confirmation dialog is
    # shown
    confirm = None

    def finalize_options(self):

        # Set additional attributes
        if 'terms' in self.options:
            self.terms = self.options['terms']
        if 'confirm' in self.options:
            self.confirm = self.options['confirm']

        # Check for command line / OS environment confirmation
        if self.confirm is None:
            if '--crypto-confirm' in sys.argv:
                sys.argv.remove('--crypto-confirm')
                self.confirm = 'ok (via --crypto-confirm)'
            elif 'EGENIX_CRYPTO_CONFIRM' in os.environ:
                self.confirm = 'ok (via EGENIX_CRYPTO_CONFIRM)'

    def confirm_package_download(self, url):

        if not self.terms:
            # Nothing to confirm
            return True

        # Show terms text and ask for confirmation
        if '#' in url:
            # Remove hash fragment
            url = url.split('#')[0]
        namespace = dict(url=url)
        namespace.update(self.options)
        # pip agressively caches package setup.py output and doesn't
        # display this unless the user actively asks for it (in some
        # cases not even then: see #1614), so we use write_console()
        # instead of print().
        write_console(
            '-' * 72 + '\n' +
            self.terms % namespace +
            '\n' +
            '-' * 72 + '\n' +
            'Please confirm the above terms by entering "ok":\n')
        if self.confirm:
            data = self.confirm
        else:
            # Interactive
            write_console(
                '\n(there is a 5 seconds timeout; '
                'hit enter to disable the timeout ...)\n')
            try:
                data = read_console(timeout=5).strip()
            except (TimeoutError, IOError, NotSupportedError), reason:
                raise DistutilsError(
                    'Crypto confirmation timed out or did not succeed.\n\n'
                    '>>> Please try setting the OS environment variable\n'
                    '>>> EGENIX_CRYPTO_CONFIRM to 1 to confirm the crypto\n'
                    '>>> download via the OS environment, e.g. for pip\n'
                    '>>> versions which filter away the installer output.\n'
                    '>>> or zc.buildout:\n'
                    '>>>\n'
                    '>>> export EGENIX_CRYPTO_CONFIRM=1 (bash shell)\n'
                    '>>> setenv EGENIX_CRYPTO_CONFIRM 1 (C shell)\n'
                    '>>> set EGENIX_CRYPTO_CONFIRM=1    (Windows)\n'
                    '\n')
            if not data:
                write_console(
                    '(timeout disabled ...)\n\n')
            while not data:
                # Read 'ok' after use hit return one or more times
                write_console(
                    'Please confirm the above terms by entering "ok":\n')
                data = read_console().strip()
            data += ' (via direct entry)'
        write_console(data + '\n')
        write_console('-'*72 + '\n')
        data = data.strip().lower()
        if data.startswith('ok'):
            return True
        else:
            raise DistutilsError(
                'Crypto confirmation not accepted. Aborting.')

def run_web_installer(dir, landmarks, web_installer_class=WebInstaller,
                      **kwargs):

    """ Run the web installer for the package unpacked in dir.

        The function will only run the download in case none of the
        landmarks file/dirs are not found in dir. The landmarks should
        not be present in the web installer itself.

        web_installer_class can be given to use a custom WebInstaller
        subclass instead of the default one.

        Additional keyword arguments are passed to the
        web_installer_class constructor, if given.

        Returns True/False depending on whether the web installer
        downloaded a package or not.

    """
    web_installer = web_installer_class(dir, landmarks, **kwargs)
    return web_installer.run()

#
# mx generic binary distribution command
#

class mx_bdist(bdist):

    """ Generic binary distribution command.
    
    """
    
    def finalize_options(self):

        # Default to <platform>-<pyversion> on all platforms
        if self.plat_name is None:
            self.plat_name = '%s-py%s' % (mx_get_platform(), py_version())
        bdist.finalize_options(self)


#
# mx RPM distribution command
#

class mx_bdist_rpm(bdist_rpm):

    """ bdist_rpm command which allows passing in distutils
        options.

        XXX Note: bdist_rpm no longer works for some reason, probably
            related to distutils rather than our small modification.

    """
    user_options = bdist_rpm.user_options + [
        ('distutils-build-options=', None,
         'extra distutils build options to use before the "build" command'),
        ('distutils-install-options=', None,
         'extra distutils install options to use after the "install" command'),
        ]

    # Defaults
    distutils_build_options = None
    distutils_install_options = None

    def finalize_options(self):

        bdist_rpm.finalize_options(self)
        if self.distutils_build_options is None:
            self.distutils_build_options = ''
        if self.distutils_install_options is None:
            self.distutils_install_options = ''

    def _make_spec_file(self):

        # Generate .spec file
        l = bdist_rpm._make_spec_file(self)

        # Insert into build command
        i = l.index('%build')
        buildcmd = l[i + 1]
        inspos = l[i + 1].find(' build')
        if inspos >= 0:
            l[i + 1] = '%s %s %s' % (buildcmd[:inspos],
                                     self.distutils_build_options,
                                     buildcmd[inspos:])
        else:
            raise DistutilsError, \
                  'could not insert distutils command in RPM build command'
        
        # Insert into install command
        i = l.index('%install')
        installcmd = l[i + 1]
        inspos = l[i + 1].find(' install')
        if inspos >= 0:
            l[i + 1] = '%s %s %s %s' % (installcmd[:inspos],
                                        self.distutils_build_options,
                                        installcmd[inspos:],
                                        self.distutils_install_options)
        else:
            raise DistutilsError, \
                  'could not insert distutils command in RPM install command'

        return l
    
#
# mx bdist_wininst command
#

class mx_bdist_wininst(bdist_wininst):

    """ We want bdist_wininst to include the Python version number
        even for pure Python distribution - in case we don't include
        the Python source code.

    """
    
    def finalize_options(self):

        bdist_wininst.finalize_options(self)

        # Force a target version if without_source was used for
        # build_py
        if not self.target_version:
            build_py = self.get_finalized_command('build_py')
            if build_py.without_source:
                self.target_version = py_version(unicode_aware=0)

#
# mx in-place binary distribution command
#

class mx_bdist_inplace(bdist_dumb):

    """ Build an in-place binary distribution.
    
    """

    # Path prefix to use in the distribution (all files will be placed
    # under this prefix)
    dist_prefix = ''

    user_options = bdist_dumb.user_options + [
        ('dist-prefix=', None,
         'path prefix the binary distribution with'),
        ]

    def finalize_options(self):

        # Default to ZIP as format on all platforms
        if self.format is None:
            self.format = 'zip'
        bdist_dumb.finalize_options(self)

    # Hack to reuse bdist_dumb for our purposes; .run() calls
    # reinitialize_command() with 'install' as command.
    def reinitialize_command(self, command, reinit_subcommands=0):

        cmdobj = bdist_dumb.reinitialize_command(self, command,
                                                 reinit_subcommands)
        if command == 'install':
            cmdobj.install_lib = self.dist_prefix
            cmdobj.install_data = self.dist_prefix
        return cmdobj

#
# mx Zope binary distribution command
#

class mx_bdist_zope(mx_bdist_inplace):

    """ Build binary Zope product distribution.
    
    """

    # Path prefix to use in the distribution (all files will be placed
    # under this prefix); for Zope instances, all code can be placed
    # into the Zope instance directory since this is on sys.path when
    # Zope starts
    dist_prefix = ''

#
# mx bdist_prebuilt command
#

class mx_bdist_prebuilt(mx_sdist):

    """ Build a pre-built distribution.

        The idea is to ship a version of the package that is already
        built, but not yet packaged or installed.

        This makes it possible to do that last step on the client's
        target machine, giving much more flexibility in the way
        software is installed.

    """
    # Skip the build process ?
    skip_build = None

    # Platform name to use
    plat_name = None

    # Keep data source files
    include_data_source_files = None

    # Files to exclude from the prebuilt archives
    exclude_files = None

    # File name version
    #
    # Version 1 format (not easy_install compatible):
    # product_name + '-' + version + '.' + platform + '-' + python_ucs + '.prebuilt.zip'
    #
    # Version 2 format (easy_install compatible):
    # product_name + '-' + version + '-' + python_ucs + '-' + platform + '-prebuilt.zip'
    #
    filename_version = 2

    user_options = mx_sdist.user_options + [
        ('plat-name=', None,
         'platform name to use'),
        ('skip-build', None,
         'skip build and reuse the existing build files'),
        ('include-data-source-files', None,
         'include the data source files '
         'in addition to the build versions'),
        ('exclude-files', None,
         'exclude files matching the given RE patterns '
         '(separated by whitespace)'),
        ]

    def finalize_options(self):

        # Determine tags
        if self.tags is None:
            self.tags = []
        self.tags.append('prebuilt')
        (os_tag,
         code_tag,
         alt_code_tags,
         python_tag,
         ucs_tag) = platform_tags()

        build = self.get_finalized_command('build')
        if (not build.has_ext_modules() and
            not build.has_c_libraries() and
            not build.has_unixlibs() and
            not sys.platform.startswith('win')):
            # We can build a platform independent distribution;
            # note that we cannot build platform independent
            # distributions on Windows, since all path names will
            # use the Windows os.sep which doesn't work on Unix
            # platforms.
            if self.plat_name is None:
                self.plat_name = 'py%s' % py_version(unicode_aware=0)
            self.tags.extend(['anyos', 'purepython', python_tag])
        else:
            # Include the platform name
            if self.plat_name is None:
                if self.filename_version == 1:
                    self.plat_name = '%s-py%s' % (
                        mx_get_platform(),
                        py_version())
                elif self.filename_version == 2:
                    self.plat_name = 'py%s-%s' % (
                        py_version(),
                        mx_get_platform())
                else:
                    raise TypeError('unsupported .filename_version')
            # Add tags
            if os_tag == 'unknown-os':
                raise DistutilsError('unknown OS found by platform_tags()')
            if code_tag == 'unknown-code-type':
                raise DistutilsError('unknown code type found by platform_tags()')
            self.tags.extend([os_tag, code_tag, python_tag, ucs_tag])
            if alt_code_tags:
                # Add fat build tags as well
                self.tags.extend(alt_code_tags)

        # Skip the build step ?
        if self.skip_build is None:
            self.skip_build = 0

        # Include data source files ?
        if self.include_data_source_files is None:
            self.include_data_source_files = 0
        else:
            # If source files are included, the package can also be
            # used to rebuild the package for other platforms, so
            # include the corresponding tags
            self.tags.append('source')
            if (build.has_ext_modules() or
                build.has_c_libraries() or
                build.has_unixlibs()):
                self.tags.append('compiler')

        # Exclude files ?
        if self.exclude_files is None:
            self.exclude_files = []
        else:
            self.exclude_files = [re.compile(pattern.strip())
                                  for pattern in self.exclude_files.split()]

        # Default to ZIP files for all platforms
        if self.formats is None:
            self.formats = ['zip']

        # Call the base method
        mx_sdist.finalize_options(self)

    def get_file_list(self):

        log.info('building list of files to include in the pre-built distribution')
        if not os.path.isfile(self.manifest):
            log.error('manifest missing: '
                      'cannot create pre-built distribution')
            return
        self.read_manifest()

        # Prepare a list of source files needed for install_data
        data_source_files = []
        for entry in self.distribution.data_files:
            if type(entry) is types.TupleType:
                (source_file, dest_file) = entry
            else:
                source_file = entry
            source_file = convert_to_platform_path(source_file)
            data_source_files.append(source_file)
        if _debug:
            print 'found these data source files: %r' % data_source_files

        # Remove most subdirs from the MANIFEST file list
        files = []
        for path in self.filelist.files:
            
            # Note: the MANIFEST file list can use both os.sep and the
            # distutils dir separator as basis

            # Filter files which are not to be included in the archive;
            # we use the distutils path for the filtering
            distutils_path = convert_to_distutils_path(path)
            skip_path = False
            for pattern_re in self.exclude_files:
                if pattern_re.match(distutils_path) is not None:
                    skip_path = True
                    break
            if skip_path:
                if _debug:
                    print '  skipping excluded file: %s' % path
                continue

            # Now filter the remaining files; we'll convert the path
            # the platform version for this
            path = convert_to_platform_path(path)
            if os.sep in path:
                # Subdir entry
                path_components = path.split(os.sep)
                if path_components[0].lower().startswith('doc'):
                    # Top-level documentation directories are always included
                    if _debug:
                        print '  found documentation file: %s' % path
                elif (self.include_data_source_files and
                      path in data_source_files):
                    # Data source files can optionally be included as
                    # well; these will already be in the build area
                    # due to mx_build_data
                    if _debug:
                        print '  found data source file: %s' % path
                else:
                    # Skip all other files in subdirectories
                    if _debug:
                        print '  skipping file: %s' % path
                    continue
            elif _debug:
                print '  found top-level file: %s' % path
            log.info('adding %s' % path)
            files.append(path)

        self.filelist.files = files

        # Add build files
        build = self.get_finalized_command('build')
        self.filelist.files.extend(build.get_outputs())

        if _debug:
            print 'pre-built files:', repr(self.filelist.files)
                
    def run(self):

        if not self.skip_build:
            self.run_command('build')

        mx_sdist.run(self)

    def make_distribution(self):

        if self.filename_version == 1:
            # Version 1 format (not easy_install compatible)
            archive_name = '%s.%s.prebuilt' % (
                self.distribution.get_fullname(),
                self.plat_name)
        elif self.filename_version == 2:
            # Version 2 format (easy_install compatible)
            archive_name = '%s-%s-prebuilt' % (
                self.distribution.get_fullname(),
                self.plat_name)
        else:
            raise TypeError('unsupported .filename_version')
        archive_path = os.path.join(self.dist_dir, archive_name)

        # Create the release tree
        self.make_release_tree(archive_name, self.filelist.files)

        # Add pre-built marker file containing the path to the build
        # pickle with the build information
        prebuilt_pathname = os.path.join(archive_name, PREBUILT_MARKER)
        prebuilt_file = open(prebuilt_pathname, 'w')
        build = self.get_finalized_command('build')
        prebuilt_file.write(build.get_build_pickle_pathname())
        prebuilt_file.close()

        # Create the requested archives
        archive_files = []
        for fmt in self.formats:
            archive = self.make_archive(
                archive_path, fmt, base_dir=archive_name)
            archive_files.append(archive)
            # XXX Not sure what .dist_files is good for...
            #self.distribution.dist_files.append(('sdist', '', archive))
            # Create tag files
            write_package_tags(archive, self.tags)
        self.archive_files = archive_files

        # Remove the release tree
        if not self.keep_temp:
            remove_tree(archive_name, dry_run=self.dry_run)

#
# mx egg binary distribution command
#

class mx_bdist_egg(bdist_dumb):

    """ Build an egg binary distribution.

        This is essentially a bdist_dumb ZIP archive with a special
        name and an .egg extension.

        In addition to the distribution files, it also contains an
        EGG-INFO directory with some additional meta-information about
        the package.
    
    """
    # Build a Unicode-aware egg ? easy_install does not support having
    # UCS2/UCS4 markers in the filename, so we place the egg files
    # into ucs2/ucs4 subdirectories of the --dist-dir if this option
    # is set. Default is not to use these subdirs.
    unicode_aware = None

    user_options = [
        ('plat-name=', 'p',
         'platform name to embed in generated filenames '
         '(default: %s)' % mx_get_platform()),
        ('skip-build', None,
         'skip rebuilding everything (for testing/debugging)'),
        ('dist-dir=', 'd',
         'directory where to put the .egg file'),
        ('unicode-aware', None,
         'put eggs into ucs2/ucs4 subdirectories of --dist-dir'),
        ]

    def finalize_options(self):

        if self.plat_name is None:
            build = self.get_finalized_command('build')
            if (not build.has_ext_modules() and
                not build.has_c_libraries() and
                not build.has_unixlibs()):
                # We can build a platform independent distribution
                self.plat_name = ''
            else:
                # Include the platform name
                self.plat_name = mx_get_platform()

        if self.unicode_aware is None:
            self.unicode_aware = 0

        bdist_dumb.finalize_options(self)

        if self.unicode_aware:
            # Put the eggs into separate dist_dir subdirectories in
            # case unicode aware eggs are to be built
            unicode_subdir = py_unicode_build()
            if unicode_subdir:
                self.dist_dir = os.path.join(self.dist_dir,
                                             unicode_subdir)

    def write_egg_info_file(self, egg_info_dir, filename, lines=()):

        f = open(os.path.join(egg_info_dir, filename), 'wb')
        f.write('\n'.join(lines))
        if lines:
            f.write('\n')
        f.close()

    def run(self):

        if not self.skip_build:
            self.run_command('build')

        # Install the package in the .bdist_dir
        install = self.reinitialize_command('install', reinit_subcommands=1)
        install.root = self.bdist_dir
        install.skip_build = self.skip_build
        install.warn_dir = 0
        # Use an in-place install without prefix
        install.install_lib = ''
        install.install_data = ''
        log.info("installing to %s" % self.bdist_dir)
        self.run_command('install')

        # Remove .egg-info file
        if python_version >= '2.5':
            # install_egg_info was added in Python 2.5 distutils
            install_egg_info = self.get_finalized_command('install_egg_info')
            for filename in install_egg_info.outputs:
                execute(os.remove, (filename,),
                        "removing %s" % filename,
                        verbose=self.verbose, dry_run=self.dry_run)
            install_egg_info.output = []

        # Create EGG-INFO dir in .bdist_dir
        egg_info_dir = os.path.join(self.bdist_dir, 'EGG-INFO')
        self.mkpath(egg_info_dir)
        if not self.dry_run:
            
            # add PKG-INFO file
            self.distribution.metadata.write_pkg_info(egg_info_dir)

            # add not-zip-safe marker to force unzipping the .egg file
            self.write_egg_info_file(egg_info_dir,
                                     'not-zip-safe')

            # add requires.txt
            if python_version >= '2.5':
                self.write_egg_info_file(egg_info_dir,
                                         'requires.txt',
                                         self.distribution.metadata.get_requires())

            # add namespace_packages.txt
            self.write_egg_info_file(egg_info_dir,
                                     'namespace_packages.txt',
                                     self.distribution.namespace_packages)

            # add top_level.txt
            top_level_modules = find_python_modules(self.bdist_dir)
            for namespace_package in self.distribution.namespace_packages:
                if '.' in namespace_package:
                    namespace_package = namespace_package.split('.')[0]
                if namespace_package not in top_level_modules:
                    top_level_modules[namespace_package] = 'namespace'
            self.write_egg_info_file(egg_info_dir,
                                     'top_level.txt',
                                     top_level_modules.keys())
                
            # add dependency_links.txt
            self.write_egg_info_file(egg_info_dir,
                                     'dependency_links.txt',
                                     self.distribution.dependency_links)

            # Add namespace module __init__.py files
            for namespace_package in self.distribution.namespace_packages:
                package_dir = os.path.join(self.bdist_dir,
                                           namespace_package.replace('.', os.sep))
                # We overwrite any existing files, if necessary
                init_file = os.path.join(package_dir, '__init__.py')
                if os.path.exists(init_file):
                    log.info('overwriting %s with setuptools namespace version' %
                             init_file)
                else:
                    log.info('adding %s with setuptools namespace marker' %
                             init_file)
                open(init_file, 'w').write(SETUPTOOLS_NAMESPACE_INIT)
                # Remove any existing byte code files
                for bytecode_suffix in ('c', 'o'):
                    filename = init_file + bytecode_suffix
                    if os.path.exists(filename):
                        execute(os.remove, (filename,),
                                "removing %s" % filename,
                                verbose=self.verbose)

        # Build egg file from .bdist_dir
        egg_name = self.distribution.metadata.get_name().replace('-', '_')
        if self.plat_name:
            unicode_aware = False # self.unicode_aware
        else:
            unicode_aware = False
        archive_basename = "%s-%s-py%s" % (
            egg_name,
            self.distribution.metadata.get_version(),
            py_version(unicode_aware=unicode_aware))
        if self.plat_name:
            archive_basename += '-' + self.plat_name
        archive_basepath = os.path.join(self.dist_dir, archive_basename)
        archive_root = self.bdist_dir
        zip_filename = self.make_archive(archive_basepath, format='zip',
                                         root_dir=archive_root)
        assert zip_filename.endswith('.zip')
        egg_filename = zip_filename[:-4] + '.egg'
        if os.path.exists(egg_filename):
            execute(os.remove, (egg_filename,),
                    "removing %s" % egg_filename,
                    verbose=self.verbose, dry_run=self.dry_run)
        self.move_file(zip_filename, egg_filename)

        # Add to distribution files
        if python_version >= '2.5':
            # This features was added in Python 2.5 distutils
            if self.distribution.has_ext_modules():
                pyversion = get_python_version()
            else:
                pyversion = 'any'
            self.distribution.dist_files.append(('bdist_egg',
                                                 pyversion,
                                                 egg_filename))

        # Cleanup
        if not self.keep_temp:
            remove_tree(self.bdist_dir, dry_run=self.dry_run)

# setuptools has its own implementation of bdist_egg, but we need to
# extend it a bit:

if setuptools is not None:

    class mx_bdist_egg_setuptools(bdist_egg):

        """ Build an egg binary distribution.

            We add a --unicode-aware option to make setuptools'
            bdist_egg compatible with out release process.

        """
        # Build a Unicode-aware egg ? easy_install does not support having
        # UCS2/UCS4 markers in the filename, so we place the egg files
        # into ucs2/ucs4 subdirectories of the --dist-dir if this option
        # is set. Default is not to use these subdirs.
        unicode_aware = None

        user_options = bdist_egg.user_options + [
            ('unicode-aware', None,
             'put eggs into ucs2/ucs4 subdirectories of --dist-dir'),
            ]

        def finalize_options(self):

            self.set_undefined_options('bdist',('dist_dir', 'dist_dir'))

            if self.unicode_aware is None:
                self.unicode_aware = 0

            # We have to adjust .dist_dir before calling the
            # setuptools' bdist_egg.finalize_options() method, because
            # it uses .dist_dir for its own finalization.
            if self.unicode_aware:
                # Put the eggs into separate dist_dir subdirectories in
                # case unicode aware eggs are to be built
                unicode_subdir = py_unicode_build()
                if unicode_subdir:
                    self.dist_dir = os.path.join(self.dist_dir,
                                                 unicode_subdir)
                    print ('Adjusted .dist_dir to %s' % self.dist_dir)

            bdist_egg.finalize_options(self)

#
# bdist_wheel distribution command
#
if bdist_wheel is not None:
    
    class mx_bdist_wheel(bdist_wheel):

        """ Build a wheel binary distribution.

            Since wheels rely on PEP 425 platform tags, which use the
            default distutils get_platform(), our default to always
            put the Python version into .plat_name of bdist doesn't
            work well with bdist_wheels, so we undo this here for this
            one command.

            This allows building wheel files from our prebuilt
            archives either separately or during installation (only
            works with pip, if the wheel package is installed as
            well).

        """
        def finalize_options(self):

            if self.plat_name is None:
                tweak_plat_name = True
            else:
                tweak_plat_name = False

            bdist_wheel.finalize_options(self)

            # Use the distutils get_platform() string instead of our
            # .plat_name from the bdist command.
            if tweak_plat_name:
                self.plat_name = mx_get_platform()

#
# mx MSI distribution command
#

if bdist_msi is not None:

    class mx_bdist_msi(bdist_msi):

        """ Build an MSI installer.

            This version allows to customize the product name used for
            the installer.

        """
        # Product name to use for the installer (this is the name that
        # gets displayed in the dialogs and on the installed software
        # list)
        product_name = None

        # Product title to use for the installer
        title = None

        # Platform name to use in the installer filename (new in Python 2.6)
        plat_name = None

        user_options = bdist_msi.user_options + [
            ('product-name=', None,
             'product name to use for the installer'),
            ('title=', None,
             'product title to use for the installer'),
            ]

        def finalize_options(self):

            # Force a target version if without_source was used for
            # build_py; this is needed since bdist_msi start to
            # default to installing to all available Python versions,
            # if no .target_version is given for Python 2.7+
            build_py = self.get_finalized_command('build_py')
            if build_py.without_source:
                self.target_version = py_version(unicode_aware=0)

            bdist_msi.finalize_options(self)
            
            if self.title is None:
                self.title = self.distribution.get_fullname()

            # Inherit the .plat_name from the bdist command
            self.set_undefined_options('bdist',
                                       ('plat_name', 'plat_name'),
                                       )


        # XXX This is basically a copy of bdist_msi.run(), restructured
        #     a bit.

        def run_install(self):

            if not self.skip_build:
                self.run_command('build')

            install = self.reinitialize_command('install', reinit_subcommands=1)
            install.prefix = self.bdist_dir
            install.skip_build = self.skip_build
            install.warn_dir = 0

            install_lib = self.reinitialize_command('install_lib')
            # we do not want to include pyc or pyo files
            install_lib.compile = 0
            install_lib.optimize = 0

            if self.distribution.has_ext_modules():
                # If we are building an installer for a Python version other
                # than the one we are currently running, then we need to ensure
                # our build_lib reflects the other Python version rather than ours.
                # Note that for target_version!=sys.version, we must have skipped the
                # build step, so there is no issue with enforcing the build of this
                # version.
                target_version = self.target_version
                if not target_version:
                    assert self.skip_build, "Should have already checked this"
                    target_version = python_version
                plat_specifier = ".%s-%s" % (self.plat_name, target_version)
                build = self.get_finalized_command('build')
                build.build_lib = os.path.join(build.build_base,
                                               'lib' + plat_specifier)

            log.info("installing to %s", self.bdist_dir)
            install.ensure_finalized()

            # avoid warning of 'install_lib' about installing
            # into a directory not in sys.path
            sys.path.insert(0, os.path.join(self.bdist_dir, 'PURELIB'))

            install.run()

            del sys.path[0]

        def get_product_version(self):

            # ProductVersion must be strictly numeric
            version = self.distribution.metadata.get_version()
            try:
                return '%d.%d.%d' % StrictVersion(version).version
            except ValueError:
                # Remove any pre-release or snapshot parts
                try:
                    verstuple = parse_mx_version(version)
                except ValueError:
                    raise DistutilsError(
                        'package version must be formatted with mx_version()')
                major, minor, patch = verstuple[:3]
                new_version = mx_version(major, minor, patch)
                log.warn(
                    "bdist_msi requires strictly numeric "
                    "version numbers: "
                    "using %r for MSI installer, instead of %r" %
                    (new_version, version))
                return new_version

        def get_product_name(self):

            # User defined product name
            if self.product_name is not None:
                product_name = self.product_name

            else:
                # Emulate bdist_msi default behavior, but make the
                # product title changeable.
                #
                # Prefix ProductName with Python x.y, so that
                # it sorts together with the other Python packages
                # in Add-Remove-Programs (APR)
                if self.target_version:
                    product_name = (
                        'Python %s %s %s' % (
                            self.target_version,
                            self.title,
                            self.distribution.metadata.get_version()))
                else:
                    # Group packages under "Python 2.x" if no
                    # .target_version is given
                    product_name = (
                        'Python 2.x %s %s' % (
                            self.title,
                            self.distribution.metadata.get_version()))
            log.info('using %r as product name.' % product_name)
            return product_name

        def run (self):

            self.run_install()

            # Create the installer
            self.mkpath(self.dist_dir)
            fullname = self.distribution.get_fullname()
            installer_name = self.get_installer_filename(fullname)
            log.info('creating MSI installer %s' % installer_name)
            installer_name = os.path.abspath(installer_name)
            if os.path.exists(installer_name):
                os.unlink(installer_name)

            metadata = self.distribution.metadata
            author = metadata.author
            if not author:
                author = metadata.maintainer
            if not author:
                author = "UNKNOWN"
            version = metadata.get_version()
            product_version = self.get_product_version()
            product_name = self.get_product_name()
            self.db = msilib.init_database(
                installer_name,
                msilib.schema,
                product_name,
                msilib.gen_uuid(),
                product_version,
                author)

            # Add tables
            msilib.add_tables(self.db, msilib.sequence)

            # Add meta-data
            props = [('DistVersion', version)]
            email = metadata.author_email or metadata.maintainer_email
            if email:
                props.append(("ARPCONTACT", email))
            if metadata.url:
                props.append(("ARPURLINFOABOUT", metadata.url))
            if props:
                msilib.add_data(self.db, 'Property', props)

            # Add sections
            self.add_find_python()
            self.add_files()
            self.add_scripts()
            self.add_ui()

            # Write the file and append to distribution's .dist_files
            self.db.Commit()
            if hasattr(self.distribution, 'dist_files'):
                self.distribution.dist_files.append(
                    ('bdist_msi', self.target_version, fullname))

            # Cleanup
            if not self.keep_temp:
                remove_tree(self.bdist_dir, dry_run=self.dry_run)

        def get_installer_filename(self, fullname):

            return os.path.join(self.dist_dir,
                                "%s.%s.msi" %
                                (fullname,
                                 self.plat_name))

else:
    
    class mx_bdist_msi:
        pass

if 0:
    # Hack to allow quick debugging of the mx_bdist_msi command
    if os.name == 'nt' and bdist_msi is None:
        raise TypeError('just testing...')

### Helper to make prebuilt packages compatible with setuptools' bdist_egg

if setuptools is not None:

    from setuptools.command import egg_info
    from distutils.filelist import FileList

    class mx_egg_info(egg_info.egg_info):

        def find_sources(self):
            
            """ This method is used to generate the SOURCES.txt
                manifest file in the EGG-INFO directory.

                Since there's no clear use of that file and it
                prevents building eggs from prebuilt binaries, we'll
                just return a list with the EGG-INFO files.

            """
            self.filelist = FileList()
            egg_info = self.get_finalized_command('egg_info')
            self.filelist.include_pattern('*',
                                          prefix=egg_info.egg_info)

else:
    mx_egg_info = None

#
# mx Upload command
#
if upload is not None:

    class mx_upload(upload):

        """ Upload command which allows to separate uploads from distribution
            builds.

            The distribution files can be given using the --dist-file command. 
            The argument has to be of the form "command,pyversion,filename"
            with:
            
            * command == 'bdist_egg', 'sdist' or 'bdist_msi'
            * pyversion == '', '2.6', '2.7', etc.
            * filename == filename of the distribution file

            command has to be the distribution command which results in
            the given distribution file.
            
        """

        # Distribution file entry
        dist_file = None

        user_options = upload.user_options + [
            ('dist-file=', None,
             'distribution file given as "command,pyversion,filename"'),
            ]

        def finalize_options(self):

            if self.dist_file is not None:
                dist_file = self.dist_file.split(',')
                if not len(dist_file) == 3:
                    raise DistutilsError(
                        '--dist-file format is "command,pyversion,filename"'
                        ' %r given' %
                        self.dist_file)
                self.dist_file = dist_file

            # Call base method
            upload.finalize_options(self)

        def run(self):
        
            dist_files = self.distribution.dist_files
            if not dist_files and self.dist_file is not None:
                dist_files = [self.dist_file]
            else:
                raise DistutilsOptionError("No dist file created in earlier command")
            for command, pyversion, filename in dist_files:
                self.upload_file(command, pyversion, filename)

else:
    mx_upload = None
    
### Helpers to allow rebasing packages within setup.py files

def rebase_packages(packages, new_base_package, filter=None):

    rebased_packages = []
    for package in packages:
        # Apply filter (only rebase packages for which the filter
        # returns true)
        if (filter is not None and
            not filter(package)):
            rebased_packages.append(package)
        else:
            # Rebase the package
            rebased_packages.append(new_base_package + '.' + package)
    return rebased_packages

def rebase_files(files, new_base_dir, filter=None):

    rebased_files = []
    for file in files:
        # Apply filter (only rebase packages for which the filter
        # returns true)
        if (filter is not None and
            not filter(file)):
            rebased_files.append(file)
        else:
            # Rebase the file
            rebased_files.append(os.path.join(new_base_dir, file))
    return rebased_files

def rebase_extensions(extensions,
                      new_base_package, new_base_dir,
                      filter_packages=None, filter_files=None):

    rebased_extensions = []
    for ext in extensions:

        # Apply package filter to the extension name
        if (filter_packages is not None and
            not filter_packages(ext)):
            rebased_extensions.append(ext)
            continue

        # Create a shallow copy
        new_ext = copy.copy(ext)
        rebased_extensions.append(new_ext)

        # Standard distutils Extension
        new_ext.name = new_base_package + '.' + ext.name
        new_ext.sources = rebase_files(
            ext.sources,
            new_base_dir,
            filter_files)
        new_ext.include_dirs = rebase_files(
            ext.include_dirs,
            new_base_dir,
            filter_files)
        new_ext.library_dirs = rebase_files(
            ext.library_dirs,
            new_base_dir,
            filter_files)
        new_ext.runtime_library_dirs = rebase_files(
            ext.runtime_library_dirs,
            new_base_dir,
            filter_files)
        if not isinstance(ext, mx_Extension):
            continue

        # mx_Extension
        new_ext.data_files = rebase_files(
            ext.data_files,
            new_base_dir,
            filter_files)
        new_ext.packages = rebase_packages(
            ext.packages,
            new_base_package,
            filter_packages)

        if 0:
            # optional_libraries will not need any rebasing, since
            # the header files rely on the standard search path
            new_optional_libraries = []
            for (libname, header_files) in ext.optional_libraries:
                new_optional_libraries.append(
                    (libname,
                     rebase_files(header_files,
                                  new_base_dir,
                                  filter_files)))
            new_ext.optional_libraries = new_optional_libraries

        new_needed_includes = []
        for (filename, dirs, pattern) in ext.needed_includes:
            new_needed_includes.append(
                (filename,
                 rebase_files(dirs,
                              new_base_dir,
                              filter_files),
                 pattern))
        new_ext.needed_includes = new_needed_includes

        new_needed_libraries = []
        for (filename, dirs, pattern) in ext.needed_libraries:
            new_needed_libraries.append(
                (filename,
                 rebase_files(dirs,
                              new_base_dir,
                              filter_files),
                 pattern))
        new_ext.needed_libraries = new_needed_libraries

    return rebased_extensions
    
###

def run_setup(configurations):

    """ Run distutils setup.

        The parameters passed to setup() are extracted from the list
        of modules, classes or instances given in configurations.

        Names with leading underscore are removed from the parameters.
        Parameters which are not strings, lists or tuples are removed
        as well.  Configurations which occur later in the
        configurations list override settings of configurations
        earlier in the list.

    """
    # Build parameter dictionary
    kws = {
        # Defaults for distutils and our add-ons
        #'version': '0.0.0',
        #'name': '',
        #'description': '',
        'license': ('(c) eGenix.com Sofware, Skills and Services GmbH, '
                    'All Rights Reserved.'),
        'author': 'eGenix.com Software, Skills and Services GmbH',
        'author_email': 'info@egenix.com',
        'maintainer': 'eGenix.com Software, Skills and Services GmbH',
        'maintainer_email': 'info@egenix.com',
        'url': 'http://www.egenix.com/',
        'download_url': 'http://www.egenix.com/',
        'platforms': [],
        'classifiers': [],
        'packages': [],
        'ext_modules': [],
        'data_files': [],
        'libraries': [],
    }
    if setuptools is not None:
        # Add defaults for setuptools
        kws.update({
            # Default to not install eggs as ZIP archives
            'zip_safe': 0,
            })
    for configuration in configurations:
        kws.update(vars(configuration))

    # Type and name checking
    for name, value in kws.items():
        if (name[:1] == '_' or
            name in UNSUPPORTED_SETUP_KEYWORDS):
            del kws[name]
            continue
        if not isinstance(value, ALLOWED_SETUP_TYPES):
            if isinstance(value, types.UnicodeType):
                # Convert Unicode values to UTF-8 encoded strings
                kws[name] = value.encode('utf-8')
            else:
                del kws[name]
            continue
        #if type(value) is types.FunctionType:
        #    kws[name] = value()

    if setuptools is not None:
        # Map requires to install_requires for setuptools
        if ('requires' in kws and
            'install_requires' not in kws):
            # Setuptools doesn't like hypens in package names, so we
            # convert them to underscores
            deps = [dep.replace('-', '_') for dep in kws['requires']]
            kws['install_requires'] = deps

    # Add setup extensions
    kws['distclass'] = mx_Distribution
    extensions = {'build': mx_build,
                  'build_unixlib': mx_build_unixlib,
                  'mx_autoconf': mx_autoconf,
                  'build_ext': mx_build_ext,
                  'build_clib': mx_build_clib,
                  'build_py': mx_build_py,
                  'build_data': mx_build_data,
                  'install': mx_install,
                  'install_data': mx_install_data,
                  'install_lib': mx_install_lib,
                  'uninstall': mx_uninstall,
                  'register': mx_register,
                  'bdist': mx_bdist,
                  'bdist_rpm': mx_bdist_rpm,
                  'bdist_zope': mx_bdist_zope,
                  'bdist_inplace': mx_bdist_inplace,
                  'bdist_wininst': mx_bdist_wininst,
                  'bdist_msi': mx_bdist_msi,
                  'bdist_prebuilt': mx_bdist_prebuilt,
                  'mx_bdist_egg': mx_bdist_egg,
                  'sdist': mx_sdist,
                  'sdist_web': mx_sdist_web,
                  'clean': mx_clean,
                  'upload': mx_upload,
                  }
    if bdist_ppm is not None:
        extensions['bdist_ppm'] = bdist_ppm.bdist_ppm
    if GenPPD is not None:
        extensions['GenPPD'] = GenPPD.GenPPD
    if mx_egg_info is not None:
        extensions['egg_info'] = mx_egg_info
    if setuptools is None:
        extensions['bdist_egg'] = mx_bdist_egg
    else:
        extensions['bdist_egg'] = mx_bdist_egg_setuptools
    if bdist_wheel is not None:
        extensions['bdist_wheel'] = mx_bdist_wheel
        
    kws['cmdclass'] = extensions

    # Invoke distutils setup
    if _debug > 1:
        print ('calling setup() with kws %r' % kws)
    apply(setup, (), kws)

