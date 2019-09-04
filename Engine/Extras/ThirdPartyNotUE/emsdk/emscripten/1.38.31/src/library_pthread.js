// Copyright 2015 The Emscripten Authors.  All rights reserved.
// Emscripten is available under two separate licenses, the MIT license and the
// University of Illinois/NCSA Open Source License.  Both these licenses can be
// found in the LICENSE file.

var LibraryPThread = {
  $PThread__postset: 'if (!ENVIRONMENT_IS_PTHREAD) PThread.initMainThreadBlock();',
  $PThread__deps: ['$PROCINFO', '_register_pthread_ptr', 'emscripten_main_thread_process_queued_calls'],
  $PThread: {
    MAIN_THREAD_ID: 1, // A special constant that identifies the main JS thread ID.
    mainThreadInfo: {
      schedPolicy: 0/*SCHED_OTHER*/,
      schedPrio: 0
    },
    // Since creating a new Web Worker is so heavy (it must reload the whole compiled script page!), maintain a pool of such
    // workers that have already parsed and loaded the scripts.
    unusedWorkerPool: [],
    // The currently executing pthreads.
    runningWorkers: [],
    // Points to a pthread_t structure in the Emscripten main heap, allocated on demand if/when first needed.
    // mainThreadBlock: undefined,
    initMainThreadBlock: function() {
      if (ENVIRONMENT_IS_PTHREAD) return undefined;
      PThread.mainThreadBlock = {{{ makeStaticAlloc(C_STRUCTS.pthread.__size__) }}};

      for (var i = 0; i < {{{ C_STRUCTS.pthread.__size__ }}}/4; ++i) HEAPU32[PThread.mainThreadBlock/4+i] = 0;

      // The pthread struct has a field that points to itself - this is used as a magic ID to detect whether the pthread_t
      // structure is 'alive'.
      {{{ makeSetValue('PThread.mainThreadBlock', C_STRUCTS.pthread.self, 'PThread.mainThreadBlock', 'i32') }}};

      // pthread struct robust_list head should point to itself.
      var headPtr = PThread.mainThreadBlock + {{{ C_STRUCTS.pthread.robust_list }}};
      {{{ makeSetValue('headPtr', 0, 'headPtr', 'i32') }}};

      // Allocate memory for thread-local storage.
      var tlsMemory = {{{ makeStaticAlloc(cDefine('PTHREAD_KEYS_MAX') * 4) }}};
      for (var i = 0; i < {{{ cDefine('PTHREAD_KEYS_MAX') }}}; ++i) HEAPU32[tlsMemory/4+i] = 0;
      Atomics.store(HEAPU32, (PThread.mainThreadBlock + {{{ C_STRUCTS.pthread.tsd }}} ) >> 2, tlsMemory); // Init thread-local-storage memory array.
      Atomics.store(HEAPU32, (PThread.mainThreadBlock + {{{ C_STRUCTS.pthread.tid }}} ) >> 2, PThread.mainThreadBlock); // Main thread ID.
      Atomics.store(HEAPU32, (PThread.mainThreadBlock + {{{ C_STRUCTS.pthread.pid }}} ) >> 2, PROCINFO.pid); // Process ID.

#if PTHREADS_PROFILING
      PThread.createProfilerBlock(PThread.mainThreadBlock);
      PThread.setThreadName(PThread.mainThreadBlock, "Browser main thread");
      PThread.setThreadStatus(PThread.mainThreadBlock, {{{ cDefine('EM_THREAD_STATUS_RUNNING') }}});
#endif
    },
    // Maps pthread_t to pthread info objects
    pthreads: {},
    pthreadIdCounter: 2, // 0: invalid thread, 1: main JS UI thread, 2+: IDs for pthreads

    exitHandlers: null, // An array of C functions to run when this thread exits.

#if PTHREADS_PROFILING
    createProfilerBlock: function(pthreadPtr) {
      var profilerBlock = (pthreadPtr == PThread.mainThreadBlock) ? {{{ makeStaticAlloc(C_STRUCTS.thread_profiler_block.__size__) }}} : _malloc({{{ C_STRUCTS.thread_profiler_block.__size__ }}});
      Atomics.store(HEAPU32, (pthreadPtr + {{{ C_STRUCTS.pthread.profilerBlock }}} ) >> 2, profilerBlock);

      // Zero fill contents at startup.
      for (var i = 0; i < {{{ C_STRUCTS.thread_profiler_block.__size__ }}}; i += 4) Atomics.store(HEAPU32, (profilerBlock + i) >> 2, 0);
      Atomics.store(HEAPU32, (pthreadPtr + {{{ C_STRUCTS.thread_profiler_block.currentStatusStartTime }}} ) >> 2, performance.now());
    },

    // Sets the current thread status, but only if it was in the given expected state before. This is used
    // to allow high-level control flow "override" the thread status before low-level (futex wait) operations set it.
    setThreadStatusConditional: function(pthreadPtr, expectedStatus, newStatus) {
      var profilerBlock = Atomics.load(HEAPU32, (pthreadPtr + {{{ C_STRUCTS.pthread.profilerBlock }}} ) >> 2);
      if (!profilerBlock) return;

      var prevStatus = Atomics.load(HEAPU32, (profilerBlock + {{{ C_STRUCTS.thread_profiler_block.threadStatus }}} ) >> 2);

      if (prevStatus != newStatus && (prevStatus == expectedStatus || expectedStatus == -1)) {
        var now = performance.now();
        var startState = HEAPF64[(profilerBlock + {{{ C_STRUCTS.thread_profiler_block.currentStatusStartTime }}} ) >> 3];
        var duration = now - startState;

        HEAPF64[((profilerBlock + {{{ C_STRUCTS.thread_profiler_block.timeSpentInStatus }}} ) >> 3) + prevStatus] += duration;
        Atomics.store(HEAPU32, (profilerBlock + {{{ C_STRUCTS.thread_profiler_block.threadStatus }}} ) >> 2, newStatus);
        HEAPF64[(profilerBlock + {{{ C_STRUCTS.thread_profiler_block.currentStatusStartTime }}} ) >> 3] = now;
      }
    },

    // Unconditionally sets the thread status.
    setThreadStatus: function(pthreadPtr, newStatus) {
      PThread.setThreadStatusConditional(pthreadPtr, -1, newStatus);
    },

    setThreadName: function(pthreadPtr, name) {
      var profilerBlock = Atomics.load(HEAPU32, (pthreadPtr + {{{ C_STRUCTS.pthread.profilerBlock }}} ) >> 2);
      if (!profilerBlock) return;
      stringToUTF8(name, profilerBlock + {{{ C_STRUCTS.thread_profiler_block.name }}}, 32);
    },

    getThreadName: function(pthreadPtr) {
      var profilerBlock = Atomics.load(HEAPU32, (pthreadPtr + {{{ C_STRUCTS.pthread.profilerBlock }}} ) >> 2);
      if (!profilerBlock) return "";
      return UTF8ToString(profilerBlock + {{{ C_STRUCTS.thread_profiler_block.name }}});
    },

    threadStatusToString: function(threadStatus) {
      switch(threadStatus) {
        case 0: return "not yet started";
        case 1: return "running";
        case 2: return "sleeping";
        case 3: return "waiting for a futex";
        case 4: return "waiting for a mutex";
        case 5: return "waiting for a proxied operation";
        case 6: return "finished execution";
        default: return "unknown (corrupt?!)";
      }
    },

    threadStatusAsString: function(pthreadPtr) {
      var profilerBlock = Atomics.load(HEAPU32, (pthreadPtr + {{{ C_STRUCTS.pthread.profilerBlock }}} ) >> 2);
      var status = (profilerBlock == 0) ? 0 : Atomics.load(HEAPU32, (profilerBlock + {{{ C_STRUCTS.thread_profiler_block.threadStatus }}} ) >> 2);
      return PThread.threadStatusToString(status);
    },
#else
    setThreadStatus: function() {},
#endif

    runExitHandlers: function() {
      if (PThread.exitHandlers !== null) {
        while (PThread.exitHandlers.length > 0) {
          PThread.exitHandlers.pop()();
        }
        PThread.exitHandlers = null;
      }

      // Call into the musl function that runs destructors of all thread-specific data.
      if (ENVIRONMENT_IS_PTHREAD && threadInfoStruct) ___pthread_tsd_run_dtors();
    },

    // Called when we are performing a pthread_exit(), either explicitly called by programmer,
    // or implicitly when leaving the thread main function.
    threadExit: function(exitCode) {
      var tb = _pthread_self();
      if (tb) { // If we haven't yet exited?
#if PTHREADS_PROFILING
        var profilerBlock = Atomics.load(HEAPU32, (threadInfoStruct + {{{ C_STRUCTS.pthread.profilerBlock }}} ) >> 2);
        Atomics.store(HEAPU32, (threadInfoStruct + {{{ C_STRUCTS.pthread.profilerBlock }}} ) >> 2, 0);
        _free(profilerBlock);
#endif
        Atomics.store(HEAPU32, (tb + {{{ C_STRUCTS.pthread.threadExitCode }}} ) >> 2, exitCode);
        // When we publish this, the main thread is free to deallocate the thread object and we are done.
        // Therefore set threadInfoStruct = 0; above to 'release' the object in this worker thread.
        Atomics.store(HEAPU32, (tb + {{{ C_STRUCTS.pthread.threadStatus }}} ) >> 2, 1);

        // Disable all cancellation so that executing the cleanup handlers won't trigger another JS
        // canceled exception to be thrown.
        Atomics.store(HEAPU32, (tb + {{{ C_STRUCTS.pthread.canceldisable }}} ) >> 2, 1/*PTHREAD_CANCEL_DISABLE*/);
        Atomics.store(HEAPU32, (tb + {{{ C_STRUCTS.pthread.cancelasync }}} ) >> 2, 0/*PTHREAD_CANCEL_DEFERRED*/);
        PThread.runExitHandlers();

        _emscripten_futex_wake(tb + {{{ C_STRUCTS.pthread.threadStatus }}}, {{{ cDefine('INT_MAX') }}});
        __register_pthread_ptr(0, 0, 0); // Unregister the thread block also inside the asm.js scope.
        threadInfoStruct = 0;
        if (ENVIRONMENT_IS_PTHREAD) {
          // This worker no longer owns any WebGL OffscreenCanvases, so transfer them back to parent thread.
          var transferList = [];

#if OFFSCREENCANVAS_SUPPORT
          var offscreenCanvases = {};
          if (typeof GL !== 'undefined') {
            offscreenCanvases = GL.offscreenCanvases;
            GL.offscreenCanvases = {};
          }
#if PTHREADS_DEBUG
          console.error('[thread ' + _pthread_self() + ', ENVIRONMENT_IS_PTHREAD: ' + ENVIRONMENT_IS_PTHREAD + ']: returning ' + Object.keys(offscreenCanvases).length + ' OffscreenCanvases to parent thread ' + parentThreadId);
#endif
          for (var i in offscreenCanvases) {
            if (offscreenCanvases[i]) transferList.push(offscreenCanvases[i].offscreenCanvas);
          }
          if (transferList.length > 0) {
            postMessage({
                targetThread: _emscripten_main_browser_thread_id(),
                cmd: 'objectTransfer',
                offscreenCanvases: offscreenCanvases,
                moduleCanvasId: Module['canvas'].id, // moduleCanvasId specifies which canvas is denoted via the "#canvas" shorthand.
                transferList: transferList
              }, transferList);
          }
          // And clear the OffscreenCanvases from lingering around in this Worker as well.
          delete Module['canvas'];
#endif

          postMessage({ cmd: 'exit' });
        }
      }
    },

    threadCancel: function() {
      PThread.runExitHandlers();
      Atomics.store(HEAPU32, (threadInfoStruct + {{{ C_STRUCTS.pthread.threadExitCode }}} ) >> 2, -1/*PTHREAD_CANCELED*/);
      Atomics.store(HEAPU32, (threadInfoStruct + {{{ C_STRUCTS.pthread.threadStatus }}} ) >> 2, 1); // Mark the thread as no longer running.
      _emscripten_futex_wake(threadInfoStruct + {{{ C_STRUCTS.pthread.threadStatus }}}, {{{ cDefine('INT_MAX') }}}); // wake all threads
      threadInfoStruct = selfThreadId = 0; // Not hosting a pthread anymore in this worker, reset the info structures to null.
      __register_pthread_ptr(0, 0, 0); // Unregister the thread block also inside the asm.js scope.
      postMessage({ cmd: 'cancelDone' });
    },

    terminateAllThreads: function() {
      for (var t in PThread.pthreads) {
        var pthread = PThread.pthreads[t];
        if (pthread) {
          PThread.freeThreadData(pthread);
          if (pthread.worker) pthread.worker.terminate();
        }
      }
      PThread.pthreads = {};
      for (var t in PThread.unusedWorkerPool) {
        var pthread = PThread.unusedWorkerPool[t];
        if (pthread) {
          PThread.freeThreadData(pthread);
          if (pthread.worker) pthread.worker.terminate();
        }
      }
      PThread.unusedWorkerPool = [];
      for (var t in PThread.runningWorkers) {
        var pthread = PThread.runningWorkers[t];
        if (pthread) {
          PThread.freeThreadData(pthread);
          if (pthread.worker) pthread.worker.terminate();
        }
      }
      PThread.runningWorkers = [];
    },
    freeThreadData: function(pthread) {
      if (!pthread) return;
      if (pthread.threadInfoStruct) {
        var tlsMemory = {{{ makeGetValue('pthread.threadInfoStruct', C_STRUCTS.pthread.tsd, 'i32') }}};
        {{{ makeSetValue('pthread.threadInfoStruct', C_STRUCTS.pthread.tsd, 0, 'i32') }}};
        _free(pthread.tlsMemory);
        _free(pthread.threadInfoStruct);
      }
      pthread.threadInfoStruct = 0;
      if (pthread.allocatedOwnStack && pthread.stackBase) _free(pthread.stackBase);
      pthread.stackBase = 0;
      if (pthread.worker) pthread.worker.pthread = null;
    },

    receiveObjectTransfer: function(data) {
#if OFFSCREENCANVAS_SUPPORT
      if (typeof GL !== 'undefined') {
        for (var i in data.offscreenCanvases) {
          GL.offscreenCanvases[i] = data.offscreenCanvases[i];
        }
        if (!Module['canvas'] && data.moduleCanvasId && GL.offscreenCanvases[data.moduleCanvasId]) {
          Module['canvas'] = GL.offscreenCanvases[data.moduleCanvasId].offscreenCanvas;
          Module['canvas'].id = data.moduleCanvasId;
        }
      }
#endif
    },

    // Allocates the given amount of new web workers and stores them in the pool of unused workers.
    // onFinishedLoading: A callback function that will be called once all of the workers have been initialized and are
    //                    ready to host pthreads. Optional. This is used to mitigate bug https://bugzilla.mozilla.org/show_bug.cgi?id=1049079
    allocateUnusedWorkers: function(numWorkers, onFinishedLoading) {
      if (typeof SharedArrayBuffer === 'undefined') return; // No multithreading support, no-op.
#if PTHREADS_DEBUG
      out('Preallocating ' + numWorkers + ' workers for a pthread spawn pool.');
#endif

      var numWorkersLoaded = 0;
      var pthreadMainJs = "{{{ PTHREAD_WORKER_FILE }}}";
      // Allow HTML module to configure the location where the 'worker.js' file will be loaded from,
      // via Module.locateFile() function. If not specified, then the default URL 'worker.js' relative
      // to the main html file is loaded.
      pthreadMainJs = locateFile(pthreadMainJs);

      for (var i = 0; i < numWorkers; ++i) {
        var worker = new Worker(pthreadMainJs);

        (function(worker) {
          worker.onmessage = function(e) {
            var d = e.data;
            // Sometimes we need to backproxy events to the calling thread (e.g. HTML5 DOM events handlers such as emscripten_set_mousemove_callback()), so keep track in a globally accessible variable about the thread that initiated the proxying.
            if (worker.pthread) PThread.currentProxiedOperationCallerThread = worker.pthread.threadInfoStruct;

            // If this message is intended to a recipient that is not the main thread, forward it to the target thread.
            if (d.targetThread && d.targetThread != _pthread_self()) {
              var thread = PThread.pthreads[d.targetThread];
              if (thread) {
                thread.worker.postMessage(e.data, d.transferList);
              } else {
                console.error('Internal error! Worker sent a message "' + d.cmd + '" to target pthread ' + d.targetThread + ', but that thread no longer exists!');
              }
              PThread.currentProxiedOperationCallerThread = undefined;
              return;
            }

            if (d.cmd === 'processQueuedMainThreadWork') {
              // TODO: Must post message to main Emscripten thread in PROXY_TO_WORKER mode.
              _emscripten_main_thread_process_queued_calls();
            } else if (d.cmd === 'spawnThread') {
              __spawn_thread(e.data);
            } else if (d.cmd === 'cleanupThread') {
              __cleanup_thread(d.thread);
            } else if (d.cmd === 'killThread') {
              __kill_thread(d.thread);
            } else if (d.cmd === 'cancelThread') {
              __cancel_thread(d.thread);
            } else if (d.cmd === 'loaded') {
              worker.loaded = true;
              // If this Worker is already pending to start running a thread, launch the thread now
              if (worker.runPthread) {
                worker.runPthread();
                delete worker.runPthread;
              }
              ++numWorkersLoaded;
              if (numWorkersLoaded === numWorkers && onFinishedLoading) {
                onFinishedLoading();
              }
            } else if (d.cmd === 'print') {
              out('Thread ' + d.threadId + ': ' + d.text);
            } else if (d.cmd === 'printErr') {
              err('Thread ' + d.threadId + ': ' + d.text);
            } else if (d.cmd === 'alert') {
              alert('Thread ' + d.threadId + ': ' + d.text);
            } else if (d.cmd === 'exit') {
              // Thread is exiting, no-op here
            } else if (d.cmd === 'exitProcess') {
              // A pthread has requested to exit the whole application process (runtime).
              Module['noExitRuntime'] = false;
              exit(d.returnCode);
            } else if (d.cmd === 'cancelDone') {
              PThread.freeThreadData(worker.pthread);
              worker.pthread = undefined; // Detach the worker from the pthread object, and return it to the worker pool as an unused worker.
              PThread.unusedWorkerPool.push(worker);
              // TODO: Free if detached.
              PThread.runningWorkers.splice(PThread.runningWorkers.indexOf(worker.pthread), 1); // Not a running Worker anymore.
            } else if (d.cmd === 'objectTransfer') {
              PThread.receiveObjectTransfer(e.data);
            } else if (e.data.target === 'setimmediate') {
              worker.postMessage(e.data); // Worker wants to postMessage() to itself to implement setImmediate() emulation.
            } else {
              err("worker sent an unknown command " + d.cmd);
            }
            PThread.currentProxiedOperationCallerThread = undefined;
          };

          worker.onerror = function(e) {
            err('pthread sent an error! ' + e.filename + ':' + e.lineno + ': ' + e.message);
          };
        }(worker));

        // Allocate tempDoublePtr for the worker. This is done here on the worker's behalf, since we may need to do this statically
        // if the runtime has not been loaded yet, etc. - so we just use getMemory, which is main-thread only.
        var tempDoublePtr = getMemory(8); // TODO: leaks. Cleanup after worker terminates.

        // Ask the new worker to load up the Emscripten-compiled page. This is a heavy operation.
        worker.postMessage({
          cmd: 'load',
          // If the application main .js file was loaded from a Blob, then it is not possible
          // to access the URL of the current script that could be passed to a Web Worker so that
          // it could load up the same file. In that case, developer must either deliver the Blob
          // object in Module['mainScriptUrlOrBlob'], or a URL to it, so that pthread Workers can
          // independently load up the same main application file.
          urlOrBlob: Module['mainScriptUrlOrBlob'] || _scriptDir,
#if WASM
          wasmMemory: wasmMemory,
          wasmModule: wasmModule,
#else
          buffer: HEAPU8.buffer,
          asmJsUrlOrBlob: Module["asmJsUrlOrBlob"],
#endif
          tempDoublePtr: tempDoublePtr,
          DYNAMIC_BASE: DYNAMIC_BASE,
          DYNAMICTOP_PTR: DYNAMICTOP_PTR,
          PthreadWorkerInit: PthreadWorkerInit
        });
        PThread.unusedWorkerPool.push(worker);
      }
    },

    getNewWorker: function() {
      if (PThread.unusedWorkerPool.length == 0) PThread.allocateUnusedWorkers(1);
      if (PThread.unusedWorkerPool.length > 0) return PThread.unusedWorkerPool.pop();
      else return null;
    },

    busySpinWait: function(msecs) {
      var t = performance.now() + msecs;
      while(performance.now() < t) {
        ;
      }
    }
  },

  _kill_thread: function(pthread_ptr) {
    if (ENVIRONMENT_IS_PTHREAD) throw 'Internal Error! _kill_thread() can only ever be called from main application thread!';
    if (!pthread_ptr) throw 'Internal Error! Null pthread_ptr in _kill_thread!';
    {{{ makeSetValue('pthread_ptr', C_STRUCTS.pthread.self, 0, 'i32') }}};
    var pthread = PThread.pthreads[pthread_ptr];
    pthread.worker.terminate();
    PThread.freeThreadData(pthread);
    // The worker was completely nuked (not just the pthread execution it was hosting), so remove it from running workers
    // but don't put it back to the pool.
    PThread.runningWorkers.splice(PThread.runningWorkers.indexOf(pthread.worker.pthread), 1); // Not a running Worker anymore.
    pthread.worker.pthread = undefined;
  },

  _cleanup_thread: function(pthread_ptr) {
    if (ENVIRONMENT_IS_PTHREAD) throw 'Internal Error! _cleanup_thread() can only ever be called from main application thread!';
    if (!pthread_ptr) throw 'Internal Error! Null pthread_ptr in _cleanup_thread!';
    {{{ makeSetValue('pthread_ptr', C_STRUCTS.pthread.self, 0, 'i32') }}};
    var pthread = PThread.pthreads[pthread_ptr];
    var worker = pthread.worker;
    PThread.freeThreadData(pthread);
    worker.pthread = undefined; // Detach the worker from the pthread object, and return it to the worker pool as an unused worker.
    PThread.unusedWorkerPool.push(worker);
    PThread.runningWorkers.splice(PThread.runningWorkers.indexOf(worker.pthread), 1); // Not a running Worker anymore.
  },

  _cancel_thread: function(pthread_ptr) {
    if (ENVIRONMENT_IS_PTHREAD) throw 'Internal Error! _cancel_thread() can only ever be called from main application thread!';
    if (!pthread_ptr) throw 'Internal Error! Null pthread_ptr in _cancel_thread!';
    var pthread = PThread.pthreads[pthread_ptr];
    pthread.worker.postMessage({ cmd: 'cancel' });
  },

  _spawn_thread: function(threadParams) {
    if (ENVIRONMENT_IS_PTHREAD) throw 'Internal Error! _spawn_thread() can only ever be called from main application thread!';

    var worker = PThread.getNewWorker();
    if (worker.pthread !== undefined) throw 'Internal error!';
    if (!threadParams.pthread_ptr) throw 'Internal error, no pthread ptr!';
    PThread.runningWorkers.push(worker);

    // Allocate memory for thread-local storage and initialize it to zero.
    var tlsMemory = _malloc({{{ cDefine('PTHREAD_KEYS_MAX') }}} * 4);
    for (var i = 0; i < {{{ cDefine('PTHREAD_KEYS_MAX') }}}; ++i) {
      {{{ makeSetValue('tlsMemory', 'i*4', 0, 'i32') }}};
    }

    var pthread = PThread.pthreads[threadParams.pthread_ptr] = { // Create a pthread info object to represent this thread.
      worker: worker,
      stackBase: threadParams.stackBase,
      stackSize: threadParams.stackSize,
      allocatedOwnStack: threadParams.allocatedOwnStack,
      thread: threadParams.pthread_ptr,
      threadInfoStruct: threadParams.pthread_ptr // Info area for this thread in Emscripten HEAP (shared)
    };
    Atomics.store(HEAPU32, (pthread.threadInfoStruct + {{{ C_STRUCTS.pthread.threadStatus }}} ) >> 2, 0); // threadStatus <- 0, meaning not yet exited.
    Atomics.store(HEAPU32, (pthread.threadInfoStruct + {{{ C_STRUCTS.pthread.threadExitCode }}} ) >> 2, 0); // threadExitCode <- 0.
    Atomics.store(HEAPU32, (pthread.threadInfoStruct + {{{ C_STRUCTS.pthread.profilerBlock }}} ) >> 2, 0); // profilerBlock <- 0.
    Atomics.store(HEAPU32, (pthread.threadInfoStruct + {{{ C_STRUCTS.pthread.detached }}} ) >> 2, threadParams.detached);
    Atomics.store(HEAPU32, (pthread.threadInfoStruct + {{{ C_STRUCTS.pthread.tsd }}} ) >> 2, tlsMemory); // Init thread-local-storage memory array.
    Atomics.store(HEAPU32, (pthread.threadInfoStruct + {{{ C_STRUCTS.pthread.tsd_used }}} ) >> 2, 0); // Mark initial status to unused.
    Atomics.store(HEAPU32, (pthread.threadInfoStruct + {{{ C_STRUCTS.pthread.tid }}} ) >> 2, pthread.threadInfoStruct); // Main thread ID.
    Atomics.store(HEAPU32, (pthread.threadInfoStruct + {{{ C_STRUCTS.pthread.pid }}} ) >> 2, PROCINFO.pid); // Process ID.

    Atomics.store(HEAPU32, (pthread.threadInfoStruct + {{{ C_STRUCTS.pthread.attr }}}) >> 2, threadParams.stackSize);
    Atomics.store(HEAPU32, (pthread.threadInfoStruct + {{{ C_STRUCTS.pthread.stack_size }}}) >> 2, threadParams.stackSize);
    Atomics.store(HEAPU32, (pthread.threadInfoStruct + {{{ C_STRUCTS.pthread.stack }}}) >> 2, threadParams.stackBase);
    Atomics.store(HEAPU32, (pthread.threadInfoStruct + {{{ C_STRUCTS.pthread.attr }}} + 8) >> 2, threadParams.stackBase);
    Atomics.store(HEAPU32, (pthread.threadInfoStruct + {{{ C_STRUCTS.pthread.attr }}} + 12) >> 2, threadParams.detached);
    Atomics.store(HEAPU32, (pthread.threadInfoStruct + {{{ C_STRUCTS.pthread.attr }}} + 20) >> 2, threadParams.schedPolicy);
    Atomics.store(HEAPU32, (pthread.threadInfoStruct + {{{ C_STRUCTS.pthread.attr }}} + 24) >> 2, threadParams.schedPrio);

    var global_libc = _emscripten_get_global_libc();
    var global_locale = global_libc + {{{ C_STRUCTS.libc.global_locale }}};
    Atomics.store(HEAPU32, (pthread.threadInfoStruct + {{{ C_STRUCTS.pthread.locale }}}) >> 2, global_locale);

#if PTHREADS_PROFILING
    PThread.createProfilerBlock(pthread.threadInfoStruct);
#endif

    worker.pthread = pthread;
    var msg = {
        cmd: 'run',
        start_routine: threadParams.startRoutine,
        arg: threadParams.arg,
        threadInfoStruct: threadParams.pthread_ptr,
        selfThreadId: threadParams.pthread_ptr, // TODO: Remove this since thread ID is now the same as the thread address.
        parentThreadId: threadParams.parent_pthread_ptr,
        stackBase: threadParams.stackBase,
        stackSize: threadParams.stackSize,
#if OFFSCREENCANVAS_SUPPORT
        moduleCanvasId: threadParams.moduleCanvasId,
        offscreenCanvases: threadParams.offscreenCanvases,
#endif
      };
    worker.runPthread = function() {
      // Ask the worker to start executing its pthread entry point function.
      msg.time = performance.now();
      worker.postMessage(msg, threadParams.transferList);
    };
    if (worker.loaded) {
      worker.runPthread();
      delete worker.runPthread;
    }
  },

  _num_logical_cores__deps: ['emscripten_force_num_logical_cores'],
  _num_logical_cores: '; if (ENVIRONMENT_IS_PTHREAD) __num_logical_cores = PthreadWorkerInit.__num_logical_cores; else { PthreadWorkerInit.__num_logical_cores = __num_logical_cores = {{{ makeStaticAlloc(4) }}}; HEAPU32[__num_logical_cores>>2] = navigator["hardwareConcurrency"] || ' + {{{ PTHREAD_HINT_NUM_CORES }}} + '; }',

  emscripten_has_threading_support: function() {
    return typeof SharedArrayBuffer !== 'undefined';
  },

  emscripten_num_logical_cores__deps: ['_num_logical_cores'],
  emscripten_num_logical_cores: function() {
    return {{{ makeGetValue('__num_logical_cores', 0, 'i32') }}};
  },

  emscripten_force_num_logical_cores: function(cores) {
    {{{ makeSetValue('__num_logical_cores', 0, 'cores', 'i32') }}};
  },

  pthread_create__deps: ['_spawn_thread', 'pthread_getschedparam', 'pthread_self'],
  pthread_create: function(pthread_ptr, attr, start_routine, arg) {
    if (typeof SharedArrayBuffer === 'undefined') {
      err('Current environment does not support SharedArrayBuffer, pthreads are not available!');
      return {{{ cDefine('EAGAIN') }}};
    }
    if (!pthread_ptr) {
      err('pthread_create called with a null thread pointer!');
      return {{{ cDefine('EINVAL') }}};
    }

    var transferList = []; // List of JS objects that will transfer ownership to the Worker hosting the thread
    var error = 0;

#if OFFSCREENCANVAS_SUPPORT
    // Deduce which WebGL canvases (HTMLCanvasElements or OffscreenCanvases) should be passed over to the
    // Worker that hosts the spawned pthread.
    var transferredCanvasNames = attr ? {{{ makeGetValue('attr', 36, 'i32') }}} : 0; // Comma-delimited list of IDs "canvas1, canvas2, ..."
    if (transferredCanvasNames) transferredCanvasNames = UTF8ToString(transferredCanvasNames).trim();
    if (transferredCanvasNames) transferredCanvasNames = transferredCanvasNames.split(',');
#if GL_DEBUG
    console.log('pthread_create: transferredCanvasNames="' + transferredCanvasNames + '"');
#endif

    var offscreenCanvases = {}; // Dictionary of OffscreenCanvas objects we'll transfer to the created thread to own
    var moduleCanvasId = Module['canvas'] ? Module['canvas'].id : '';
    for (var i in transferredCanvasNames) {
      var name = transferredCanvasNames[i].trim();
      var offscreenCanvasInfo;
      try {
        if (name == '#canvas') {
          if (!Module['canvas']) {
            err('pthread_create: could not find canvas with ID "' + name + '" to transfer to thread!');
            error = {{{ cDefine('EINVAL') }}};
            break;
          }
          name = Module['canvas'].id;
        }
#if ASSERTIONS
        assert(typeof GL === 'object', 'OFFSCREENCANVAS_SUPPORT assumes GL is in use (you can force-include it with -s \'DEFAULT_LIBRARY_FUNCS_TO_INCLUDE=["$GL"]\')');
#endif
        if (GL.offscreenCanvases[name]) {
          offscreenCanvasInfo = GL.offscreenCanvases[name];
          GL.offscreenCanvases[name] = null; // This thread no longer owns this canvas.
          if (Module['canvas'] instanceof OffscreenCanvas && name === Module['canvas'].id) Module['canvas'] = null;
        } else {
          var canvas = (Module['canvas'] && Module['canvas'].id === name) ? Module['canvas'] : document.getElementById(name);
          if (!canvas) {
            err('pthread_create: could not find canvas with ID "' + name + '" to transfer to thread!');
            error = {{{ cDefine('EINVAL') }}};
            break;
          }
          if (canvas.controlTransferredOffscreen) {
            err('pthread_create: cannot transfer canvas with ID "' + name + '" to thread, since the current thread does not have control over it!');
            error = {{{ cDefine('EPERM') }}}; // Operation not permitted, some other thread is accessing the canvas.
            break;
          }
          if (canvas.transferControlToOffscreen) {
#if GL_DEBUG
            Module['printErr']('pthread_create: canvas.transferControlToOffscreen(), transferring canvas by name "' + name + '" (DOM id="' + canvas.id + '") from main thread to pthread');
#endif
            // Create a shared information block in heap so that we can control the canvas size from any thread.
            if (!canvas.canvasSharedPtr) {
              canvas.canvasSharedPtr = _malloc(12);
              {{{ makeSetValue('canvas.canvasSharedPtr', 0, 'canvas.width', 'i32') }}};
              {{{ makeSetValue('canvas.canvasSharedPtr', 4, 'canvas.height', 'i32') }}};
              {{{ makeSetValue('canvas.canvasSharedPtr', 8, 0, 'i32') }}}; // pthread ptr to the thread that owns this canvas, filled in below.
            }
            offscreenCanvasInfo = {
              offscreenCanvas: canvas.transferControlToOffscreen(),
              canvasSharedPtr: canvas.canvasSharedPtr,
              id: canvas.id
            }
            // After calling canvas.transferControlToOffscreen(), it is no longer possible to access certain operations on the canvas, such as resizing it or obtaining GL contexts via it.
            // Use this field to remember that we have permanently converted this Canvas to be controlled via an OffscreenCanvas (there is no way to undo this in the spec)
            canvas.controlTransferredOffscreen = true;
          } else {
            err('pthread_create: cannot transfer control of canvas "' + name + '" to pthread, because current browser does not support OffscreenCanvas!');
            // If building with OFFSCREEN_FRAMEBUFFER=1 mode, we don't need to be able to transfer control to offscreen, but WebGL can be proxied from worker to main thread.
#if !OFFSCREEN_FRAMEBUFFER
            Module['printErr']('pthread_create: Build with -s OFFSCREEN_FRAMEBUFFER=1 to enable fallback proxying of GL commands from pthread to main thread.');
            return {{{ cDefine('ENOSYS') }}}; // Function not implemented, browser doesn't have support for this.
#endif
          }
        }
        if (offscreenCanvasInfo) {
          transferList.push(offscreenCanvasInfo.offscreenCanvas);
          offscreenCanvases[offscreenCanvasInfo.id] = offscreenCanvasInfo;
        }
      } catch(e) {
        err('pthread_create: failed to transfer control of canvas "' + name + '" to OffscreenCanvas! Error: ' + e);
        return {{{ cDefine('EINVAL') }}}; // Hitting this might indicate an implementation bug or some other internal error
      }
    }
#endif

    // Synchronously proxy the thread creation to main thread if possible. If we need to transfer ownership of objects, then
    // proxy asynchronously via postMessage.
    if (ENVIRONMENT_IS_PTHREAD && (transferList.length == 0 || error)) {
      return _emscripten_sync_run_in_main_thread_4({{{ cDefine('EM_PROXIED_PTHREAD_CREATE') }}}, pthread_ptr, attr, start_routine, arg);
    }

    // If on the main thread, and accessing Canvas/OffscreenCanvas failed, abort with the detected error.
    if (error) return error;

    var stackSize = 0;
    var stackBase = 0;
    var detached = 0; // Default thread attr is PTHREAD_CREATE_JOINABLE, i.e. start as not detached.
    var schedPolicy = 0; /*SCHED_OTHER*/
    var schedPrio = 0;
    if (attr) {
      stackSize = {{{ makeGetValue('attr', 0, 'i32') }}};
      // Musl has a convention that the stack size that is stored to the pthread attribute structure is always musl's #define DEFAULT_STACK_SIZE
      // smaller than the actual created stack size. That is, stored stack size of 0 would mean a stack of DEFAULT_STACK_SIZE in size. All musl
      // functions hide this impl detail, and offset the size transparently, so pthread_*() API user does not see this offset when operating with
      // the pthread API. When reading the structure directly on JS side however, we need to offset the size manually here.
      stackSize += 81920 /*DEFAULT_STACK_SIZE*/;
      stackBase = {{{ makeGetValue('attr', 8, 'i32') }}};
      detached = {{{ makeGetValue('attr', 12/*_a_detach*/, 'i32') }}} != 0/*PTHREAD_CREATE_JOINABLE*/;
      var inheritSched = {{{ makeGetValue('attr', 16/*_a_sched*/, 'i32') }}} == 0/*PTHREAD_INHERIT_SCHED*/;
      if (inheritSched) {
        var prevSchedPolicy = {{{ makeGetValue('attr', 20/*_a_policy*/, 'i32') }}};
        var prevSchedPrio = {{{ makeGetValue('attr', 24/*_a_prio*/, 'i32') }}};
        // If we are inheriting the scheduling properties from the parent thread, we need to identify the parent thread properly - this function call may
        // be getting proxied, in which case _pthread_self() will point to the thread performing the proxying, not the thread that initiated the call.
        var parentThreadPtr = PThread.currentProxiedOperationCallerThread ? PThread.currentProxiedOperationCallerThread : _pthread_self();
        _pthread_getschedparam(parentThreadPtr, attr + 20, attr + 24);
        schedPolicy = {{{ makeGetValue('attr', 20/*_a_policy*/, 'i32') }}};
        schedPrio = {{{ makeGetValue('attr', 24/*_a_prio*/, 'i32') }}};
        {{{ makeSetValue('attr', 20/*_a_policy*/, 'prevSchedPolicy', 'i32') }}};
        {{{ makeSetValue('attr', 24/*_a_prio*/, 'prevSchedPrio', 'i32') }}};
      } else {
        schedPolicy = {{{ makeGetValue('attr', 20/*_a_policy*/, 'i32') }}};
        schedPrio = {{{ makeGetValue('attr', 24/*_a_prio*/, 'i32') }}};
      }
    } else {
      // According to http://man7.org/linux/man-pages/man3/pthread_create.3.html, default stack size if not specified is 2 MB, so follow that convention.
      stackSize = {{{ DEFAULT_PTHREAD_STACK_SIZE }}};
    }
    var allocatedOwnStack = stackBase == 0; // If allocatedOwnStack == true, then the pthread impl maintains the stack allocation.
    if (allocatedOwnStack) {
      stackBase = _malloc(stackSize); // Allocate a stack if the user doesn't want to place the stack in a custom memory area.
    } else {
      // Musl stores the stack base address assuming stack grows downwards, so adjust it to Emscripten convention that the
      // stack grows upwards instead.
      stackBase -= stackSize;
      assert(stackBase > 0);
    }

    // Allocate thread block (pthread_t structure).
    var threadInfoStruct = _malloc({{{ C_STRUCTS.pthread.__size__ }}});
    for (var i = 0; i < {{{ C_STRUCTS.pthread.__size__ }}} >> 2; ++i) HEAPU32[(threadInfoStruct>>2) + i] = 0; // zero-initialize thread structure.
    {{{ makeSetValue('pthread_ptr', 0, 'threadInfoStruct', 'i32') }}};

    // The pthread struct has a field that points to itself - this is used as a magic ID to detect whether the pthread_t
    // structure is 'alive'.
    {{{ makeSetValue('threadInfoStruct', C_STRUCTS.pthread.self, 'threadInfoStruct', 'i32') }}};

    // pthread struct robust_list head should point to itself.
    var headPtr = threadInfoStruct + {{{ C_STRUCTS.pthread.robust_list }}};
    {{{ makeSetValue('headPtr', 0, 'headPtr', 'i32') }}};

#if OFFSCREENCANVAS_SUPPORT
    // Register for each of the transferred canvases that the new thread now owns the OffscreenCanvas.
    for (var i in offscreenCanvases) {
      {{{ makeSetValue('offscreenCanvases[i].canvasSharedPtr', 8, 'threadInfoStruct', 'i32') }}}; // pthread ptr to the thread that owns this canvas.
    }
#endif

    var threadParams = {
      stackBase: stackBase,
      stackSize: stackSize,
      allocatedOwnStack: allocatedOwnStack,
      schedPolicy: schedPolicy,
      schedPrio: schedPrio,
      detached: detached,
      startRoutine: start_routine,
      pthread_ptr: threadInfoStruct,
      parent_pthread_ptr: _pthread_self(),
      arg: arg,
#if OFFSCREENCANVAS_SUPPORT
      moduleCanvasId: moduleCanvasId,
      offscreenCanvases: offscreenCanvases,
#endif
      transferList: transferList
    };

    if (ENVIRONMENT_IS_PTHREAD) {
      // The prepopulated pool of web workers that can host pthreads is stored in the main JS thread. Therefore if a
      // pthread is attempting to spawn a new thread, the thread creation must be deferred to the main JS thread.
      threadParams.cmd = 'spawnThread';
      postMessage(threadParams, transferList);
    } else {
      // We are the main thread, so we have the pthread warmup pool in this thread and can fire off JS thread creation
      // directly ourselves.
      __spawn_thread(threadParams);
    }

    return 0;
  },

  // TODO HACK! Remove this function, it is a JS side copy of the function pthread_testcancel() in library_pthread.c.
  // Just call pthread_testcancel() everywhere.
  _pthread_testcancel_js: function() {
    if (!ENVIRONMENT_IS_PTHREAD) return;
    if (!threadInfoStruct) return;
    var cancelDisabled = Atomics.load(HEAPU32, (threadInfoStruct + {{{ C_STRUCTS.pthread.canceldisable }}} ) >> 2);
    if (cancelDisabled) return;
    var canceled = Atomics.load(HEAPU32, (threadInfoStruct + {{{ C_STRUCTS.pthread.threadStatus }}} ) >> 2);
    if (canceled == 2) throw 'Canceled!';
  },

  pthread_join__deps: ['_cleanup_thread', '_pthread_testcancel_js', 'emscripten_main_thread_process_queued_calls'],
  pthread_join: function(thread, status) {
    if (!thread) {
      err('pthread_join attempted on a null thread pointer!');
      return ERRNO_CODES.ESRCH;
    }
    if (ENVIRONMENT_IS_PTHREAD && selfThreadId == thread) {
      err('PThread ' + thread + ' is attempting to join to itself!');
      return ERRNO_CODES.EDEADLK;
    }
    else if (!ENVIRONMENT_IS_PTHREAD && PThread.mainThreadBlock == thread) {
      err('Main thread ' + thread + ' is attempting to join to itself!');
      return ERRNO_CODES.EDEADLK;
    }
    var self = {{{ makeGetValue('thread', C_STRUCTS.pthread.self, 'i32') }}};
    if (self != thread) {
      err('pthread_join attempted on thread ' + thread + ', which does not point to a valid thread, or does not exist anymore!');
      return ERRNO_CODES.ESRCH;
    }

    var detached = Atomics.load(HEAPU32, (thread + {{{ C_STRUCTS.pthread.detached }}} ) >> 2);
    if (detached) {
      err('Attempted to join thread ' + thread + ', which was already detached!');
      return ERRNO_CODES.EINVAL; // The thread is already detached, can no longer join it!
    }
    for (;;) {
      var threadStatus = Atomics.load(HEAPU32, (thread + {{{ C_STRUCTS.pthread.threadStatus }}} ) >> 2);
      if (threadStatus == 1) { // Exited?
        var threadExitCode = Atomics.load(HEAPU32, (thread + {{{ C_STRUCTS.pthread.threadExitCode }}} ) >> 2);
        if (status) {{{ makeSetValue('status', 0, 'threadExitCode', 'i32') }}};
        Atomics.store(HEAPU32, (thread + {{{ C_STRUCTS.pthread.detached }}} ) >> 2, 1); // Mark the thread as detached.

        if (!ENVIRONMENT_IS_PTHREAD) __cleanup_thread(thread);
        else postMessage({ cmd: 'cleanupThread', thread: thread});
        return 0;
      }
      // TODO HACK! Replace the _js variant with just _pthread_testcancel:
      //_pthread_testcancel();
      __pthread_testcancel_js();
      // In main runtime thread (the thread that initialized the Emscripten C runtime and launched main()), assist pthreads in performing operations
      // that they need to access the Emscripten main runtime for.
      if (!ENVIRONMENT_IS_PTHREAD) _emscripten_main_thread_process_queued_calls();
      _emscripten_futex_wait(thread + {{{ C_STRUCTS.pthread.threadStatus }}}, threadStatus, ENVIRONMENT_IS_PTHREAD ? 100 : 1);
    }
  },

  pthread_kill__deps: ['_kill_thread'],
  pthread_kill: function(thread, signal) {
    if (signal < 0 || signal >= 65/*_NSIG*/) return ERRNO_CODES.EINVAL;
    if (thread == PThread.MAIN_THREAD_ID) {
      if (signal == 0) return 0; // signal == 0 is a no-op.
      err('Main thread (id=' + thread + ') cannot be killed with pthread_kill!');
      return ERRNO_CODES.ESRCH;
    }
    if (!thread) {
      err('pthread_kill attempted on a null thread pointer!');
      return ERRNO_CODES.ESRCH;
    }
    var self = {{{ makeGetValue('thread', C_STRUCTS.pthread.self, 'i32') }}};
    if (self != thread) {
      err('pthread_kill attempted on thread ' + thread + ', which does not point to a valid thread, or does not exist anymore!');
      return ERRNO_CODES.ESRCH;
    }
    if (signal != 0) {
      if (!ENVIRONMENT_IS_PTHREAD) __kill_thread(thread);
      else postMessage({ cmd: 'killThread', thread: thread});
    }
    return 0;
  },

  pthread_cancel__deps: ['_cancel_thread'],
  pthread_cancel: function(thread) {
    if (thread == PThread.MAIN_THREAD_ID) {
      err('Main thread (id=' + thread + ') cannot be canceled!');
      return ERRNO_CODES.ESRCH;
    }
    if (!thread) {
      err('pthread_cancel attempted on a null thread pointer!');
      return ERRNO_CODES.ESRCH;
    }
    var self = {{{ makeGetValue('thread', C_STRUCTS.pthread.self, 'i32') }}};
    if (self != thread) {
      err('pthread_cancel attempted on thread ' + thread + ', which does not point to a valid thread, or does not exist anymore!');
      return ERRNO_CODES.ESRCH;
    }
    Atomics.compareExchange(HEAPU32, (thread + {{{ C_STRUCTS.pthread.threadStatus }}} ) >> 2, 0, 2); // Signal the thread that it needs to cancel itself.
    if (!ENVIRONMENT_IS_PTHREAD) __cancel_thread(thread);
    else postMessage({ cmd: 'cancelThread', thread: thread});
    return 0;
  },

  pthread_detach: function(thread) {
    if (!thread) {
      err('pthread_detach attempted on a null thread pointer!');
      return ERRNO_CODES.ESRCH;
    }
    var self = {{{ makeGetValue('thread', C_STRUCTS.pthread.self, 'i32') }}};
    if (self != thread) {
      err('pthread_detach attempted on thread ' + thread + ', which does not point to a valid thread, or does not exist anymore!');
      return ERRNO_CODES.ESRCH;
    }
    var threadStatus = Atomics.load(HEAPU32, (thread + {{{ C_STRUCTS.pthread.threadStatus }}} ) >> 2);
    // Follow musl convention: detached:0 means not detached, 1 means the thread was created as detached, and 2 means that the thread was detached via pthread_detach.
    var wasDetached = Atomics.compareExchange(HEAPU32, (thread + {{{ C_STRUCTS.pthread.detached }}} ) >> 2, 0, 2);

    return wasDetached ? ERRNO_CODES.EINVAL : 0;
  },

  pthread_exit__deps: ['exit'],
  pthread_exit: function(status) {
    if (!ENVIRONMENT_IS_PTHREAD) _exit(status);
    else PThread.threadExit(status);
#if WASM_BACKEND
    // pthread_exit is marked noReturn, so we must not return from it.
    throw 'pthread_exit';
#endif
  },

  _pthread_ptr: 0,
  _pthread_is_main_runtime_thread: 0,
  _pthread_is_main_browser_thread: 0,

  _register_pthread_ptr__deps: ['_pthread_ptr', '_pthread_is_main_runtime_thread', '_pthread_is_main_browser_thread'],
  _register_pthread_ptr__asm: true,
  _register_pthread_ptr__sig: 'viii',
  _register_pthread_ptr: function(pthreadPtr, isMainBrowserThread, isMainRuntimeThread) {
    pthreadPtr = pthreadPtr|0;
    isMainBrowserThread = isMainBrowserThread|0;
    isMainRuntimeThread = isMainRuntimeThread|0;
    __pthread_ptr = pthreadPtr;
    __pthread_is_main_browser_thread = isMainBrowserThread;
    __pthread_is_main_runtime_thread = isMainRuntimeThread;
  },

  // Public pthread_self() function which returns a unique ID for the thread.
  pthread_self__deps: ['_pthread_ptr'],
  pthread_self__asm: true,
  pthread_self__sig: 'i',
  pthread_self: function() {
    return __pthread_ptr|0;
  },

  emscripten_is_main_runtime_thread__asm: true,
  emscripten_is_main_runtime_thread__sig: 'i',
  emscripten_is_main_runtime_thread__deps: ['_pthread_is_main_runtime_thread'],
  emscripten_is_main_runtime_thread: function() {
    return __pthread_is_main_runtime_thread|0; // Semantically the same as testing "!ENVIRONMENT_IS_PTHREAD" outside the asm.js scope
  },

  emscripten_is_main_browser_thread__asm: true,
  emscripten_is_main_browser_thread__sig: 'i',
  emscripten_is_main_browser_thread__deps: ['_pthread_is_main_browser_thread'],
  emscripten_is_main_browser_thread: function() {
    return __pthread_is_main_browser_thread|0; // Semantically the same as testing "!ENVIRONMENT_IS_WORKER" outside the asm.js scope
  },

  pthread_getschedparam: function(thread, policy, schedparam) {
    if (!policy && !schedparam) return ERRNO_CODES.EINVAL;

    if (!thread) {
      err('pthread_getschedparam called with a null thread pointer!');
      return ERRNO_CODES.ESRCH;
    }
    var self = {{{ makeGetValue('thread', C_STRUCTS.pthread.self, 'i32') }}};
    if (self != thread) {
      err('pthread_getschedparam attempted on thread ' + thread + ', which does not point to a valid thread, or does not exist anymore!');
      return ERRNO_CODES.ESRCH;
    }

    var schedPolicy = Atomics.load(HEAPU32, (thread + {{{ C_STRUCTS.pthread.attr }}} + 20 ) >> 2);
    var schedPrio = Atomics.load(HEAPU32, (thread + {{{ C_STRUCTS.pthread.attr }}} + 24 ) >> 2);

    if (policy) {{{ makeSetValue('policy', 0, 'schedPolicy', 'i32') }}};
    if (schedparam) {{{ makeSetValue('schedparam', 0, 'schedPrio', 'i32') }}};
    return 0;
  },

  pthread_setschedparam: function(thread, policy, schedparam) {
    if (!thread) {
      err('pthread_setschedparam called with a null thread pointer!');
      return ERRNO_CODES.ESRCH;
    }
    var self = {{{ makeGetValue('thread', C_STRUCTS.pthread.self, 'i32') }}};
    if (self != thread) {
      err('pthread_setschedparam attempted on thread ' + thread + ', which does not point to a valid thread, or does not exist anymore!');
      return ERRNO_CODES.ESRCH;
    }

    if (!schedparam) return ERRNO_CODES.EINVAL;

    var newSchedPrio = {{{ makeGetValue('schedparam', 0, 'i32') }}};
    if (newSchedPrio < 0) return ERRNO_CODES.EINVAL;
    if (policy == 1/*SCHED_FIFO*/ || policy == 2/*SCHED_RR*/) {
      if (newSchedPrio > 99) return ERRNO_CODES.EINVAL;
    } else {
      if (newSchedPrio > 1) return ERRNO_CODES.EINVAL;
    }

    Atomics.store(HEAPU32, (thread + {{{ C_STRUCTS.pthread.attr }}} + 20) >> 2, policy);
    Atomics.store(HEAPU32, (thread + {{{ C_STRUCTS.pthread.attr }}} + 24) >> 2, newSchedPrio);
    return 0;
  },

  // Marked as obsolescent in pthreads specification: http://pubs.opengroup.org/onlinepubs/9699919799/functions/pthread_getconcurrency.html
  pthread_getconcurrency: function() {
    return 0;
  },

  // Marked as obsolescent in pthreads specification.
  pthread_setconcurrency: function(new_level) {
    // no-op
    return 0;
  },

  pthread_mutexattr_getprioceiling: function(attr, prioceiling) {
    // Not supported either in Emscripten or musl, return a faked value.
    if (prioceiling) {{{ makeSetValue('prioceiling', 0, 99, 'i32') }}};
    return 0;
  },

  pthread_mutexattr_setprioceiling: function(attr, prioceiling) {
    // Not supported either in Emscripten or musl, return an error.
    return ERRNO_CODES.EPERM;
  },

  pthread_getcpuclockid: function(thread, clock_id) {
    return ERRNO_CODES.ENOENT; // pthread API recommends returning this error when "Per-thread CPU time clocks are not supported by the system."
  },

  pthread_setschedprio: function(thread, prio) {
    if (!thread) {
      err('pthread_setschedprio called with a null thread pointer!');
      return ERRNO_CODES.ESRCH;
    }
    var self = {{{ makeGetValue('thread', C_STRUCTS.pthread.self, 'i32') }}};
    if (self != thread) {
      err('pthread_setschedprio attempted on thread ' + thread + ', which does not point to a valid thread, or does not exist anymore!');
      return ERRNO_CODES.ESRCH;
    }
    if (prio < 0) return ERRNO_CODES.EINVAL;

    var schedPolicy = Atomics.load(HEAPU32, (thread + {{{ C_STRUCTS.pthread.attr }}} + 20 ) >> 2);
    if (schedPolicy == 1/*SCHED_FIFO*/ || schedPolicy == 2/*SCHED_RR*/) {
      if (prio > 99) return ERRNO_CODES.EINVAL;
    } else {
      if (prio > 1) return ERRNO_CODES.EINVAL;
    }

    Atomics.store(HEAPU32, (thread + {{{ C_STRUCTS.pthread.attr }}} + 24) >> 2, prio);
    return 0;
  },

  pthread_cleanup_push: function(routine, arg) {
    if (PThread.exitHandlers === null) {
      PThread.exitHandlers = [];
      if (!ENVIRONMENT_IS_PTHREAD) {
        __ATEXIT__.push(function() { PThread.runExitHandlers(); });
      }
    }
    PThread.exitHandlers.push(function() { {{{ makeDynCall('vi') }}}(routine, arg) });
  },

  pthread_cleanup_pop: function(execute) {
    var routine = PThread.exitHandlers.pop();
    if (execute) routine();
  },

  // pthread_sigmask - examine and change mask of blocked signals
  pthread_sigmask: function(how, set, oldset) {
    err('pthread_sigmask() is not supported: this is a no-op.');
    return 0;
  },

  pthread_atfork: function(prepare, parent, child) {
    err('fork() is not supported: pthread_atfork is a no-op.');
    return 0;
  },

  // Stores the memory address that the main thread is waiting on, if any.
  _main_thread_futex_wait_address: '; if (ENVIRONMENT_IS_PTHREAD) __main_thread_futex_wait_address = PthreadWorkerInit.__main_thread_futex_wait_address; else PthreadWorkerInit.__main_thread_futex_wait_address = __main_thread_futex_wait_address = {{{ makeStaticAlloc(4) }}}',

  // Returns 0 on success, or one of the values -ETIMEDOUT, -EWOULDBLOCK or -EINVAL on error.
  emscripten_futex_wait__deps: ['_main_thread_futex_wait_address', 'emscripten_main_thread_process_queued_calls'],
  emscripten_futex_wait: function(addr, val, timeout) {
    if (addr <= 0 || addr > HEAP8.length || addr&3 != 0) return -{{{ cDefine('EINVAL') }}};
//    dump('futex_wait addr:' + addr + ' by thread: ' + _pthread_self() + (ENVIRONMENT_IS_PTHREAD?'(pthread)':'') + '\n');
    if (ENVIRONMENT_IS_WORKER) {
#if PTHREADS_PROFILING
      PThread.setThreadStatusConditional(_pthread_self(), {{{ cDefine('EM_THREAD_STATUS_RUNNING') }}}, {{{ cDefine('EM_THREAD_STATUS_WAITFUTEX') }}});
#endif
      var ret = Atomics.wait(HEAP32, addr >> 2, val, timeout);
//    dump('futex_wait done by thread: ' + _pthread_self() + (ENVIRONMENT_IS_PTHREAD?'(pthread)':'') + '\n');
#if PTHREADS_PROFILING
      PThread.setThreadStatusConditional(_pthread_self(), {{{ cDefine('EM_THREAD_STATUS_WAITFUTEX') }}}, {{{ cDefine('EM_THREAD_STATUS_RUNNING') }}});
#endif
      if (ret === 'timed-out') return -{{{ cDefine('ETIMEDOUT') }}};
      if (ret === 'not-equal') return -{{{ cDefine('EWOULDBLOCK') }}};
      if (ret === 'ok') return 0;
      throw 'Atomics.wait returned an unexpected value ' + ret;
    } else {
      // Atomics.wait is not available in the main browser thread, so simulate it via busy spinning.
      var loadedVal = Atomics.load(HEAP32, addr >> 2);
      if (val != loadedVal) return -{{{ cDefine('EWOULDBLOCK') }}};

      var tNow = performance.now();
      var tEnd = tNow + timeout;

#if PTHREADS_PROFILING
      PThread.setThreadStatusConditional(_pthread_self(), {{{ cDefine('EM_THREAD_STATUS_RUNNING') }}}, {{{ cDefine('EM_THREAD_STATUS_WAITFUTEX') }}});
#endif

      // Register globally which address the main thread is simulating to be waiting on. When zero, main thread is not waiting on anything,
      // and on nonzero, the contents of address pointed by __main_thread_futex_wait_address tell which address the main thread is simulating its wait on.
      Atomics.store(HEAP32, __main_thread_futex_wait_address >> 2, addr);
      var ourWaitAddress = addr; // We may recursively re-enter this function while processing queued calls, in which case we'll do a spurious wakeup of the older wait operation.
      while (addr == ourWaitAddress) {
        tNow = performance.now();
        if (tNow > tEnd) {
#if PTHREADS_PROFILING
          PThread.setThreadStatusConditional(_pthread_self(), {{{ cDefine('EM_THREAD_STATUS_RUNNING') }}}, {{{ cDefine('EM_THREAD_STATUS_WAITFUTEX') }}});
#endif
          return -{{{ cDefine('ETIMEDOUT') }}};
        }
        _emscripten_main_thread_process_queued_calls(); // We are performing a blocking loop here, so must pump any pthreads if they want to perform operations that are proxied.
        addr = Atomics.load(HEAP32, __main_thread_futex_wait_address >> 2); // Look for a worker thread waking us up.
      }
#if PTHREADS_PROFILING
      PThread.setThreadStatusConditional(_pthread_self(), {{{ cDefine('EM_THREAD_STATUS_RUNNING') }}}, {{{ cDefine('EM_THREAD_STATUS_WAITFUTEX') }}});
#endif
      return 0;
    }
  },

  // Returns the number of threads (>= 0) woken up, or the value -EINVAL on error.
  // Pass count == INT_MAX to wake up all threads.
  emscripten_futex_wake__deps: ['_main_thread_futex_wait_address'],
  emscripten_futex_wake: function(addr, count) {
    if (addr <= 0 || addr > HEAP8.length || addr&3 != 0 || count < 0) return -{{{ cDefine('EINVAL') }}};
    if (count == 0) return 0;
//    dump('futex_wake addr:' + addr + ' by thread: ' + _pthread_self() + (ENVIRONMENT_IS_PTHREAD?'(pthread)':'') + '\n');

    // See if main thread is waiting on this address? If so, wake it up by resetting its wake location to zero.
    // Note that this is not a fair procedure, since we always wake main thread first before any workers, so
    // this scheme does not adhere to real queue-based waiting.
    var mainThreadWaitAddress = Atomics.load(HEAP32, __main_thread_futex_wait_address >> 2);
    var mainThreadWoken = 0;
    if (mainThreadWaitAddress == addr) {
      var loadedAddr = Atomics.compareExchange(HEAP32, __main_thread_futex_wait_address >> 2, mainThreadWaitAddress, 0);
      if (loadedAddr == mainThreadWaitAddress) {
        --count;
        mainThreadWoken = 1;
        if (count <= 0) return 1;
      }
    }

    // Wake any workers waiting on this address.
    var ret = Atomics.notify(HEAP32, addr >> 2, count);
    if (ret >= 0) return ret + mainThreadWoken;
    throw 'Atomics.notify returned an unexpected value ' + ret;
  },

  __atomic_is_lock_free: function(size, ptr) {
    return size <= 4 && (size & (size-1)) == 0 && (ptr&(size-1)) == 0;
  },

  __call_main: function(argc, argv) {
    var returnCode = _main(argc, argv);
    if (!Module['noExitRuntime']) postMessage({ cmd: 'exitProcess', returnCode: returnCode });
    return returnCode;
  },

  emscripten_conditional_set_current_thread_status_js: function(expectedStatus, newStatus) {
#if PTHREADS_PROFILING
    PThread.setThreadStatusConditional(_pthread_self(), expectedStatus, newStatus);
#endif
  },

  emscripten_set_current_thread_status_js: function(newStatus) {
#if PTHREADS_PROFILING
    PThread.setThreadStatus(_pthread_self(), newStatus);
#endif
  },

  emscripten_set_thread_name_js: function(threadId, name) {
#if PTHREADS_PROFILING
    PThread.setThreadName(threadId, UTF8ToString(name));
#endif
  },

  // The profiler setters are defined twice, here in asm.js so that they can be #ifdeffed out
  // without having to pay the impact of a FFI transition for a no-op in non-profiling builds.
  emscripten_conditional_set_current_thread_status__asm: true,
  emscripten_conditional_set_current_thread_status__sig: 'vii',
  emscripten_conditional_set_current_thread_status__deps: ['emscripten_conditional_set_current_thread_status_js'],
  emscripten_conditional_set_current_thread_status: function(expectedStatus, newStatus) {
    expectedStatus = expectedStatus|0;
    newStatus = newStatus|0;
#if PTHREADS_PROFILING
    _emscripten_conditional_set_current_thread_status_js(expectedStatus|0, newStatus|0);
#endif
  },

  emscripten_set_current_thread_status__asm: true,
  emscripten_set_current_thread_status__sig: 'vi',
  emscripten_set_current_thread_status__deps: ['emscripten_set_current_thread_status_js'],
  emscripten_set_current_thread_status: function(newStatus) {
    newStatus = newStatus|0;
#if PTHREADS_PROFILING
    _emscripten_set_current_thread_status_js(newStatus|0);
#endif
  },

  emscripten_set_thread_name__asm: true,
  emscripten_set_thread_name__sig: 'vii',
  emscripten_set_thread_name__deps: ['emscripten_set_thread_name_js'],
  emscripten_set_thread_name: function(threadId, name) {
    threadId = threadId|0;
    name = name|0;
#if PTHREADS_PROFILING
    _emscripten_set_thread_name_js(threadId|0, name|0);
#endif
  },

  emscripten_proxy_to_main_thread_js: function(index, sync) {
    // Additional arguments are passed after those two, which are the actual
    // function arguments.
    // The serialization buffer contains the number of call params, and then
    // all the args here.
    // We also pass 'sync' to C separately, since C needs to look at it.
    var numCallArgs = arguments.length - 2;
    // Allocate a buffer, which will be copied by the C code.
    var stack = stackSave();
    var buffer = stackAlloc(numCallArgs * 8);
    for (var i = 0; i < numCallArgs; i++) {
      HEAPF64[(buffer >> 3) + i] = arguments[2 + i];
    }
    var ret = _emscripten_run_in_main_runtime_thread_js(index, numCallArgs, buffer, sync);
    stackRestore(stack);
    return ret;
  },

  emscripten_receive_on_main_thread_js__deps: ['emscripten_proxy_to_main_thread_js'],
  emscripten_receive_on_main_thread_js: function(index, numCallArgs, buffer) {
    // Avoid garbage by reusing a single JS array for call arguments.
    if (!_emscripten_receive_on_main_thread_js.callArgs) {
      _emscripten_receive_on_main_thread_js.callArgs = [];
    }
    var callArgs = _emscripten_receive_on_main_thread_js.callArgs;
    callArgs.length = numCallArgs;
    for (var i = 0; i < numCallArgs; i++) {
      callArgs[i] = HEAPF64[(buffer >> 3) + i];
    }
    // Proxied JS library funcs are encoded as positive values, and
    // EM_ASMs as negative values (see include_asm_consts)
    var func;
    if (index > 0) {
      func = proxiedFunctionTable[index];
    } else {
      func = ASM_CONSTS[-index - 1];
    }
#if ASSERTIONS
    assert(func.length == numCallArgs);
#endif
    return func.apply(null, callArgs);
  },

#if MODULARIZE
  $establishStackSpaceInJsModule: function(stackBase, stackMax) {
    STACK_BASE = STACKTOP = stackBase;
    STACK_MAX = stackMax;
  },
#endif
};

autoAddDeps(LibraryPThread, '$PThread');
mergeInto(LibraryManager.library, LibraryPThread);
