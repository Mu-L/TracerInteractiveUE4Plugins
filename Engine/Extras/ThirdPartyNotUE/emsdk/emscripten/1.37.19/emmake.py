#!/usr/bin/env python2

'''
This is a helper script. It runs make for you, setting
the environment variables to use emcc and so forth. Usage:

  emmake make [FLAGS]

Note that if you ran configure with emconfigure, then
the environment variables have already been detected
and set. This script is useful if you have no configure
step, and your Makefile uses the environment vars
directly.

The difference between this and emconfigure is that
emconfigure runs compilation into native code, so
that configure tests pass. emmake uses Emscripten to
generate JavaScript.
'''

import os, sys
from tools import shared
from subprocess import CalledProcessError

#
# Main run() function
#
def run():
  if len(sys.argv) < 2 or sys.argv[1] != 'make':
    print >> sys.stderr, '''
  emmake is a helper for make, setting various environment
  variables so that emcc etc. are used. Typical usage:

    emmake make [FLAGS]

  (but you can run any command instead of make)

  '''

  try:
    shared.Building.make(sys.argv[1:])
  except CalledProcessError, e:
    sys.exit(e.returncode)

if __name__ == '__main__':
  run()
