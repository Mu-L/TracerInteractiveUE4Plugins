# Copyright 2014 The Emscripten Authors.  All rights reserved.
# Emscripten is available under two separate licenses, the MIT license and the
# University of Illinois/NCSA Open Source License.  Both these licenses can be
# found in the LICENSE file.

from __future__ import print_function
import json
import logging
import os
import re
import shutil
import sys
import tarfile
import zipfile
from collections import namedtuple

from . import ports
from . import shared
from tools.shared import check_call

import platform

stdout = None
stderr = None

logger = logging.getLogger('system_libs')


def call_process(cmd):
  shared.run_process(cmd, stdout=stdout, stderr=stderr)


def run_commands(commands):
# EPIC EDIT start -- nick.shin 2019-06-20 -- UE-76599
  if platform.system() == "Windows":
    # reduce the incident of: "[Error 5] Access is denied" on Windows
    cores = 1
  else:
    cores = min(len(commands), shared.Building.get_num_cores())
# EPIC EDIT end -- nick.shin 2019-06-20 -- UE-76599
  if cores <= 1:
    for command in commands:
      call_process(command)
  else:
    pool = shared.Building.get_multiprocessing_pool()
    # https://stackoverflow.com/questions/1408356/keyboard-interrupts-with-pythons-multiprocessing-pool
    # https://bugs.python.org/issue8296
    # 999999 seconds (about 11 days) is reasonably huge to not trigger actual timeout
    # and is smaller than the maximum timeout value 4294967.0 for Python 3 on Windows (threading.TIMEOUT_MAX)
    pool.map_async(call_process, commands, chunksize=1).get(999999)


def files_in_path(path_components, filenames):
  srcdir = shared.path_from_root(*path_components)
  return [os.path.join(srcdir, f) for f in filenames]


def get_cflags(force_object_files=False):
  flags = []
  if force_object_files:
    flags += ['-s', 'WASM_OBJECT_FILES=1']
  elif not shared.Settings.WASM_OBJECT_FILES:
    flags += ['-s', 'WASM_OBJECT_FILES=0']
  if shared.Settings.RELOCATABLE:
    flags += ['-s', 'RELOCATABLE']
  return flags


def create_lib(libname, inputs):
  """Create a library from a set of input objects."""
  if libname.endswith('.bc'):
    shared.Building.link_to_object(inputs, libname)
  elif libname.endswith('.a'):
    shared.Building.emar('cr', libname, inputs)
  else:
    raise Exception('unknown suffix ' + libname)


def calculate(temp_files, in_temp, stdout_, stderr_, forced=[]):
  global stdout, stderr
  stdout = stdout_
  stderr = stderr_

  # Check if we need to include some libraries that we compile. (We implement libc ourselves in js, but
  # compile a malloc implementation and stdlibc++.)

  def read_symbols(path):
    with open(path) as f:
      content = f.read()

      # Require that Windows newlines should not be present in a symbols file, if running on Linux or macOS
      # This kind of mismatch can occur if one copies a zip file of Emscripten cloned on Windows over to
      # a Linux or macOS system. It will result in Emscripten linker getting confused on stray \r characters,
      # and be unable to link any library symbols properly. We could harden against this by .strip()ping the
      # opened files, but it is possible that the mismatching line endings can cause random problems elsewhere
      # in the toolchain, hence abort execution if so.
      if os.name != 'nt' and '\r\n' in content:
        raise Exception('Windows newlines \\r\\n detected in symbols file "' + path + '"! This could happen for example when copying Emscripten checkout from Windows to Linux or macOS. Please use Unix line endings on checkouts of Emscripten on Linux and macOS!')

      return shared.Building.parse_symbols(content).defs

  default_opts = ['-Werror']

  # XXX We also need to add libc symbols that use malloc, for example strdup. It's very rare to use just them and not
  #     a normal malloc symbol (like free, after calling strdup), so we haven't hit this yet, but it is possible.
  libc_symbols = read_symbols(shared.path_from_root('system', 'lib', 'libc.symbols'))
  libcxx_symbols = read_symbols(shared.path_from_root('system', 'lib', 'libcxx', 'symbols'))
  libcxxabi_symbols = read_symbols(shared.path_from_root('system', 'lib', 'libcxxabi', 'symbols'))
  gl_symbols = read_symbols(shared.path_from_root('system', 'lib', 'gl.symbols'))
  al_symbols = read_symbols(shared.path_from_root('system', 'lib', 'al.symbols'))
  compiler_rt_symbols = read_symbols(shared.path_from_root('system', 'lib', 'compiler-rt.symbols'))
  libc_extras_symbols = read_symbols(shared.path_from_root('system', 'lib', 'libc_extras.symbols'))
  pthreads_symbols = read_symbols(shared.path_from_root('system', 'lib', 'pthreads.symbols'))
  asmjs_pthreads_symbols = read_symbols(shared.path_from_root('system', 'lib', 'asmjs_pthreads.symbols'))
  stub_pthreads_symbols = read_symbols(shared.path_from_root('system', 'lib', 'stub_pthreads.symbols'))
  wasm_libc_symbols = read_symbols(shared.path_from_root('system', 'lib', 'wasm-libc.symbols'))
  html5_symbols = read_symbols(shared.path_from_root('system', 'lib', 'html5.symbols'))

  def get_wasm_libc_rt_files():
    # Static linking is tricky with LLVM, since e.g. memset might not be used
    # from libc, but be used as an intrinsic, and codegen will generate a libc
    # call from that intrinsic *after* static linking would have thought it is
    # all in there. In asm.js this is not an issue as we do JS linking anyhow,
    # and have asm.js-optimized versions of all the LLVM intrinsics. But for
    # wasm, we need a better solution. For now, make another archive that gets
    # included at the same time as compiler-rt.
    # Note that this also includes things that may be depended on by those
    # functions - fmin uses signbit, for example, so signbit must be here (so if
    # fmin is added by codegen, it will have all it needs).
    math_files = files_in_path(
      path_components=['system', 'lib', 'libc', 'musl', 'src', 'math'],
      filenames=[
        'fmin.c', 'fminf.c', 'fminl.c',
        'fmax.c', 'fmaxf.c', 'fmaxl.c',
        'fmod.c', 'fmodf.c', 'fmodl.c',
        'log2.c', 'log2f.c', 'log10.c', 'log10f.c',
        'exp2.c', 'exp2f.c', 'exp10.c', 'exp10f.c',
        'scalbn.c', '__fpclassifyl.c',
        '__signbitl.c', '__signbitf.c', '__signbit.c'
      ])
    string_files = files_in_path(
      path_components=['system', 'lib', 'libc', 'musl', 'src', 'string'],
      filenames=['memset.c', 'memmove.c'])
    other_files = files_in_path(
      path_components=['system', 'lib', 'libc'],
      filenames=['emscripten_memcpy.c'])
    return math_files + string_files + other_files

  # XXX we should disable EMCC_DEBUG when building libs, just like in the relooper

  def musl_internal_includes():
    return [
      '-I', shared.path_from_root('system', 'lib', 'libc', 'musl', 'src', 'internal'),
      '-I', shared.path_from_root('system', 'lib', 'libc', 'musl', 'arch', 'js'),
    ]

  def build_libc(lib_filename, files, lib_opts):
    o_s = []
    commands = []
    # Hide several musl warnings that produce a lot of spam to unit test build server logs.
    # TODO: When updating musl the next time, feel free to recheck which of their warnings might have been fixed, and which ones of these could be cleaned up.
    c_opts = ['-Wno-return-type', '-Wno-parentheses', '-Wno-ignored-attributes',
              '-Wno-shift-count-overflow', '-Wno-shift-negative-value',
              '-Wno-dangling-else', '-Wno-unknown-pragmas',
              '-Wno-shift-op-parentheses', '-Wno-string-plus-int',
              '-Wno-logical-op-parentheses', '-Wno-bitwise-op-parentheses',
              '-Wno-visibility', '-Wno-pointer-sign', '-Wno-absolute-value',
              '-Wno-empty-body']
    for src in files:
      o = in_temp(os.path.basename(src) + '.o')
      commands.append([shared.PYTHON, shared.EMCC, shared.path_from_root('system', 'lib', src), '-o', o] + musl_internal_includes() + default_opts + c_opts + lib_opts + get_cflags())
      o_s.append(o)
    run_commands(commands)
    create_lib(in_temp(lib_filename), o_s)
    return in_temp(lib_filename)

  def build_libcxx(src_dirname, lib_filename, files, lib_opts, has_noexcept_version=False):
    o_s = []
    commands = []
    opts = default_opts + lib_opts
    # Make sure we don't mark symbols as default visibility.  This works around
    # an issue with the wasm backend where all default visibility symbols are
    # exported (and therefore can't be GC'd).
    # FIXME(https://github.com/emscripten-core/emscripten/issues/7383)
    opts += ['-D_LIBCPP_DISABLE_VISIBILITY_ANNOTATIONS']
    if has_noexcept_version and shared.Settings.DISABLE_EXCEPTION_CATCHING:
      opts += ['-fno-exceptions']
    for src in files:
      o = in_temp(src + '.o')
      srcfile = shared.path_from_root(src_dirname, src)
      commands.append([shared.PYTHON, shared.EMXX, srcfile, '-o', o, '-std=c++11'] + opts + get_cflags())
      o_s.append(o)
    run_commands(commands)
    create_lib(in_temp(lib_filename), o_s)

    return in_temp(lib_filename)

  # Returns linker flags specific to singlethreading or multithreading
  def threading_flags(libname):
    if shared.Settings.USE_PTHREADS:
      assert '-mt' in libname
      return ['-s', 'USE_PTHREADS=1']
    else:
      assert '-mt' not in libname
      return []

  def legacy_gl_emulation_flags(libname):
    if shared.Settings.LEGACY_GL_EMULATION:
      assert '-emu' in libname
      return ['-DLEGACY_GL_EMULATION=1']
    else:
      assert '-emu' not in libname
      return []

  def gl_version_flags(libname):
    if shared.Settings.USE_WEBGL2:
      assert '-webgl2' in libname
      return ['-DUSE_WEBGL2=1']
    else:
      assert '-webgl2' not in libname
      return []

  # libc
  def create_libc(libname):
    logger.debug(' building libc for cache')
    libc_files = []
    musl_srcdir = shared.path_from_root('system', 'lib', 'libc', 'musl', 'src')

    # musl modules
    blacklist = [
        'ipc', 'passwd', 'thread', 'signal', 'sched', 'ipc', 'time', 'linux',
        'aio', 'exit', 'legacy', 'mq', 'process', 'search', 'setjmp', 'env',
        'ldso', 'conf'
    ]

    # individual files
    blacklist += [
        'memcpy.c', 'memset.c', 'memmove.c', 'getaddrinfo.c', 'getnameinfo.c',
        'inet_addr.c', 'res_query.c', 'res_querydomain.c', 'gai_strerror.c',
        'proto.c', 'gethostbyaddr.c', 'gethostbyaddr_r.c', 'gethostbyname.c',
        'gethostbyname2_r.c', 'gethostbyname_r.c', 'gethostbyname2.c',
        'usleep.c', 'alarm.c', 'syscall.c', '_exit.c', 'popen.c',
        'getgrouplist.c', 'initgroups.c', 'wordexp.c', 'timer_create.c',
        'faccessat.c',
    ]

    # individual math files
    blacklist += [
        'abs.c', 'cos.c', 'cosf.c', 'cosl.c', 'sin.c', 'sinf.c', 'sinl.c',
        'tan.c', 'tanf.c', 'tanl.c', 'acos.c', 'acosf.c', 'acosl.c', 'asin.c',
        'asinf.c', 'asinl.c', 'atan.c', 'atanf.c', 'atanl.c', 'atan2.c',
        'atan2f.c', 'atan2l.c', 'exp.c', 'expf.c', 'expl.c', 'log.c', 'logf.c',
        'logl.c', 'sqrt.c', 'sqrtf.c', 'sqrtl.c', 'fabs.c', 'fabsf.c',
        'fabsl.c', 'ceil.c', 'ceilf.c', 'ceill.c', 'floor.c', 'floorf.c',
        'floorl.c', 'pow.c', 'powf.c', 'powl.c', 'round.c', 'roundf.c',
        'rintf.c'
    ]

    if shared.Settings.WASM_BACKEND:
      # With the wasm backend these are included in wasm_libc_rt instead
      blacklist += [os.path.basename(f) for f in get_wasm_libc_rt_files()]

    blacklist = set(blacklist)
    # TODO: consider using more math code from musl, doing so makes box2d faster
    for dirpath, dirnames, filenames in os.walk(musl_srcdir):
      for f in filenames:
        if f.endswith('.c'):
          if f in blacklist:
            continue
          dir_parts = os.path.split(dirpath)
          cancel = False
          for part in dir_parts:
            if part in blacklist:
              cancel = True
              break
          if not cancel:
            libc_files.append(os.path.join(musl_srcdir, dirpath, f))

    # Without -fno-builtin, LLVM can optimize away or convert calls to library
    # functions to something else based on assumptions that they behave exactly
    # like the standard library. This can cause unexpected bugs when we use our
    # custom standard library. The same for other libc/libm builds.
    args = ['-Os', '-fno-builtin']
    args += threading_flags(libname)
    return build_libc(libname, libc_files, args)

  def create_pthreads(libname):
    # Add pthread files.
    pthreads_files = files_in_path(
      path_components=['system', 'lib', 'libc', 'musl', 'src', 'thread'],
      filenames=[
        'pthread_attr_destroy.c', 'pthread_condattr_setpshared.c',
        'pthread_mutex_lock.c', 'pthread_spin_destroy.c', 'pthread_attr_get.c',
        'pthread_cond_broadcast.c', 'pthread_mutex_setprioceiling.c',
        'pthread_spin_init.c', 'pthread_attr_init.c', 'pthread_cond_destroy.c',
        'pthread_mutex_timedlock.c', 'pthread_spin_lock.c',
        'pthread_attr_setdetachstate.c', 'pthread_cond_init.c',
        'pthread_mutex_trylock.c', 'pthread_spin_trylock.c',
        'pthread_attr_setguardsize.c', 'pthread_cond_signal.c',
        'pthread_mutex_unlock.c', 'pthread_spin_unlock.c',
        'pthread_attr_setinheritsched.c', 'pthread_cond_timedwait.c',
        'pthread_once.c', 'sem_destroy.c', 'pthread_attr_setschedparam.c',
        'pthread_cond_wait.c', 'pthread_rwlockattr_destroy.c', 'sem_getvalue.c',
        'pthread_attr_setschedpolicy.c', 'pthread_equal.c', 'pthread_rwlockattr_init.c',
        'sem_init.c', 'pthread_attr_setscope.c', 'pthread_getspecific.c',
        'pthread_rwlockattr_setpshared.c', 'sem_open.c', 'pthread_attr_setstack.c',
        'pthread_key_create.c', 'pthread_rwlock_destroy.c', 'sem_post.c',
        'pthread_attr_setstacksize.c', 'pthread_mutexattr_destroy.c',
        'pthread_rwlock_init.c', 'sem_timedwait.c', 'pthread_barrierattr_destroy.c',
        'pthread_mutexattr_init.c', 'pthread_rwlock_rdlock.c', 'sem_trywait.c',
        'pthread_barrierattr_init.c', 'pthread_mutexattr_setprotocol.c',
        'pthread_rwlock_timedrdlock.c', 'sem_unlink.c',
        'pthread_barrierattr_setpshared.c', 'pthread_mutexattr_setpshared.c',
        'pthread_rwlock_timedwrlock.c', 'sem_wait.c', 'pthread_barrier_destroy.c',
        'pthread_mutexattr_setrobust.c', 'pthread_rwlock_tryrdlock.c',
        '__timedwait.c', 'pthread_barrier_init.c', 'pthread_mutexattr_settype.c',
        'pthread_rwlock_trywrlock.c', 'vmlock.c', 'pthread_barrier_wait.c',
        'pthread_mutex_consistent.c', 'pthread_rwlock_unlock.c', '__wait.c',
        'pthread_condattr_destroy.c', 'pthread_mutex_destroy.c',
        'pthread_rwlock_wrlock.c', 'pthread_condattr_init.c',
        'pthread_mutex_getprioceiling.c', 'pthread_setcanceltype.c',
        'pthread_condattr_setclock.c', 'pthread_mutex_init.c',
        'pthread_setspecific.c', 'pthread_setcancelstate.c'
      ])
    pthreads_files += [os.path.join('pthread', 'library_pthread.c')]
    return build_libc(libname, pthreads_files, ['-O2', '-s', 'USE_PTHREADS=1'])

  def create_pthreads_stub(libname):
    pthreads_files = [os.path.join('pthread', 'library_pthread_stub.c')]
    return build_libc(libname, pthreads_files, ['-O2'])

  def create_pthreads_asmjs(libname):
    pthreads_files = [os.path.join('pthread', 'library_pthread_asmjs.c')]
    return build_libc(libname, pthreads_files, ['-O2', '-s', 'USE_PTHREADS=1'])

  def create_pthreads_wasm(libname):
    pthreads_files = [os.path.join('pthread', 'library_pthread_wasm.c')]
    return build_libc(libname, pthreads_files, ['-O2', '-s', 'USE_PTHREADS=1'])

  def create_wasm_libc(libname):
    # in asm.js we just use Math.sin etc., which is good for code size. But
    # wasm doesn't have such builtins, so we need to bundle in more code
    files = files_in_path(
      path_components=['system', 'lib', 'libc', 'musl', 'src', 'math'],
      filenames=['cos.c', 'cosf.c', 'cosl.c', 'sin.c', 'sinf.c', 'sinl.c',
                 'tan.c', 'tanf.c', 'tanl.c', 'acos.c', 'acosf.c', 'acosl.c',
                 'asin.c', 'asinf.c', 'asinl.c', 'atan.c', 'atanf.c', 'atanl.c',
                 'atan2.c', 'atan2f.c', 'atan2l.c', 'exp.c', 'expf.c', 'expl.c',
                 'log.c', 'logf.c', 'logl.c', 'pow.c', 'powf.c', 'powl.c'])

    return build_libc(libname, files, ['-O2', '-fno-builtin'])

  # libc++
  def create_libcxx(libname):
    logger.debug('building libc++ for cache')
    libcxx_files = [
      'algorithm.cpp',
      'any.cpp',
      'bind.cpp',
      'chrono.cpp',
      'condition_variable.cpp',
      'debug.cpp',
      'exception.cpp',
      'future.cpp',
      'functional.cpp',
      'hash.cpp',
      'ios.cpp',
      'iostream.cpp',
      'locale.cpp',
      'memory.cpp',
      'mutex.cpp',
      'new.cpp',
      'optional.cpp',
      'random.cpp',
      'regex.cpp',
      'shared_mutex.cpp',
      'stdexcept.cpp',
      'string.cpp',
      'strstream.cpp',
      'system_error.cpp',
      'thread.cpp',
      'typeinfo.cpp',
      'utility.cpp',
      'valarray.cpp',
      'variant.cpp',
      'vector.cpp',
      os.path.join('experimental', 'memory_resource.cpp'),
      os.path.join('experimental', 'filesystem', 'directory_iterator.cpp'),
      os.path.join('experimental', 'filesystem', 'path.cpp'),
      os.path.join('experimental', 'filesystem', 'operations.cpp')
    ]
    libcxxabi_include = shared.path_from_root('system', 'lib', 'libcxxabi', 'include')
    return build_libcxx(
      os.path.join('system', 'lib', 'libcxx'), libname, libcxx_files,
      ['-DLIBCXX_BUILDING_LIBCXXABI=1', '-D_LIBCPP_BUILDING_LIBRARY', '-Oz', '-I' + libcxxabi_include],
      has_noexcept_version=True)

  # libcxxabi - just for dynamic_cast for now
  def create_libcxxabi(libname):
    logger.debug('building libc++abi for cache')
    libcxxabi_files = [
      'abort_message.cpp',
      'cxa_aux_runtime.cpp',
      'cxa_default_handlers.cpp',
      'cxa_demangle.cpp',
      'cxa_exception_storage.cpp',
      'cxa_guard.cpp',
      'cxa_new_delete.cpp',
      'cxa_handlers.cpp',
      'exception.cpp',
      'stdexcept.cpp',
      'typeinfo.cpp',
      'private_typeinfo.cpp'
    ]
    libcxxabi_include = shared.path_from_root('system', 'lib', 'libcxxabi', 'include')
    return build_libcxx(
      os.path.join('system', 'lib', 'libcxxabi', 'src'), libname, libcxxabi_files,
      ['-Oz', '-I' + libcxxabi_include])

  # gl
  def create_gl(libname):
    src_dir = shared.path_from_root('system', 'lib', 'gl')
    files = []
    for dirpath, dirnames, filenames in os.walk(src_dir):
      filenames = filter(lambda f: f.endswith('.c'), filenames)
      files += map(lambda f: os.path.join(src_dir, f), filenames)
    flags = ['-Oz', '-s', 'USE_WEBGL2=1']
    flags += threading_flags(libname)
    flags += legacy_gl_emulation_flags(libname)
    flags += gl_version_flags(libname)
    return build_libc(libname, files, flags)

  # al
  def create_al(libname): # libname is ignored, this is just one .o file
    o = in_temp('al.o')
    check_call([shared.PYTHON, shared.EMCC, shared.path_from_root('system', 'lib', 'al.c'), '-o', o, '-Os'] + get_cflags())
    return o

  def create_html5(libname):
    src_dir = shared.path_from_root('system', 'lib', 'html5')
    files = []
    for dirpath, dirnames, filenames in os.walk(src_dir):
      files += [os.path.join(src_dir, f) for f in filenames]
    return build_libc(libname, files, ['-Oz'])

  def create_compiler_rt(libname):
    files = files_in_path(
      path_components=['system', 'lib', 'compiler-rt', 'lib', 'builtins'],
      filenames=['divdc3.c', 'divsc3.c', 'muldc3.c', 'mulsc3.c'])

    o_s = []
    commands = []
    for src in files:
      o = in_temp(os.path.basename(src) + '.o')
      commands.append([shared.PYTHON, shared.EMCC, shared.path_from_root('system', 'lib', src), '-O2', '-o', o] + get_cflags())
      o_s.append(o)
    run_commands(commands)
    shared.Building.emar('cr', in_temp(libname), o_s)
    return in_temp(libname)

  # libc_extras
  def create_libc_extras(libname): # libname is ignored, this is just one .o file
    o = in_temp('libc_extras.o')
    check_call([shared.PYTHON, shared.EMCC, shared.path_from_root('system', 'lib', 'libc', 'extras.c'), '-o', o] + get_cflags())
    return o

  # decides which malloc to use, and returns the source for malloc and the full library name
  def malloc_decision():
    if shared.Settings.MALLOC == 'dlmalloc':
      base = 'dlmalloc'
    elif shared.Settings.MALLOC == 'emmalloc':
      base = 'emmalloc'
    else:
      raise Exception('malloc must be one of "emmalloc", "dlmalloc", see settings.js')

    # only dlmalloc supports most modes
    def require_dlmalloc(what):
      if base != 'dlmalloc':
        shared.exit_with_error('only dlmalloc is possible when using %s' % what)

    extra = ''
    if shared.Settings.DEBUG_LEVEL >= 3:
      extra += '_debug'
    if not shared.Settings.SUPPORT_ERRNO:
      # emmalloc does not use errno anyhow
      if base != 'emmalloc':
        extra += '_noerrno'
    if shared.Settings.USE_PTHREADS:
      extra += '_threadsafe'
      require_dlmalloc('pthreads')
    if shared.Settings.EMSCRIPTEN_TRACING:
      extra += '_tracing'
      require_dlmalloc('tracing')
    if base == 'dlmalloc':
      source = 'dlmalloc.c'
    elif base == 'emmalloc':
      source = 'emmalloc.cpp'
    return (source, 'lib' + base + extra)

  def malloc_source():
    return malloc_decision()[0]

  def malloc_name():
    return malloc_decision()[1]

  def create_malloc(out_name):
    o = in_temp(out_name)
    cflags = ['-O2', '-fno-builtin']
    if shared.Settings.USE_PTHREADS:
      cflags += ['-s', 'USE_PTHREADS=1']
    if shared.Settings.EMSCRIPTEN_TRACING:
      cflags += ['--tracing']
    if shared.Settings.DEBUG_LEVEL >= 3:
      cflags += ['-UNDEBUG', '-DDLMALLOC_DEBUG']
      # TODO: consider adding -DEMMALLOC_DEBUG, but that is quite slow
    else:
      cflags += ['-DNDEBUG']
    if not shared.Settings.SUPPORT_ERRNO:
      cflags += ['-DMALLOC_FAILURE_ACTION=']
    check_call([shared.PYTHON, shared.EMCC, shared.path_from_root('system', 'lib', malloc_source()), '-o', o] + cflags + get_cflags())
    return o

  def create_wasm_rt_lib(libname, files):
    # compiler-rt has to be built with WASM_OBJECT_FILES=1.   This is because
    # it includes the builtin symbols that the LTO complication can generate.
    # It seems that LTO as implemented by lld assumes that builtins do not
    # take part in LTO.
    # TODO(sbc): If we ever fix https://bugs.llvm.org/show_bug.cgi?id=41384 then
    # this restriction can be removed.
    o_s = []
    commands = []
    for src in files:
      o = in_temp(os.path.basename(src) + '.o')
      commands.append([shared.PYTHON, shared.EMCC, '-fno-builtin', '-O2',
                       '-c', shared.path_from_root('system', 'lib', src),
                       '-o', o] +
                      musl_internal_includes() +
                      get_cflags(force_object_files=True))
      o_s.append(o)
    run_commands(commands)
    lib = in_temp(libname)
    shared.Building.emar('cr', lib, o_s)
    return lib

  def create_wasm_compiler_rt(libname):
    files = files_in_path(
      path_components=['system', 'lib', 'compiler-rt', 'lib', 'builtins'],
      filenames=['addtf3.c', 'ashlti3.c', 'ashrti3.c', 'atomic.c', 'comparetf2.c',
                 'divtf3.c', 'divti3.c', 'udivmodti4.c',
                 'extenddftf2.c', 'extendsftf2.c',
                 'fixdfti.c', 'fixsfti.c', 'fixtfdi.c', 'fixtfsi.c', 'fixtfti.c',
                 'fixunsdfti.c', 'fixunssfti.c', 'fixunstfdi.c', 'fixunstfsi.c', 'fixunstfti.c',
                 'floatditf.c', 'floatsitf.c', 'floattidf.c', 'floattisf.c',
                 'floatunditf.c', 'floatunsitf.c', 'floatuntidf.c', 'floatuntisf.c', 'lshrti3.c',
                 'modti3.c', 'multc3.c', 'multf3.c', 'multi3.c', 'subtf3.c', 'udivti3.c', 'umodti3.c', 'ashrdi3.c',
                 'ashldi3.c', 'fixdfdi.c', 'floatdidf.c', 'lshrdi3.c', 'moddi3.c',
                 'trunctfdf2.c', 'trunctfsf2.c', 'umoddi3.c', 'fixunsdfdi.c', 'muldi3.c',
                 'divdi3.c', 'divmoddi4.c', 'udivdi3.c', 'udivmoddi4.c'])
    files += files_in_path(path_components=['system', 'lib', 'compiler-rt'],
                           filenames=['extras.c'])
    return create_wasm_rt_lib(libname, files)

  def create_wasm_libc_rt(libname):
    return create_wasm_rt_lib(libname, get_wasm_libc_rt_files())

  # Set of libraries to include on the link line, as opposed to `force` which
  # is the set of libraries to force include (with --whole-archive).
  always_include = set()

  # Setting this will only use the forced libs in EMCC_FORCE_STDLIBS. This avoids spending time checking
  # for unresolved symbols in your project files, which can speed up linking, but if you do not have
  # the proper list of actually needed libraries, errors can occur. See below for how we must
  # export all the symbols in deps_info when using this option.
  only_forced = os.environ.get('EMCC_ONLY_FORCED_STDLIBS')
  if only_forced:
    temp_files = []

  # Add in some hacks for js libraries. If a js lib depends on a symbol provided by a C library, it must be
  # added to here, because our deps go only one way (each library here is checked, then we check the next
  # in order - libc++, libcxextra, etc. - and then we run the JS compiler and provide extra symbols from
  # library*.js files. But we cannot then go back to the C libraries if a new dep was added!
  # TODO: Move all __deps from src/library*.js to deps_info.json, and use that single source of info
  #       both here and in the JS compiler.
  deps_info = json.loads(open(shared.path_from_root('src', 'deps_info.json')).read())
  added = set()

  def add_back_deps(need):
    more = False
    for ident, deps in deps_info.items():
      if ident in need.undefs and ident not in added:
        added.add(ident)
        more = True
        for dep in deps:
          need.undefs.add(dep)
          if shared.Settings.VERBOSE:
            logger.debug('adding dependency on %s due to deps-info on %s' % (dep, ident))
          shared.Settings.EXPORTED_FUNCTIONS.append('_' + dep)
    if more:
      add_back_deps(need) # recurse to get deps of deps

  # Scan symbols
  symbolses = shared.Building.parallel_llvm_nm([os.path.abspath(t) for t in temp_files])

  if len(symbolses) == 0:
    class Dummy(object):
      defs = set()
      undefs = set()
    symbolses.append(Dummy())

  # depend on exported functions
  for export in shared.Settings.EXPORTED_FUNCTIONS:
    if shared.Settings.VERBOSE:
      logger.debug('adding dependency on export %s' % export)
    symbolses[0].undefs.add(export[1:])

  for symbols in symbolses:
    add_back_deps(symbols)

  # If we are only doing forced stdlibs, then we don't know the actual symbols we need,
  # and must assume all of deps_info must be exported. Note that this might cause
  # warnings on exports that do not exist.
  if only_forced:
    for key, value in deps_info.items():
      for dep in value:
        shared.Settings.EXPORTED_FUNCTIONS.append('_' + dep)

  if shared.Settings.WASM_OBJECT_FILES:
    ext = 'a'
  else:
    ext = 'bc'

  libc_name = 'libc'
  libc_deps = ['libcompiler_rt']
  if shared.Settings.WASM:
    libc_deps += ['libc-wasm']
  if shared.Settings.USE_PTHREADS:
    libc_name = 'libc-mt'
    always_include.add('libpthreads')
    if not shared.Settings.WASM_BACKEND:
      always_include.add('libpthreads_asmjs')
    else:
      always_include.add('libpthreads_wasm')
  else:
    always_include.add('libpthreads_stub')
  always_include.add(malloc_name())
  if shared.Settings.WASM_BACKEND:
    always_include.add('libcompiler_rt')

  Library = namedtuple('Library', ['shortname', 'suffix', 'create', 'symbols', 'deps', 'can_noexcept'])

  system_libs = [Library('libc++',        'a', create_libcxx,      libcxx_symbols,      ['libc++abi'], True), # noqa
                 Library('libc++abi',     ext, create_libcxxabi,   libcxxabi_symbols,   [libc_name],   False), # noqa
                 Library('libal',         ext, create_al,          al_symbols,          [libc_name],   False), # noqa
                 Library('libhtml5',      ext, create_html5,       html5_symbols,       [],            False), # noqa
                 Library('libcompiler_rt','a', create_compiler_rt, compiler_rt_symbols, [libc_name],   False), # noqa
                 Library(malloc_name(),   ext, create_malloc,      [],                  [],            False)] # noqa

  gl_name = 'libgl'
  if shared.Settings.USE_PTHREADS:
    gl_name += '-mt'
  if shared.Settings.LEGACY_GL_EMULATION:
    gl_name += '-emu'
  if shared.Settings.USE_WEBGL2:
    gl_name += '-webgl2'
  system_libs += [Library(gl_name,        ext, create_gl,          gl_symbols,          [libc_name],   False)] # noqa

  if shared.Settings.USE_PTHREADS:
    system_libs += [Library('libpthreads',       ext, create_pthreads,       pthreads_symbols,       [libc_name],  False)] # noqa
    if not shared.Settings.WASM_BACKEND:
      system_libs += [Library('libpthreads_asmjs', ext, create_pthreads_asmjs, asmjs_pthreads_symbols, [libc_name], False)] # noqa
    else:
      system_libs += [Library('libpthreads_wasm', ext, create_pthreads_wasm,   [],                     [libc_name], False)] # noqa
  else:
    system_libs += [Library('libpthreads_stub',  ext, create_pthreads_stub,  stub_pthreads_symbols,  [libc_name],  False)] # noqa

  system_libs.append(Library(libc_name, ext, create_libc, libc_symbols, libc_deps, False))

  # if building to wasm, we need more math code, since we have less builtins
  if shared.Settings.WASM:
    system_libs.append(Library('libc-wasm', ext, create_wasm_libc, wasm_libc_symbols, [], False))

  # Add libc-extras at the end, as libc may end up requiring them, and they depend on nothing.
  system_libs.append(Library('libc-extras', ext, create_libc_extras, libc_extras_symbols, [], False))

  libs_to_link = []
  already_included = set()
  system_libs_map = {l.shortname: l for l in system_libs}

  # Setting this in the environment will avoid checking dependencies and make building big projects a little faster
  # 1 means include everything; otherwise it can be the name of a lib (libc++, etc.)
  # You can provide 1 to include everything, or a comma-separated list with the ones you want
  force = os.environ.get('EMCC_FORCE_STDLIBS')
  if force == '1':
    force = ','.join(system_libs_map.keys())
  force_include = set((force.split(',') if force else []) + forced)
  if force_include:
    logger.debug('forcing stdlibs: ' + str(force_include))

  for lib in always_include:
    assert lib in system_libs_map

  for lib in force_include:
    if lib not in system_libs_map:
      shared.exit_with_error('invalid forced library: %s', lib)

  def maybe_noexcept(name):
    if shared.Settings.DISABLE_EXCEPTION_CATCHING:
      name += '_noexcept'
    return name

  def add_library(lib):
    if lib.shortname in already_included:
      return
    already_included.add(lib.shortname)

    shortname = lib.shortname
    if lib.can_noexcept:
      shortname = maybe_noexcept(shortname)
    name = shortname + '.' + lib.suffix

    logger.debug('including %s' % name)

    def do_create():
      return lib.create(name)

    libfile = shared.Cache.get(name, do_create)
    need_whole_archive = lib.shortname in force_include and lib.suffix != 'bc'
    libs_to_link.append((libfile, need_whole_archive))

    # Recursively add dependencies
    for d in lib.deps:
      add_library(system_libs_map[d])

  # Go over libraries to figure out which we must include
  for lib in system_libs:
    assert lib.shortname.startswith('lib')
    if lib.shortname in already_included:
      continue
    force_this = lib.shortname in force_include
    if not force_this and only_forced:
      continue
    include_this = force_this or lib.shortname in always_include

    if not include_this:
      need_syms = set()
      has_syms = set()
      for symbols in symbolses:
        if shared.Settings.VERBOSE:
          logger.debug('undefs: ' + str(symbols.undefs))
        for library_symbol in lib.symbols:
          if library_symbol in symbols.undefs:
            need_syms.add(library_symbol)
          if library_symbol in symbols.defs:
            has_syms.add(library_symbol)
      for haz in has_syms:
        if haz in need_syms:
          # remove symbols that are supplied by another of the inputs
          need_syms.remove(haz)
      if shared.Settings.VERBOSE:
        logger.debug('considering %s: we need %s and have %s' % (lib.shortname, str(need_syms), str(has_syms)))
      if not len(need_syms):
        continue

    # We need to build and link the library in
    add_library(lib)

  if shared.Settings.WASM_BACKEND:
    libs_to_link.append((shared.Cache.get('libcompiler_rt_wasm.a', lambda: create_wasm_compiler_rt('libcompiler_rt_wasm.a')), False))
    libs_to_link.append((shared.Cache.get('libc_rt_wasm.a', lambda: create_wasm_libc_rt('libc_rt_wasm.a')), False))

  libs_to_link.sort(key=lambda x: x[0].endswith('.a')) # make sure to put .a files at the end.

  # libc++abi and libc++ *static* linking is tricky. e.g. cxa_demangle.cpp disables c++
  # exceptions, but since the string methods in the headers are *weakly* linked, then
  # we might have exception-supporting versions of them from elsewhere, and if libc++abi
  # is first then it would "win", breaking exception throwing from those string
  # header methods. To avoid that, we link libc++abi last.
  libs_to_link.sort(key=lambda x: x[0].endswith('libc++abi.bc'))

  # Wrap libraries in --whole-archive, as needed.  We need to do this last
  # since otherwise the abort sorting won't make sense.
  ret = []
  in_group = False
  for name, need_whole_archive in libs_to_link:
    if need_whole_archive and not in_group:
      ret.append('--whole-archive')
      in_group = True
    if in_group and not need_whole_archive:
      ret.append('--no-whole-archive')
      in_group = False
    ret.append(name)
  if in_group:
    ret.append('--no-whole-archive')

  return ret


class Ports(object):
  """emscripten-ports library management (https://github.com/emscripten-ports).
  """

  @staticmethod
  def get_lib_name(name):
    return shared.static_library_name(name)

  @staticmethod
  def build_port(src_path, output_path, includes=[], flags=[], exclude_files=[], exclude_dirs=[]):
    srcs = []
    for root, dirs, files in os.walk(src_path, topdown=False):
      if any((excluded in root) for excluded in exclude_dirs):
        continue
      for f in files:
        ext = os.path.splitext(f)[1]
        if ext in ('.c', '.cpp') and not any((excluded in f) for excluded in exclude_files):
            srcs.append(os.path.join(root, f))
    include_commands = ['-I' + src_path]
    for include in includes:
      include_commands.append('-I' + include)

    commands = []
    objects = []
    for src in srcs:
      obj = src + '.o'
      commands.append([shared.PYTHON, shared.EMCC, '-c', src, '-O2', '-o', obj, '-w'] + include_commands + flags + get_cflags())
      objects.append(obj)

    run_commands(commands)
    print('create_lib', output_path)
    create_lib(output_path, objects)
    return output_path

  @staticmethod
  def run_commands(commands): # make easily available for port objects
    run_commands(commands)

  @staticmethod
  def create_lib(libname, inputs): # make easily available for port objects
    create_lib(libname, inputs)

  @staticmethod
  def get_dir():
    dirname = os.environ.get('EM_PORTS') or os.path.expanduser(os.path.join('~', '.emscripten_ports'))
    shared.safe_ensure_dirs(dirname)
    return dirname

  @staticmethod
  def erase():
    dirname = Ports.get_dir()
    shared.try_delete(dirname)
    if os.path.exists(dirname):
      logger.warning('could not delete ports dir %s - try to delete it manually' % dirname)

  @staticmethod
  def get_build_dir():
    return shared.Cache.get_path('ports-builds')

  name_cache = set()

  @staticmethod
  def fetch_project(name, url, subdir, is_tarbz2=False):
    fullname = os.path.join(Ports.get_dir(), name)

    # if EMCC_LOCAL_PORTS is set, we use a local directory as our ports. This is useful
    # for testing. This env var should be in format
    #     name=dir,name=dir
    # e.g.
    #     sdl2=/home/username/dev/ports/SDL2
    # so you could run
    #     EMCC_LOCAL_PORTS="sdl2=/home/alon/Dev/ports/SDL2" ./tests/runner.py browser.test_sdl2_mouse
    # this will simply copy that directory into the ports directory for sdl2, and use that. It also
    # clears the build, so that it is rebuilt from that source.
    local_ports = os.environ.get('EMCC_LOCAL_PORTS')
    if local_ports:
      logger.warning('using local ports: %s' % local_ports)
      local_ports = [pair.split('=', 1) for pair in local_ports.split(',')]
      for local in local_ports:
        if name == local[0]:
          path = local[1]
          if name not in ports.ports_by_name:
            shared.exit_with_error('%s is not a known port' % name)
          port = ports.ports_by_name[name]
          if not hasattr(port, 'SUBDIR'):
            logger.error('port %s lacks .SUBDIR attribute, which we need in order to override it locally, please update it' % name)
            sys.exit(1)
          subdir = port.SUBDIR
          logger.warning('grabbing local port: ' + name + ' from ' + path + ' to ' + fullname + ' (subdir: ' + subdir + ')')
          shared.try_delete(fullname)
          shutil.copytree(path, os.path.join(fullname, subdir))
          Ports.clear_project_build(name)
          return

    if is_tarbz2:
      fullpath = fullname + '.tar.bz2'
    elif url.endswith('.tar.gz'):
      fullpath = fullname + '.tar.gz'
    else:
      fullpath = fullname + '.zip'

    if name not in Ports.name_cache: # only mention each port once in log
      logger.debug('including port: ' + name)
      logger.debug('    (at ' + fullname + ')')
      Ports.name_cache.add(name)

    class State(object):
      retrieved = False
      unpacked = False

    def retrieve():
      # retrieve from remote server
      logger.warning('retrieving port: ' + name + ' from ' + url)
      try:
        from urllib.request import urlopen
      except ImportError:
        # Python 2 compatibility
        from urllib2 import urlopen
      f = urlopen(url)
      data = f.read()
      open(fullpath, 'wb').write(data)
      State.retrieved = True

    def check_tag():
      if is_tarbz2:
        names = tarfile.open(fullpath, 'r:bz2').getnames()
      elif url.endswith('.tar.gz'):
        names = tarfile.open(fullpath, 'r:gz').getnames()
      else:
        names = zipfile.ZipFile(fullpath, 'r').namelist()

      # check if first entry of the archive is prefixed with the same
      # tag as we need so no longer download and recompile if so
      return bool(re.match(subdir + r'(\\|/|$)', names[0]))

    def unpack():
      logger.warning('unpacking port: ' + name)
      shared.safe_ensure_dirs(fullname)

      # TODO: Someday when we are using Python 3, we might want to change the
      # code below to use shlib.unpack_archive
      # e.g.: shutil.unpack_archive(filename=fullpath, extract_dir=fullname)
      # (https://docs.python.org/3/library/shutil.html#shutil.unpack_archive)
      if is_tarbz2:
        z = tarfile.open(fullpath, 'r:bz2')
      elif url.endswith('.tar.gz'):
        z = tarfile.open(fullpath, 'r:gz')
      else:
        z = zipfile.ZipFile(fullpath, 'r')
      try:
        cwd = os.getcwd()
        os.chdir(fullname)
        z.extractall()
      finally:
        os.chdir(cwd)

      State.unpacked = True

    # main logic. do this under a cache lock, since we don't want multiple jobs to
    # retrieve the same port at once

    shared.Cache.acquire_cache_lock()
    try:
      if not os.path.exists(fullpath):
        retrieve()

      if not os.path.exists(fullname):
        unpack()

      if not check_tag():
        logger.warning('local copy of port is not correct, retrieving from remote server')
        shared.try_delete(fullname)
        shared.try_delete(fullpath)
        retrieve()
        unpack()

      if State.unpacked:
        # we unpacked a new version, clear the build in the cache
        Ports.clear_project_build(name)
    finally:
      shared.Cache.release_cache_lock()

  @staticmethod
  def clear_project_build(name):
    port = ports.ports_by_name[name]
    port.clear(Ports, shared)
    shared.try_delete(os.path.join(Ports.get_build_dir(), name))

  @staticmethod
  def build_native(subdir):
    shared.Building.ensure_no_emmake('We cannot build the native system library in "%s" when under the influence of emmake/emconfigure. To avoid this, create system dirs beforehand, so they are not auto-built on demand. For example, for binaryen, do "python embuilder.py build binaryen"' % subdir)

    old = os.getcwd()

    try:
      os.chdir(subdir)

      cmake_build_type = 'Release'

      # Configure
      check_call(['cmake', '-DCMAKE_BUILD_TYPE=' + cmake_build_type, '.'])

      # Check which CMake generator CMake used so we know which form to pass parameters to make/msbuild/etc. build tool.
      generator = re.search('CMAKE_GENERATOR:INTERNAL=(.*)$', open('CMakeCache.txt', 'r').read(), re.MULTILINE).group(1)

      # Make variants support '-jX' for number of cores to build, MSBuild does /maxcpucount:X
      num_cores = str(shared.Building.get_num_cores())
      make_args = []
      if 'Makefiles' in generator and 'NMake' not in generator:
        make_args = ['--', '-j', num_cores]
      elif 'Visual Studio' in generator:
        make_args = ['--config', cmake_build_type, '--', '/maxcpucount:' + num_cores]

      # Kick off the build.
      check_call(['cmake', '--build', '.'] + make_args)
    finally:
      os.chdir(old)


# get all ports
def get_ports(settings):
  ret = []

  try:
    process_dependencies(settings)
    for port in ports.ports:
      # ports return their output files, which will be linked, or a txt file
      ret += [f for f in port.get(Ports, settings, shared) if not f.endswith('.txt')]
  except:
    logger.error('a problem occurred when using an emscripten-ports library.  try to run `emcc --clear-ports` and then run this command again')
    raise

  ret.reverse()
  return ret


def process_dependencies(settings):
  for port in reversed(ports.ports):
    if hasattr(port, "process_dependencies"):
      port.process_dependencies(settings)


def process_args(args, settings):
  process_dependencies(settings)
  for port in ports.ports:
    args = port.process_args(Ports, args, settings, shared)
  return args


# get a single port
def get_port(name, settings):
  port = ports.ports_by_name[name]
  if hasattr(port, "process_dependencies"):
    port.process_dependencies(settings)
  # ports return their output files, which will be linked, or a txt file
  return [f for f in port.get(Ports, settings, shared) if not f.endswith('.txt')]


def show_ports():
  print('Available ports:')
  for port in ports.ports:
    print('   ', port.show())
