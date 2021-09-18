//
// Implementation of the meta-data tree routines
//

#include "common/trace.hh"
#include "common/scopeguard.hh"
#include "common/error.hh"
#include "common/ntohll.hh"
#include "metatree.hh"

#ifdef __unix__
# include <sys/types.h>
# include <sys/stat.h>
# include <fcntl.h>
# include <unistd.h>
#endif

namespace {
  //! Tracer for meta cache
  trace::Path t_cache("/cache");

  //! We commit a transaction every period
  const DiffTime g_txn_grp_period(DiffTime::iso("PT60S"));
}

namespace {

  /// We want to have an easy way for debug printing of an fsobjid_t
  std::ostream &operator<<(std::ostream &out, const fsobjid_t &id)
  {
    out << id.device << "," << id.fileid;
    return out;
  }

  objseq_t parseHashes(sqlite3_stmt *stmt, int col)
  {
    objseq_t ret;

    size_t blobsize = sqlite3_column_bytes(stmt, col);
    if (blobsize % 32)
      throw error("BLOB (hash) of non-256 bit multiple");
    ret.resize(blobsize / 32);
    const uint8_t *rawp
      = reinterpret_cast<const uint8_t*>(sqlite3_column_blob(stmt, col));
    for (size_t i = 0; i != blobsize / 32; ++i) {
      ret[i] = sha256::parse(std::vector<uint8_t>(rawp + i * 32,
                                                  rawp + (i+1) * 32));
    }
    return ret;
  }

}



FSCache::FSCache(const std::string &fname)
  : m_fname(fname)
  , m_db(0)
  , m_txn_start(Time::now())
{
  if (!sqlite3_threadsafe())
    throw error("SQLite library not thread safe");

  if (SQLITE_OK != sqlite3_config(SQLITE_CONFIG_SERIALIZED))
    throw error("Unable to set SERIALIZED mode on SQLite");

  opendb();

  try {
    // Clean up old schema
  //  execute("DROP INDEX IF EXISTS objs_di_ndx;");
  //  execute("DROP TABLE IF EXISTS objs;");
    // Create the cache table if it wasn't there already
    execute("CREATE TABLE IF NOT EXISTS objs2 "
            "( id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  dev INT8 NOT NULL,"
            "  ino INT8 NOT NULL,"
            "  ctime_s INT8 NOT NULL,"
            "  mtime_s INT8 NOT NULL,"
            "  ctime_ns INT4 NOT NULL,"
            "  mtime_ns INT4 NOT NULL,"
            "  hash  BLOB, " // length determines number of hashes in sequence
            "  treesize INT8 NOT NULL"
            ");");
    execute("CREATE UNIQUE INDEX IF NOT EXISTS objs2_di_ndx ON objs2 (dev,ino);");

  } catch (error &) {
    sqlite3_close(m_db);
    throw;
  }

  quiesce();

}

void FSCache::clearCache()
{
    MutexLock l(m_db_lock);
    if (!m_db)
        return;
    
        std::cout << "delete bd" << std::endl;
        char *zErr;
        int res = sqlite3_open(m_fname.c_str(), &m_db);
        res = sqlite3_exec(m_db, "delete from objs2", NULL, NULL, &zErr);
        if( res != SQLITE_OK )
        {
            std::cout << "delete bd all is OK" << std::endl;
            sqlite3_free( zErr );
        }
           
    m_db = 0;
}

void FSCache::changeCache()
{
    MutexLock l(m_db_lock);
    if (!m_db)
        return;
    
    execute("COMMIT TRANSACTION");
    if (SQLITE_OK != sqlite3_close(m_db))
        throw error("Unable to close database");
}

void FSCache::opendb()
{
  MutexLock l(m_db_lock);
  if (m_db)
    return;

  int res = sqlite3_open(m_fname.c_str(), &m_db);
  if (res != SQLITE_OK)
    throw error("Unable to open cache: " + m_fname);

  if (SQLITE_OK != sqlite3_extended_result_codes(m_db, 1))
    throw error("Unable to enable extended result codes in sqlite");

  // Start txn
  execute("BEGIN TRANSACTION");
}

void FSCache::quiesce()
{
  MutexLock l(m_db_lock);
  if (!m_db)
    return;

  execute("COMMIT TRANSACTION");
  if (SQLITE_OK != sqlite3_close(m_db))
    throw error("Unable to close database");
  m_db = 0;
}

FSCache::~FSCache()
{
  if (!m_db)
    return;
  // Commit whatever we have, ignore failure
  try { execute("COMMIT TRANSACTION"); }
  catch (...) { }

  // Close - if we fail, well, there isn't really any meaningful
  // recovery...
  sqlite3_close(m_db);
  sqlite3_shutdown();
}

bool FSCache::readObj(const fsobjid_t &id, CObject &obj)
{
  opendb();
  // If we find it or not, update the id
  obj.m_id = id;

  // Locate object
  { std::ostringstream prep;
    prep << "SELECT id, ctime_s, ctime_ns, mtime_s, mtime_ns, hash, treesize"
         << " FROM objs2"
         << " WHERE dev = " << id.device
         << " AND ino = " << id.fileid
         << ";";
    sqlite3_stmt *pstmt = 0;
    int pres = sqlite3_prepare_v2(m_db, prep.str().c_str(), -1, &pstmt, 0);
    if (pres != SQLITE_OK)
      throw error("PREP " + prep.str() + ": " + sqlite3_errmsg(m_db));
    ON_BLOCK_EXIT(sqlite3_finalize, pstmt);

    int res = sqlite3_step(pstmt);
    if (res == SQLITE_DONE) {
      MTrace(t_cache, trace::Debug, "Cache did not contain " << id);
      // Default initialise obj and set id
      obj = CObject();
      obj.m_id = id;
      return false;
    }
    if (res != SQLITE_ROW)
      throw error("STEP " + prep.str() + ": " + sqlite3_errmsg(m_db));

    MTrace(t_cache, trace::Debug, "Located cached object " << id);

    // Fine, fetch columns
    const int64_t db_id = obj.m_dbid = sqlite3_column_int64(pstmt, 0);
    obj.m_id = id;

    const uint64_t ctime_s = sqlite3_column_int64(pstmt, 1);
    const uint32_t ctime_ns = sqlite3_column_int(pstmt, 2);
    const uint64_t mtime_s = sqlite3_column_int64(pstmt, 3);
    const uint32_t mtime_ns = sqlite3_column_int(pstmt, 4);

    obj.m_hash = parseHashes(pstmt, 5);
    obj.m_treesize = sqlite3_column_int64(pstmt, 6);

    // Validate query termination
    int sr = sqlite3_step(pstmt);
    switch (sr) {
    case SQLITE_DONE: // ok done
      break;
    case SQLITE_ROW: // more rows ready... odd
      { std::ostringstream err;
        err << "Multiple objects of " << id;
        throw error(err.str());
      }
    default:
      { std::ostringstream err;
        err << "DB error " << sr << " after retrieving object from cache";
        throw error(err.str());
      }
    }

#if defined(__unix__) || defined(__APPLE__)
    // See if ctime matches. If it does not, report that we don't have the object
    if (ctime_s != id.ctime_s || ctime_ns != id.ctime_ns
        || mtime_s != id.mtime_s || mtime_ns != id.mtime_ns) {
      MTrace(t_cache, trace::Debug, "Located outdated object - reporting not found");
      obj = CObject();
      obj.m_dbid = db_id;
      obj.m_id = id;
      return false;
    }
#endif

#if defined(_WIN32)
    // See if creation time and write time matches. If it does not,
    // report that we don't have the object
    if (ctime_s != id.creation_time.dwHighDateTime || ctime_ns != id.creation_time.dwLowDateTime
	|| mtime_s != id.write_time.dwHighDateTime || mtime_ns != id.write_time.dwLowDateTime) {
      MTrace(t_cache, trace::Debug, "Located outdated object - reporting not found: cs=("
             << ctime_s << "," << id.creation_time.dwHighDateTime << ") cn=("
             << ctime_ns << "," << id.creation_time.dwLowDateTime << ") ms=("
             << mtime_s << "," << id.write_time.dwHighDateTime << ") mn=("
             << mtime_ns << "," << id.write_time.dwLowDateTime << ")");
      obj = CObject();
      obj.m_dbid = db_id;
      obj.m_id = id;
      return false;
    }
#endif
  }

  MTrace(t_cache, trace::Debug, " Times identical for object - reporting un-changed");
  return true;
}

void FSCache::insert(const CObject &obj)
{
  opendb();
  // Create the object
  //
  // Note; because of inode re-use, we actually have a race like this:
  //
  // Thread 1:   Open file inode 1 - not found in db
  // System:     delete file inode 1
  // System:     create new file - re-use inode 1
  // Thread 2:   Open file inode 1 - not found in db
  // Thread 1:   Complete processing - insert inode 1 in database
  // Thread 2:   Complete processing - insert inode 1 in database  <-- fail!
  //
  // It is of course impossible to know whether it is the first or the
  // second thread that has the "most recent" data.  If we simply
  // ignore the failure, two things can happen: Either we - by chance
  // - retained the correct record - or, we stored information which
  // will not match the retained file and therefore we will have to
  // re-visit the file in the next backup run.
  //
  // In either case, OR IGNORE (or OR REPLACE) are correct solutions
  // to the problem.
  //
  { std::ostringstream prep;
    prep << "INSERT OR IGNORE INTO objs2 "
         << "(dev, ino, ctime_s, ctime_ns, mtime_s, mtime_ns, hash, treesize) "
         << "VALUES (?,?,?,?,?,?,?,?);";
    sqlite3_stmt *pstmt = 0;
    int pres = sqlite3_prepare_v2(m_db, prep.str().c_str(), -1, &pstmt, 0);
    if (pres != SQLITE_OK)
      throw error("PREP " + prep.str() + ": " + sqlite3_errmsg(m_db));
    ON_BLOCK_EXIT(sqlite3_finalize, pstmt);

    sqlite3_bind_int64(pstmt, 1, obj.m_id.device);
    sqlite3_bind_int64(pstmt, 2, obj.m_id.fileid);

#if defined(__unix__) || defined(__APPLE__)
    sqlite3_bind_int64(pstmt, 3, obj.m_id.ctime_s);
    sqlite3_bind_int(pstmt, 4, obj.m_id.ctime_ns);
    sqlite3_bind_int64(pstmt, 5, obj.m_id.mtime_s);
    sqlite3_bind_int(pstmt, 6, obj.m_id.mtime_ns);
#endif

#if defined(_WIN32)
    sqlite3_bind_int64(pstmt, 3, obj.m_id.creation_time.dwHighDateTime);
    sqlite3_bind_int(pstmt, 4, obj.m_id.creation_time.dwLowDateTime);
    sqlite3_bind_int64(pstmt, 5, obj.m_id.write_time.dwHighDateTime);
    sqlite3_bind_int(pstmt, 6, obj.m_id.write_time.dwLowDateTime);
#endif

    std::vector<uint8_t> blob;
    for (objseq_t::const_iterator i = obj.m_hash.begin();
         i != obj.m_hash.end(); ++i)
      blob.insert(blob.end(), i->m_raw.begin(), i->m_raw.end());

    sqlite3_bind_blob(pstmt, 7, (blob.empty() ? 0 : &blob[0]), int(blob.size()), SQLITE_TRANSIENT);
    sqlite3_bind_int64(pstmt, 8, obj.m_treesize);

    int res = sqlite3_step(pstmt);
    if (res != SQLITE_DONE)
      throw error("STEP " + prep.str() + ": " + sqlite3_errmsg(m_db));
  }

  MTrace(t_cache, trace::Debug, "Added object " << obj.m_id << " to cache");

  // Optionally commit
  didUpdate();
}

void FSCache::update(const CObject &obj)
{
  opendb();
  // Update this object
  { std::ostringstream prep;
    prep << "UPDATE objs2"
         << " SET ctime_s = ?, ctime_ns = ?, mtime_s = ?, mtime_ns = ?, "
         << "     hash = ?, treesize = ?"
         << " WHERE id = ?;";

    sqlite3_stmt *pstmt = 0;
    int pres = sqlite3_prepare_v2(m_db, prep.str().c_str(), -1, &pstmt, 0);
    if (pres != SQLITE_OK)
      throw error("PREP " + prep.str() + ": " + sqlite3_errmsg(m_db));
    ON_BLOCK_EXIT(sqlite3_finalize, pstmt);

#if defined(__unix__) || defined(__APPLE__)
    sqlite3_bind_int64(pstmt, 1, obj.m_id.ctime_s);
    sqlite3_bind_int(pstmt, 2, obj.m_id.ctime_ns);
    sqlite3_bind_int64(pstmt, 3, obj.m_id.mtime_s);
    sqlite3_bind_int(pstmt, 4, obj.m_id.mtime_ns);
#endif

#if defined(_WIN32)
    sqlite3_bind_int64(pstmt, 1, obj.m_id.creation_time.dwHighDateTime);
    sqlite3_bind_int(pstmt, 2, obj.m_id.creation_time.dwLowDateTime);
    sqlite3_bind_int64(pstmt, 3, obj.m_id.write_time.dwHighDateTime);
    sqlite3_bind_int(pstmt, 4, obj.m_id.write_time.dwLowDateTime);

    MTrace(t_cache, trace::Debug, "Updated object " << obj.m_id << " in cache: cs=("
           << obj.m_id.creation_time.dwHighDateTime << ") cn=("
           << obj.m_id.creation_time.dwLowDateTime << ") ms=("
           << obj.m_id.write_time.dwHighDateTime << ") mn=("
           << obj.m_id.write_time.dwLowDateTime << ")");
#endif

    std::vector<uint8_t> blob;
    for (objseq_t::const_iterator i = obj.m_hash.begin();
         i != obj.m_hash.end(); ++i)
      blob.insert(blob.end(), i->m_raw.begin(), i->m_raw.end());

    sqlite3_bind_blob(pstmt, 5, (blob.empty() ? 0 : &blob[0]), int(blob.size()), SQLITE_TRANSIENT);
    sqlite3_bind_int64(pstmt, 6, obj.m_treesize);
    sqlite3_bind_int64(pstmt, 7, obj.m_dbid);

    int res = sqlite3_step(pstmt);
    if (res != SQLITE_DONE)
      throw error("STEP " + prep.str() + ": " + sqlite3_errmsg(m_db));
  }

  // Optionally commit the txn
  didUpdate();
}

void FSCache::execute(const std::string &stmt)
{
  sqlite3_stmt *pstmt = 0;
  int pres = sqlite3_prepare_v2(m_db, stmt.c_str(), -1, &pstmt, 0);
  if (pres != SQLITE_OK)
    throw error("PREP " + stmt + ": " + sqlite3_errmsg(m_db));
  ON_BLOCK_EXIT(sqlite3_finalize, pstmt);

  int res = sqlite3_step(pstmt);
  if (res != SQLITE_DONE)
      throw error("STEP " + stmt + ": " + sqlite3_errmsg(m_db));
}

void FSCache::didUpdate()
{
  MutexLock l(m_db_lock);
  if (m_txn_start + g_txn_grp_period < Time::now()) {
    execute("COMMIT TRANSACTION");
    execute("BEGIN TRANSACTION");
    // We take 'now' again, because the commit above may have taken a
    // long time and we really want to guarantee g_txn_grp_period of
    // no commits to prevent excessive load on the system we are
    // running on.
    m_txn_start = Time::now();
  }
}



CObject::CObject()
  : m_dbid(-1)
  , m_treesize(0)
{
}
