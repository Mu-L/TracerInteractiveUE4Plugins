#!/usr/bin/env python2
# -*- Mode: python -*-

'''
emcc - compiler helper script
=============================

emcc is a drop-in replacement for a compiler like gcc or clang.

See  emcc --help  for details.

emcc can be influenced by a few environment variables:

  EMCC_DEBUG - "1" will log out useful information during compilation, as well as
               save each compiler step as an emcc-* file in the temp dir
               (by default /tmp/emscripten_temp). "2" will save additional emcc-*
               steps, that would normally not be separately produced (so this
               slows down compilation).

  EMMAKEN_NO_SDK - Will tell emcc *not* to use the emscripten headers. Instead
                   your system headers will be used.

  EMMAKEN_COMPILER - The compiler to be used, if you don't want the default clang.
'''

from tools.toolchain_profiler import ToolchainProfiler, exit
if __name__ == '__main__':
  ToolchainProfiler.record_process_start()

import os, sys, shutil, tempfile, subprocess, shlex, time, re, logging, urllib
from subprocess import PIPE
from tools import shared, jsrun, system_libs
from tools.shared import execute, suffix, unsuffixed, unsuffixed_basename, WINDOWS, safe_move
from tools.response_file import read_response_file
import tools.line_endings

# endings = dot + a suffix, safe to test by  filename.endswith(endings)
C_ENDINGS = ('.c', '.C', '.i')
CXX_ENDINGS = ('.cpp', '.cxx', '.cc', '.c++', '.CPP', '.CXX', '.CC', '.C++', '.ii')
OBJC_ENDINGS = ('.m', '.mi')
OBJCXX_ENDINGS = ('.mm', '.mii')
SPECIAL_ENDINGLESS_FILENAMES = ('/dev/null',)

SOURCE_ENDINGS = C_ENDINGS + CXX_ENDINGS + OBJC_ENDINGS + OBJCXX_ENDINGS + SPECIAL_ENDINGLESS_FILENAMES
C_ENDINGS = C_ENDINGS + SPECIAL_ENDINGLESS_FILENAMES # consider the special endingless filenames like /dev/null to be C

BITCODE_ENDINGS = ('.bc', '.o', '.obj', '.lo')
DYNAMICLIB_ENDINGS = ('.dylib', '.so') # Windows .dll suffix is not included in this list, since those are never linked to directly on the command line.
STATICLIB_ENDINGS = ('.a',)
ASSEMBLY_ENDINGS = ('.ll',)
HEADER_ENDINGS = ('.h', '.hxx', '.hpp', '.hh', '.H', '.HXX', '.HPP', '.HH')
WASM_ENDINGS = ('.wasm', '.wast')

SUPPORTED_LINKER_FLAGS = ('--start-group', '-(', '--end-group', '-)')

LIB_PREFIXES = ('', 'lib')

JS_CONTAINING_SUFFIXES = ('js', 'html')
EXECUTABLE_SUFFIXES = JS_CONTAINING_SUFFIXES + ('wasm',)

DEFERRED_REPONSE_FILES = ('EMTERPRETIFY_BLACKLIST', 'EMTERPRETIFY_WHITELIST')

# Mapping of emcc opt levels to llvm opt levels. We use llvm opt level 3 in emcc opt
# levels 2 and 3 (emcc 3 is unsafe opts, so unsuitable for the only level to get
# llvm opt level 3, and speed-wise emcc level 2 is already the slowest/most optimizing
# level)
LLVM_OPT_LEVEL = {
  0: 0,
  1: 1,
  2: 3,
  3: 3,
}

DEBUG = os.environ.get('EMCC_DEBUG')
if DEBUG == "0":
  DEBUG = None

TEMP_DIR = os.environ.get('EMCC_TEMP_DIR')
LEAVE_INPUTS_RAW = os.environ.get('EMCC_LEAVE_INPUTS_RAW') # Do not compile .ll files into .bc, just compile them with emscripten directly
                                                           # Not recommended, this is mainly for the test runner, or if you have some other
                                                           # specific need.
                                                           # One major limitation with this mode is that libc and libc++ cannot be
                                                           # added in. Also, LLVM optimizations will not be done, nor dead code elimination
# If emcc is running with LEAVE_INPUTS_RAW and then launches an emcc to build something like the struct info, then we don't want
# LEAVE_INPUTS_RAW to be active in that emcc subprocess.
if LEAVE_INPUTS_RAW:
  del os.environ['EMCC_LEAVE_INPUTS_RAW']
AUTODEBUG = os.environ.get('EMCC_AUTODEBUG') # If set to 1, we will run the autodebugger (the automatic debugging tool, see tools/autodebugger).
                                             # Note that this will disable inclusion of libraries. This is useful because including
                                             # dlmalloc makes it hard to compare native and js builds
EMCC_CFLAGS = os.environ.get('EMCC_CFLAGS') # Additional compiler flags that we treat as if they were passed to us on the commandline

# Target options
final = None


class Intermediate(object):
  counter = 0
def save_intermediate(name=None, suffix='js'):
  name = os.path.join(shared.get_emscripten_temp_dir(), 'emcc-%d%s.%s' % (Intermediate.counter, '' if name is None else '-' + name, suffix))
  if type(final) != str:
    logging.debug('(not saving intermediate %s because deferring linking)' % name)
    return
  shutil.copyfile(final, name)
  Intermediate.counter += 1


class TimeLogger(object):
  last = time.time()

  @staticmethod
  def update():
    TimeLogger.last = time.time()

def log_time(name):
  """Log out times for emcc stages"""
  if DEBUG:
    now = time.time()
    logging.debug('emcc step "%s" took %.2f seconds', name, now - TimeLogger.last)
    TimeLogger.update()


class EmccOptions(object):
  def __init__(self):
    self.opt_level = 0
    self.debug_level = 0
    self.shrink_level = 0
    self.requested_debug = ''
    self.profiling = False
    self.profiling_funcs = False
    self.tracing = False
    self.emit_symbol_map = False
    self.js_opts = None
    self.force_js_opts = False
    self.llvm_opts = None
    self.llvm_lto = None
    self.default_cxx_std = '-std=c++03' # Enforce a consistent C++ standard when compiling .cpp files, if user does not specify one on the cmdline.
    self.use_closure_compiler = None
    self.js_transform = None
    self.pre_js = '' # before all js
    self.post_module = '' # in js, after Module exists
    self.post_js = '' # after all js
    self.preload_files = []
    self.embed_files = []
    self.exclude_files = []
    self.ignore_dynamic_linking = False
    self.shell_path = shared.path_from_root('src', 'shell.html')
    self.source_map_base = None
    self.js_libraries = []
    self.bind = False
    self.emrun = False
    self.cpu_profiler = False
    self.thread_profiler = False
    self.memory_profiler = False
    self.save_bc = False
    self.memory_init_file = None
    self.use_preload_cache = False
    self.no_heap_copy = False
    self.use_preload_plugins = False
    self.proxy_to_worker = False
    self.default_object_extension = '.o'
    self.valid_abspaths = []
    self.separate_asm = False
    self.cfi = False
    # Specifies the line ending format to use for all generated text files.
    # Defaults to using the native EOL on each platform (\r\n on Windows, \n on Linux&OSX)
    self.output_eol = os.linesep


class JSOptimizer(object):
  def __init__(self, target, options, misc_temp_files, js_transform_tempfiles):
    self.queue = []
    self.extra_info = {}
    self.queue_history = []
    self.blacklist = (os.environ.get('EMCC_JSOPT_BLACKLIST') or '').split(',')
    self.minify_whitespace = False
    self.cleanup_shell = False

    self.target = target
    self.opt_level = options.opt_level
    self.debug_level = options.debug_level
    self.emit_symbol_map = options.emit_symbol_map
    self.profiling_funcs = options.profiling_funcs
    self.use_closure_compiler = options.use_closure_compiler

    self.misc_temp_files = misc_temp_files
    self.js_transform_tempfiles = js_transform_tempfiles

  def flush(self, title='js_opts'):
    self.queue = filter(lambda p: p not in self.blacklist, self.queue)

    assert not shared.Settings.WASM_BACKEND, 'JSOptimizer should not run with pure wasm output'

    if self.extra_info is not None and len(self.extra_info) == 0:
      self.extra_info = None

    if len(self.queue) > 0 and not(not shared.Settings.ASM_JS and len(self.queue) == 1 and self.queue[0] == 'last'):
      passes = self.queue[:]

      if DEBUG != '2' or len(passes) < 2:
        # by assumption, our input is JS, and our output is JS. If a pass is going to run in the native optimizer in C++, then we
        # must give it JSON and receive from it JSON
        chunks = []
        curr = []
        for p in passes:
          if len(curr) == 0:
            curr.append(p)
          else:
            native = shared.js_optimizer.use_native(p, source_map=self.debug_level >= 4)
            last_native = shared.js_optimizer.use_native(curr[-1], source_map=self.debug_level >= 4)
            if native == last_native:
              curr.append(p)
            else:
              curr.append('emitJSON')
              chunks.append(curr)
              curr = ['receiveJSON', p]
        if len(curr) > 0:
          chunks.append(curr)
        if len(chunks) == 1:
          self.run_passes(chunks[0], title, just_split=False, just_concat=False)
        else:
          for i in range(len(chunks)):
            self.run_passes(chunks[i], 'js_opts_' + str(i), just_split='receiveJSON' in chunks[i], just_concat='emitJSON' in chunks[i])
      else:
        # DEBUG 2, run each pass separately
        extra_info = self.extra_info
        for p in passes:
          self.queue = [p]
          self.flush(p)
          self.extra_info = extra_info # flush wipes it
          log_time('part of js opts')
      self.queue_history += self.queue
      self.queue = []
    self.extra_info = {}

  def run_passes(self, passes, title, just_split, just_concat):
    global final
    passes = ['asm'] + passes
    if shared.Settings.PRECISE_F32:
      passes = ['asmPreciseF32'] + passes
    if (self.emit_symbol_map or shared.Settings.CYBERDWARF) and 'minifyNames' in passes:
      passes += ['symbolMap=' + self.target + '.symbols']
    if self.profiling_funcs and 'minifyNames' in passes:
      passes += ['profilingFuncs']
    if self.minify_whitespace and 'last' in passes:
      passes += ['minifyWhitespace']
    if self.cleanup_shell and 'last' in passes:
      passes += ['cleanup']
    logging.debug('applying js optimization passes: %s', ' '.join(passes))
    self.misc_temp_files.note(final)
    final = shared.Building.js_optimizer(final, passes, self.debug_level >= 4,
                                         self.extra_info, just_split=just_split,
                                         just_concat=just_concat)
    self.misc_temp_files.note(final)
    self.js_transform_tempfiles.append(final)
    if DEBUG: save_intermediate(title, suffix='js' if 'emitJSON' not in passes else 'json')

  def do_minify(self):
    """minifies the code.

    this is also when we do certain optimizations that must be done right before or after minification
    """
    if shared.Settings.SPLIT_MEMORY:
      # must be done before minification
      self.queue += ['splitMemory', 'simplifyExpressions']

    if self.opt_level >= 2:
      if self.debug_level < 2 and not self.use_closure_compiler == 2:
        self.queue += ['minifyNames']
      if self.debug_level == 0:
        self.minify_whitespace = True

    if self.use_closure_compiler == 1:
      self.queue += ['closure']
    elif self.debug_level <= 2 and shared.Settings.FINALIZE_ASM_JS and not self.use_closure_compiler:
      self.cleanup_shell = True


#
# Main run() function
#
def run():
  global final
  target = None

  if DEBUG: logging.warning('invocation: ' + ' '.join(sys.argv) + (' + ' + EMCC_CFLAGS if EMCC_CFLAGS else '') + '  (in ' + os.getcwd() + ')')
  if EMCC_CFLAGS: sys.argv.extend(shlex.split(EMCC_CFLAGS))

  if DEBUG and LEAVE_INPUTS_RAW: logging.warning('leaving inputs raw')

  stdout = PIPE if not DEBUG else None # suppress output of child processes
  stderr = PIPE if not DEBUG else None # unless we are in DEBUG mode

  EMCC_CXX = '--emscripten-cxx' in sys.argv
  sys.argv = filter(lambda x: x != '--emscripten-cxx', sys.argv)

  if len(sys.argv) <= 1 or ('--help' not in sys.argv and len(sys.argv) >= 2 and sys.argv[1] != '--version'):
    shared.check_sanity(force=DEBUG)

  misc_temp_files = shared.configuration.get_temp_files()

  # Handle some global flags

  if len(sys.argv) == 1:
    logging.warning('no input files')
    exit(1)

  # read response files very early on
  response_file = True
  while response_file:
    response_file = None
    for index in range(1, len(sys.argv)):
      if sys.argv[index][0] == '@':
        # found one, loop again next time
        response_file = True
        extra_args = read_response_file(sys.argv[index])
        # slice in extra_args in place of the response file arg
        sys.argv[index:index+1] = extra_args
        break

  if len(sys.argv) == 1 or '--help' in sys.argv:
    # Documentation for emcc and its options must be updated in:
    #    site/source/docs/tools_reference/emcc.rst
    # A prebuilt local version of the documentation is available at:
    #    site/build/text/docs/tools_reference/emcc.txt
    #    (it is read from there and printed out when --help is invoked)
    # You can also build docs locally as HTML or other formats in site/
    # An online HTML version (which may be of a different version of Emscripten)
    #    is up at http://kripken.github.io/emscripten-site/docs/tools_reference/emcc.html

    print '''%s

------------------------------------------------------------------

emcc: supported targets: llvm bitcode, javascript, NOT elf
(autoconf likes to see elf above to enable shared object support)
''' % (open(shared.path_from_root('site', 'build', 'text', 'docs', 'tools_reference', 'emcc.txt')).read())
    exit(0)

  elif sys.argv[1] == '--version':
    revision = '(unknown revision)'
    here = os.getcwd()
    os.chdir(shared.path_from_root())
    try:
      revision = execute(['git', 'show'], stdout=PIPE, stderr=PIPE)[0].split('\n')[0]
    except:
      pass
    finally:
      os.chdir(here)
    print '''emcc (Emscripten gcc/clang-like replacement) %s (%s)
Copyright (C) 2014 the Emscripten authors (see AUTHORS.txt)
This is free and open source software under the MIT license.
There is NO warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  ''' % (shared.EMSCRIPTEN_VERSION, revision)
    exit(0)

  elif len(sys.argv) == 2 and sys.argv[1] == '-v': # -v with no inputs
    # autoconf likes to see 'GNU' in the output to enable shared object support
    print 'emcc (Emscripten gcc/clang-like replacement + linker emulating GNU ld) %s' % shared.EMSCRIPTEN_VERSION
    code = subprocess.call([shared.CLANG, '-v'])
    shared.check_sanity(force=True)
    exit(code)

  elif '-dumpmachine' in sys.argv:
    print shared.get_llvm_target()
    exit(0)

  elif '--cflags' in sys.argv:
    # fake running the command, to see the full args we pass to clang
    debug_env = os.environ.copy()
    debug_env['EMCC_DEBUG'] = '1'
    args = filter(lambda x: x != '--cflags', sys.argv)
    with misc_temp_files.get_file(suffix='.o') as temp_target:
      input_file = 'hello_world.c'
      out, err = subprocess.Popen([shared.PYTHON] + args + [shared.path_from_root('tests', input_file), '-c', '-o', temp_target], stderr=subprocess.PIPE, env=debug_env).communicate()
      lines = filter(lambda x: shared.CLANG_CC in x and input_file in x, err.split(os.linesep))
      line = re.search('running: (.*)', lines[0]).group(1)
      parts = shlex.split(line.replace('\\', '\\\\'))
      parts = filter(lambda x: x != '-c' and x != '-o' and input_file not in x and temp_target not in x and '-emit-llvm' not in x, parts)
      print ' '.join(shared.Building.doublequote_spaces(parts[1:]))
    exit(0)

  def is_minus_s_for_emcc(newargs, i):
    assert newargs[i] == '-s'
    if i+1 < len(newargs) and '=' in newargs[i+1] and not newargs[i+1].startswith('-'): # -s OPT=VALUE is for us, -s by itself is a linker option
      return True
    else:
      logging.debug('treating -s as linker option and not as -s OPT=VALUE for js compilation')
      return False

  # If this is a configure-type thing, do not compile to JavaScript, instead use clang
  # to compile to a native binary (using our headers, so things make sense later)
  CONFIGURE_CONFIG = (os.environ.get('EMMAKEN_JUST_CONFIGURE') or 'conftest.c' in sys.argv) and not os.environ.get('EMMAKEN_JUST_CONFIGURE_RECURSE')
  CMAKE_CONFIG = 'CMakeFiles/cmTryCompileExec.dir' in ' '.join(sys.argv)# or 'CMakeCCompilerId' in ' '.join(sys.argv)
  if CONFIGURE_CONFIG or CMAKE_CONFIG:
    debug_configure = 0 # XXX use this to debug configure stuff. ./configure's generally hide our normal output including stderr so we write to a file

    # Whether we fake configure tests using clang - the local, native compiler - or not. if not we generate JS and use node with a shebang
    # Beither approach is perfect, you can try both, but may need to edit configure scripts in some cases
    # By default we configure in js, which can break on local filesystem access, etc., but is otherwise accurate so we
    # disable this if we think we have to. A value of '2' here will force JS checks in all cases. In summary:
    # 0 - use native compilation for configure checks
    # 1 - use js when we think it will work
    # 2 - always use js for configure checks
    use_js = int(os.environ.get('EMCONFIGURE_JS') or 1)

    if debug_configure:
      tempout = '/tmp/emscripten_temp/out'
      if not os.path.exists(tempout):
        open(tempout, 'w').write('//\n')

    src = None
    for arg in sys.argv:
      if arg.endswith(SOURCE_ENDINGS):
        try:
          src = open(arg).read()
          if debug_configure: open(tempout, 'a').write('============= ' + arg + '\n' + src + '\n=============\n\n')
        except:
          pass
      elif arg.endswith('.s'):
        if debug_configure: open(tempout, 'a').write('(compiling .s assembly, must use clang\n')
        if use_js == 1: use_js = 0
      elif arg == '-E' or arg == '-M' or arg == '-MM':
        if use_js == 1: use_js = 0

    if src:
      if 'fopen' in src and '"w"' in src:
        if use_js == 1: use_js = 0 # we cannot write to files from js!
        if debug_configure: open(tempout, 'a').write('Forcing clang since uses fopen to write\n')

    compiler = os.environ.get('CONFIGURE_CC') or (shared.CLANG if not use_js else shared.EMCC) # if CONFIGURE_CC is defined, use that. let's you use local gcc etc. if you need that
    if not ('CXXCompiler' in ' '.join(sys.argv) or EMCC_CXX):
      compiler = shared.to_cc(compiler)

    def filter_emscripten_options(argv):
      idx = 0
      skip_next = False
      for el in argv:
        if skip_next:
          skip_next = False
          idx += 1
          continue
        if not use_js and el == '-s' and is_minus_s_for_emcc(argv, idx): # skip -s X=Y if not using js for configure
          skip_next = True
        if not use_js and el == '--tracing':
          pass
        else:
          yield el
        idx += 1

    if compiler == shared.EMCC: compiler = [shared.PYTHON, shared.EMCC]
    else: compiler = [compiler]
    cmd = compiler + list(filter_emscripten_options(sys.argv[1:]))
    if not use_js:
      cmd += shared.EMSDK_OPTS + ['-D__EMSCRIPTEN__']
      # The preprocessor define EMSCRIPTEN is deprecated. Don't pass it to code in strict mode. Code should use the define __EMSCRIPTEN__ instead.
      if not shared.Settings.STRICT:
        cmd += ['-DEMSCRIPTEN']
    if use_js: cmd += ['-s', 'ERROR_ON_UNDEFINED_SYMBOLS=1'] # configure tests should fail when an undefined symbol exists

    logging.debug('just configuring: ' + ' '.join(cmd))
    if debug_configure: open(tempout, 'a').write('emcc, just configuring: ' + ' '.join(cmd) + '\n\n')

    if not use_js:
      exit(subprocess.call(cmd))
    else:
      only_object = '-c' in cmd
      for i in range(len(cmd)-1):
        if cmd[i] == '-o':
          if not only_object:
            cmd[i+1] += '.js'
          target = cmd[i+1]
          break
      if not target:
        target = 'a.out.js'
      os.environ['EMMAKEN_JUST_CONFIGURE_RECURSE'] = '1'
      ret = subprocess.call(cmd)
      os.environ['EMMAKEN_JUST_CONFIGURE_RECURSE'] = ''
      if not os.path.exists(target): exit(ret) # note that emcc -c will cause target to have the wrong value here;
                                               # but then, we don't care about bitcode outputs anyhow, below, so
                                               # skipping to exit(ret) is fine
      if target.endswith('.js'):
        shutil.copyfile(target, unsuffixed(target))
        target = unsuffixed(target)
      if not target.endswith(BITCODE_ENDINGS):
        src = open(target).read()
        full_node = ' '.join(shared.NODE_JS)
        if os.path.sep not in full_node:
          full_node = '/usr/bin/' + full_node # TODO: use whereis etc. And how about non-*NIX?
        open(target, 'w').write('#!' + full_node + '\n' + src) # add shebang
        import stat
        try:
          os.chmod(target, stat.S_IMODE(os.stat(target).st_mode) | stat.S_IXUSR) # make executable
        except:
          pass # can fail if e.g. writing the executable to /dev/null
      exit(ret)

  if os.environ.get('EMMAKEN_COMPILER'):
    CXX = os.environ['EMMAKEN_COMPILER']
  else:
    CXX = shared.CLANG

  CC = shared.to_cc(CXX)

  # If we got here from a redirection through emmakenxx.py, then force a C++ compiler here
  if EMCC_CXX:
    CC = CXX

  CC_ADDITIONAL_ARGS = shared.COMPILER_OPTS

  EMMAKEN_CFLAGS = os.environ.get('EMMAKEN_CFLAGS')
  if EMMAKEN_CFLAGS: sys.argv += shlex.split(EMMAKEN_CFLAGS)

  # ---------------- Utilities ---------------

  seen_names = {}
  def uniquename(name):
    if name not in seen_names:
      seen_names[name] = str(len(seen_names))
    return unsuffixed(name) + '_' + seen_names[name] + (('.' + suffix(name)) if suffix(name) else '')

  # ---------------- End configs -------------

  if len(sys.argv) == 1 or sys.argv[1] in ['x', 't']:
    # noop ar
    logging.debug('just ar')
    sys.exit(0)

  # Check if a target is specified
  target = None
  for i in range(len(sys.argv)-1):
    if sys.argv[i].startswith('-o='):
      raise Exception('Invalid syntax: do not use -o=X, use -o X')

    if sys.argv[i] == '-o':
      target = sys.argv[i+1]
      sys.argv = sys.argv[:i] + sys.argv[i+2:]
      break

  specified_target = target
  target = specified_target if specified_target is not None else 'a.out.js' # specified_target is the user-specified one, target is what we will generate
  target_basename = unsuffixed_basename(target)

  if '.' in target:
    final_suffix = target.split('.')[-1]
  else:
    final_suffix = ''

  if TEMP_DIR:
    temp_dir = TEMP_DIR
    if os.path.exists(temp_dir):
      shutil.rmtree(temp_dir) # clear it
    os.makedirs(temp_dir)
  else:
    temp_root = shared.TEMP_DIR
    if not os.path.exists(temp_root):
      os.makedirs(temp_root)
    temp_dir = tempfile.mkdtemp(dir=temp_root)

  def in_temp(name):
    return os.path.join(temp_dir, os.path.basename(name))

  def in_directory(root, child):
    # make both path absolute
    root = os.path.realpath(root)
    child = os.path.realpath(child)

    # return true, if the common prefix of both is equal to directory
    # e.g. /a/b/c/d.rst and directory is /a/b, the common prefix is /a/b
    return os.path.commonprefix([root, child]) == root

  # Parses the essential suffix of a filename, discarding Unix-style version numbers in the name. For example for 'libz.so.1.2.8' returns '.so'
  def filename_type_suffix(filename):
    for i in reversed(filename.split('.')[1:]):
      if not i.isdigit():
        return i
    return ''

  def filename_type_ending(filename):
    if filename in SPECIAL_ENDINGLESS_FILENAMES:
      return filename
    suffix = filename_type_suffix(filename)
    return '' if not suffix else ('.' + suffix)

  use_cxx = True

  try:
    with ToolchainProfiler.profile_block('parse arguments and setup'):
      ## Parse args

      newargs = sys.argv[1:]

      # Scan and strip emscripten specific cmdline warning flags
      # This needs to run before other cmdline flags have been parsed, so that warnings are properly printed during arg parse
      newargs = shared.WarningManager.capture_warnings(newargs)

      for i in range(len(newargs)):
        if newargs[i] in ['-l', '-L', '-I']:
          # Scan for individual -l/-L/-I arguments and concatenate the next arg on if there is no suffix
          newargs[i] += newargs[i+1]
          newargs[i+1] = ''

      def detect_fixed_language_mode(args):
        check_next = False
        for item in args:
          if check_next:
            if item in ("c++", "c"):
              return True
            else:
              check_next = False
          if item.startswith("-x"):
            lmode = item[2:] if len(item) > 2 else None
            if lmode in ("c++", "c"):
              return True
            else:
              check_next = True
              continue
        return False

      has_fixed_language_mode = detect_fixed_language_mode(newargs)

      options, settings_changes, newargs = parse_args(newargs)

      for i in range(0, len(newargs)):
        arg = newargs[i]
        if arg == '-xc':
          use_cxx = False
          break
        elif arg == '-xc++':
          use_cxx = True
          break
        elif not arg.startswith('-'):
          if arg.endswith(C_ENDINGS + OBJC_ENDINGS):
            use_cxx = False

      if not use_cxx:
        options.default_cxx_std = '' # Compiling C code with .c files, don't enforce a default C++ std.

      call = CXX if use_cxx else CC

      # If user did not specify a default -std for C++ code, specify the emscripten default.
      if options.default_cxx_std:
        newargs = newargs + [options.default_cxx_std]

      if options.emrun:
        options.pre_js += open(shared.path_from_root('src', 'emrun_prejs.js')).read() + '\n'
        options.post_js += open(shared.path_from_root('src', 'emrun_postjs.js')).read() + '\n'

      if options.cpu_profiler:
        options.post_js += open(shared.path_from_root('src', 'cpuprofiler.js')).read() + '\n'

      if options.memory_profiler:
        options.post_js += open(shared.path_from_root('src', 'memoryprofiler.js')).read() + '\n'

      if options.thread_profiler:
        options.post_js += open(shared.path_from_root('src', 'threadprofiler.js')).read() + '\n'

      if options.js_opts is None:
        options.js_opts = options.opt_level >= 2
      if options.llvm_opts is None:
        options.llvm_opts = LLVM_OPT_LEVEL[options.opt_level]
      if options.memory_init_file is None:
        options.memory_init_file = options.opt_level >= 2

      # TODO: support source maps with js_transform
      if options.js_transform and options.debug_level >= 4:
        logging.warning('disabling source maps because a js transform is being done')
        options.debug_level = 3

      if DEBUG: start_time = time.time() # done after parsing arguments, which might affect debug state

      for i in range(len(newargs)):
        if newargs[i] == '-s':
          if is_minus_s_for_emcc(newargs, i):
            key = newargs[i+1]
            settings_changes.append(key)
            newargs[i] = newargs[i+1] = ''
            assert key != 'WASM_BACKEND', 'do not set -s WASM_BACKEND, instead set EMCC_WASM_BACKEND=1 in the environment'
      newargs = [arg for arg in newargs if arg is not '']

      # Find input files

      # These three arrays are used to store arguments of different types for
      # type-specific processing. In order to shuffle the arguments back together
      # after processing, all of these arrays hold tuples (original_index, value).
      # Note that the index part of the tuple can have a fractional part for input
      # arguments that expand into multiple processed arguments, as in -Wl,-f1,-f2.
      input_files = []
      libs = []
      link_flags = []

      # All of the above arg lists entries contain indexes into the full argument
      # list. In order to add extra implicit args (embind.cc, etc) below, we keep a
      # counter for the next index that should be used.
      next_arg_index = len(newargs)

      has_source_inputs = False
      has_header_inputs = False
      lib_dirs = [shared.path_from_root('system', 'local', 'lib'),
                  shared.path_from_root('system', 'lib')]
      for i in range(len(newargs)): # find input files XXX this a simple heuristic. we should really analyze based on a full understanding of gcc params,
                                    # right now we just assume that what is left contains no more |-x OPT| things
        arg = newargs[i]

        if i > 0:
          prev = newargs[i-1]
          if prev in ['-MT', '-MF', '-MQ', '-D', '-U', '-o', '-x', '-Xpreprocessor', '-include', '-imacros', '-idirafter', '-iprefix', '-iwithprefix', '-iwithprefixbefore', '-isysroot', '-imultilib', '-A', '-isystem', '-iquote', '-install_name', '-compatibility_version', '-current_version', '-I', '-L', '-include-pch']: continue # ignore this gcc-style argument

        if os.path.islink(arg) and os.path.realpath(arg).endswith(SOURCE_ENDINGS + BITCODE_ENDINGS + DYNAMICLIB_ENDINGS + ASSEMBLY_ENDINGS + HEADER_ENDINGS):
          arg = os.path.realpath(arg)

        if not arg.startswith('-'):
          if not os.path.exists(arg):
            logging.error('%s: No such file or directory ("%s" was expected to be an input file, based on the commandline arguments provided)', arg, arg)
            exit(1)

          arg_ending = filename_type_ending(arg)
          if arg_ending.endswith(SOURCE_ENDINGS + BITCODE_ENDINGS + DYNAMICLIB_ENDINGS + ASSEMBLY_ENDINGS + HEADER_ENDINGS) or shared.Building.is_ar(arg): # we already removed -o <target>, so all these should be inputs
            newargs[i] = ''
            if arg_ending.endswith(SOURCE_ENDINGS):
              input_files.append((i, arg))
              has_source_inputs = True
            elif arg_ending.endswith(HEADER_ENDINGS):
              input_files.append((i, arg))
              has_header_inputs = True
            elif arg_ending.endswith(ASSEMBLY_ENDINGS) or shared.Building.is_bitcode(arg): # this should be bitcode, make sure it is valid
              input_files.append((i, arg))
            elif arg_ending.endswith(STATICLIB_ENDINGS + DYNAMICLIB_ENDINGS):
              # if it's not, and it's a library, just add it to libs to find later
              l = unsuffixed_basename(arg)
              for prefix in LIB_PREFIXES:
                if not prefix: continue
                if l.startswith(prefix):
                  l = l[len(prefix):]
                  break
              libs.append((i, l))
              newargs[i] = ''
            else:
              logging.warning(arg + ' is not valid LLVM bitcode')
          elif arg_ending.endswith(STATICLIB_ENDINGS):
            if not shared.Building.is_ar(arg):
              if shared.Building.is_bitcode(arg):
                logging.error(arg + ': File has a suffix of a static library ' + str(STATICLIB_ENDINGS) + ', but instead is an LLVM bitcode file! When linking LLVM bitcode files, use one of the suffixes ' + str(BITCODE_ENDINGS))
              else:
                logging.error(arg + ': Unknown format, not a static library!')
              exit(1)
          else:
            if has_fixed_language_mode:
              newargs[i] = ''
              input_files.append((i, arg))
              has_source_inputs = True
            else:
              logging.error(arg + ": Input file has an unknown suffix, don't know what to do with it!")
              exit(1)
        elif arg.startswith('-L'):
          lib_dirs.append(arg[2:])
          newargs[i] = ''
        elif arg.startswith('-l'):
          libs.append((i, arg[2:]))
          newargs[i] = ''
        elif arg.startswith('-Wl,'):
          # Multiple comma separated link flags can be specified. Create fake
          # fractional indices for these: -Wl,a,b,c,d at index 4 becomes:
          # (4, a), (4.25, b), (4.5, c), (4.75, d)
          link_flags_to_add = arg.split(',')[1:]
          for flag_index, flag in enumerate(link_flags_to_add):
            # Only keep flags that shared.Building.link knows how to deal with.
            # We currently can't handle flags with options (like
            # -Wl,-rpath,/bin:/lib, where /bin:/lib is an option for the -rpath
            # flag).
            if flag in SUPPORTED_LINKER_FLAGS:
              link_flags.append((i + float(flag_index) / len(link_flags_to_add), flag))
          newargs[i] = ''

      original_input_files = input_files[:]

      newargs = [arg for arg in newargs if arg is not '']

      # -c means do not link in gcc, and for us, the parallel is to not go all the way to JS, but stop at bitcode
      has_dash_c = '-c' in newargs
      if has_dash_c:
        assert has_source_inputs or has_header_inputs, 'Must have source code or header inputs to use -c'
        target = target_basename + '.o'
        final_suffix = 'o'
      if '-E' in newargs:
        final_suffix = 'eout' # not bitcode, not js; but just result from preprocessing stage of the input file
      if '-M' in newargs or '-MM' in newargs:
        final_suffix = 'mout' # not bitcode, not js; but just dependency rule of the input file
      final_ending = ('.' + final_suffix) if len(final_suffix) > 0 else ''

      # target is now finalized, can finalize other _target s
      js_target = unsuffixed(target) + '.js'

      asm_target = unsuffixed(js_target) + '.asm.js' # might not be used, but if it is, this is the name
      wasm_text_target = asm_target.replace('.asm.js', '.wast') # ditto, might not be used
      wasm_binary_target = asm_target.replace('.asm.js', '.wasm') # ditto, might not be used

      if final_suffix == 'html' and not options.separate_asm and ('PRECISE_F32=2' in settings_changes or 'USE_PTHREADS=2' in settings_changes):
        options.separate_asm = True
        logging.warning('forcing separate asm output (--separate-asm), because -s PRECISE_F32=2 or -s USE_PTHREADS=2 was passed.')
      if options.separate_asm:
        shared.Settings.SEPARATE_ASM = os.path.basename(asm_target)

      if 'EMCC_STRICT' in os.environ:
        shared.Settings.STRICT = os.environ.get('EMCC_STRICT') != '0'

      # Libraries are searched before settings_changes are applied, so apply the value for STRICT and ERROR_ON_MISSING_LIBRARIES from
      # command line already now.

      def get_last_setting_change(setting):
        return ([None] + filter(lambda x: x.startswith(setting + '='), settings_changes))[-1]

      strict_cmdline = get_last_setting_change('STRICT')
      if strict_cmdline:
        shared.Settings.STRICT = int(strict_cmdline[len('STRICT='):])

      if shared.Settings.STRICT:
        shared.Settings.ERROR_ON_UNDEFINED_SYMBOLS = 1
        shared.Settings.ERROR_ON_MISSING_LIBRARIES = 1

      error_on_missing_libraries_cmdline = get_last_setting_change('ERROR_ON_MISSING_LIBRARIES')
      if error_on_missing_libraries_cmdline:
        shared.Settings.ERROR_ON_MISSING_LIBRARIES = int(error_on_missing_libraries_cmdline[len('ERROR_ON_MISSING_LIBRARIES='):])

      settings_changes.append(system_js_libraries_setting_str(libs, lib_dirs, settings_changes, input_files))

      # If not compiling to JS, then we are compiling to an intermediate bitcode objects or library, so
      # ignore dynamic linking, since multiple dynamic linkings can interfere with each other
      if filename_type_suffix(target) not in JS_CONTAINING_SUFFIXES or options.ignore_dynamic_linking:
        def check(input_file):
          if filename_type_ending(input_file) in DYNAMICLIB_ENDINGS:
            if not options.ignore_dynamic_linking: logging.warning('ignoring dynamic library %s because not compiling to JS or HTML, remember to link it when compiling to JS or HTML at the end', os.path.basename(input_file))
            return False
          else:
            return True
        input_files = [(i, input_file) for (i, input_file) in input_files if check(input_file)]

      if len(input_files) == 0:
        logging.error('no input files\nnote that input files without a known suffix are ignored, make sure your input files end with one of: ' + str(SOURCE_ENDINGS + BITCODE_ENDINGS + DYNAMICLIB_ENDINGS + STATICLIB_ENDINGS + ASSEMBLY_ENDINGS + HEADER_ENDINGS))
        exit(1)

      newargs = CC_ADDITIONAL_ARGS + newargs

      if options.separate_asm and final_suffix != 'html':
        shared.WarningManager.warn('SEPARATE_ASM')

      # If we are using embind and generating JS, now is the time to link in bind.cpp
      if options.bind and final_suffix in JS_CONTAINING_SUFFIXES:
        input_files.append((next_arg_index, shared.path_from_root('system', 'lib', 'embind', 'bind.cpp')))
        next_arg_index += 1

      # Apply optimization level settings
      shared.Settings.apply_opt_level(opt_level=options.opt_level, shrink_level=options.shrink_level, noisy=True)

      if os.environ.get('EMCC_FAST_COMPILER') == '0':
        logging.critical('Non-fastcomp compiler is no longer available, please use fastcomp or an older version of emscripten')
        sys.exit(1)

      # Set ASM_JS default here so that we can override it from the command line.
      shared.Settings.ASM_JS = 1 if options.opt_level > 0 else 2

      # Apply -s settings in newargs here (after optimization levels, so they can override them)
      for change in settings_changes:
        key, value = change.split('=')

        # In those settings fields that represent amount of memory, translate suffixes to multiples of 1024.
        if key in ['TOTAL_STACK', 'TOTAL_MEMORY', 'GL_MAX_TEMP_BUFFER_SIZE', 'SPLIT_MEMORY', 'BINARYEN_MEM_MAX']:
          value = str(shared.expand_byte_size_suffixes(value))

        original_exported_response = False

        if value[0] == '@':
          if key not in DEFERRED_REPONSE_FILES:
            if key == 'EXPORTED_FUNCTIONS':
              original_exported_response = value
            value = open(value[1:]).read()
          else:
            value = '"' + value + '"'
        else:
          value = value.replace('\\', '\\\\')
        exec 'shared.Settings.' + key + ' = ' + value in globals(), locals()
        if key == 'EXPORTED_FUNCTIONS':
          # used for warnings in emscripten.py
          shared.Settings.ORIGINAL_EXPORTED_FUNCTIONS = original_exported_response or shared.Settings.EXPORTED_FUNCTIONS[:]

      # -s ASSERTIONS=1 implies the heaviest stack overflow check mode. Set the implication here explicitly to avoid having to
      # do preprocessor "#if defined(ASSERTIONS) || defined(STACK_OVERFLOW_CHECK)" in .js files, which is not supported.
      if shared.Settings.ASSERTIONS:
        shared.Settings.STACK_OVERFLOW_CHECK = 2

      if not shared.Settings.STRICT:
        # The preprocessor define EMSCRIPTEN is deprecated. Don't pass it to code in strict mode. Code should use the define __EMSCRIPTEN__ instead.
        shared.COMPILER_OPTS += ['-DEMSCRIPTEN']

        # The system include path system/include/emscripten/ is deprecated, i.e. instead of #include <emscripten.h>, one should pass in #include <emscripten/emscripten.h>.
        # This path is not available in Emscripten strict mode.
        if shared.USE_EMSDK:
          shared.C_INCLUDE_PATHS += [shared.path_from_root('system', 'include', 'emscripten')]

      # Use settings

      try:
        assert shared.Settings.ASM_JS > 0, 'ASM_JS must be enabled in fastcomp'
        assert shared.Settings.SAFE_HEAP in [0, 1], 'safe heap must be 0 or 1 in fastcomp'
        assert shared.Settings.UNALIGNED_MEMORY == 0, 'forced unaligned memory not supported in fastcomp'
        assert shared.Settings.FORCE_ALIGNED_MEMORY == 0, 'forced aligned memory is not supported in fastcomp'
        assert shared.Settings.PGO == 0, 'pgo not supported in fastcomp'
        assert shared.Settings.QUANTUM_SIZE == 4, 'altering the QUANTUM_SIZE is not supported'
      except Exception, e:
        logging.error('Compiler settings are incompatible with fastcomp. You can fall back to the older compiler core, although that is not recommended, see http://kripken.github.io/emscripten-site/docs/building_from_source/LLVM-Backend.html')
        raise e

      assert not shared.Settings.PGO, 'cannot run PGO in ASM_JS mode'

      if shared.Settings.SAFE_HEAP:
        if not options.js_opts:
          logging.debug('enabling js opts for SAFE_HEAP')
          options.js_opts = True
        options.force_js_opts = True

      if options.debug_level > 1 and options.use_closure_compiler:
        logging.warning('disabling closure because debug info was requested')
        options.use_closure_compiler = False

      assert not (shared.Settings.NO_DYNAMIC_EXECUTION and options.use_closure_compiler), 'cannot have both NO_DYNAMIC_EXECUTION and closure compiler enabled at the same time'

      if options.use_closure_compiler:
        shared.Settings.USE_CLOSURE_COMPILER = options.use_closure_compiler
        if not shared.check_closure_compiler():
          logging.error('fatal: closure compiler is not configured correctly')
          sys.exit(1)
        if options.use_closure_compiler == 2 and shared.Settings.ASM_JS == 1:
          shared.WarningManager.warn('ALMOST_ASM', 'not all asm.js optimizations are possible with --closure 2, disabling those - your code will be run more slowly')
          shared.Settings.ASM_JS = 2

      if shared.Settings.MAIN_MODULE:
        assert not shared.Settings.SIDE_MODULE
        if shared.Settings.MAIN_MODULE != 2:
          shared.Settings.INCLUDE_FULL_LIBRARY = 1
      elif shared.Settings.SIDE_MODULE:
        assert not shared.Settings.MAIN_MODULE
        options.memory_init_file = False # memory init file is not supported with asm.js side modules, must be executable synchronously (for dlopen)

      if shared.Settings.MAIN_MODULE or shared.Settings.SIDE_MODULE:
        assert shared.Settings.ASM_JS, 'module linking requires asm.js output (-s ASM_JS=1)'
        if shared.Settings.MAIN_MODULE != 2 and shared.Settings.SIDE_MODULE != 2:
          shared.Settings.LINKABLE = 1
        shared.Settings.RELOCATABLE = 1
        shared.Settings.PRECISE_I64_MATH = 1 # other might use precise math, we need to be able to print it
        assert not options.use_closure_compiler, 'cannot use closure compiler on shared modules'
        assert not shared.Settings.ALLOW_MEMORY_GROWTH, 'memory growth is not supported with shared modules yet'

      if shared.Settings.EMULATE_FUNCTION_POINTER_CASTS:
        shared.Settings.ALIASING_FUNCTION_POINTERS = 0

      if shared.Settings.SPLIT_MEMORY:
        assert shared.Settings.SPLIT_MEMORY > shared.Settings.TOTAL_STACK, 'SPLIT_MEMORY must be at least TOTAL_STACK (stack must fit in first chunk)'
        assert shared.Settings.SPLIT_MEMORY & (shared.Settings.SPLIT_MEMORY-1) == 0, 'SPLIT_MEMORY must be a power of 2'
        if shared.Settings.ASM_JS == 1:
          shared.WarningManager.warn('ALMOST_ASM', "not all asm.js optimizations are possible with SPLIT_MEMORY, disabling those.")
          shared.Settings.ASM_JS = 2
        if shared.Settings.SAFE_HEAP:
          shared.Settings.SAFE_HEAP = 0
          shared.Settings.SAFE_SPLIT_MEMORY = 1 # we use our own infrastructure
        assert not shared.Settings.RELOCATABLE, 'no SPLIT_MEMORY with RELOCATABLE'
        assert not shared.Settings.USE_PTHREADS, 'no SPLIT_MEMORY with pthreads'
        if not options.js_opts:
          options.js_opts = True
          logging.debug('enabling js opts for SPLIT_MEMORY')
        options.force_js_opts = True
        if options.use_closure_compiler:
          options.use_closure_compiler = False
          logging.warning('cannot use closure compiler on split memory, for now, disabling')

      if shared.Settings.STB_IMAGE and final_suffix in JS_CONTAINING_SUFFIXES:
        input_files.append((next_arg_index, shared.path_from_root('third_party', 'stb_image.c')))
        next_arg_index += 1
        shared.Settings.EXPORTED_FUNCTIONS += ['_stbi_load', '_stbi_load_from_memory', '_stbi_image_free']
        # stb_image 2.x need to have STB_IMAGE_IMPLEMENTATION defined to include the implementation when compiling
        newargs.append('-DSTB_IMAGE_IMPLEMENTATION')

      if shared.Settings.ASMFS and final_suffix in JS_CONTAINING_SUFFIXES:
        input_files.append((next_arg_index, shared.path_from_root('system', 'lib', 'fetch', 'asmfs.cpp')))
        newargs.append('-D__EMSCRIPTEN_ASMFS__=1')
        next_arg_index += 1
        shared.Settings.NO_FILESYSTEM = 1
        shared.Settings.FETCH = 1
        if not shared.Settings.USE_PTHREADS:
          logging.error('-s ASMFS=1 requires either -s USE_PTHREADS=1 or -s USE_PTHREADS=2 to be set!')
          sys.exit(1)

      if shared.Settings.FETCH and final_suffix in JS_CONTAINING_SUFFIXES:
        input_files.append((next_arg_index, shared.path_from_root('system', 'lib', 'fetch', 'emscripten_fetch.cpp')))
        next_arg_index += 1
        options.js_libraries.append(shared.path_from_root('src', 'library_fetch.js'))

      forced_stdlibs = []
      if shared.Settings.DEMANGLE_SUPPORT:
        shared.Settings.EXPORTED_FUNCTIONS += ['___cxa_demangle']
        forced_stdlibs += ['libcxxabi']

      if not shared.Settings.ONLY_MY_CODE:
        if type(shared.Settings.EXPORTED_FUNCTIONS) in (list, tuple):
          # always need malloc and free to be kept alive and exported, for internal use and other modules
          for required_export in ['_malloc', '_free']:
            if required_export not in shared.Settings.EXPORTED_FUNCTIONS:
              shared.Settings.EXPORTED_FUNCTIONS.append(required_export)
        else:
          logging.debug('using response file for EXPORTED_FUNCTIONS, make sure it includes _malloc and _free')

      assert not (shared.Settings.NO_DYNAMIC_EXECUTION and shared.Settings.RELOCATABLE), 'cannot have both NO_DYNAMIC_EXECUTION and RELOCATABLE enabled at the same time, since RELOCATABLE needs to eval()'

      if shared.Settings.RELOCATABLE:
        assert shared.Settings.GLOBAL_BASE < 1
        if shared.Settings.EMULATED_FUNCTION_POINTERS == 0:
          shared.Settings.EMULATED_FUNCTION_POINTERS = 2 # by default, use optimized function pointer emulation
        shared.Settings.ERROR_ON_UNDEFINED_SYMBOLS = shared.Settings.WARN_ON_UNDEFINED_SYMBOLS = 0
        if not shared.Settings.SIDE_MODULE:
          shared.Settings.EXPORT_ALL = 1

      if shared.Settings.EMTERPRETIFY:
        shared.Settings.FINALIZE_ASM_JS = 0
        #shared.Settings.GLOBAL_BASE = 8*256 # keep enough space at the bottom for a full stack frame, for z-interpreter
        shared.Settings.SIMPLIFY_IFS = 0 # this is just harmful for emterpreting
        shared.Settings.EXPORTED_FUNCTIONS += ['emterpret']
        if not options.js_opts:
          logging.debug('enabling js opts for EMTERPRETIFY')
          options.js_opts = True
        options.force_js_opts = True
        assert options.use_closure_compiler is not 2, 'EMTERPRETIFY requires valid asm.js, and is incompatible with closure 2 which disables that'

      if shared.Settings.DEAD_FUNCTIONS:
        if not options.js_opts:
          logging.debug('enabling js opts for DEAD_FUNCTIONS')
          options.js_opts = True
        options.force_js_opts = True

      if options.proxy_to_worker:
        shared.Settings.PROXY_TO_WORKER = 1

      if options.use_preload_plugins or len(options.preload_files) > 0 or len(options.embed_files) > 0:
        # if we include any files, or intend to use preload plugins, then we definitely need filesystem support
        shared.Settings.FORCE_FILESYSTEM = 1

      if options.proxy_to_worker or options.use_preload_plugins:
        shared.Settings.DEFAULT_LIBRARY_FUNCS_TO_INCLUDE += ['$Browser']

      if not shared.Settings.NO_FILESYSTEM and not shared.Settings.ONLY_MY_CODE:
        shared.Settings.EXPORTED_FUNCTIONS += ['___errno_location'] # so FS can report errno back to C
        if not shared.Settings.NO_EXIT_RUNTIME:
          shared.Settings.EXPORTED_FUNCTIONS += ['_fflush'] # to flush the streams on FS quit
                                                            # TODO this forces 4 syscalls, maybe we should avoid it?

      if shared.Settings.USE_PTHREADS:
        if not any(s.startswith('PTHREAD_POOL_SIZE=') for s in settings_changes):
          settings_changes.append('PTHREAD_POOL_SIZE=0')
        options.js_libraries.append(shared.path_from_root('src', 'library_pthread.js'))
        newargs.append('-D__EMSCRIPTEN_PTHREADS__=1')
        shared.Settings.FORCE_FILESYSTEM = 1 # proxying of utime requires the filesystem
      else:
        options.js_libraries.append(shared.path_from_root('src', 'library_pthread_stub.js'))

      if shared.Settings.USE_PTHREADS:
        if shared.Settings.LINKABLE:
          logging.error('-s LINKABLE=1 is not supported with -s USE_PTHREADS>0!')
          exit(1)
        if shared.Settings.SIDE_MODULE:
          logging.error('-s SIDE_MODULE=1 is not supported with -s USE_PTHREADS>0!')
          exit(1)
        if shared.Settings.MAIN_MODULE:
          logging.error('-s MAIN_MODULE=1 is not supported with -s USE_PTHREADS>0!')
          exit(1)
        if shared.Settings.EMTERPRETIFY:
          logging.error('-s EMTERPRETIFY=1 is not supported with -s USE_PTHREADS>0!')
          exit(1)

      if shared.Settings.OUTLINING_LIMIT:
        if not options.js_opts:
          logging.debug('enabling js opts as optional functionality implemented as a js opt was requested')
          options.js_opts = True
        options.force_js_opts = True

      if shared.Settings.WASM:
        shared.Settings.BINARYEN = 1 # these are synonyms

        # When only targeting wasm, the .asm.js file is not executable, so is treated as an intermediate build file that can be cleaned up.
        if shared.Building.is_wasm_only():
          asm_target = asm_target.replace('.asm.js', '.temp.asm.js')
          if not DEBUG:
            misc_temp_files.note(asm_target)

      assert shared.Settings.TOTAL_MEMORY >= 16*1024*1024, 'TOTAL_MEMORY must be at least 16MB, was ' + str(shared.Settings.TOTAL_MEMORY)
      if shared.Settings.BINARYEN:
        assert shared.Settings.TOTAL_MEMORY % 65536 == 0, 'For wasm, TOTAL_MEMORY must be a multiple of 64KB, was ' + str(shared.Settings.TOTAL_MEMORY)
      else:
        assert shared.Settings.TOTAL_MEMORY % (16*1024*1024) == 0, 'For asm.js, TOTAL_MEMORY must be a multiple of 16MB, was ' + str(shared.Settings.TOTAL_MEMORY)
      assert shared.Settings.TOTAL_MEMORY >= shared.Settings.TOTAL_STACK, 'TOTAL_MEMORY must be larger than TOTAL_STACK, was ' + str(shared.Settings.TOTAL_MEMORY) + ' (TOTAL_STACK=' + str(shared.Settings.TOTAL_STACK) + ')'
      assert shared.Settings.BINARYEN_MEM_MAX == -1 or shared.Settings.BINARYEN_MEM_MAX % 65536 == 0, 'BINARYEN_MEM_MAX must be a multiple of 64KB, was ' + str(shared.Settings.BINARYEN_MEM_MAX)

      if shared.Settings.WASM_BACKEND:
        options.js_opts = None
        shared.Settings.BINARYEN = 1

        # to bootstrap struct_info, we need binaryen
        os.environ['EMCC_WASM_BACKEND_BINARYEN'] = '1'

      if shared.Settings.BINARYEN:
        # set file locations, so that JS glue can find what it needs
        shared.Settings.WASM_TEXT_FILE = os.path.basename(wasm_text_target)
        shared.Settings.WASM_BINARY_FILE = os.path.basename(wasm_binary_target)
        shared.Settings.ASMJS_CODE_FILE = os.path.basename(asm_target)

        shared.Settings.ASM_JS = 2 # when targeting wasm, we use a wasm Memory, but that is not compatible with asm.js opts
        shared.Settings.GLOBAL_BASE = 1024 # leave some room for mapping global vars
        assert not shared.Settings.SPLIT_MEMORY, 'WebAssembly does not support split memory'
        assert not shared.Settings.USE_PTHREADS, 'WebAssembly does not support pthreads'
        if shared.Settings.ELIMINATE_DUPLICATE_FUNCTIONS:
          logging.warning('for wasm there is no need to set ELIMINATE_DUPLICATE_FUNCTIONS, the binaryen optimizer does it automatically')
          shared.Settings.ELIMINATE_DUPLICATE_FUNCTIONS = 0
        # if root was not specified in -s, it might be fixed in ~/.emscripten, copy from there
        if not shared.Settings.BINARYEN_ROOT:
          try:
            shared.Settings.BINARYEN_ROOT = shared.BINARYEN_ROOT
          except:
            pass
        # default precise-f32 to on, since it works well in wasm
        # also always use f32s when asm.js is not in the picture
        if ('PRECISE_F32=0' not in settings_changes and 'PRECISE_F32=2' not in settings_changes) or 'asmjs' not in shared.Settings.BINARYEN_METHOD:
          shared.Settings.PRECISE_F32 = 1
        if options.js_opts and not options.force_js_opts and 'asmjs' not in shared.Settings.BINARYEN_METHOD:
          options.js_opts = None
          logging.debug('asm.js opts not forced by user or an option that depends them, and we do not intend to run the asm.js, so disabling and leaving opts to the binaryen optimizer')
        assert not options.use_closure_compiler == 2, 'closure compiler mode 2 assumes the code is asm.js, so not meaningful for wasm'
        # for simplicity, we always have a mem init file, which may also be imported into the wasm module.
        #  * if we also supported js mem inits we'd have 4 modes
        #  * and js mem inits are useful for avoiding a side file, but the wasm module avoids that anyhow
        options.memory_init_file = True
        # async compilation requires wasm-only mode, and also not interpreting (the interpreter needs sync input)
        if shared.Settings.BINARYEN_ASYNC_COMPILATION == 1 and shared.Building.is_wasm_only() and 'interpret' not in shared.Settings.BINARYEN_METHOD:
          # async compilation requires a swappable module - we swap it in when it's ready
          shared.Settings.SWAPPABLE_ASM_MODULE = 1
        else:
          # if not wasm-only, we can't do async compilation as the build can run in other
          # modes than wasm (like asm.js) which may not support an async step
          shared.Settings.BINARYEN_ASYNC_COMPILATION = 0
          if 'BINARYEN_ASYNC_COMPILATION=1' in settings_changes:
            logging.warning('BINARYEN_ASYNC_COMPILATION requested, but disabled since not in wasm-only mode')

      # wasm outputs are only possible with a side wasm
      if target.endswith(WASM_ENDINGS):
        if not (shared.Settings.BINARYEN and shared.Settings.SIDE_MODULE):
          logging.warning('output file "%s" has a wasm suffix, but we cannot emit wasm by itself, except as a dynamic library (see SIDE_MODULE option). specify an output file with suffix .js or .html, and a wasm file will be created on the side' % target)
          sys.exit(1)

      if shared.Settings.EVAL_CTORS:
        if not shared.Settings.BINARYEN:
          # for asm.js: this option is not a js optimizer pass, but does run the js optimizer internally, so
          # we need to generate proper code for that (for wasm, we run a binaryen tool for this)
          shared.Settings.RUNNING_JS_OPTS = 1
        else:
          if 'interpret' in shared.Settings.BINARYEN_METHOD:
            logging.warning('disabling EVAL_CTORS as the bundled interpreter confuses the ctor tool')
            shared.Settings.EVAL_CTORS = 0
          else:
            # for wasm, we really want no-exit-runtime, so that atexits don't stop us
            if final_suffix in JS_CONTAINING_SUFFIXES and not shared.Settings.NO_EXIT_RUNTIME:
              logging.warning('you should enable  -s NO_EXIT_RUNTIME=1  so that EVAL_CTORS can work at full efficiency (it gets rid of atexit calls which might disrupt EVAL_CTORS)')

      if shared.Settings.ALLOW_MEMORY_GROWTH and shared.Settings.ASM_JS == 1:
        # this is an issue in asm.js, but not wasm
        if not shared.Settings.WASM or 'asmjs' in shared.Settings.BINARYEN_METHOD:
          shared.WarningManager.warn('ALMOST_ASM')
          shared.Settings.ASM_JS = 2 # memory growth does not validate as asm.js http://discourse.wicg.io/t/request-for-comments-switching-resizing-heaps-in-asm-js/641/23

      if options.js_opts:
        shared.Settings.RUNNING_JS_OPTS = 1

      if shared.Settings.CYBERDWARF:
        newargs.append('-g')
        shared.Settings.BUNDLED_CD_DEBUG_FILE = target + ".cd"
        options.js_libraries.append(shared.path_from_root('src', 'library_cyberdwarf.js'))
        options.js_libraries.append(shared.path_from_root('src', 'library_debugger_toolkit.js'))

      if options.tracing:
        if shared.Settings.ALLOW_MEMORY_GROWTH:
          shared.Settings.DEFAULT_LIBRARY_FUNCS_TO_INCLUDE += ['emscripten_trace_report_memory_layout']

      if shared.Settings.ONLY_MY_CODE:
        shared.Settings.DEFAULT_LIBRARY_FUNCS_TO_INCLUDE = []
        options.separate_asm = True
        shared.Settings.FINALIZE_ASM_JS = False

      if shared.Settings.GLOBAL_BASE < 0:
        shared.Settings.GLOBAL_BASE = 8 # default if nothing else sets it

      if shared.Settings.WASM_BACKEND:
        if shared.Settings.SIMD:
          newargs.append('-msimd128')
      else:
        # We leave the -O option in place so that the clang front-end runs in that
        # optimization mode, but we disable the actual optimization passes, as we'll
        # run them separately.
        if options.opt_level > 0:
          newargs.append('-mllvm')
          newargs.append('-disable-llvm-optzns')

      if not shared.Settings.LEGALIZE_JS_FFI:
        assert shared.Building.is_wasm_only(), 'LEGALIZE_JS_FFI incompatible with RUNNING_JS_OPTS and non-wasm BINARYEN_METHOD.'

      shared.Settings.EMSCRIPTEN_VERSION = shared.EMSCRIPTEN_VERSION
      shared.Settings.OPT_LEVEL = options.opt_level
      shared.Settings.DEBUG_LEVEL = options.debug_level
      shared.Settings.PROFILING_FUNCS = options.profiling_funcs

      ## Compile source code to bitcode

      logging.debug('compiling to bitcode')

      temp_files = []

    # exit block 'parse arguments and setup'
    log_time('parse arguments and setup')

    with ToolchainProfiler.profile_block('bitcodeize inputs'):
      # Precompiled headers support
      if has_header_inputs:
        headers = [header for _, header in input_files]
        for header in headers:
          assert header.endswith(HEADER_ENDINGS), 'if you have one header input, we assume you want to precompile headers, and cannot have source files or other inputs as well: ' + str(headers) + ' : ' + header
        args = newargs + shared.EMSDK_CXX_OPTS + headers
        if specified_target:
          args += ['-o', specified_target]
        args = system_libs.process_args(args, shared.Settings)
        logging.debug("running (for precompiled headers): " + call + ' ' + ' '.join(args))
        execute([call] + args) # let compiler frontend print directly, so colors are saved (PIPE kills that)
        sys.exit(0)

      def get_bitcode_file(input_file):
        if final_suffix not in JS_CONTAINING_SUFFIXES:
          # no need for a temp file, just emit to the right place
          if len(input_files) == 1:
            # can just emit directly to the target
            if specified_target:
              if specified_target.endswith('/') or specified_target.endswith('\\') or os.path.isdir(specified_target):
                return os.path.join(specified_target, os.path.basename(unsuffixed(input_file))) + options.default_object_extension
              return specified_target
            return unsuffixed(input_file) + final_ending
          else:
            if has_dash_c: return unsuffixed(input_file) + options.default_object_extension
        return in_temp(unsuffixed(uniquename(input_file)) + options.default_object_extension)

      # Request LLVM debug info if explicitly specified, or building bitcode with -g, or if building a source all the way to JS with -g
      if options.debug_level >= 4 or ((final_suffix not in JS_CONTAINING_SUFFIXES or (has_source_inputs and final_suffix in JS_CONTAINING_SUFFIXES)) and options.requested_debug == '-g'):
        # do not save llvm debug info if js optimizer will wipe it out anyhow (but if source maps are used, keep it)
        if options.debug_level == 4 or not (final_suffix in JS_CONTAINING_SUFFIXES and options.js_opts):
          newargs.append('-g') # preserve LLVM debug info
          options.debug_level = 4

      # Bitcode args generation code
      def get_bitcode_args(input_files):
        file_ending = filename_type_ending(input_files[0])
        args = [call] + newargs + input_files
        if file_ending.endswith(CXX_ENDINGS):
          args += shared.EMSDK_CXX_OPTS
        if not shared.Building.can_inline():
          args.append('-fno-inline-functions')
        args = system_libs.process_args(args, shared.Settings)
        return args

      # -E preprocessor-only support
      if '-E' in newargs or '-M' in newargs or '-MM' in newargs:
        input_files = map(lambda x: x[1], input_files)
        cmd = get_bitcode_args(input_files)
        if specified_target:
          cmd += ['-o', specified_target]
        # Do not compile, but just output the result from preprocessing stage or output the dependency rule. Warning: clang and gcc behave differently with -MF! (clang seems to not recognize it)
        logging.debug(('just preprocessor ' if '-E' in newargs else 'just dependencies: ') + ' '.join(cmd))
        exit(subprocess.call(cmd))

      def compile_source_file(i, input_file):
        logging.debug('compiling source file: ' + input_file)
        output_file = get_bitcode_file(input_file)
        temp_files.append((i, output_file))
        args = get_bitcode_args([input_file]) + ['-emit-llvm', '-c', '-o', output_file]
        logging.debug("running: " + ' '.join(shared.Building.doublequote_spaces(args))) # NOTE: Printing this line here in this specific format is important, it is parsed to implement the "emcc --cflags" command
        execute(args) # let compiler frontend print directly, so colors are saved (PIPE kills that)
        if not os.path.exists(output_file):
          logging.error('compiler frontend failed to generate LLVM bitcode, halting')
          sys.exit(1)

      # First, generate LLVM bitcode. For each input file, we get base.o with bitcode
      for i, input_file in input_files:
        file_ending = filename_type_ending(input_file)
        if file_ending.endswith(SOURCE_ENDINGS):
          compile_source_file(i, input_file)
        else: # bitcode
          if file_ending.endswith(BITCODE_ENDINGS):
            logging.debug('using bitcode file: ' + input_file)
            temp_files.append((i, input_file))
          elif file_ending.endswith(DYNAMICLIB_ENDINGS) or shared.Building.is_ar(input_file):
            logging.debug('using library file: ' + input_file)
            temp_files.append((i, input_file))
          elif file_ending.endswith(ASSEMBLY_ENDINGS):
            if not LEAVE_INPUTS_RAW:
              logging.debug('assembling assembly file: ' + input_file)
              temp_file = in_temp(unsuffixed(uniquename(input_file)) + '.o')
              shared.Building.llvm_as(input_file, temp_file)
              temp_files.append((i, temp_file))
          else:
            if has_fixed_language_mode:
              compile_source_file(i, input_file)
            else:
              logging.error(input_file + ': Unknown file suffix when compiling to LLVM bitcode!')
              sys.exit(1)

    # exit block 'bitcodeize inputs'
    log_time('bitcodeize inputs')

    with ToolchainProfiler.profile_block('process inputs'):
      if not LEAVE_INPUTS_RAW and not shared.Settings.WASM_BACKEND:
        assert len(temp_files) == len(input_files)

        # Optimize source files
        if options.llvm_opts > 0:
          for pos, (_, input_file) in enumerate(input_files):
            file_ending = filename_type_ending(input_file)
            if file_ending.endswith(SOURCE_ENDINGS):
              temp_file = temp_files[pos][1]
              logging.debug('optimizing %s', input_file)
              #if DEBUG: shutil.copyfile(temp_file, os.path.join(shared.configuration.CANONICAL_TEMP_DIR, 'to_opt.bc')) # useful when LLVM opt aborts
              new_temp_file = in_temp(unsuffixed(uniquename(temp_file)) + '.o')
              shared.Building.llvm_opt(temp_file, options.llvm_opts, new_temp_file)
              temp_files[pos] = (temp_files[pos][0], new_temp_file)

      # Decide what we will link
      linker_inputs = [val for _, val in sorted(temp_files + link_flags)]

      # If we were just asked to generate bitcode, stop there
      if final_suffix not in EXECUTABLE_SUFFIXES:
        if not specified_target:
          assert len(temp_files) == len(input_files)
          for i in range(len(input_files)):
            safe_move(temp_files[i][1], unsuffixed_basename(input_files[i][1]) + final_ending)
        else:
          if len(input_files) == 1:
            _, input_file = input_files[0]
            _, temp_file = temp_files[0]
            bitcode_target = specified_target if specified_target else unsuffixed_basename(input_file) + final_ending
            if temp_file != input_file:
              safe_move(temp_file, bitcode_target)
            else:
              shutil.copyfile(temp_file, bitcode_target)
            temp_output_base = unsuffixed(temp_file)
            if os.path.exists(temp_output_base + '.d'):
              # There was a .d file generated, from -MD or -MMD and friends, save a copy of it to where the output resides,
              # adjusting the target name away from the temporary file name to the specified target.
              # It will be deleted with the rest of the temporary directory.
              deps = open(temp_output_base + '.d').read()
              deps = deps.replace(temp_output_base + options.default_object_extension, specified_target)
              with open(os.path.join(os.path.dirname(specified_target), os.path.basename(unsuffixed(input_file) + '.d')), "w") as out_dep:
                out_dep.write(deps)
          else:
            assert len(original_input_files) == 1 or not has_dash_c, 'fatal error: cannot specify -o with -c with multiple files' + str(sys.argv) + ':' + str(original_input_files)
            # We have a specified target (-o <target>), which is not JavaScript or HTML, and
            # we have multiple files: Link them
            logging.debug('link: ' + str(linker_inputs) + specified_target)
            # Sort arg tuples and pass the extracted values to link.
            shared.Building.link(linker_inputs, specified_target)
        logging.debug('stopping at bitcode')
        if final_suffix.lower() in ['so', 'dylib', 'dll']:
          logging.warning('Dynamic libraries (.so, .dylib, .dll) are currently not supported by Emscripten. For build system emulation purposes, Emscripten'
            + ' will now generate a static library file (.bc) with the suffix \'.' + final_suffix + '\'. For best practices,'
            + ' please adapt your build system to directly generate a static LLVM bitcode library by setting the output suffix to \'.bc.\')')
        exit(0)

    # exit block 'process inputs'
    log_time('process inputs')

    ## Continue on to create JavaScript

    with ToolchainProfiler.profile_block('calculate system libraries'):
      logging.debug('will generate JavaScript')

      extra_files_to_link = []

      if not LEAVE_INPUTS_RAW and \
         not shared.Settings.BUILD_AS_SHARED_LIB and \
         not shared.Settings.BOOTSTRAPPING_STRUCT_INFO and \
         not shared.Settings.ONLY_MY_CODE and \
         not shared.Settings.SIDE_MODULE: # shared libraries/side modules link no C libraries, need them in parent
        extra_files_to_link = system_libs.get_ports(shared.Settings)
        extra_files_to_link += system_libs.calculate([f for _, f in sorted(temp_files)] + extra_files_to_link, in_temp, stdout_=None, stderr_=None, forced=forced_stdlibs)

    # exit block 'calculate system libraries'
    log_time('calculate system libraries')

    with ToolchainProfiler.profile_block('link'):
      # final will be an array if linking is deferred, otherwise a normal string.
      DEFAULT_FINAL = in_temp(target_basename + '.bc')
      def get_final():
        global final
        if type(final) != str:
          final = DEFAULT_FINAL
        return final

      # First, combine the bitcode files if there are several. We must also link if we have a singleton .a
      if len(input_files) + len(extra_files_to_link) > 1 or \
         (not LEAVE_INPUTS_RAW and not (suffix(temp_files[0][1]) in BITCODE_ENDINGS or suffix(temp_files[0][1]) in DYNAMICLIB_ENDINGS) and shared.Building.is_ar(temp_files[0][1])):
        linker_inputs += extra_files_to_link
        logging.debug('linking: ' + str(linker_inputs))
        # force archive contents to all be included, if just archives, or if linking shared modules
        force_archive_contents = len([temp for i, temp in temp_files if not temp.endswith(STATICLIB_ENDINGS)]) == 0 or not shared.Building.can_build_standalone()

        # if  EMCC_DEBUG=2  then we must link now, so the temp files are complete.
        # if using the wasm backend, we might be using vanilla LLVM, which does not allow our fastcomp deferred linking opts.
        # TODO: we could check if this is a fastcomp build, and still speed things up here
        just_calculate = DEBUG != '2' and not shared.Settings.WASM_BACKEND
        final = shared.Building.link(linker_inputs, DEFAULT_FINAL, force_archive_contents=force_archive_contents, temp_files=misc_temp_files, just_calculate=just_calculate)
      else:
        if not LEAVE_INPUTS_RAW:
          _, temp_file = temp_files[0]
          _, input_file = input_files[0]
          final = in_temp(target_basename + '.bc')
          if temp_file != input_file:
            shutil.move(temp_file, final)
          else:
            shutil.copyfile(temp_file, final)
        else:
          _, input_file = input_files[0]
          final = in_temp(input_file)
          shutil.copyfile(input_file, final)

    # exit block 'link'
    log_time('link')

    with ToolchainProfiler.profile_block('post-link'):
      if DEBUG:
        logging.debug('saving intermediate processing steps to %s', shared.get_emscripten_temp_dir())
        if not LEAVE_INPUTS_RAW: save_intermediate('basebc', 'bc')

      # Optimize, if asked to
      if not LEAVE_INPUTS_RAW:
        # remove LLVM debug if we are not asked for it
        link_opts = [] if options.debug_level >= 4 or shared.Settings.CYBERDWARF else ['-strip-debug']
        if not shared.Settings.ASSERTIONS:
          link_opts += ['-disable-verify']
        else:
          # when verifying, LLVM debug info has some tricky linking aspects, and llvm-link will
          # disable the type map in that case. we added linking to opt, so we need to do
          # something similar, which we can do with a param to opt
          link_opts += ['-disable-debug-info-type-map']

        if options.llvm_lto >= 2 and options.llvm_opts > 0:
          logging.debug('running LLVM opts as pre-LTO')
          final = shared.Building.llvm_opt(final, options.llvm_opts, DEFAULT_FINAL)
          if DEBUG: save_intermediate('opt', 'bc')

        # If we can LTO, do it before dce, since it opens up dce opportunities
        if shared.Building.can_build_standalone() and options.llvm_lto and options.llvm_lto != 2:
          if not shared.Building.can_inline(): link_opts.append('-disable-inlining')
          # add a manual internalize with the proper things we need to be kept alive during lto
          link_opts += shared.Building.get_safe_internalize() + ['-std-link-opts']
          # execute it now, so it is done entirely before we get to the stage of legalization etc.
          final = shared.Building.llvm_opt(final, link_opts, DEFAULT_FINAL)
          if DEBUG: save_intermediate('lto', 'bc')
          link_opts = []
        else:
          # At minimum remove dead functions etc., this potentially saves a lot in the size of the generated code (and the time to compile it)
          link_opts += shared.Building.get_safe_internalize() + ['-globaldce']

        if options.cfi:
          if use_cxx:
             link_opts.append("-wholeprogramdevirt")
          link_opts.append("-lowertypetests")

        if AUTODEBUG:
          # let llvm opt directly emit ll, to skip writing and reading all the bitcode
          link_opts += ['-S']
          final = shared.Building.llvm_opt(final, link_opts, get_final() + '.link.ll')
          if DEBUG: save_intermediate('linktime', 'll')
        else:
          if len(link_opts) > 0:
            final = shared.Building.llvm_opt(final, link_opts, DEFAULT_FINAL)
            if DEBUG: save_intermediate('linktime', 'bc')
          if options.save_bc:
            shutil.copyfile(final, options.save_bc)

      # Prepare .ll for Emscripten
      if LEAVE_INPUTS_RAW:
        assert len(input_files) == 1
      if DEBUG and options.save_bc: save_intermediate('ll', 'll')

      if AUTODEBUG:
        logging.debug('autodebug')
        next = get_final() + '.ad.ll'
        execute([shared.PYTHON, shared.AUTODEBUGGER, final, next])
        final = next
        if DEBUG: save_intermediate('autodebug', 'll')

      assert type(final) == str, 'we must have linked the final files, if linking was deferred, by this point'

    # exit block 'post-link'
    log_time('post-link')

    with ToolchainProfiler.profile_block('emscript'):
      # Emscripten
      logging.debug('LLVM => JS')
      extra_args = []
      if options.js_libraries:
        extra_args = ['--libraries', ','.join(map(os.path.abspath, options.js_libraries))]
      if options.memory_init_file:
        shared.Settings.MEM_INIT_METHOD = 1
      else:
        assert shared.Settings.MEM_INIT_METHOD != 1
      final = shared.Building.emscripten(final, append_ext=False, extra_args=extra_args)
      if DEBUG: save_intermediate('original')

      if shared.Settings.WASM_BACKEND:
        # we also received wast and wasm at this stage
        temp_basename = unsuffixed(final)
        wast_temp = temp_basename + '.wast'
        shutil.move(wast_temp, wasm_text_target)
        shutil.move(temp_basename + '.wasm', wasm_binary_target)
        open(wasm_text_target + '.mappedGlobals', 'w').write('{}') # no need for mapped globals for now, but perhaps some day

      if shared.Settings.CYBERDWARF:
        cd_target = final + '.cd'
        shutil.move(cd_target, target + '.cd')

    # exit block 'emscript'
    log_time('emscript (llvm => executable code)')

    with ToolchainProfiler.profile_block('source transforms'):
      # Embed and preload files
      if shared.Settings.SPLIT_MEMORY:
        options.no_heap_copy = True # copying into the heap is risky when split - the chunks might be too small for the file package!

      if len(options.preload_files) + len(options.embed_files) > 0:
        logging.debug('setting up files')
        file_args = []
        if len(options.preload_files) > 0:
          file_args.append('--preload')
          file_args += options.preload_files
        if len(options.embed_files) > 0:
          file_args.append('--embed')
          file_args += options.embed_files
        if len(options.exclude_files) > 0:
          file_args.append('--exclude')
          file_args += options.exclude_files
        if options.use_preload_cache:
          file_args.append('--use-preload-cache')
        if options.no_heap_copy:
          file_args.append('--no-heap-copy')
        if not options.use_closure_compiler:
          file_args.append('--no-closure')
        if shared.Settings.LZ4:
          file_args.append('--lz4')
        if options.use_preload_plugins:
          file_args.append('--use-preload-plugins')
        file_code = execute([shared.PYTHON, shared.FILE_PACKAGER, unsuffixed(target) + '.data'] + file_args, stdout=PIPE)[0]
        options.pre_js = file_code + options.pre_js

      # Apply pre and postjs files
      if options.pre_js or options.post_module or options.post_js:
        logging.debug('applying pre/postjses')
        src = open(final).read()
        if options.post_module:
          src = src.replace('// {{PREAMBLE_ADDITIONS}}', options.post_module + '\n// {{PREAMBLE_ADDITIONS}}')
        final += '.pp.js'
        if WINDOWS: # Avoid duplicating \r\n to \r\r\n when writing out.
          if options.pre_js:
            options.pre_js = options.pre_js.replace('\r\n', '\n')
          if options.post_js:
            options.post_js = options.post_js.replace('\r\n', '\n')
        outfile = open(final, 'w')
        outfile.write(options.pre_js)
        outfile.write(src) # this may be large, don't join it to others
        outfile.write(options.post_js)
        outfile.close()
        options.pre_js = src = options.post_js = None
        if DEBUG: save_intermediate('pre-post')

      # Apply a source code transformation, if requested
      if options.js_transform:
        shutil.copyfile(final, final + '.tr.js')
        final += '.tr.js'
        posix = True if not shared.WINDOWS else False
        logging.debug('applying transform: %s', options.js_transform)
        subprocess.check_call(shared.Building.remove_quotes(shlex.split(options.js_transform, posix=posix) + [os.path.abspath(final)]))
        if DEBUG: save_intermediate('transformed')

      js_transform_tempfiles = [final]

    # exit block 'source transforms'
    log_time('source transforms')

    with ToolchainProfiler.profile_block('memory initializer'):
      memfile = None
      if shared.Settings.MEM_INIT_METHOD > 0:
        memfile = target + '.mem'
        shared.try_delete(memfile)
        def repl(m):
          # handle chunking of the memory initializer
          s = m.group(1)
          if len(s) == 0: return '' # don't emit 0-size ones
          membytes = [int(x or '0') for x in s.split(',')]
          while membytes and membytes[-1] == 0:
            membytes.pop()
          if not membytes: return ''
          if not options.memory_init_file:
            # memory initializer in a string literal
            return "memoryInitializer = '%s';" % shared.JS.generate_string_initializer(list(membytes))
          open(memfile, 'wb').write(''.join(map(chr, membytes)))
          if DEBUG:
            # Copy into temp dir as well, so can be run there too
            shared.safe_copy(memfile, os.path.join(shared.get_emscripten_temp_dir(), os.path.basename(memfile)))
          if not shared.Settings.BINARYEN:
            return 'memoryInitializer = "%s";' % os.path.basename(memfile)
          else:
            # with wasm, we may have the mem init file in the wasm binary already
            return ('memoryInitializer = Module["wasmJSMethod"].indexOf("asmjs") >= 0 || '
                    'Module["wasmJSMethod"].indexOf("interpret-asm2wasm") >= 0 ? "%s" : null;'
                    % os.path.basename(memfile))
        src = re.sub(shared.JS.memory_initializer_pattern, repl, open(final).read(), count=1)
        open(final + '.mem.js', 'w').write(src)
        final += '.mem.js'
        src = None
        js_transform_tempfiles[-1] = final # simple text substitution preserves comment line number mappings
        if DEBUG:
          if os.path.exists(memfile):
            save_intermediate('meminit')
            logging.debug('wrote memory initialization to %s', memfile)
          else:
            logging.debug('did not see memory initialization')
      elif not shared.Settings.MAIN_MODULE and not shared.Settings.SIDE_MODULE and options.debug_level < 4:
        # not writing a binary init, but we can at least optimize them by splitting them up
        src = open(final).read()
        src = shared.JS.optimize_initializer(src)
        if src is not None:
          logging.debug('optimizing memory initialization')
          open(final + '.mem.js', 'w').write(src)
          final += '.mem.js'
          src = None

      if shared.Settings.USE_PTHREADS:
        target_dir = os.path.dirname(os.path.abspath(target))
        shutil.copyfile(shared.path_from_root('src', 'pthread-main.js'),
                        os.path.join(target_dir, 'pthread-main.js'))

      # Generate the fetch-worker.js script for multithreaded emscripten_fetch() support if targeting pthreads.
      if shared.Settings.FETCH and shared.Settings.USE_PTHREADS:
        shared.make_fetch_worker(final, os.path.join(os.path.dirname(os.path.abspath(target)), 'fetch-worker.js'))

    # exit block 'memory initializer'
    log_time('memory initializer')

    optimizer = JSOptimizer(
      target=target,
      options=options,
      misc_temp_files=misc_temp_files,
      js_transform_tempfiles=js_transform_tempfiles,
    )
    with ToolchainProfiler.profile_block('js opts'):
      # It is useful to run several js optimizer passes together, to save on unneeded unparsing/reparsing
      if shared.Settings.DEAD_FUNCTIONS:
        optimizer.queue += ['eliminateDeadFuncs']
        optimizer.extra_info['dead_functions'] = shared.Settings.DEAD_FUNCTIONS

      if options.opt_level >= 1 and options.js_opts:
        logging.debug('running js post-opts')

        if DEBUG == '2':
          # Clean up the syntax a bit
          optimizer.queue += ['noop']

        def get_eliminate():
          if shared.Settings.ALLOW_MEMORY_GROWTH:
            return 'eliminateMemSafe'
          else:
            return 'eliminate'

        if options.opt_level >= 2:
          optimizer.queue += [get_eliminate()]

          if shared.Settings.AGGRESSIVE_VARIABLE_ELIMINATION:
            # note that this happens before registerize/minification, which can obfuscate the name of 'label', which is tricky
            optimizer.queue += ['aggressiveVariableElimination']

          optimizer.queue += ['simplifyExpressions']

          if shared.Settings.EMTERPRETIFY:
            # emterpreter code will not run through a JS optimizing JIT, do more work ourselves
            optimizer.queue += ['localCSE']

      if shared.Settings.EMTERPRETIFY:
        # add explicit label setting, as we will run aggressiveVariableElimination late, *after* 'label' is no longer notable by name
        optimizer.queue += ['safeLabelSetting']

      if options.opt_level >= 1 and options.js_opts:
        if options.opt_level >= 2:
          # simplify ifs if it is ok to make the code somewhat unreadable, and unless outlining (simplified ifs
          # with commaified code breaks late aggressive variable elimination)
          # do not do this with binaryen, as commaifying confuses binaryen call type detection (FIXME, in theory, but unimportant)
          debugging = options.debug_level == 0 or options.profiling
          if shared.Settings.SIMPLIFY_IFS and debugging and shared.Settings.OUTLINING_LIMIT == 0 and not shared.Settings.BINARYEN:
            optimizer.queue += ['simplifyIfs']

          if shared.Settings.PRECISE_F32: optimizer.queue += ['optimizeFrounds']

      if options.js_opts:
        if shared.Settings.SAFE_HEAP: optimizer.queue += ['safeHeap']

        if shared.Settings.OUTLINING_LIMIT > 0:
          optimizer.queue += ['outline']
          optimizer.extra_info['sizeToOutline'] = shared.Settings.OUTLINING_LIMIT

        if options.opt_level >= 2 and options.debug_level < 3:
          if options.opt_level >= 3 or options.shrink_level > 0:
            optimizer.queue += ['registerizeHarder']
          else:
            optimizer.queue += ['registerize']

        # NOTE: Important that this comes after registerize/registerizeHarder
        if shared.Settings.ELIMINATE_DUPLICATE_FUNCTIONS and options.opt_level >= 2:
          optimizer.flush()
          shared.Building.eliminate_duplicate_funcs(final)

      if shared.Settings.EVAL_CTORS and options.memory_init_file and options.debug_level < 4 and not shared.Settings.BINARYEN:
        optimizer.flush()
        shared.Building.eval_ctors(final, memfile)
        if DEBUG: save_intermediate('eval-ctors', 'js')

      if options.js_opts:
        # some compilation modes require us to minify later or not at all
        if not shared.Settings.EMTERPRETIFY and not shared.Settings.BINARYEN:
          optimizer.do_minify()

        if options.opt_level >= 2:
          optimizer.queue += ['asmLastOpts']

        if shared.Settings.FINALIZE_ASM_JS: optimizer.queue += ['last']

        optimizer.flush()

      if options.use_closure_compiler == 2:
        optimizer.flush()

        logging.debug('running closure')
        # no need to add this to js_transform_tempfiles, because closure and
        # debug_level > 0 are never simultaneously true
        final = shared.Building.closure_compiler(final, pretty=options.debug_level >= 1)
        if DEBUG: save_intermediate('closure')

    log_time('js opts')
    # exit block 'js opts'

    with ToolchainProfiler.profile_block('final emitting'):
      if shared.Settings.EMTERPRETIFY:
        emterpretify(js_target, optimizer, options)

      # Remove some trivial whitespace # TODO: do not run when compress has already been done on all parts of the code
      #src = open(final).read()
      #src = re.sub(r'\n+[ \n]*\n+', '\n', src)
      #open(final, 'w').write(src)

      # Bundle symbol data in with the cyberdwarf file
      if shared.Settings.CYBERDWARF:
          execute([shared.PYTHON, shared.path_from_root('tools', 'emdebug_cd_merger.py'), target + '.cd', target+'.symbols'])

      if options.debug_level >= 4 and not shared.Settings.BINARYEN:
        emit_js_source_maps(target, optimizer.js_transform_tempfiles)

      # track files that will need native eols
      generated_text_files_with_native_eols = []

      if (options.separate_asm or shared.Settings.BINARYEN) and not shared.Settings.WASM_BACKEND:
        separate_asm_js(final, asm_target)
        generated_text_files_with_native_eols += [asm_target]

      binaryen_method_sanity_check()
      if shared.Settings.BINARYEN:
        final = do_binaryen(final, target, asm_target, options, memfile, wasm_binary_target,
                            wasm_text_target, misc_temp_files, optimizer)

      if shared.Settings.MODULARIZE:
        final = modularize(final)

      # The JS is now final. Move it to its final location
      shutil.move(final, js_target)

      generated_text_files_with_native_eols += [js_target]

      # If we were asked to also generate HTML, do that
      if final_suffix == 'html':
        generate_html(target, options, js_target, target_basename,
                      asm_target, wasm_binary_target,
                      memfile, optimizer)
      else:
        if options.proxy_to_worker:
          generate_worker_js(target, js_target, target_basename)

      for f in generated_text_files_with_native_eols:
        tools.line_endings.convert_line_endings_in_file(f, os.linesep, options.output_eol)
    log_time('final emitting')
    # exit block 'final emitting'

    if DEBUG: logging.debug('total time: %.2f seconds', (time.time() - start_time))

  finally:
    if not TEMP_DIR:
      try:
        shutil.rmtree(temp_dir)
      except:
        pass
    else:
      logging.info('emcc saved files are in:' + temp_dir)


def parse_args(newargs):
  options = EmccOptions()
  settings_changes = []
  should_exit = False

  for i in range(len(newargs)):
    # On Windows Vista (and possibly others), excessive spaces in the command line 
    # leak into the items in this array, so trim e.g. 'foo.cpp ' -> 'foo.cpp'
    newargs[i] = newargs[i].strip()
    if newargs[i].startswith('-O'):
      # Let -O default to -O2, which is what gcc does.
      options.requested_level = newargs[i][2:] or '2'
      if options.requested_level == 's':
        options.llvm_opts = ['-Os']
        options.requested_level = 2
        options.shrink_level = 1
        settings_changes.append('INLINING_LIMIT=50')
      elif options.requested_level == 'z':
        options.llvm_opts = ['-Oz']
        options.requested_level = 2
        options.shrink_level = 2
        settings_changes.append('INLINING_LIMIT=25')
      options.opt_level = validate_arg_level(options.requested_level, 3, 'Invalid optimization level: ' + newargs[i])
    elif newargs[i].startswith('--js-opts'):
      check_bad_eq(newargs[i])
      options.js_opts = eval(newargs[i+1])
      if options.js_opts:
        options.force_js_opts = True
      newargs[i] = ''
      newargs[i+1] = ''
    elif newargs[i].startswith('--llvm-opts'):
      check_bad_eq(newargs[i])
      options.llvm_opts = eval(newargs[i+1])
      newargs[i] = ''
      newargs[i+1] = ''
    elif newargs[i].startswith('--llvm-lto'):
      check_bad_eq(newargs[i])
      options.llvm_lto = eval(newargs[i+1])
      newargs[i] = ''
      newargs[i+1] = ''
    elif newargs[i].startswith('--closure'):
      check_bad_eq(newargs[i])
      options.use_closure_compiler = int(newargs[i+1])
      newargs[i] = ''
      newargs[i+1] = ''
    elif newargs[i].startswith('--js-transform'):
      check_bad_eq(newargs[i])
      options.js_transform = newargs[i+1]
      newargs[i] = ''
      newargs[i+1] = ''
    elif newargs[i].startswith('--pre-js'):
      check_bad_eq(newargs[i])
      options.pre_js += open(newargs[i+1]).read() + '\n'
      newargs[i] = ''
      newargs[i+1] = ''
    elif newargs[i].startswith('--post-js'):
      check_bad_eq(newargs[i])
      options.post_js += open(newargs[i+1]).read() + '\n'
      newargs[i] = ''
      newargs[i+1] = ''
    elif newargs[i].startswith('--minify'):
      check_bad_eq(newargs[i])
      assert newargs[i+1] == '0', '0 is the only supported option for --minify; 1 has been deprecated'
      options.debug_level = max(1, options.debug_level)
      newargs[i] = ''
      newargs[i+1] = ''
    elif newargs[i].startswith('-g'):
      requested_level = newargs[i][2:] or '3'
      options.debug_level = validate_arg_level(requested_level, 4, 'Invalid debug level: ' + newargs[i])
      options.requested_debug = newargs[i]
      newargs[i] = ''
    elif newargs[i] == '-profiling' or newargs[i] == '--profiling':
      options.debug_level = 2
      options.profiling = True
      newargs[i] = ''
    elif newargs[i] == '-profiling-funcs' or newargs[i] == '--profiling-funcs':
      options.profiling_funcs = True
      newargs[i] = ''
    elif newargs[i] == '--tracing' or newargs[i] == '--memoryprofiler':
      if newargs[i] == '--memoryprofiler':
        options.memory_profiler = True
      options.tracing = True
      newargs[i] = ''
      newargs.append('-D__EMSCRIPTEN_TRACING__=1')
      settings_changes.append("EMSCRIPTEN_TRACING=1")
      options.js_libraries.append(shared.path_from_root('src', 'library_trace.js'))
    elif newargs[i] == '--emit-symbol-map':
      options.emit_symbol_map = True
      newargs[i] = ''
    elif newargs[i] == '--bind':
      options.bind = True
      newargs[i] = ''
      options.js_libraries.append(shared.path_from_root('src', 'embind', 'emval.js'))
      options.js_libraries.append(shared.path_from_root('src', 'embind', 'embind.js'))
      if options.default_cxx_std:
        # Force C++11 for embind code, but only if user has not explicitly overridden a standard.
        options.default_cxx_std = '-std=c++11'
    elif newargs[i].startswith('-std=') or newargs[i].startswith('--std='):
      # User specified a standard to use, clear Emscripten from specifying it.
      options.default_cxx_std = ''
    elif newargs[i].startswith('--embed-file'):
      check_bad_eq(newargs[i])
      options.embed_files.append(newargs[i+1])
      newargs[i] = ''
      newargs[i+1] = ''
    elif newargs[i].startswith('--preload-file'):
      check_bad_eq(newargs[i])
      options.preload_files.append(newargs[i+1])
      newargs[i] = ''
      newargs[i+1] = ''
    elif newargs[i].startswith('--exclude-file'):
      check_bad_eq(newargs[i])
      options.exclude_files.append(newargs[i+1])
      newargs[i] = ''
      newargs[i+1] = ''
    elif newargs[i].startswith('--use-preload-cache'):
      options.use_preload_cache = True
      newargs[i] = ''
    elif newargs[i].startswith('--no-heap-copy'):
      options.no_heap_copy = True
      newargs[i] = ''
    elif newargs[i].startswith('--use-preload-plugins'):
      options.use_preload_plugins = True
      newargs[i] = ''
    elif newargs[i] == '--ignore-dynamic-linking':
      options.ignore_dynamic_linking = True
      newargs[i] = ''
    elif newargs[i] == '-v':
      shared.COMPILER_OPTS += ['-v']
      shared.check_sanity(force=True)
      newargs[i] = ''
    elif newargs[i].startswith('--shell-file'):
      check_bad_eq(newargs[i])
      options.shell_path = newargs[i+1]
      newargs[i] = ''
      newargs[i+1] = ''
    elif newargs[i].startswith('--source-map-base'):
      check_bad_eq(newargs[i])
      options.source_map_base = newargs[i+1]
      newargs[i] = ''
      newargs[i+1] = ''
    elif newargs[i].startswith('--js-library'):
      check_bad_eq(newargs[i])
      options.js_libraries.append(newargs[i+1])
      newargs[i] = ''
      newargs[i+1] = ''
    elif newargs[i] == '--remove-duplicates':
      logging.warning('--remove-duplicates is deprecated as it is no longer needed. If you cannot link without it, file a bug with a testcase')
      newargs[i] = ''
    elif newargs[i] == '--jcache':
      logging.error('jcache is no longer supported')
      newargs[i] = ''
    elif newargs[i] == '--cache':
      check_bad_eq(newargs[i])
      os.environ['EM_CACHE'] = newargs[i+1]
      shared.reconfigure_cache()
      newargs[i] = ''
      newargs[i+1] = ''
    elif newargs[i] == '--clear-cache':
      logging.info('clearing cache as requested by --clear-cache')
      shared.Cache.erase()
      shared.check_sanity(force=True) # this is a good time for a sanity check
      should_exit = True
    elif newargs[i] == '--clear-ports':
      logging.info('clearing ports and cache as requested by --clear-ports')
      system_libs.Ports.erase()
      shared.Cache.erase()
      shared.check_sanity(force=True) # this is a good time for a sanity check
      should_exit = True
    elif newargs[i] == '--show-ports':
      system_libs.show_ports()
      should_exit = True
    elif newargs[i] == '--save-bc':
      check_bad_eq(newargs[i])
      options.save_bc = newargs[i+1]
      newargs[i] = ''
      newargs[i+1] = ''
    elif newargs[i] == '--memory-init-file':
      check_bad_eq(newargs[i])
      options.memory_init_file = int(newargs[i+1])
      newargs[i] = ''
      newargs[i+1] = ''
    elif newargs[i] == '--proxy-to-worker':
      options.proxy_to_worker = True
      newargs[i] = ''
    elif newargs[i] == '--valid-abspath':
      options.valid_abspaths.append(newargs[i+1])
      newargs[i] = ''
      newargs[i+1] = ''
    elif newargs[i] == '--separate-asm':
      options.separate_asm = True
      newargs[i] = ''
    elif newargs[i].startswith(('-I', '-L')):
      options.path_name = newargs[i][2:]
      if os.path.isabs(options.path_name) and not is_valid_abspath(options, options.path_name):
        # Of course an absolute path to a non-system-specific library or header
        # is fine, and you can ignore this warning. The danger are system headers
        # that are e.g. x86 specific and nonportable. The emscripten bundled
        # headers are modified to be portable, local system ones are generally not.
        shared.WarningManager.warn(
          'ABSOLUTE_PATHS', '-I or -L of an absolute path "' + newargs[i] +
          '" encountered. If this is to a local system header/library, it may '
          'cause problems (local system files make sense for compiling natively '
          'on your system, but not necessarily to JavaScript).')
    elif newargs[i] == '--emrun':
      options.emrun = True
      newargs[i] = ''
    elif newargs[i] == '--cpuprofiler':
      options.cpu_profiler = True
      newargs[i] = ''
    elif newargs[i] == '--threadprofiler':
      options.thread_profiler = True
      settings_changes.append('PTHREADS_PROFILING=1')
      newargs[i] = ''
    elif newargs[i] == '--default-obj-ext':
      newargs[i] = ''
      options.default_object_extension = newargs[i+1]
      if not options.default_object_extension.startswith('.'):
        options.default_object_extension = '.' + options.default_object_extension
      newargs[i+1] = ''
    elif newargs[i] == '-msse':
      newargs.append('-D__SSE__=1')
      newargs[i] = ''
    elif newargs[i] == '-msse2':
      newargs.append('-D__SSE__=1')
      newargs.append('-D__SSE2__=1')
      newargs[i] = ''
    elif newargs[i] == '-msse3':
      newargs.append('-D__SSE__=1')
      newargs.append('-D__SSE2__=1')
      newargs.append('-D__SSE3__=1')
      newargs[i] = ''
    elif newargs[i] == '-mssse3':
      newargs.append('-D__SSE__=1')
      newargs.append('-D__SSE2__=1')
      newargs.append('-D__SSE3__=1')
      newargs.append('-D__SSSE3__=1')
      newargs[i] = ''
    elif newargs[i] == '-msse4.1':
      newargs.append('-D__SSE__=1')
      newargs.append('-D__SSE2__=1')
      newargs.append('-D__SSE3__=1')
      newargs.append('-D__SSSE3__=1')
      newargs.append('-D__SSE4_1__=1')
      newargs[i] = ''
    elif newargs[i].startswith("-fsanitize=cfi"):
      options.cfi = True
    elif newargs[i] == "--output_eol":
      if newargs[i+1].lower() == 'windows':
        options.output_eol = '\r\n'
      elif newargs[i+1].lower() == 'linux':
        options.output_eol = '\n'
      else:
        logging.error('Invalid value "' + newargs[i+1] + '" to --output_eol!')
        exit(1)
      newargs[i] = ''
      newargs[i+1] = ''

  if should_exit:
    sys.exit(0)

  newargs = [arg for arg in newargs if arg is not '']
  return options, settings_changes, newargs


def emterpretify(js_target, optimizer, options):
  global final
  optimizer.flush()
  logging.debug('emterpretifying')
  import json
  try:
    # move temp js to final position, alongside its mem init file
    shutil.move(final, js_target)
    args = [shared.PYTHON,
            shared.path_from_root('tools', 'emterpretify.py'),
            js_target,
            final + '.em.js',
            json.dumps(shared.Settings.EMTERPRETIFY_BLACKLIST),
            json.dumps(shared.Settings.EMTERPRETIFY_WHITELIST),
            '',
            str(shared.Settings.SWAPPABLE_ASM_MODULE),
           ]
    if shared.Settings.EMTERPRETIFY_ASYNC:
      args += ['ASYNC=1']
    if shared.Settings.EMTERPRETIFY_ADVISE:
      args += ['ADVISE=1']
    if options.profiling or options.profiling_funcs:
      args += ['PROFILING=1']
    if shared.Settings.ASSERTIONS:
      args += ['ASSERTIONS=1']
    if shared.Settings.PRECISE_F32:
      args += ['FROUND=1']
    if shared.Settings.ALLOW_MEMORY_GROWTH:
      args += ['MEMORY_SAFE=1']
    if shared.Settings.EMTERPRETIFY_FILE:
      args += ['FILE="' + shared.Settings.EMTERPRETIFY_FILE + '"']
    execute(args)
    final = final + '.em.js'
  finally:
    shared.try_delete(js_target)

  if shared.Settings.EMTERPRETIFY_ADVISE:
    logging.warning('halting compilation due to EMTERPRETIFY_ADVISE')
    sys.exit(0)

  # minify (if requested) after emterpreter processing, and finalize output
  logging.debug('finalizing emterpreted code')
  shared.Settings.FINALIZE_ASM_JS = 1
  if not shared.Settings.BINARYEN:
    optimizer.do_minify()
  optimizer.queue += ['last']
  optimizer.flush()

  # finalize the original as well, if we will be swapping it in (TODO: add specific option for this)
  if shared.Settings.SWAPPABLE_ASM_MODULE:
    real = final
    original = js_target + '.orig.js' # the emterpretify tool saves the original here
    final = original
    logging.debug('finalizing original (non-emterpreted) code at ' + final)
    if not shared.Settings.BINARYEN:
      optimizer.do_minify()
    optimizer.queue += ['last']
    optimizer.flush()
    safe_move(final, original)
    final = real


def emit_js_source_maps(target, js_transform_tempfiles):
  logging.debug('generating source maps')
  jsrun.run_js(shared.path_from_root('tools', 'source-maps', 'sourcemapper.js'),
    shared.NODE_JS, js_transform_tempfiles +
      ['--sourceRoot', os.getcwd(),
        '--mapFileBaseName', target,
        '--offset', str(0)])


def separate_asm_js(final, asm_target):
  """Separate out the asm.js code, if asked. Or, if necessary for another option"""
  logging.debug('separating asm')
  subprocess.check_call([shared.PYTHON, shared.path_from_root('tools', 'separate_asm.py'), final, asm_target, final])

  # extra only-my-code logic
  if shared.Settings.ONLY_MY_CODE:
    temp = asm_target + '.only.js'
    print jsrun.run_js(shared.path_from_root('tools', 'js-optimizer.js'), shared.NODE_JS, args=[asm_target, 'eliminateDeadGlobals', 'last', 'asm'], stdout=open(temp, 'w'))
    shutil.move(temp, asm_target)


def binaryen_method_sanity_check():
  if shared.Settings.BINARYEN_METHOD:
    methods = shared.Settings.BINARYEN_METHOD.split(',')
    valid_methods = ['asmjs', 'native-wasm', 'interpret-s-expr', 'interpret-binary', 'interpret-asm2wasm']
    for m in methods:
      if m.strip() not in valid_methods:
        logging.error('Unrecognized BINARYEN_METHOD "' + m.strip() + '" specified! Please pass a comma-delimited list containing one or more of: ' + ','.join(valid_methods))
        sys.exit(1)


def do_binaryen(final, target, asm_target, options, memfile, wasm_binary_target,
                wasm_text_target, misc_temp_files, optimizer):
  logging.debug('using binaryen, with method: ' + shared.Settings.BINARYEN_METHOD)
  binaryen_bin = os.path.join(shared.Settings.BINARYEN_ROOT, 'bin')
  # Emit wasm.js at the top of the js. This is *not* optimized with the rest of the code, since
  # (1) it contains asm.js, whose validation would be broken, and (2) it's very large so it would
  # be slow in cleanup/JSDCE etc.
  # TODO: for html, it could be a separate script tag
  # We need wasm.js if there is a chance the polyfill will be used. If the user sets
  # BINARYEN_METHOD with something that doesn't use the polyfill, then we don't need it.
  if not shared.Settings.BINARYEN_METHOD or 'interpret' in shared.Settings.BINARYEN_METHOD:
    logging.debug('integrating wasm.js polyfill interpreter')
    wasm_js = open(os.path.join(binaryen_bin, 'wasm.js')).read()
    wasm_js = wasm_js.replace('EMSCRIPTEN_', 'emscripten_') # do not confuse the markers
    js = open(final).read()
    combined = open(final, 'w')
    combined.write(wasm_js)
    combined.write('\n//^wasm.js\n')
    combined.write(js)
    combined.close()
  # normally we emit binary, but for debug info, we might emit text first
  wrote_wasm_text = False
  debug_info = options.debug_level >= 2 or options.profiling_funcs
  # finish compiling to WebAssembly, using asm2wasm, if we didn't already emit WebAssembly directly using the wasm backend.
  if not shared.Settings.WASM_BACKEND:
    if DEBUG:
      # save the asm.js input
      shared.safe_copy(asm_target, os.path.join(shared.get_emscripten_temp_dir(), os.path.basename(asm_target)))
    cmd = [os.path.join(binaryen_bin, 'asm2wasm'), asm_target, '--total-memory=' + str(shared.Settings.TOTAL_MEMORY)]
    if shared.Settings.BINARYEN_TRAP_MODE == 'js':
      cmd += ['--emit-jsified-potential-traps']
    elif shared.Settings.BINARYEN_TRAP_MODE == 'clamp':
      cmd += ['--emit-clamped-potential-traps']
    elif shared.Settings.BINARYEN_TRAP_MODE == 'allow':
      cmd += ['--emit-potential-traps']
    else:
      logging.error('invalid BINARYEN_TRAP_MODE value: ' + shared.Settings.BINARYEN_TRAP_MODE + ' (should be js/clamp/allow)')
      sys.exit(1)
    if shared.Settings.BINARYEN_IGNORE_IMPLICIT_TRAPS:
      cmd += ['--ignore-implicit-traps']
    # pass optimization level to asm2wasm (if not optimizing, or which passes we should run was overridden, do not optimize)
    if options.opt_level > 0 and not shared.Settings.BINARYEN_PASSES:
      cmd.append(shared.Building.opt_level_to_str(options.opt_level, options.shrink_level))
    # import mem init file if it exists, and if we will not be using asm.js as a binaryen method (as it needs the mem init file, of course)
    mem_file_exists = options.memory_init_file and os.path.exists(memfile)
    ok_binaryen_method = 'asmjs' not in shared.Settings.BINARYEN_METHOD and 'interpret-asm2wasm' not in shared.Settings.BINARYEN_METHOD
    import_mem_init = mem_file_exists and ok_binaryen_method
    if import_mem_init:
      cmd += ['--mem-init=' + memfile]
      if not shared.Settings.RELOCATABLE:
        cmd += ['--mem-base=' + str(shared.Settings.GLOBAL_BASE)]
    # various options imply that the imported table may not be the exact size as the wasm module's own table segments
    if shared.Settings.RELOCATABLE or shared.Settings.RESERVED_FUNCTION_POINTERS > 0 or shared.Settings.EMULATED_FUNCTION_POINTERS:
      cmd += ['--table-max=-1']
    if shared.Settings.SIDE_MODULE:
      cmd += ['--mem-max=-1']
    elif shared.Settings.BINARYEN_MEM_MAX >= 0:
      cmd += ['--mem-max=' + str(shared.Settings.BINARYEN_MEM_MAX)]
    if shared.Settings.LEGALIZE_JS_FFI != 1:
      cmd += ['--no-legalize-javascript-ffi']
    if shared.Building.is_wasm_only():
      cmd += ['--wasm-only'] # this asm.js is code not intended to run as asm.js, it is only ever going to be wasm, an can contain special fastcomp-wasm support
    if debug_info:
      cmd += ['-g']
    if options.emit_symbol_map or shared.Settings.CYBERDWARF:
      cmd += ['--symbolmap=' + target + '.symbols']
    # we prefer to emit a binary, as it is more efficient. however, when we
    # want full debug info support (not just function names), then we must
    # emit text (at least until wasm gains support for debug info in binaries)
    target_binary = options.debug_level < 3
    if target_binary:
      cmd += ['-o', wasm_binary_target]
    else:
      cmd += ['-o', wasm_text_target, '-S']
      wrote_wasm_text = True
    logging.debug('asm2wasm (asm.js => WebAssembly): ' + ' '.join(cmd))
    TimeLogger.update()
    subprocess.check_call(cmd)

    if not target_binary:
      cmd = [os.path.join(binaryen_bin, 'wasm-as'), wasm_text_target, '-o', wasm_binary_target]
      if debug_info:
        cmd += ['-g']
        if options.debug_level >= 4:
          cmd += ['--source-map=' + wasm_binary_target + '.map']
          if options.source_map_base:
            cmd += ['--source-map-url=' + options.source_map_base + wasm_binary_target + '.map']
      logging.debug('wasm-as (text => binary): ' + ' '.join(cmd))
      subprocess.check_call(cmd)
    if import_mem_init:
      # remove and forget about the mem init file in later processing; it does not need to be prefetched in the html, etc.
      if DEBUG:
        safe_move(memfile, os.path.join(shared.get_emscripten_temp_dir(), os.path.basename(memfile)))
      else:
        os.unlink(memfile)
      options.memory_init_file = False
    log_time('asm2wasm')
  if shared.Settings.BINARYEN_PASSES:
    shutil.move(wasm_binary_target, wasm_binary_target + '.pre')
    # BINARYEN_PASSES is comma-separated, and we support both '-'-prefixed and unprefixed pass names
    passes = map(lambda p: ('--' + p) if p[0] != '-' else p, shared.Settings.BINARYEN_PASSES.split(','))
    cmd = [os.path.join(binaryen_bin, 'wasm-opt'), wasm_binary_target + '.pre', '-o', wasm_binary_target] + passes
    logging.debug('wasm-opt on BINARYEN_PASSES: ' + ' '.join(cmd))
    subprocess.check_call(cmd)
  if not wrote_wasm_text and 'interpret-s-expr' in shared.Settings.BINARYEN_METHOD:
    cmd = [os.path.join(binaryen_bin, 'wasm-dis'), wasm_binary_target, '-o', wasm_text_target]
    logging.debug('wasm-dis (binary => text): ' + ' '.join(cmd))
    subprocess.check_call(cmd)
  if shared.Settings.BINARYEN_SCRIPTS:
    binaryen_scripts = os.path.join(shared.Settings.BINARYEN_ROOT, 'scripts')
    script_env = os.environ.copy()
    root_dir = os.path.abspath(os.path.dirname(__file__))
    if script_env.get('PYTHONPATH'):
      script_env['PYTHONPATH'] += ':' + root_dir
    else:
      script_env['PYTHONPATH'] = root_dir
    for script in shared.Settings.BINARYEN_SCRIPTS.split(','):
      logging.debug('running binaryen script: ' + script)
      subprocess.check_call([shared.PYTHON, os.path.join(binaryen_scripts, script), final, wasm_text_target], env=script_env)
  if shared.Settings.EVAL_CTORS:
    if DEBUG:
      save_intermediate('pre-eval-ctors', 'js')
      shutil.copyfile(wasm_binary_target, os.path.join(shared.get_emscripten_temp_dir(), 'pre-eval-ctors.wasm'))
    shared.Building.eval_ctors(final, wasm_binary_target, binaryen_bin, debug_info=debug_info)
  # after generating the wasm, do some final operations
  if not shared.Settings.WASM_BACKEND:
    if shared.Settings.SIDE_MODULE:
      wso = shared.WebAssembly.make_shared_library(final, wasm_binary_target)
      # replace the wasm binary output with the dynamic library. TODO: use a specific suffix for such files?
      shutil.move(wso, wasm_binary_target)
      if not DEBUG:
        os.unlink(asm_target) # we don't need the asm.js, it can just confuse
      sys.exit(0) # and we are done.
  if options.opt_level >= 2:
    # minify the JS
    optimizer.do_minify() # calculate how to minify
    if optimizer.cleanup_shell or optimizer.minify_whitespace or options.use_closure_compiler:
      misc_temp_files.note(final)
      if DEBUG: save_intermediate('preclean', 'js')
      if options.use_closure_compiler:
        logging.debug('running closure on shell code')
        final = shared.Building.closure_compiler(final, pretty=not optimizer.minify_whitespace)
      else:
        assert optimizer.cleanup_shell
        logging.debug('running cleanup on shell code')
        passes = ['noPrintMetadata', 'JSDCE', 'last']
        if optimizer.minify_whitespace:
          passes.append('minifyWhitespace')
        final = shared.Building.js_optimizer_no_asmjs(final, passes)
      if DEBUG: save_intermediate('postclean', 'js')
  return final


def modularize(final):
  logging.debug('Modularizing, assigning to var ' + shared.Settings.EXPORT_NAME)
  src = open(final).read()
  final = final + '.modular.js'
  f = open(final, 'w')
  f.write('var ' + shared.Settings.EXPORT_NAME + ' = function(' + shared.Settings.EXPORT_NAME + ') {\n')
  f.write('  ' + shared.Settings.EXPORT_NAME + ' = ' + shared.Settings.EXPORT_NAME + ' || {};\n')
  f.write('  var Module = ' + shared.Settings.EXPORT_NAME + ';\n') # included code may refer to Module (e.g. from file packager), so alias it
  f.write('\n')
  f.write(src)
  f.write('\n')
  f.write('  return ' + shared.Settings.EXPORT_NAME + ';\n')
  f.write('};\n')
  # Export the function if this is for Node (or similar UMD-style exporting), otherwise it is lost.
  f.write('if (typeof module === "object" && module.exports) {\n')
  f.write("  module['exports'] = " + shared.Settings.EXPORT_NAME + ';\n')
  f.write('};\n')
  f.close()
  if DEBUG: save_intermediate('modularized', 'js')
  return final


def generate_html(target, options, js_target, target_basename,
                  asm_target, wasm_binary_target,
                  memfile, optimizer):
  script = ScriptSource()

  logging.debug('generating HTML')
  shell = open(options.shell_path).read()
  assert '{{{ SCRIPT }}}' in shell, 'HTML shell must contain  {{{ SCRIPT }}}  , see src/shell.html for an example'
  base_js_target = os.path.basename(js_target)

  asm_mods = []

  if options.proxy_to_worker:
    proxy_worker_filename = shared.Settings.PROXY_TO_WORKER_FILENAME or target_basename
    worker_js = worker_js_script(proxy_worker_filename)
    script.inline = '''
  if ((',' + window.location.search.substr(1) + ',').indexOf(',noProxy,') < 0) {
    console.log('running code in a web worker');
''' + worker_js + '''
  } else {
    // note: no support for code mods (PRECISE_F32==2)
    console.log('running code on the main thread');
    var script = document.createElement('script');
    script.src = "%s.js";
    document.body.appendChild(script);
  }
''' % proxy_worker_filename
  else:
    # Normal code generation path
    script.src = base_js_target

    from tools import client_mods
    asm_mods = client_mods.get_mods(shared.Settings,
                                    minified = 'minifyNames' in optimizer.queue_history,
                                    separate_asm = options.separate_asm)

  if shared.Settings.EMTERPRETIFY_FILE:
    # We need to load the emterpreter file before anything else, it has to be synchronously ready
    script.un_src()
    script.inline = '''
          var emterpretXHR = new XMLHttpRequest();
          emterpretXHR.open('GET', '%s', true);
          emterpretXHR.responseType = 'arraybuffer';
          emterpretXHR.onload = function() {
            Module.emterpreterFile = emterpretXHR.response;
%s
          };
          emterpretXHR.send(null);
''' % (shared.Settings.EMTERPRETIFY_FILE, script.inline)

  if options.memory_init_file:
    # start to load the memory init file in the HTML, in parallel with the JS
    script.un_src()
    script.inline = ('''
          (function() {
            var memoryInitializer = '%s';
            if (typeof Module['locateFile'] === 'function') {
              memoryInitializer = Module['locateFile'](memoryInitializer);
            } else if (Module['memoryInitializerPrefixURL']) {
              memoryInitializer = Module['memoryInitializerPrefixURL'] + memoryInitializer;
            }
            var meminitXHR = Module['memoryInitializerRequest'] = new XMLHttpRequest();
            meminitXHR.open('GET', memoryInitializer, true);
            meminitXHR.responseType = 'arraybuffer';
            meminitXHR.send(null);
          })();
''' % os.path.basename(memfile)) + script.inline

  # Download .asm.js if --separate-asm was passed in an asm.js build, or if 'asmjs' is one
  # of the wasm run methods.
  if not options.separate_asm or (shared.Settings.BINARYEN and 'asmjs' not in shared.Settings.BINARYEN_METHOD):
    assert len(asm_mods) == 0, 'no --separate-asm means no client code mods are possible'
  else:
    script.un_src()
    if len(asm_mods) == 0:
      # just load the asm, then load the rest
      script.inline = '''
    var script = document.createElement('script');
    script.src = "%s";
    script.onload = function() {
      setTimeout(function() {
        %s
      }, 1); // delaying even 1ms is enough to allow compilation memory to be reclaimed
    };
    document.body.appendChild(script);
''' % (os.path.basename(asm_target), script.inline)
    else:
      # may need to modify the asm code, load it as text, modify, and load asynchronously
      script.inline = '''
    var codeXHR = new XMLHttpRequest();
    codeXHR.open('GET', '%s', true);
    codeXHR.onload = function() {
      var code = codeXHR.responseText;
      %s
      var blob = new Blob([code], { type: 'text/javascript' });
      codeXHR = null;
      var src = URL.createObjectURL(blob);
      var script = document.createElement('script');
      script.src = src;
      script.onload = function() {
        setTimeout(function() {
          %s
        }, 1); // delaying even 1ms is enough to allow compilation memory to be reclaimed
        URL.revokeObjectURL(script.src);
      };
      document.body.appendChild(script);
    };
    codeXHR.send(null);
''' % (os.path.basename(asm_target), '\n'.join(asm_mods), script.inline)

  if shared.Settings.BINARYEN and not shared.Settings.BINARYEN_ASYNC_COMPILATION:
    # We need to load the wasm file before anything else, it has to be synchronously ready TODO: optimize
    script.un_src()
    script.inline = '''
          var wasmXHR = new XMLHttpRequest();
          wasmXHR.open('GET', '%s', true);
          wasmXHR.responseType = 'arraybuffer';
          wasmXHR.onload = function() {
            Module.wasmBinary = wasmXHR.response;
%s
          };
          wasmXHR.send(null);
''' % (os.path.basename(wasm_binary_target), script.inline)

  html = open(target, 'wb')
  html_contents = shell.replace('{{{ SCRIPT }}}', script.replacement())
  html_contents = tools.line_endings.convert_line_endings(html_contents, '\n', options.output_eol)
  html.write(html_contents)
  html.close()


def generate_worker_js(target, js_target, target_basename):
  shutil.move(js_target, unsuffixed(js_target) + '.worker.js') # compiler output goes in .worker.js file
  worker_target_basename = target_basename + '.worker'
  proxy_worker_filename = shared.Settings.PROXY_TO_WORKER_FILENAME or worker_target_basename
  target_contents = worker_js_script(proxy_worker_filename)
  open(target, 'w').write(target_contents)


def worker_js_script(proxy_worker_filename):
  web_gl_client_src = open(shared.path_from_root('src', 'webGLClient.js')).read()
  idb_store_src = open(shared.path_from_root('src', 'IDBStore.js')).read()
  proxy_client_src = (
    open(shared.path_from_root('src', 'proxyClient.js')).read()
    .replace('{{{ filename }}}', proxy_worker_filename)
    .replace('{{{ IDBStore.js }}}', idb_store_src)
  )

  return web_gl_client_src + '\n' + proxy_client_src


def system_js_libraries_setting_str(libs, lib_dirs, settings_changes, input_files):
  libraries = []

  # Find library files
  for i, lib in libs:
    logging.debug('looking for library "%s"', lib)
    found = False
    for prefix in LIB_PREFIXES:
      for suff in STATICLIB_ENDINGS + DYNAMICLIB_ENDINGS:
        name = prefix + lib + suff
        for lib_dir in lib_dirs:
          path = os.path.join(lib_dir, name)
          if os.path.exists(path):
            logging.debug('found library "%s" at %s', lib, path)
            input_files.append((i, path))
            found = True
            break
        if found: break
      if found: break
    if not found:
      libraries += shared.Building.path_to_system_js_libraries(lib)

  # Certain linker flags imply some link libraries to be pulled in by default.
  libraries += shared.Building.path_to_system_js_libraries_for_settings(settings_changes)
  return 'SYSTEM_JS_LIBRARIES="' + ','.join(libraries) + '"'


class ScriptSource(object):
  def __init__(self):
    self.src = None # if set, we have a script to load with a src attribute
    self.inline = None # if set, we have the contents of a script to write inline in a script

  def un_src(self):
    """Use this if you want to modify the script and need it to be inline."""
    if self.src is None:
      return
    self.inline = '''
          var script = document.createElement('script');
          script.src = "%s";
          document.body.appendChild(script);
''' % self.src
    self.src = None

  def replacement(self):
    """Returns the script tag to replace the {{{ SCRIPT }}} tag in the target"""
    assert (self.src or self.inline) and not (self.src and self.inline)
    if self.src:
      return '<script async type="text/javascript" src="%s"></script>' % urllib.quote(self.src)
    else:
      return '<script>\n%s\n</script>' % self.inline


def is_valid_abspath(options, path_name):
  # Any path that is underneath the emscripten repository root must be ok.
  if shared.path_from_root().replace('\\', '/') in path_name.replace('\\', '/'):
    return True

  for valid_abspath in options.valid_abspaths:
    if in_directory(valid_abspath, path_name):
      return True
  return False


def check_bad_eq(arg):
  assert '=' not in arg, 'Invalid parameter (do not use "=" with "--" options)'


def validate_arg_level(level_string, max_level, err_msg):
  try:
    level = int(level_string)
    assert 0 <= level <= max_level
  except:
    raise Exception(err_msg)
  return level


if __name__ == '__main__':
  run()
  sys.exit(0)
