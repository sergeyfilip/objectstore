///
/// Upload routines
///
//
// $Id: upload.cc,v 1.57 2013/10/15 07:48:35 sf Exp $
//

#include "upload.hh"
#include "common/trace.hh"
#include "common/error.hh"
#include "common/scopeguard.hh"
#include "common/time.hh"
#include "common/string.hh"
#include "objparser/objparser.hh"
#include "xml/xmlio.hh"

#include <algorithm>

#if defined(__unix__) || defined(__APPLE__)
# include <sys/types.h>
# include <sys/stat.h>
# include <fcntl.h>
# include <dirent.h>
# include <pwd.h>
# include <grp.h>
# include <errno.h>
#endif

#if defined(__linux__)
# include <sys/inotify.h>
#endif

namespace {
  //! Tracer for uploader
  trace::Path t_up("/upload");
  //! Detailed tracing for object serialisation
  trace::Path t_ser("/upload/ser");
  //! Detailed tracing for CDP optimisation
  trace::Path t_cdp("/upload/cdp");
  //! Detailed upload worker logging
  trace::Path t_worker("/upload/worker");
  //! Tracer for upload manager
  trace::Path t_upm("/upload/manager");
}

namespace {

  /// For short delays on retry, call this
  void short_delay()
  {
#if defined(__unix__) || defined(__APPLE__)
    sleep(1);
#endif
#if defined(_WIN32)
    Sleep(1000);
#endif
  }


  void ser(std::vector<uint8_t> &data, const uint8_t &d)
  {
    data.push_back(d);
    MTrace(t_ser, trace::Debug, "Serialised uint8 "
           << uint16_t(d) << " as "
           << uint32_t(*(data.end()-1)));
  }

  void ser(std::vector<uint8_t> &data, const uint16_t &d)
  {
    // big endian - most significant byte first
    data.push_back(uint8_t((d >> 8) & 0xFF));
    data.push_back(uint8_t(d & 0xFF));
    MTrace(t_ser, trace::Debug, "Serialised uint16 "
           << d << " as "
           << uint32_t(*(data.end() - 2))
           << "," << uint32_t(*(data.end()-1)));
  }

  void ser(std::vector<uint8_t> &data, const uint32_t &d)
  {
    // big endian - most significant byte first
    data.push_back(uint8_t((d >> 24) & 0xFF));
    data.push_back(uint8_t((d >> 16) & 0xFF));
    data.push_back(uint8_t((d >> 8) & 0xFF));
    data.push_back(uint8_t(d & 0xFF));
    MTrace(t_ser, trace::Debug, "Serialised uint32 " << d);
  }

  void ser(std::vector<uint8_t> &data, const uint64_t &d)
  {
    // big endian - most significant byte first
    data.push_back(uint8_t((d >> 56) & 0xFF));
    data.push_back(uint8_t((d >> 48) & 0xFF));
    data.push_back(uint8_t((d >> 40) & 0xFF));
    data.push_back(uint8_t((d >> 32) & 0xFF));
    data.push_back(uint8_t((d >> 24) & 0xFF));
    data.push_back(uint8_t((d >> 16) & 0xFF));
    data.push_back(uint8_t((d >> 8) & 0xFF));
    data.push_back(uint8_t(d & 0xFF));
    MTrace(t_ser, trace::Debug, "Serialised uint64 " << d);
  }

  void ser(std::vector<uint8_t> &data, const std::string &d)
  {
    ser(data, uint32_t(d.size()));
    data.insert(data.end(), d.begin(), d.end());
    MTrace(t_ser, trace::Debug, "Serialised string \"" << d << "\""
           << " of size " << d.size());
  }

  void ser(std::vector<uint8_t> &data, const std::vector<uint8_t> &d)
  {
    ser(data, uint32_t(d.size()));
    data.insert(data.end(), d.begin(), d.end());
    MTrace(t_ser, trace::Debug, "Serialised vector of size " << d.size());
  }

  void ser(std::vector<uint8_t> &data, const objseq_t &d)
  {
    ser(data, uint32_t(d.size()));
    for (size_t i = 0; i != d.size(); ++i) {
      data.insert(data.end(), &d[i].m_raw[0], &d[i].m_raw[0] + 32);
    }
    MTrace(t_ser, trace::Debug, "Serialised objseq with length 32 * " << d.size());
  }

}

struct dirobj_t {
  dirobj_t() : treesize(0) { }
  dirobj_t(const objseq_t &lor, const std::vector<uint8_t> &lom, uint64_t s)
    : lor_hash(lor), lom_data(lom), treesize(s) { }
  objseq_t lor_hash;
  std::vector<uint8_t> lom_data;
  uint64_t treesize;
};

size_t encodeDirObjs(std::vector<uint8_t> &object_out, uint64_t &treesize_out,
                     const std::list<dirobj_t> &dirobj_lorm, bool partial)
{
  ser(object_out, uint8_t(0x00)); // Version 0 object
  // 0xdd => Partial directory entry, 0xde => Complete directory entry
  ser(object_out, uint8_t(partial?0xdd:0xde));
  
  //
  // Find out how many entries of LoR and LoM we can add until we
  // reach the chunk size.
  //
  std::list<dirobj_t>::const_iterator i = dirobj_lorm.begin();
  size_t n = 0;
  size_t head_size = 1 // version
  + 1  // type
  + 8  // tree-size
  + 4; // length of LoR
  size_t lom_size = 0;
  uint64_t treesize = 0;
  
  while (i != dirobj_lorm.end()) {
    // We can encode up to the current head minus four bytes for LoR
    // length minus four bytes for LoM length (LoR can be longer
    // than LoM because each M entry may reference several objects)
    const size_t hs_add = 4 + i->lor_hash.size() * 32; // 32 bytes per hash in LoR
    const size_t ls_add = i->lom_data.size();
    // See if we are at the limit or if we can go on
    if (head_size + hs_add + lom_size + ls_add < ng_chunk_size) {
      head_size += hs_add;
      lom_size += ls_add;
      treesize += i->treesize;
      ++i;
      ++n;
    } else break;
  }
  
  // If we could not add any objects, we have a single directory
  // entry that is too big for an object. We simply do not handle
  // that now. This would happen if a file is greater than 0.54 TiB.
  if (i == dirobj_lorm.begin())
    throw error("Oversized directory entry - giving up");
  
  // Sum of ourselves plus our children
  // We are head + lom
  treesize += head_size + lom_size;
  ser(object_out, uint64_t(treesize));
  
  MTrace(t_up, trace::Debug, "Object has treesize " << treesize);
  
  // So, add this - first, length of LoR
  ser(object_out, uint32_t(n));
  // Then the LoR - note; the LoR is a list of lists...  The n'th
  // LoM entry references the n'th LoR entry.
  for (std::list<dirobj_t>::const_iterator l = dirobj_lorm.begin(); l != i; ++l) {
    ser(object_out, l->lor_hash);
  }
  // And then the LoM... It is already raw data.
  for (std::list<dirobj_t>::const_iterator l = dirobj_lorm.begin(); l != i; ++l)
    object_out.insert(object_out.end(), l->lom_data.begin(), l->lom_data.end());
  
  treesize_out = treesize;
  return n;
}

ServerConnection::Reply Upload::execute(ServerConnection &conn,
                                        ServerConnection::Request &req)
{
  while (!m_backup_cancelled) {
    ServerConnection::Reply rep;
    try {
      rep = conn.execute(req);
    } catch (error &e) {
      MTrace(t_up, trace::Info, "Back end error: " << e.toString());
      short_delay();
      continue;
    }
    // If we got a 500 error, retry as well
    if (rep.getCode() >= 500) {
      MTrace(t_up, trace::Info, "Back end error: " << rep.toString());
      short_delay();
      continue;
    }
    // Fine, return reply then
    return rep;
  }
  // We got cancelled.
  throw error("Operation cancelled.");
}

bool Upload::testObject(ServerConnection &conn, const sha256 &hash)
{
  ServerConnection::Request req(ServerConnection::mHEAD, "/object/" + hash.m_hex);
  req.setBasicAuth(conn);
  ServerConnection::Reply rep = execute(conn, req);
  if (rep.getCode() == 204)
    return true;
  if (rep.getCode() == 404)
    return false;
  throw error("Verify object got unexpected reply: " + rep.toString());
}

void Upload::uploadObject(ServerConnection &conn, const std::vector<uint8_t> &obj)
{
  ServerConnection::Request req(ServerConnection::mPOST,
                                "/object/" + sha256::hash(obj).m_hex);
  req.setBasicAuth(conn);
  req.setBody(obj);
  ServerConnection::Reply rep = execute(conn, req);
  if (rep.getCode() != 201)
    throw error("Unable to upload object - server said: " + rep.toString());
}

/// Download object
void fetchObject(ServerConnection &conn, const sha256 &hash,
                 std::vector<uint8_t> &obj)
{
  ServerConnection::Request req(ServerConnection::mGET,
                                "/object/" + hash.m_hex);
  req.setBasicAuth(conn);
  ServerConnection::Reply rep = conn.execute(req);
  if (rep.getCode() != 200)
    throw error("Cannot retrieve object " + hash.m_hex
                + ": " + rep.toString());

  // Steal the data body from the reply
  obj.swap(rep.refBody());
}



Upload::Upload(FSCache &cache, ServerConnection &conn, const std::string &d, const std::string &p)
  : m_cache(cache)
  , m_conn(conn)
  , m_filter(0)
  , m_progress(0)
  , m_completion_notify(0)
  , m_completion_notify_done(false)
  , m_snapshot_notify(0)
  , m_backup_cancelled(0)
  , m_nworkers(2)
  , m_device_name(d)
  , m_backup_root(p)
  , m_wroot(0, std::string())
#if defined(__linux__)
  , m_addWatchDelegate(0)
#endif
{
  if (m_device_name.empty())
    throw error("Empty device name given to Upload engine");
}

Upload::~Upload()
{
  // Make our processor threads exit
  m_workqueue_sem.increment();

  // Wait for every processor to exit before we move on to deleting
  // our notifiers and filters
  for (std::list<Processor>::iterator i = m_workers.begin();
       i != m_workers.end(); ++i)
    i->join_nothrow();

  delete m_snapshot_notify;
  delete m_completion_notify;
  delete m_progress;
  delete m_filter;
#if defined(__linux__)
  delete m_addWatchDelegate;
#endif
}

Upload &Upload::setWorkers(size_t n)
{
  m_nworkers = n;
  return *this;
}

Upload &Upload::setFilter(const BindF1Base<bool,const std::string&> &f)
{
  delete m_filter;
  m_filter = f.clone();
  return *this;
}

Upload &Upload::setCompletionNotification(const BindF1Base<void, Upload&> &f)
{
  delete m_completion_notify;
  m_completion_notify = f.clone();
  return *this;
}

Upload &Upload::setSnapshotNotification(const BindF1Base<void, Upload&> &f)
{
  delete m_snapshot_notify;
  m_snapshot_notify = f.clone();
  return *this;
}

Upload &Upload::setProgressLog(const BindF1Base<void,Upload&> &p)
{
  delete m_progress;
  m_progress = p.clone();
  return *this;
}

#if defined(__linux__)
Upload &Upload::setAddWatchDelegate(const BindF1Base<int,const std::string&> &f)
{
  delete m_addWatchDelegate;
  m_addWatchDelegate = f.clone();
  return *this;
}
#endif

Upload::dirstate_t::dirstate_t(const std::string &n, dirstate_t *p,
                               Upload::wnode_t &w, const CObject &co)
  : name(n)
  , parent(p)
  , watch(w)
  , cobj(co)
#if defined(__unix__) || defined(__APPLE__)
  , meta_uid(0)
  , meta_gid(0)
  , meta_mode(0)
#endif
#if defined(_WIN32)
  , meta_fattr(0)
#endif
{
  MTrace(t_cdp, trace::Debug, "Created dirstate " << n
         << " with wn " << w.name
         << " having queued=" << w.m_cdp_queued);
}

Upload::dirstate_t::~dirstate_t()
{
}

size_t Upload::dirstate_t::getDepth() const
{
  if (parent)
    return 1 + parent->getDepth();
  return 0;
}

void Upload::dirstate_t::process_scan(Upload::Processor &p)
{
  // If backup was cancelled, don't do anything!
  if (p.refUpload().cancelInProgress()) {
    MTrace(t_worker, trace::Debug, "Ignoring scan of item \"" << name
           << "\" because of cancel");
    return;
  }

  MTrace(t_worker, trace::Debug, "Got work item \"" << name
         << "\" - will scan");

  // Fine, we need to scan this directory (this will also add all
  // child directories as work items in our parent Upload object)
  p.setStatus(threadstatus_t::OSScanning, name);
  if (scan(p)) {
    // We must make sure that all children are added before we start
    // working on them. Otherwise we might wrongfully conclude that a
    // directory has had its last (possibly only) child directory
    // processed, only to discover that another one was added while we
    // uploaded it.
    //
    // However, as soon as the first child is added as a work item, our
    // incomplete_children (and other) members may be changed by other
    // worker threads. To avoid having other threads write to our
    // incomplete_children while we're traversing, we hold the
    // dirstate_lock
    MutexLock l(p.refUpload().m_dirstate_lock);
    for (std::set<dirstate_t*>::const_iterator i = incomplete_children.begin();
         i != incomplete_children.end(); ++i)
      p.refUpload()
        .addWorkItem((*i)->getDepth(),
                     papply(*i, &Upload::dirstate_t::process_scan).clone());

    //
    // We have incomplete children - we can therefore not proceed
    // to actually upload anything. At some point in the future, a
    // leaf descendent will back-track and upload us.
    //
    // We cannot log our name though, because actually the
    // back-tracking might already have occurred and the item may
    // alreay be deleted.
    //
    MTrace(t_worker, trace::Debug, "Scanned directory has incomplete children");
    return;
  }

  // Ok we have no incomplete children. We must schedule an upload.
  p.refUpload()
    .addWorkItem(getDepth(),
                 papply(this, &Upload::dirstate_t::process_upload).clone());
}

void Upload::dirstate_t::process_upload(Processor &p)
{
  if (p.refUpload().cancelInProgress()) {
    MTrace(t_worker, trace::Debug, "Ignoring upload of items under \"" << name
           << "\" because of cancel");
    return;
  }

  MTrace(t_worker, trace::Debug, "Directory " << name
         << " is ready - will upload");
  //
  // We have a leaf directory. We want to process this directory
  // (upload it - referencing all its child objects)
  //
  upload(p);
  watch.m_cdp_queued = false; // no longer queued for upload!

  //
  // When done with the processing, we want to move this directory
  // from the incomplete_children list to the complete_children of
  // the parent.
  //
  { MutexLock l(p.refUpload().m_dirstate_lock);

    // Delete all complete_children entries as we no longer will
    // need them.
    for (std::vector<dirstate_t*>::iterator cci = complete_children.begin();
         cci != complete_children.end(); ++cci)
      delete *cci;
    complete_children.clear();
    MAssert(incomplete_children.empty(), "Ended upload of directory "
            "with incomplete children");

    // Fine, now do parent processing.
    //
    // Now when we finish uploading a directory and step up to the
    // parent, we may not need to process the parent. Consider a
    // situation where a directory P has two child directories C0
    // and C1. C0 is being processed in Thread 0 and C1 in Thread
    // 1.
    //
    // Assuming that T0 finishes C0 and wishes to step up, at the
    // same time as T1 finishes C1 and also wishes to step
    // up. Obviously EITHER T0 or T1 need to process P, but NOT
    // BOTH.
    //
    // This is handled since the last thread to enter the
    // dirstate_lock will be the one that sees that the
    // parent->incomplete_children is empty.
    //
    if (parent) {
      parent->complete_children.push_back(this);
      const bool found = parent->incomplete_children.erase(this);
      MAssert(found, "Parent " << parent->name
              << " did not have child " << name
              << " as incomplete child");

      //
      // So, this move *could* have caused the parent to become
      // eligible for upload, in that it has no incomplete children.
      //
      if (parent->incomplete_children.empty()) {
        MTrace(t_worker, trace::Debug, "Parent of " << name
               << " is eligible for upload. Stepping up");
        p.refUpload()
          .addWorkItem(parent->getDepth(),
                       papply(parent, &Upload::dirstate_t::process_upload).clone());
        return;
      }
      //
      // Ok fine, our parent has incomplete children. This item
      // will be uploaded when the last incomplete_child finishes
      // uploading. For now, just pick the next item from the work
      // queue.
      //
      MTrace(t_worker, trace::Debug, "Parent of " << name
             << " still has "
             << parent->incomplete_children.size()
             << " unprocessed child directories.");

      // Create partial snapshots every 60 seconds
      if (Time::now() > p.refUpload().m_latestSnapshotInfo.tstamp + DiffTime::iso("PT60S")) {
        dirstate_t *parentTmp = parent;
        while (parentTmp) {
          MTrace(t_worker, trace::Info, "Creating partial folder " << parentTmp->name);
          parentTmp->upload(p);
          if (!parentTmp->parent) {
            {
              MutexLock l(p.refUpload().m_latestSnapshotInfoLock);
              p.refUpload().m_latestSnapshotInfo.tstamp = Time::now();
              p.refUpload().m_latestSnapshotInfo.type = BTPartial;
              p.refUpload().m_latestSnapshotInfo.hash = parentTmp->cobj.m_hash;
              p.refUpload().m_latestSnapshotInfo.treesize = parentTmp->cobj.m_treesize;
            }
            // Notify about root chunk upload
            if (p.refUpload().m_snapshot_notify) {
              (*p.refUpload().m_snapshot_notify)(p.refUpload());
            } else {
              // Invoke "Partial upload" completion handler
              p.refUpload().complete(p.refConn(), parentTmp->cobj.m_hash, BTPartial);
            }

            MTrace(t_worker, trace::Debug, "Partial snapshot created:\n"
                   << parentTmp->cobj.m_hash[0].m_hex);
          }
          parentTmp = parentTmp->parent;
        }
      }

      return;
    }
  }

  // If we are here, then we have no parent....
  p.setStatus(threadstatus_t::OSFinishing, std::string());

  //
  // If we are here, the item has no parent.
  //
  MAssert(!parent, "Root item has parent");
  //
  // If the item we completed work on is the root object that we
  // were originally given for backup, we're done and should make
  // sure the workers exit.
  //
  MTrace(t_worker, trace::Debug, "Ended processing of root "
         << name);

  {
    MutexLock l(p.refUpload().m_latestSnapshotInfoLock);
    p.refUpload().m_latestSnapshotInfo.tstamp = Time::now();
    p.refUpload().m_latestSnapshotInfo.type = BTComplete;
    p.refUpload().m_latestSnapshotInfo.hash = cobj.m_hash;
    p.refUpload().m_latestSnapshotInfo.treesize = cobj.m_treesize;
  }
  // Notify about root chunk upload
  if (p.refUpload().m_snapshot_notify) {
    (*p.refUpload().m_snapshot_notify)(p.refUpload());
  } else {
    // Invoke completion handler
    p.refUpload().complete(p.refConn(), cobj.m_hash);
  }

  // This object is never deleted by anyone else. Suicide it is then!
  delete this;
}

bool Upload::startUpload()
{
  // We manipulate our worker threads, so take the workers_lock
  MutexLock l(m_workers_lock);

  // Sanity check
  if (m_backup_root.empty())
    throw error("Backup root not yet set");

  //
  // See whether or not we are running a backup already...
  //
  if (isWorking_nowl())
    return false;

  MTrace(t_up, trace::Info, "Starting backup job");

  // Clear notification status
  m_completion_notify_done = false;
  // Clear cancellation status
  m_backup_cancelled = 0;
  // Move wnode_t touched status to queued status
  { MutexLock l(m_wroot_lock);
    m_wroot.queueTouched();
  }
  // Set up
  addWorkItem(0, papply(new dirstate_t(m_backup_root, 0, m_wroot),
                        &Upload::dirstate_t::process_scan).clone());
  // Start additional workers if any are needed
  for (size_t i = m_workers.size(); i < m_nworkers; ++i) {
    m_workers.push_back(Processor(*this, i));
    m_workers.back().start();
  }
  // Success!
  return true;
}

void Upload::cancelUpload()
{
  // We've cancelled. Register that.
  m_backup_cancelled = true;
}

bool Upload::cancelInProgress()
{
  MutexLock l(m_workqueue_lock);
  return m_backup_cancelled;
}

void Upload::checkForCompletion()
{
  if (!isWorking()) {
    MutexLock l(m_workqueue_lock);
    if (!m_completion_notify_done) {
      MTrace(t_worker, trace::Info, "Last worker calling completion notify");
      m_completion_notify_done = true;
      if (m_completion_notify)
        (*m_completion_notify)(*this);
    }
  }
}

bool Upload::isWorking_nowl()
{
  MutexLock l2(m_workqueue_lock);

  // If there are items in the work queue, we're working
  if (!m_workqueue.empty())
    return true;

  // If any of the threads are not OSIdle, then we're working
  for (std::list<Processor>::iterator i = m_workers.begin();
       i != m_workers.end(); ++i)
    if (i->isBusy())
      return true;

  // Fine, we're not working then
  return false;
}

bool Upload::isWorking()
{
  MutexLock l(m_workers_lock);
  return isWorking_nowl();
}

/// Get path of backup root
const std::string &Upload::getBackupRoot()
{
  return m_backup_root;
}

Upload::LatestSnapshotInfo Upload::getLatestSnapshotInfo()
{
  MutexLock l(m_latestSnapshotInfoLock);
  return m_latestSnapshotInfo;
}

void Upload::setStatus(size_t ndx, threadstatus_t::objstat s, const std::string &o,
		       const Optional<double> &p)
{
  bool changed = false;
  {
    MutexLock l(m_procstate_lock);
    // Make sure we have room for this thread
    if (m_procstate.size() <= ndx)
      m_procstate.resize(ndx+1);
    // Fine, set the status
    if (m_procstate[ndx].state != s) {
      m_procstate[ndx].state = s;
      changed = true;
    }
    if (m_procstate[ndx].object != o) {
      m_procstate[ndx].object = o;
      changed = true;
    }
    if (m_procstate[ndx].object_progress != p) {
      m_procstate[ndx].object_progress = p;
      changed = true;
    }
  }
  // Call our callback to notify of change
  if (changed && m_progress)
    (*m_progress)(*this);
}

std::vector<Upload::threadstatus_t> Upload::getProgressInfo()
{
  MutexLock l(m_procstate_lock);
  return m_procstate;
}

ServerConnection &Upload::Processor::refConn()
{
  return m_conn;
}

Upload &Upload::Processor::refUpload()
{
  return m_parent;
}

bool Upload::Processor::isBusy() const
{
  return m_is_busy;
}

bool Upload::touchPath(const std::string &fullpath)
{
  MutexLock l(m_wroot_lock);
  // See if we are excluded by a filter
  if (m_filter && !(*m_filter)(fullpath)) {
    MTrace(t_up, trace::Debug, "Touched directory filtered - ignoring");
  } else {
    // Add (if needed) path components to scan tree
    std::string path = fullpath.substr(m_backup_root.length()-1);
    if (*path.rbegin() == '/' || *path.rbegin() == '\\') {
      path=path.substr(0, path.length()-1);
    }
    m_wroot.insert(path).markTouched();
    return true;
  }
  return false;
}

Upload::workitem_t *Upload::getWorkItem()
{
  // Wait for an entry to appear
  m_workqueue_sem.decrement();
  // Fine, de-queue the item
  workitem_t *item = 0;
  { MutexLock l(m_workqueue_lock);
    if (m_workqueue.empty()) {
      // No more work. We want to return 0 and let the other threads
      // know too. So re-increment the semaphore!
      m_workqueue_sem.increment();
    } else {
      // More work. Pick the item with the greatest depth
      std::multimap<size_t,workitem_t*>::iterator i = m_workqueue.end();
      --i;
      item = i->second;
      m_workqueue.erase(i);
    }
  }
  if (item) {
    MTrace(t_worker, trace::Debug, "Returning work item");
  } else {
    MTrace(t_worker, trace::Debug, "Returning STOP item");
  }
  return item;
}

void Upload::addWorkItem(size_t p, BindF1Base<void,Upload::Processor&> *i)
{
  { MutexLock l(m_workqueue_lock);
    m_workqueue.insert(std::make_pair(p,i));
  }
  m_workqueue_sem.increment();
}

void Upload::stopWorkQueue()
{
  { MutexLock l(m_workqueue_lock);
    MAssert(m_workqueue.empty(), "Zero work item added on non-empty queue");
  }
  m_workqueue_sem.increment();
}

void Upload::complete(ServerConnection &conn,
                      const objseq_t &root, BackupType backupType)
{
  MAssert(!root.empty(), "Empty root given to completion handler");
  //
  // Fine, upload a new root object.
  //
  MTrace(t_worker, trace::Info, "Uploading new device root "
         << root[0].m_hex << " under device name "
         << m_device_name);
  using namespace xml;
  std::string rstr(root[0].m_hex);
  char type = (backupType == BTPartial)?'p':'c';
  const IDocument &doc = mkDoc
    (Element("backup")
     (Element("tstamp")(CharData<Time>(m_latestSnapshotInfo.tstamp))
      & Element("root")(CharData<std::string>(rstr))
      & Element("type")(CharData<char>(type))));

  ServerConnection::Request req(ServerConnection::mPOST,
                                "/devices/" + str2url(m_device_name)
                                + "/history");
  req.setBasicAuth(conn);
  req.setBody(doc);
  ServerConnection::Reply rep = execute(conn, req);
  if (rep.getCode() == 201) {
    MTrace(t_worker, trace::Debug, "Successfully uploaded new root");
  } else {
    MTrace(t_worker, trace::Warn, "New root error: " << rep.toString());
  }
}


Upload::Processor::Processor(Upload &p, size_t i)
  : m_parent(p)
  , m_instance_id(i)
  , m_is_busy(false)
  , m_conn(p.m_conn)
{
}

Upload::Processor::Processor(const Processor &o)
  : m_parent(o.m_parent)
  , m_instance_id(o.m_instance_id)
  , m_is_busy(o.m_is_busy)
  , m_conn(o.m_conn)
{
}

void Upload::Processor::setStatus(threadstatus_t::objstat s, const std::string &o,
				  const Optional<double> &p)
{
  // Let our parent register the current status of us
  m_parent.setStatus(m_instance_id, s, o, p);
}


void Upload::Processor::run()
{
  /// Pick a directory to work on from the todo stack
  while (true) {
    // Register that we are not busy
    setStatus(threadstatus_t::OSIdle, std::string());
    m_is_busy = false;

    // If all threads are idle and the queue is empty, then our backup
    // has completed. Precisely one worker thread needs to call the
    // completion routine.
    m_parent.checkForCompletion();

    //
    // Wait for item from work queue
    //
    if (workitem_t *item = m_parent.getWorkItem()) {
      //
      // Register that we are busy and execute the item
      //
      m_is_busy = true;
      try {
        (*item)(*this);
      } catch (error &e) {
        // We failed processing - we want to notify the parent of
        // this.
        MTrace(t_worker, trace::Warn, "Aborting backup: " << e.toString());
        m_parent.cancelUpload();
      }
      delete item;
    } else {
      MTrace(t_worker, trace::Info, "Work stack empty, worker exit");
      return;
    }
  }
  m_is_busy = false;
}

Upload::wnode_t::wnode_t(wnode_t *p, const std::string &n)
  : m_cdp_touched(false)
  , m_cdp_queued(true) // all new nodes created during backup should
                       // be visited
#if defined(__linux__)
  , m_wd(-1)
#endif
  , m_parent(p)
  , name(n)
{
  // All new nodes created during backup should mark their parent(s)
  // as having children queued for backup so that we are guaranteed to
  // visit this child
  for (wnode_t *i = m_parent; i && !i->m_cdp_queued; i = i->m_parent)
    i->m_cdp_queued = true;
}

Upload::wnode_t::~wnode_t()
{
  for (std::vector<wnode_t*>::iterator i = children.begin();
       i != children.end(); ++i)
    delete *i;
}

void Upload::wnode_t::touchAll()
{
  m_cdp_touched = true;
  for (std::vector<wnode_t*>::const_iterator i = children.begin();
       i != children.end(); ++i)
    (*i)->touchAll();
}

void Upload::wnode_t::markTouched()
{
  // If we' already touched, there is nothing more to do
  if (m_cdp_touched) return;

  // Fine, mark us and mark our parents
  m_cdp_touched = true;
  if (m_parent)
    m_parent->markTouched();
}

void Upload::wnode_t::queueTouched()
{
  // Move and clear
  if (m_cdp_touched)
    MTrace(t_cdp, trace::Debug, "Queuing touched entry: " << name);
  else
    MTrace(t_cdp, trace::Debug, "Not queuing untouched entry: " << name);
  m_cdp_queued = m_cdp_touched;
  m_cdp_touched = false;
  // Traverse children - if needed
  if (m_cdp_queued)
    for (std::vector<wnode_t*>::const_iterator i = children.begin();
         i != children.end(); ++i)
      (*i)->queueTouched();
}

std::string Upload::wnode_t::absName() const
{
#if defined(__unix__) || defined(__APPLE__)
  const char separator = '/';
#endif
#if defined(_WIN32)
  const char separator = '\\';
#endif
  if (m_parent)
    return m_parent->absName() + separator + name;
  else
    return name;
}

Upload::wnode_t &Upload::wnode_t::insert(const std::string &name, size_t nofs)
{
  // If we're at the end, we are the target
  if (nofs == name.npos)
    return *this;

  // Cut out component name
#if defined(__unix__) || defined(__APPLE__)
  const char separator = '/';
#endif
#if defined(_WIN32)
  const char separator = '\\';
#endif
  const size_t cend = name.find(separator, nofs + 1);
  const std::string cname = name.substr(nofs + 1, cend - nofs - 1);

  // See if any child has the name
  for (std::vector<wnode_t*>::iterator c = children.begin();
       c != children.end(); ++c)
    if ((*c)->name == cname)
      return (*c)->insert(name, cend);

  // No match, insert.
  children.push_back(new wnode_t(this, cname));
  return children.back()->insert(name, cend);
}

Upload::wnode_t &Upload::wnode_t::getChild(const std::string &cn)
{
  for (std::vector<wnode_t*>::const_iterator i = children.begin();
       i != children.end(); ++i)
    if ((*i)->name == cn) {
      MTrace(t_cdp, trace::Debug, "wnode_t::getChild(" << cn << ") found match (q="
             << (*i)->m_cdp_queued << ")");
      return **i;
    }
  // Not found. Create
  MTrace(t_cdp, trace::Debug, "wnode_t::getChild(" << cn << ") creating new child");
  children.push_back(new wnode_t(this, cn));
  return *children.back();
}

void Upload::wnode_t::tracetree(size_t indent)
{
  MTrace(t_up, trace::Info, std::string(indent, '|') << name);
  for (std::vector<wnode_t*>::iterator c = children.begin();
       c != children.end(); ++c)
    (*c)->tracetree(indent + 1);
}

///#if defined (__APPLE__) 
UploadManager::UploadManager(FSCache &cache, ServerConnection &conn, const std::string &deviceID, const std::string &userID)

: m_cache(cache)
, m_conn(conn)
, m_deviceName(deviceID)
, m_userID (userID)
, m_filter(0)
, m_progress(0)
, m_scheduler(*this)
{
    m_dirMonitor.setChangeNotification(papply(this, &UploadManager::handleChangeNotification));
}
///#else
// Upload Manager constructor
UploadManager::UploadManager(FSCache &cache, ServerConnection &conn,
                             const std::string &deviceName)
: m_cache(cache)
, m_conn(conn)
, m_deviceName(deviceName)
, m_filter(0)
, m_progress(0)
, m_scheduler(*this)
{
    m_dirMonitor.setChangeNotification(papply(this, &UploadManager::handleChangeNotification));
}
///#endif

UploadManager::~UploadManager()
{
  for (uploads_t::iterator uploadIt = m_uploads.begin();
       uploadIt != m_uploads.end(); ++uploadIt) {
    (*uploadIt)->cancelUpload();
    delete (*uploadIt);
  }
  delete m_filter;
  delete m_progress;
}

UploadManager &UploadManager::setFilter(const BindF1Base<bool,const std::string&> &f)
{
  delete m_filter;
  m_filter = f.clone();
  return *this;
}

UploadManager &UploadManager::setProgressLog(const BindF1Base<void,UploadManager&> &p)
{
  if (m_progress !=0) delete m_progress;
  m_progress = p.clone();
  return *this;
}

void UploadManager::addUploadRoot(const std::string &p)
{
  std::string path = p;
#if defined(__unix__) || defined(__APPLE__)
  if (*path.rbegin() != '/') path += "/";
#endif
#if defined(_WIN32)
  if (*path.rbegin() != '\\') path += "\\";
#endif
  Upload* upload = new Upload(m_cache, m_conn, m_deviceName, path);
  m_uploads.push_back(upload);

  upload->setWorkers(2);
  upload->setSnapshotNotification(papply(this, &UploadManager::handleSnapshotNotification));
  if (m_filter) {
    upload->setFilter(*m_filter);
  }
  upload->setProgressLog(papply(this, &UploadManager::handleProgressNotification));
#if defined(__linux__)
  upload->setAddWatchDelegate(papply(&m_dirMonitor, &DirMonitor::addDir));
#endif
}

void UploadManager::addPathMonitor(const std::string &p)
{
  std::string path = p;
#if defined(__unix__) || defined(__APPLE__)
  if (*path.rbegin() != '/') path += "/";
#endif
#if defined(_WIN32)
  if (*path.rbegin() != '\\') path += "\\";
#endif
  // associate monitored path with upload object
  // Check path to monitor /A/B/C/ against upload roots:
  // /A/B/C/ and /A/B/ and /A/ and /
  size_t pos = path.find_last_of("\\/");
  while (pos != std::string::npos) {
    std::string subpath = path.substr(0, pos+1);

    for (uploads_t::iterator uploadIt = m_uploads.begin();
         uploadIt != m_uploads.end(); ++uploadIt) {
      if ((*uploadIt)->getBackupRoot() == subpath) {
        std::pair<monitor2Upload_t::iterator,bool> ret;
        ret = m_monitor2Upload.insert( std::pair<std::string, Upload*>(path,(*uploadIt)) );
        MAssert(ret.second, "monitoring path has been already added");
        m_dirMonitor.addDir(path);
        return;
      }
    }

    if (pos == 0) {
      break;
    }
    pos = path.find_last_of("\\/", pos-1);
   }
  MAssert(0, "could not associate monitor with upload");
}

bool UploadManager::isWorking()
{
  // Is any Upload active
  for (uploads_t::iterator uploadIt = m_uploads.begin();
       uploadIt != m_uploads.end(); ++uploadIt) {
    if ((*uploadIt)->isWorking()) {
      return true;
    }
  }
  return false;
}

void UploadManager::handleChangeNotification(DirMonitor &dirMonitor)
{
  DirMonitor::FileChangeEvent_t event;
  bool rc = dirMonitor.popFileChangeEvent(event);
  while (rc) {
#if defined(__APPLE__) || defined(_WIN32)
    MutexLock l(m_glistLock);
    Time now = Time::now();
    greylist_t::iterator i = m_glist.find(event);
    if (i == m_glist.end()) {
      m_glist[event] = now;
      prepareForUpload(event);
    } else {
      // if file is already in list update it's timestamp
      Time real = i->second;
      i->second = now;
      MTrace(t_upm, trace::Debug, "File was updated in storage for delay backup:  " << event.root+event.fileName);
      if(((now - real) > DiffTime::iso("PT90S"))) {
        prepareForUpload(event);
      }
    }
#endif
#if defined(__linux__)
    // Currently we don't implement greylist for linux
    prepareForUpload(event);
#endif
    rc = dirMonitor.popFileChangeEvent(event);
  }

  // Notify scheduled that we have files to upload
  if (!m_touchedRoots.empty()) {
    m_scheduler.notifyChange(event.fileName);
  }
}

#if defined(__APPLE__) || defined(_WIN32)
void UploadManager::oneSecondTimer()
{
  static int expirationCounter = 0;
  if (++expirationCounter%20 == 0) {
    scheduleExpiredForUpload();
  }
}

void UploadManager::scheduleExpiredForUpload()
{
  MutexLock l(m_glistLock);
  // Check all the list if file timestamp expired - let free it.
  greylist_t::iterator i = m_glist.begin();
  while (i != m_glist.end()) {
    Time now = Time::now();
    Time start = i->second;
    if((now - start) > DiffTime::iso("PT90S")) {
      MTrace(t_upm, trace::Debug, "File expired: " << i->first.root+i->first.fileName);
      prepareForUpload(i->first);
      m_glist.erase(i++);
    } else {
      ++i;
    }
  }

  // Notify scheduled that we have files to upload
  if (!m_touchedRoots.empty()) {
    m_scheduler.notifyChange("");
  }
}
#endif

void UploadManager::prepareForUpload(const DirMonitor::FileChangeEvent_t &event)
{
#if defined(__APPLE__) || defined(_WIN32)
  MTrace(t_upm, trace::Debug, "Changed:" + event.root + " " + event.fileName);
  monitor2Upload_t::iterator uploadIt = m_monitor2Upload.find(event.root);
  if (uploadIt != m_monitor2Upload.end()) {
    // touch the path that is relative to backup (not monitoring) root
    uploadIt->second->touchPath(event.root+event.fileName);
    m_touchedRoots.push_back(uploadIt->second);
  }
#endif
#if defined(__linux__)
  MTrace(t_upm, trace::Debug, "ChangedWD:" << event.root + " " + event.fileName);
  for (uploads_t::iterator uploadIt = m_uploads.begin();
       uploadIt != m_uploads.end(); ++uploadIt) {
    if ((*uploadIt)->touchPathWD(event.root)) {
      m_touchedRoots.push_back(*uploadIt);
      break;
    }
  }
#endif
}

void UploadManager::startUploadAllRoots()
{
  // Initiate backup for all Upload objects
  for (uploads_t::iterator uploadIt = m_uploads.begin();
       uploadIt != m_uploads.end(); ++uploadIt) {
    (*uploadIt)->startUpload();
  }
}

bool UploadManager::startUploadTouchedRoots()
{
  // Start backup only for touched(affected) roots
  for (uploads_t::iterator uploadIt = m_touchedRoots.begin();
       uploadIt != m_touchedRoots.end(); ++uploadIt) {
    if ((*uploadIt)->startUpload())
        *uploadIt = 0;
  }
  m_touchedRoots.remove(0);
  return m_touchedRoots.empty();
}

void UploadManager::handleSnapshotNotification(Upload &upload)
{
  MutexLock l(m_snapshotNotificationLock);

  objseq_t root_hash;
  std::list<dirobj_t> dirobj_lorm;
  bool partialSnapshot = false;

  for (uploads_t::iterator uploadIt = m_uploads.begin();
       uploadIt != m_uploads.end(); ++uploadIt) {

    Upload::LatestSnapshotInfo info = (*uploadIt)->getLatestSnapshotInfo();
    if (info.type == Upload::BTUnknown) {
      partialSnapshot = true;
      continue;
    }
    if (info.type == Upload::BTPartial) {
      partialSnapshot = true;
    }

    // Produce name of backup subroot from its path
    std::string name = (*uploadIt)->getBackupRoot();
#if defined(_WIN32)
    // Remove "\\\\?\\" prefix
    name = name.substr(4);
    // Substitute slashes (otherwise Webclient breaks)
    std::replace(name.begin(), name.end(), '\\', '_');
#else
    // Substitute slashes (otherwise Webclient breaks)
    std::replace(name.begin(), name.end(), '/', '_');
#endif

    //
    // Encode directory meta-data LoM entry and store for later
    // (until upload)
    //
    std::vector<uint8_t> meta;
    ser(meta, uint8_t(0x02)); // 0x02 => directory
    // name
    ser(meta, name);
    // owner user and group
    ser(meta, info.username); // owner user
    ser(meta, info.groupname); // owner group
    // permissions
    ser(meta, uint32_t(06666));
    // mtime and ctime - we do not back up atime
    ser(meta, uint64_t(info.tstamp.to_timet()));
    ser(meta, uint64_t(info.tstamp.to_timet()));

    // Add LoR and LoM data
    dirobj_lorm.push_back(dirobj_t(info.hash, meta,
                                   info.treesize));
  }

  // All objects in directory updated.
  MTrace(t_upm, trace::Debug, "Will encode backup root directory object");

  //
  // Now create a directory object holding our LoR and LoM.
  // - split as necessary to stay within ng_chunk_size.
  //
  while (!dirobj_lorm.empty()) {
    std::vector<uint8_t> object;
    uint64_t treesize;
    size_t n = encodeDirObjs(object, treesize, dirobj_lorm, partialSnapshot);

    // Remove the entries we serialised
    for (size_t i = 0; i < n; i++) {
      dirobj_lorm.pop_front();
    }

    MTrace(t_upm, trace::Debug, "Encoded backup root object of size "
           << object.size());

    //
    // Compute child object hash and add it to the cobj hash list
    //
    const sha256 object_hash(sha256::hash(object));
    root_hash.push_back(object_hash);

    //
    // Fine, we now have an object to upload.
    //
    if (!upload.testObject(m_conn, object_hash)) {
      MTrace(t_upm, trace::Debug, " backup root upload necessary");
      upload.uploadObject(m_conn, object);
    } else {
      MTrace(t_upm, trace::Debug, " backup root object already on servers");
    }
    // Continue until nothing left
  }

  if (!root_hash.empty()) {
    //
    // Fine, upload a new root object.
    //
      
#if defined (__APPLE__)
      MTrace(t_upm, trace::Info, "Uploading new device root "
             << root_hash[0].m_hex << " under device name "
             << m_deviceName<<" For user "<< m_userID);
#else
      MTrace(t_upm, trace::Info, "Uploading new device root "
             << root_hash[0].m_hex << " under device name "
             << m_deviceName);
#endif

    using namespace xml;
    std::string rstr(root_hash[0].m_hex);
    Time tstamp = Time::now();
    char backupType = partialSnapshot?'p':'c'; // partial:complete
    const IDocument &doc = mkDoc
    (Element("backup")
     (Element("tstamp")(CharData<Time>(tstamp))
      & Element("root")(CharData<std::string>(rstr))
      & Element("type")(CharData<char>(backupType))));

#if defined (__APPLE__) || defined(_WIN32)
      ServerConnection::Request req(ServerConnection::mPOST,
                                     "/users/"+str2url(m_userID)+"/devices/" + str2url(m_deviceName)
                                    + "/history");
#else
      ServerConnection::Request req(ServerConnection::mPOST,
                                    "/devices/" + str2url(m_deviceName)
                                    + "/history");
#endif
    
    req.setBasicAuth(m_conn);
    req.setBody(doc);
    
    ServerConnection::Reply rep = m_conn.execute(req);
    if (rep.getCode() == 201) {
      MTrace(t_upm, trace::Debug, "Successfully uploaded new root");
    } else {
      MTrace(t_upm, trace::Warn, "New root error: " << rep.toString());
    }
  }
}

void UploadManager::handleProgressNotification(Upload &upload)
{
  if (m_progress) {
    (*m_progress)(*this);
  }
}

const std::vector<Upload::threadstatus_t>& UploadManager::getProgressInfo()
{
  m_progressInfo.clear();
  for (uploads_t::iterator uploadIt = m_uploads.begin();
       uploadIt != m_uploads.end(); ++uploadIt) {
    std::vector<Upload::threadstatus_t> progressItem = (*uploadIt)->getProgressInfo();
    m_progressInfo.insert(m_progressInfo.end(), progressItem.begin(), progressItem.end());
  }
  return m_progressInfo;
}

UploadManager::Scheduler::Scheduler(UploadManager &p)
  : m_parent(p)
  , m_timeout(Time::END_OF_TIME)
  , m_runmode(TRNormal)
{
  start(); // Immediately start scheduling thread
}

UploadManager::Scheduler::~Scheduler()
{
  // Signal termination
  m_runmode = TRQuit;
  m_signal.increment();
  // Wait for exit
  join_nothrow();
}

void UploadManager::Scheduler::notifyChange(const std::string &f)
{
  // Set new timeout (if not set already) and notify thread
  MutexLock l(m_tout_lock);

  if (m_timeout == Time::END_OF_TIME) {
    m_timeout = Time::now() + DiffTime::iso("PT1S");
    MTrace(t_upm, trace::Debug, "Sheduled CDP backup at: " << m_timeout 
           << " due to " << f <<" ||  time:" << Time::now());
    m_signal.increment();
  } else {
    MTrace(t_upm, trace::Info, "CDP backup already scheduled at: " << m_timeout);
    
  }
}

void UploadManager::Scheduler::run()
{
  while (m_runmode == TRNormal) {
    // Wait for change signal
    Time tout;
    { MutexLock l(m_tout_lock);
      tout = m_timeout;
    }
    bool timeout = false;
#if defined(__unix__) || defined(_WIN32)
    timeout = !m_signal.decrement(tout);
#endif
#if defined(__APPLE__) 
    m_signal.decrement();
    if (m_runmode == TRNormal) {
      sleep(1);
      if (m_runmode == TRNormal) {
        timeout = true;
      }
    }
#endif
    
    if (timeout) {
      MutexLock l(m_tout_lock);
      // Let's schedule a backup!
      if (m_parent.startUploadTouchedRoots()) {
        MTrace(t_upm, trace::Debug, "Successfully initiated backup from CDP");
        // Success. No need to schedule anything else
        m_timeout = Time::END_OF_TIME;
      } else {
        MTrace(t_upm, trace::Debug, "Backup initiation from CDP failed - will retry");
        // Failed. Re-schedule in five seconds.
        m_timeout = Time::now() + DiffTime::iso("PT5S");
      }
      continue;
    }

  }
}

#if defined(__unix__) || defined(__APPLE__)
# include "upload_posix.cc"
#endif

#if defined(_WIN32)
# include "upload_win32.cc"
#endif

