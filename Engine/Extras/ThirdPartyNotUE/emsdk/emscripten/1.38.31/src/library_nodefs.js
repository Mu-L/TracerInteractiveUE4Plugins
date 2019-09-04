// Copyright 2013 The Emscripten Authors.  All rights reserved.
// Emscripten is available under two separate licenses, the MIT license and the
// University of Illinois/NCSA Open Source License.  Both these licenses can be
// found in the LICENSE file.

mergeInto(LibraryManager.library, {
  $NODEFS__deps: ['$FS', '$PATH'],
  $NODEFS__postset: 'if (ENVIRONMENT_IS_NODE) { var fs = require("fs"); var NODEJS_PATH = require("path"); NODEFS.staticInit(); }',
  $NODEFS: {
    isWindows: false,
    staticInit: function() {
      NODEFS.isWindows = !!process.platform.match(/^win/);
      var flags = process["binding"]("constants");
      // Node.js 4 compatibility: it has no namespaces for constants
      if (flags["fs"]) {
        flags = flags["fs"];
      }
      NODEFS.flagsForNodeMap = {
        "{{{ cDefine('O_APPEND') }}}": flags["O_APPEND"],
        "{{{ cDefine('O_CREAT') }}}": flags["O_CREAT"],
        "{{{ cDefine('O_EXCL') }}}": flags["O_EXCL"],
        "{{{ cDefine('O_RDONLY') }}}": flags["O_RDONLY"],
        "{{{ cDefine('O_RDWR') }}}": flags["O_RDWR"],
        "{{{ cDefine('O_DSYNC') }}}": flags["O_SYNC"],
        "{{{ cDefine('O_TRUNC') }}}": flags["O_TRUNC"],
        "{{{ cDefine('O_WRONLY') }}}": flags["O_WRONLY"]
      };
    },
    bufferFrom: function (arrayBuffer) {
      // Node.js < 4.5 compatibility: Buffer.from does not support ArrayBuffer
      // Buffer.from before 4.5 was just a method inherited from Uint8Array
      // Buffer.alloc has been added with Buffer.from together, so check it instead
      return Buffer.alloc ? Buffer.from(arrayBuffer) : new Buffer(arrayBuffer);
    },
    mount: function (mount) {
      assert(ENVIRONMENT_IS_NODE);
      return NODEFS.createNode(null, '/', NODEFS.getMode(mount.opts.root), 0);
    },
    createNode: function (parent, name, mode, dev) {
      if (!FS.isDir(mode) && !FS.isFile(mode) && !FS.isLink(mode)) {
        throw new FS.ErrnoError({{{ cDefine('EINVAL') }}});
      }
      var node = FS.createNode(parent, name, mode);
      node.node_ops = NODEFS.node_ops;
      node.stream_ops = NODEFS.stream_ops;
      return node;
    },
    getMode: function (path) {
      var stat;
      try {
        stat = fs.lstatSync(path);
        if (NODEFS.isWindows) {
          // Node.js on Windows never represents permission bit 'x', so
          // propagate read bits to execute bits
          stat.mode = stat.mode | ((stat.mode & 292) >> 2);
        }
      } catch (e) {
        if (!e.code) throw e;
        throw new FS.ErrnoError(-e.errno); // syscall errnos are negated, node's are not
      }
      return stat.mode;
    },
    realPath: function (node) {
      var parts = [];
      while (node.parent !== node) {
        parts.push(node.name);
        node = node.parent;
      }
      parts.push(node.mount.opts.root);
      parts.reverse();
      return PATH.join.apply(null, parts);
    },
    // This maps the integer permission modes from http://linux.die.net/man/3/open
    // to node.js-specific file open permission strings at http://nodejs.org/api/fs.html#fs_fs_open_path_flags_mode_callback
    flagsForNode: function(flags) {
      flags &= ~0x200000 /*O_PATH*/; // Ignore this flag from musl, otherwise node.js fails to open the file.
      flags &= ~0x800 /*O_NONBLOCK*/; // Ignore this flag from musl, otherwise node.js fails to open the file.
      flags &= ~0x8000 /*O_LARGEFILE*/; // Ignore this flag from musl, otherwise node.js fails to open the file.
      flags &= ~0x80000 /*O_CLOEXEC*/; // Some applications may pass it; it makes no sense for a single process.
      var newFlags = 0;
      for (var k in NODEFS.flagsForNodeMap) {
        if (flags & k) {
          newFlags |= NODEFS.flagsForNodeMap[k];
          flags ^= k;
        }
      }

      if (!flags) {
        return newFlags;
      } else {
        throw new FS.ErrnoError({{{ cDefine('EINVAL') }}});
      }
    },
    node_ops: {
      getattr: function(node) {
        var path = NODEFS.realPath(node);
        var stat;
        try {
          stat = fs.lstatSync(path);
        } catch (e) {
          if (!e.code) throw e;
          throw new FS.ErrnoError(-e.errno);
        }
        // node.js v0.10.20 doesn't report blksize and blocks on Windows. Fake them with default blksize of 4096.
        // See http://support.microsoft.com/kb/140365
        if (NODEFS.isWindows && !stat.blksize) {
          stat.blksize = 4096;
        }
        if (NODEFS.isWindows && !stat.blocks) {
          stat.blocks = (stat.size+stat.blksize-1)/stat.blksize|0;
        }
        return {
          dev: stat.dev,
          ino: stat.ino,
          mode: stat.mode,
          nlink: stat.nlink,
          uid: stat.uid,
          gid: stat.gid,
          rdev: stat.rdev,
          size: stat.size,
          atime: stat.atime,
          mtime: stat.mtime,
          ctime: stat.ctime,
          blksize: stat.blksize,
          blocks: stat.blocks
        };
      },
      setattr: function(node, attr) {
        var path = NODEFS.realPath(node);
        try {
          if (attr.mode !== undefined) {
            fs.chmodSync(path, attr.mode);
            // update the common node structure mode as well
            node.mode = attr.mode;
          }
          if (attr.timestamp !== undefined) {
            var date = new Date(attr.timestamp);
            fs.utimesSync(path, date, date);
          }
          if (attr.size !== undefined) {
            fs.truncateSync(path, attr.size);
          }
        } catch (e) {
          if (!e.code) throw e;
          throw new FS.ErrnoError(-e.errno);
        }
      },
      lookup: function (parent, name) {
        var path = PATH.join2(NODEFS.realPath(parent), name);
        var mode = NODEFS.getMode(path);
        return NODEFS.createNode(parent, name, mode);
      },
      mknod: function (parent, name, mode, dev) {
        var node = NODEFS.createNode(parent, name, mode, dev);
        // create the backing node for this in the fs root as well
        var path = NODEFS.realPath(node);
        try {
          if (FS.isDir(node.mode)) {
            fs.mkdirSync(path, node.mode);
          } else {
            fs.writeFileSync(path, '', { mode: node.mode });
          }
        } catch (e) {
          if (!e.code) throw e;
          throw new FS.ErrnoError(-e.errno);
        }
        return node;
      },
      rename: function (oldNode, newDir, newName) {
        var oldPath = NODEFS.realPath(oldNode);
        var newPath = PATH.join2(NODEFS.realPath(newDir), newName);
        try {
          fs.renameSync(oldPath, newPath);
        } catch (e) {
          if (!e.code) throw e;
          throw new FS.ErrnoError(-e.errno);
        }
      },
      unlink: function(parent, name) {
        var path = PATH.join2(NODEFS.realPath(parent), name);
        try {
          fs.unlinkSync(path);
        } catch (e) {
          if (!e.code) throw e;
          throw new FS.ErrnoError(-e.errno);
        }
      },
      rmdir: function(parent, name) {
        var path = PATH.join2(NODEFS.realPath(parent), name);
        try {
          fs.rmdirSync(path);
        } catch (e) {
          if (!e.code) throw e;
          throw new FS.ErrnoError(-e.errno);
        }
      },
      readdir: function(node) {
        var path = NODEFS.realPath(node);
        try {
          return fs.readdirSync(path);
        } catch (e) {
          if (!e.code) throw e;
          throw new FS.ErrnoError(-e.errno);
        }
      },
      symlink: function(parent, newName, oldPath) {
        var newPath = PATH.join2(NODEFS.realPath(parent), newName);
        try {
          fs.symlinkSync(oldPath, newPath);
        } catch (e) {
          if (!e.code) throw e;
          throw new FS.ErrnoError(-e.errno);
        }
      },
      readlink: function(node) {
        var path = NODEFS.realPath(node);
        try {
          path = fs.readlinkSync(path);
          path = NODEJS_PATH.relative(NODEJS_PATH.resolve(node.mount.opts.root), path);
          return path;
        } catch (e) {
          if (!e.code) throw e;
          throw new FS.ErrnoError(-e.errno);
        }
      },
    },
    stream_ops: {
      open: function (stream) {
        var path = NODEFS.realPath(stream.node);
        try {
          if (FS.isFile(stream.node.mode)) {
            stream.nfd = fs.openSync(path, NODEFS.flagsForNode(stream.flags));
          }
        } catch (e) {
          if (!e.code) throw e;
          throw new FS.ErrnoError(-e.errno);
        }
      },
      close: function (stream) {
        try {
          if (FS.isFile(stream.node.mode) && stream.nfd) {
            fs.closeSync(stream.nfd);
          }
        } catch (e) {
          if (!e.code) throw e;
          throw new FS.ErrnoError(-e.errno);
        }
      },
      read: function (stream, buffer, offset, length, position) {
        // Node.js < 6 compatibility: node errors on 0 length reads
        if (length === 0) return 0;
        try {
          return fs.readSync(stream.nfd, NODEFS.bufferFrom(buffer.buffer), offset, length, position);
        } catch (e) {
          throw new FS.ErrnoError(-e.errno);
        }
      },
      write: function (stream, buffer, offset, length, position) {
        try {
          return fs.writeSync(stream.nfd, NODEFS.bufferFrom(buffer.buffer), offset, length, position);
        } catch (e) {
          throw new FS.ErrnoError(-e.errno);
        }
      },
      llseek: function (stream, offset, whence) {
        var position = offset;
        if (whence === 1) {  // SEEK_CUR.
          position += stream.position;
        } else if (whence === 2) {  // SEEK_END.
          if (FS.isFile(stream.node.mode)) {
            try {
              var stat = fs.fstatSync(stream.nfd);
              position += stat.size;
            } catch (e) {
              throw new FS.ErrnoError(-e.errno);
            }
          }
        }

        if (position < 0) {
          throw new FS.ErrnoError({{{ cDefine('EINVAL') }}});
        }

        return position;
      }
    }
  }
});
