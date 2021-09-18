//
// Meta-data tree structure.
//
// This data model is updated to match the file system - and the data
// model is then synchronised to the server.
//

#ifndef DEMOCLI_METATREE_HH
#define DEMOCLI_METATREE_HH

#include <vector>
#include <list>
#include <string>
#include <stdint.h>
#include "sqlite/sqlite3.h"

#if defined(__unix__) || defined(__APPLE__)
# include <sys/types.h>
# include <sys/stat.h>
# include <unistd.h>
#endif

#include "common/hash.hh"
#include "common/mutex.hh"
#include "common/time.hh"
#include "client/serverconnection.hh"

//
// The MetaNode structure will reflect the local file-system. For
// example:
//
//  /u/joe/foo.vob
//
// Is represented as:
//
// [--------hash0------------]
// [Type: DirectoryStart     ]
// [LoR: [hash1]             ]
// [treeSize: ... bytes      ]
// [Extender: Ø              ]
// [-------------------------]
// [meta0: "u" 0770 root:sys ]
// [       ref=0             ]
// [-------------------------]
//
// [--------hash1------------]
// [Type: DirectoryStart     ]
// [LoR: [hash2]             ]
// [treeSize: ... bytes      ]
// [Extender: Ø              ]
// [-------------------------]
// [meta0: "joe" 0770 joe:joe]
// [       ref=0             ]
// [-------------------------]
//
// [--------hash2------------]
// [Type: DirectoryStart     ]
// [LoR: [hash3]             ]
// [treeSize: ... bytes      ]
// [Extender: Ø              ]
// [-------------------------]
// [meta0: "foo.vob" 0600    ]
// [       joe:joe ref=0     ]
// [-------------------------]
//
// [--------hash3------------]
// [Type: Data               ]
// [treeSize: ... bytes      ]
// [-------------------------]
// [ ... file data here ...  ]
// [-------------------------]
//
//
// Whenever an object goes above our KEEPIT_MAX_OBJECT_SIZE, it is
// split.
//
// The server needs to be able to read the LoR from any
// object. Therefore, when we split objects, we split at a high level
// - for example, a Directory object is split into two proper
// Directory objects which each have a LoR and a size.
//


/// When we upload objects, we may need to split them due to size
/// restrictions. Therefore, when we refer to "an object" it may
/// actually be a sequence of objects on the back end.
typedef std::vector<sha256> objseq_t;

// The volume serial number, file index and creation time uniquely
// identifies a file on the system on Win32, whereas we use a device,
// inode and ctime on POSIX.
//
// The reason for using ctime on POSIX is that ctime is outside of
// user control. The mtime can be set by the user, whereas the ctime
// serves as an ever-increasing sequence number which will prevent
// inode re-use from causing file identification collisions.
struct fsobjid_t {
#if defined(__unix__) || defined(__APPLE__)
  fsobjid_t()
    : device(-1)
    , fileid(-1)
    , ctime_s(0)
    , ctime_ns(0)
    , mtime_s(0)
    , mtime_ns(0)
  { }
#if defined(__APPLE__)
  fsobjid_t(const struct stat &st)
    : device(st.st_dev)
    , fileid(st.st_ino)
    , ctime_s(st.st_ctimespec.tv_sec)
    , ctime_ns(st.st_ctimespec.tv_nsec)
    , mtime_s(st.st_mtimespec.tv_sec)
    , mtime_ns(st.st_mtimespec.tv_nsec)
  { }
#else
  fsobjid_t(const struct stat &st)
  : device(st.st_dev)
  , fileid(st.st_ino)
  , ctime_s(st.st_ctim.tv_sec)
  , ctime_ns(st.st_ctim.tv_nsec)
  , mtime_s(st.st_mtim.tv_sec)
  , mtime_ns(st.st_mtim.tv_nsec)
  { }
#endif
  dev_t device;
  ino_t fileid;
  uint64_t ctime_s;
  uint64_t ctime_ns;
  uint64_t mtime_s;
  uint64_t mtime_ns;
#endif
#if defined(_WIN32)
  fsobjid_t()
    : device(-1)
    , fileid(-1)
  {
    memset(&creation_time, 0, sizeof creation_time);
    memset(&write_time, 0, sizeof write_time);
  }
  fsobjid_t(const BY_HANDLE_FILE_INFORMATION &fi)
    : device(fi.dwVolumeSerialNumber)
    , fileid(uint64_t(fi.nFileIndexHigh) << 32 | fi.nFileIndexLow)
    , creation_time(fi.ftCreationTime)
    , write_time(fi.ftLastWriteTime)
  { }
  uint32_t device;
  uint64_t fileid;
  FILETIME creation_time; // stored in ctime_s and ctime_ns in db
  FILETIME write_time;    // stored in mtime_s and mtime_ns in db
#endif
};


///
/// File system object cache entry
//
///
/// Regular File:
///  The m_id refers to the regular file inode.
///  The m_hash is the file content chunk object hashes
///  There are no children.
///
/// Regular Directory:
///  The m_id refers to the directory inode.
///  The m_hash is the directory object hash(es) (object is split if
///  it grows too big, just like file content dat)
///  The children are the directory children; other directories,
///  files, etc.
///
class CObject {
public:
  CObject();
  /// Internal cache db id of this object
  int64_t m_dbid;
  /// Unique ID of file system object
  fsobjid_t m_id;
  /// After successful upload we set our hash-list.
  objseq_t m_hash;
  /// This is the sum of our size and the size of our children
  uint64_t m_treesize;
};




///
/// Our cache
///
class FSCache {
public:
  /// Initialise
  FSCache(const std::string &fname);
  /// Shutdown
  ~FSCache();

  /// Locate object in cache. If found, fill in argument object and
  /// return true. Otherwise return false. On false return, obj will
  /// be default-initialised and the m_id field of the obj will be set
  //
  /// When we search the database, we search for a match on fsid,
  /// inode, ctime and mtime.
  //
  /// We may have the m_dbid set even when readObj returns false - if
  /// the database does contain a record for the fsid/ino but it is
  /// outdated. So, the decision to use insert or update should be
  /// made based on whether m_dbid is -1, not whether readObj returns
  /// true or false.
  bool readObj(const fsobjid_t &id, CObject &obj);

  /// Insert object in cache - does not use m_dbid
  void insert(const CObject &obj);

  /// Update object in cache - requires m_dbid, assumes m_id is
  /// constant and updates mtime/ctime/hash
  void update(const CObject &obj);

  /// When we complete a sequence of operations on the database (for
  /// example, we complete a backup), then we can issue this call
  /// which will commit the currently open transaction and close the
  /// database. The database will then automatically be re-opened as
  /// soon as it is needed by any of the read/insert/update methods.
  void quiesce();
    
    void changeCache();
    void clearCache();

private:
  /// Protect against copying
  FSCache(const FSCache &);
  FSCache &operator=(const FSCache &);

  /// Our database file name
  const std::string m_fname;

  /// Our db handle
  sqlite3 *m_db;

  /// We group transactions to not commit on every insert. After all,
  /// if we lose some changes to our cache due to
  /// crash/power-loss/whatever, it will only mean that we inspect a
  /// couple of extra files on our next run. No data is lost and no
  /// harm done.
  //
  /// So, this time stamp holds the current transaction start time
  Time m_txn_start;

  /// Our mutex to protect access to the underlying db.
  Mutex m_db_lock;

  /// Open database if not open already
  void opendb();

  /// Execute a statement
  void execute(const std::string &stmt);

  /// Call this method to optionally (if the txn time has been
  /// reached) commit the current transaction and start a new
  void didUpdate();
};



#endif

