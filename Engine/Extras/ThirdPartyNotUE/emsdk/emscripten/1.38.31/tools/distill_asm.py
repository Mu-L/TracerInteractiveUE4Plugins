# Copyright 2014 The Emscripten Authors.  All rights reserved.
# Emscripten is available under two separate licenses, the MIT license and the
# University of Illinois/NCSA Open Source License.  Both these licenses can be
# found in the LICENSE file.

"""Gets the core asm module out of an emscripten output file.

By default it adds a ';' to end the

  var asm = ...

statement. You can add a third param to customize that. If the third param is 'swap-in', it will emit code to swap this asm module in, instead of the default one.

XXX this probably doesn't work with closure compiler advanced yet XXX
"""

import os
import sys

sys.path.insert(1, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from tools import asm_module

infile = sys.argv[1]
outfile = sys.argv[2]
extra = sys.argv[3] if len(sys.argv) >= 4 else ';'

module = asm_module.AsmModule(infile).asm_js

if extra == 'swap-in':
  # we do |var asm = | just like the original codebase, so that gets overridden anyhow (assuming global scripts).
  extra = r''' (asmGlobalArg, asmLibraryArg, buffer);
 // special fixups
 asm.stackRestore(Module['asm'].stackSave()); // if this fails, make sure the original was built to be swappable (-s SWAPPABLE_ASM_MODULE=1)
 // Finish swap
 Module['asm'] = asm;
 if (Module['onAsmSwap']) Module['onAsmSwap']();
'''
elif extra == 'just-func':
  module = module[module.find('=') + 1:] # strip the initial "var asm =" bit, leave just the raw module as a function
  extra = ';'

open(outfile, 'w').write(module + extra)
