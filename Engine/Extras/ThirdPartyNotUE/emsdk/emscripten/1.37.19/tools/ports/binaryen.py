import os, shutil, logging

TAG = 'version_34'

def needed(settings, shared, ports):
  if not settings.BINARYEN: return False
  try:
    if shared.BINARYEN_ROOT: # if defined, and not falsey, we don't need the port
      logging.debug('binaryen root already set to ' + shared.BINARYEN_ROOT)
      settings.BINARYEN_ROOT = shared.BINARYEN_ROOT
      return False
  except:
    pass
  settings.BINARYEN_ROOT = os.path.join(ports.get_dir(), 'binaryen', 'binaryen-' + TAG)
  logging.debug('setting binaryen root to ' + settings.BINARYEN_ROOT)
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
  return [shared.Cache.get('binaryen_tag_' + TAG, create, what='port', extension='.txt')]

def process_args(ports, args, settings, shared):
  # we don't need to do anything just for compile args; binaryen
  # is used in the final link stage to wasm, not bitcode
  # (otherwise, if we get binaryen while building to bitcode, we
  # may be building system libraries, so this would be recursive.
  # TODO: consider avoiding such recursion in a more direct way)
  return args

def show():
  return 'Binaryen (Apache 2.0 license)'

