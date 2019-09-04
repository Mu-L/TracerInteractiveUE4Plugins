# Copyright 2016 The Emscripten Authors.  All rights reserved.
# Emscripten is available under two separate licenses, the MIT license and the
# University of Illinois/NCSA Open Source License.  Both these licenses can be
# found in the LICENSE file.

import os
import logging

TAG = 'version_82'


def needed(settings, shared, ports):
  if not settings.WASM:
    return False
  if shared.BINARYEN_ROOT: # if defined, and not falsey, we don't need the port
    logging.debug('binaryen root already set to ' + shared.BINARYEN_ROOT)
    return False
  shared.BINARYEN_ROOT = os.path.join(ports.get_dir(), 'binaryen', 'binaryen-' + TAG)
  logging.debug('setting binaryen root to ' + shared.BINARYEN_ROOT)
  return True


def get(ports, settings, shared):
  if not needed(settings, shared, ports):
    return []

  ports.fetch_project('binaryen', 'https://github.com/WebAssembly/binaryen/archive/' + TAG + '.zip', 'binaryen-' + TAG)

  def create():
    logging.info('building port: binaryen')
    ports.build_native(os.path.join(ports.get_dir(), 'binaryen', 'binaryen-' + TAG))
    # the "output" of this port build is a tag file, saying which port we have
    tag_file = os.path.join(ports.get_dir(), 'binaryen', 'tag.txt')
    open(tag_file, 'w').write(TAG)
    return tag_file

  return [shared.Cache.get('binaryen_tag_' + TAG + '.txt', create, what='port')]


def clear(ports, shared):
  shared.Cache.erase_file('binaryen_tag_' + TAG + '.txt')


def process_args(ports, args, settings, shared):
  return args


def show():
  return 'Binaryen (Apache 2.0 license)'
