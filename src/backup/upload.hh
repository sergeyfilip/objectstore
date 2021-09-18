///
/// Upload utility routines
//
// $Id: upload.hh,v 1.43 2013/10/17 14:29:20 sf Exp $
//

#ifndef BACKUP_UPLOAD_HH
#define BACKUP_UPLOAD_HH

#include "client/serverconnection.hh"
#include "common/hash.hh"
#include "common/partial.hh"
#include "common/semaphore.hh"
#include "common/thread.hh"
#include "common/mutex.hh"
#include "metatree.hh"
#include "utils.hh"
#include "dir_monitor.hh"

#if defined(__unix__) || defined(__APPLE__)
# include <sys/types.h>
# include <sys/stat.h>
# include <unistd.h>
#endif

#if defined(__linux__)
#include <sys/vfs.h>
#endif

#if defined(__APPLE__)
#include <sys/param.h>
#include <sys/mount.h>
#include <CoreFoundation/CFRunLoop.h>
#include <CoreServices/CoreServices.h>
#endif

#if defined(_WIN32)
# include <windows.h>
#endif

#include <set>
#include <map>
#include <stack>
#include <vector>
#include <list>

struct sizehash_t {
  sizehash_t() : size(0) { }
  sizehash_t(uint64_t s, const objseq_t &h)
    : size(s), hash(h) { }
  uint64_t size;
  objseq_t hash;
};

/// Download object
void fetchObject(ServerConnection &conn, const sha256 &hash,
                 std::vector<uint8_t> &obj);

/// The Upload class will perform a backup of a directory
/// hierarchy. Various attributes can be set on the object in order to
/// alter the way backup is performed - for example, exclude filtering
/// can be added.
class Upload {
public:
  /// We must be given a FSCache reference since we need to update it
  /// during our working. We must be the only user of the cache.
  Upload(FSCache &, ServerConnection &,
         const std::string &device_name, const std::string &backup_root);

  /// We must destroy allocated structures
  ~Upload();

  /// Set number of workers to use.
  Upload &setWorkers(size_t n);

  /// Include a filter for exclude filtering. This closure is applied
  /// on every file system object we encounter, and if it returns
  /// false the object is skipped.
  //
  /// Note; the filter closure will be call concurrently by several
  /// worker threads. It must be re-entrant.
  Upload &setFilter(const BindF1Base<bool,const std::string&> &);

  /// For completion notification - when a backup process completes
  //  (successfully or not), this callback is called.
  //
  Upload &setCompletionNotification(const BindF1Base<void,Upload&>&);
  
  /// Called when root chunk was uploaded to server
  Upload &setSnapshotNotification(const BindF1Base<void,Upload&>&);

  /// For progress feedback we send our progress structure. It
  /// contains the current status of each of the worker threads and
  /// may optionally contain a progress (from 0 to 1) if known.
  struct threadstatus_t {
    threadstatus_t() : state(OSIdle) { }
    /// For each worker thread we have a thread status
    enum objstat { OSIdle, OSScanning, OSUploading, OSFinishing } state;
    /// If the thread is working on a specific object, this is the
    /// name of the object (otherwise the string is empty)
    std::string object;
    /// We may have a progress on the actual object we're processing
    /// (from 0 to 1)
    Optional<double> object_progress;
  };
  /// Get status of backup working threads
  std::vector<threadstatus_t> getProgressInfo();

  /// Progress feedback; when we traverse the tree, we can log
  /// progress information to a closure passed here. Beware that the
  /// callback will be called from the worker threads.
  Upload &setProgressLog(const BindF1Base<void,Upload&>&);

  /// Stop progress feedback
  Upload &unsetProgressLog();

  /// Spawn upload threads and start backing up from the previously given root.
  bool startUpload();

  /// Cancel upload
  void cancelUpload();

  /// Is a cancel in progress? This is true from the moment a worker
  /// thread calls cancelUpload() until the point where all our work
  /// items have been removed from the work queue.
  bool cancelInProgress();

  /// Is a backup currently in progress?
  bool isWorking();
  
  /// Get path of backup root
  const std::string &getBackupRoot();

  enum BackupType { BTUnknown, BTComplete, BTPartial };

  struct LatestSnapshotInfo {
    LatestSnapshotInfo():type(BTUnknown), treesize(0) {}    
    Time tstamp;
    BackupType type;
    objseq_t hash;
    uint64_t treesize;
    std::string username;
    std::string groupname;
  };
  LatestSnapshotInfo getLatestSnapshotInfo();
  
  /// Touch the path relative to the root
  bool touchPath(const std::string &path);
#if defined(__linux__)
  /// Touch the path that leads to watched folder defined by inotify watch descriptor
  bool touchPathWD(int wd);
  Upload &setAddWatchDelegate(const BindF1Base<int,const std::string&> &);
#endif

 private:
  /// Our FSCache
  FSCache &m_cache;

  /// Our ServerConnection
  ServerConnection &m_conn;

  /// Our exclude filter
  const BindF1Base<bool,const std::string&> *m_filter;

  /// Our progress logger
  const BindF1Base<void,Upload&> *m_progress;

  /// Our completion notifier
  const BindF1Base<void,Upload&> *m_completion_notify;

  /// Set to false on every backup start, set to true when the last
  /// worker calls the completion notify routine. Ensures we call
  /// completion notifier precisely once for each backup
  bool m_completion_notify_done;

  /// When a worker thread completes processing, it will call this
  /// method. If the thread is the last thread to complete working and
  /// there are no more jobs on the work queue, this method will call
  /// the completion notify callback, if any.
  void checkForCompletion();
  
  /// Our snapshot notifier (when root chunk uploaded)
  const BindF1Base<void,Upload&> *m_snapshot_notify;

  /// Our processing threads will tell us about their status
  void setStatus(size_t ndx, threadstatus_t::objstat, const std::string &,
                 const Optional<double> &);

  /// Lock and data structure for thread status tracking
  Mutex m_procstate_lock;
  std::vector<threadstatus_t> m_procstate;

  /// Same as isWorking - but isWorking() will take the
  /// m_workers_lock, and for internal logic we may need to hold that
  /// lock already - therefore, this version of isWorking does not
  /// take the m_workers_lock - it requires the caller to already hold
  /// that lock.
  bool isWorking_nowl();

  ///
  /// We use one or more processor threads to scan and upload
  /// directories.
  ///
  class Processor : public Thread {
  public:
    /// Instantiate with Upload parent object and processor/thread
    /// instance id [0-n]
    Processor(Upload &p, size_t);
    Processor(const Processor &);

#if defined(__unix__) || defined(__APPLE__)
    /// Our dirstate_t object can call this member to look up a
    /// username from a uid.
    std::string username(uid_t);

    /// Our dirstate_t object can call this member to look up a
    /// groupname from a gid.
    std::string groupname(gid_t);
#endif

    /// For progress updates - called by our dirstate_t - will report
    /// back to the Upload parent
    void setStatus(threadstatus_t::objstat, const std::string &,
		   const Optional<double> & = Optional<double>());

    /// Reference the processor connection - used by dirstate_t when
    /// uploading...
    ServerConnection &refConn();

    /// Reference the Upload parent. Used by dirstate_t to add work
    /// items for example.
    Upload &refUpload();

    /// Report whether this processor is executing a work queue item
    /// or not
    bool isBusy() const;

  protected:
    void run();

  private:
    /// Protect against assignment
    Processor &operator=(const Processor&);
    /// We must have a reference to our parent because we need to
    /// consume and add work items.
    Upload &m_parent;
    /// We remember our instance id
    const size_t m_instance_id;

    /// We are either executing something or we're not
    bool m_is_busy;

    /// We have a connection
    ServerConnection m_conn;

#if defined(__unix__) || defined(__APPLE__)
    struct l_uid_t {
      l_uid_t(uid_t u, const std::string &n)
        : uid(u), name(n) { };
      l_uid_t() : uid(0) { }
      uid_t uid;
      std::string name;
    };

    /// UID cache
    Optional<l_uid_t> m_last_uid;
    Mutex m_uid_lock;

    struct l_gid_t {
      l_gid_t(gid_t u, const std::string &n)
        : gid(u), name(n) { };
      l_gid_t() : gid(0) { }
      gid_t gid;
      std::string name;
    };

    /// GID cache
    Optional<l_gid_t> m_last_gid;
    Mutex m_gid_lock;
#endif
  };

  /// A directory state
  struct wnode_t;
  struct dirstate_t {
    /// Initialise a dirstate for a new directory to process
    dirstate_t(const std::string &name, dirstate_t *parent,
               Upload::wnode_t &w, const CObject & = CObject());

    ~dirstate_t();

    /// For sorting before we encode the object - this sorting should
    /// just maintain ordering of children to a directory to ensure
    /// that unchanged directories de-duplicate (something we are not
    /// guaranteed otherwise since child directory processing is
    /// parallel).
    static bool sortkey(dirstate_t *a, dirstate_t *b) {
      return a->name < b->name;
    }

    /// When an entry is inserted in the work queue, we need to know
    /// its depth. This method computes the depth of the entry. Cache
    /// the result from this function if you need it multiple times,
    /// as it does not cache the result by itself (and therefore runs
    /// in O(d) time where d is the depth (distance from root) of the
    /// current node in the tree).
    size_t getDepth() const;

    /// Our absolute path name
    std::string absPath() const;

    /// This method will execute scan(), and if necessary, schedule an
    /// execution of process_upload().
    void process_scan(Processor&);

    /// This method will execute upload(). Then, it will update the
    /// parent dirstate_t (if any) to reflect the completeness of this
    /// child. It may then schedule the execution of process_upload()
    /// for the parent. Finally, if no parent exists, this is the last
    /// object to upload, and this method will invoke the complete()
    /// method in the calling Processor object
    void process_upload(Processor&);

    /// Our name
    std::string name;

    /// This is our parent directory (or null if we are the root)
    dirstate_t *parent;

    /// This is our associated watch object
    wnode_t &watch;

    /// This is the list of child directories that have not yet
    /// completed processing
    std::set<dirstate_t*> incomplete_children;

    /// This is the list of child directories that have completed
    /// processing
    std::vector<dirstate_t*> complete_children;

    /// We keep our meta-tree representation here. It is initialised
    /// by the parent and passed to us in our constructor, and it is
    /// updated by ourselves on upload. For all entries except the
    /// root (so for all entries that have a parent pointer) the cobj
    /// will have a m_id with a proper device id and inode number
    CObject cobj;

    /// Additional meta data needed for directory entry encoding
    /// during upload
#if defined(__unix__) || defined(__APPLE__)
    uid_t meta_uid;
    gid_t meta_gid;
    uint32_t meta_mode;
#endif
#if defined(_WIN32)
    std::string meta_owner;
    uint32_t meta_fattr;
    std::string meta_sddl;
#endif

    /// The scan of a directory. This method will add all
    /// sub-directories to our incomplete_children table and add them
    /// as work items. We need a reference to the Upload object in
    /// order to be able to add the work items, we need to run filters
    /// and we need to access the FS cache.
    //
    /// This method will return false if the object is a has no
    /// incomplete_children and therefore should be uploaded
    /// immediately after the scan. It will return true if the object
    /// has incomplete_children and therefore must not be uploaded
    /// directly (it will be uploaded later when a leaf ancestor
    /// object does back-tracking)
    bool scan(Processor &);

    /// If we have no more incomplete_children, then the worker will
    /// call this method. This will cause us to process all directory
    /// child objects that need processing (for example changed file
    /// uploads). The worker has its own server connection - but aside
    /// from that we use the Upload object FS cache.
    void upload(Processor &);
  };

#if defined(__linux__)
  /// On Linux we need to map from inotify watch descriptor into
  /// wnode_t.
  typedef std::map<int, wnode_t*> inotify2wnode_t;
  inotify2wnode_t m_inotify2wnode;
#endif

  /// This is our completely generalised priority queue for work
  /// during upload processing.
  //
  /// Work items on this queue is executed in order, with the highest
  /// keys executing first.
  //
  /// During normal upload processing, we add the processing of a
  /// dirstate_t to the queue with the key set to the absolute depth
  /// of the dirstate_t object. This will give us a depth-first-like
  /// upload behaviour.
  //
  /// When performing a partial upload, we add the necessary upload
  /// jobs to the queue with a much higher priority - we simply add an
  /// integer constant higher than any directory depth can be to
  /// facilitate this.
  //
  /// After execution, the de-queued item is deleted by the
  /// Processor::run method that executes the item.
  //
  Mutex m_workqueue_lock;
  std::multimap<size_t,BindF1Base<void,Processor&>*> m_workqueue;

  /// We use a semaphore for waiting on work queue items
  Semaphore m_workqueue_sem;

  /// Set to false when a backup is initiated - set true when a worker
  /// thread exits due to a cancelled backup. The variable is not
  /// protected by locks
  bool m_backup_cancelled;

  /// When we start scanning a directory, we remove it from the todo
  /// stack. We then add all its child directories to the todo
  /// stack. Finally, we check the directory for upload-readiness.
  //
  /// A directory is ready for upload if it has no
  /// incomplete_children.
  //
  /// If a directory is upload-ready, we process all its files and we
  /// already have the complete_children list for child directory
  /// names and multirefs. When the directory is uploaded, we must
  /// follow its parent pointer and move the directory from the
  /// incomplete_children list in the parent to the complete_children
  /// list in the parent. After that, the upload-readiness check must
  /// be performed on the parent as well.
  //
  /// When a directory is uploaded, we want to delete its
  /// complete_children dirstates - but since we also need to
  /// recursively check for parent upload readiness, the cleanup gets
  /// slightly messy.  Strictly speaking, we could call
  /// parent->checkUploadReady() as the last statement in
  /// checkUploadReady() and therefore still function even though the
  /// parent will delete us.
  //
  /// We need a mutex to protect the m_dir_todo stack.
  //
  /// We also need a mutex to proctect the *_children lists in the
  /// dirstate_t objects. However, we cannot use a mutex in every
  /// object (mutexes cost), so we could simply use the same mutex as
  /// we use for the todo stack.
  Mutex m_dirstate_lock;

  /// Our work queue works with closures taking one Processor&
  /// argument. This typedef simply helps our code...
  typedef BindF1Base<void,Processor&> workitem_t;

  /// Called by the processor to request a work item. Returns 0 if no
  /// more work is to be had and the processor should exit.
  workitem_t *getWorkItem();

  /// Called by the processor to add a directory as a work item.
  void addWorkItem(size_t, workitem_t*);

  /// Called by the processor when the root directory has been
  /// uploaded. This will cause getWorkItem() to start returning 0.
  void stopWorkQueue();

  /// For completion of a backup set, the completing Processor object
  /// will invoke this method to have a new root item uploaded and to
  /// have the completion notifier callback - if any - called
  void complete(ServerConnection&, const objseq_t&,
                BackupType backupType = BTComplete);

  /// Utility routine for server request processing. Will indefinitely
  /// retry operations on server connection errors or on HTTP 500
  /// errors. Will respect backup cancellation by throwing an
  /// exception.
  ServerConnection::Reply execute(ServerConnection &,
                                  ServerConnection::Request &);

public:
  /// Called by upload processor logic to test if an object exists on
  /// the server (a HEAD request).
  bool testObject(ServerConnection &, const sha256 &);

  /// Called by upload processor to upload an object. Note; you
  /// *should* use testObject() to test for existence before calling
  /// this method.
  void uploadObject(ServerConnection &, const std::vector<uint8_t> &);
private:
  /// Our set of workers. This is resized on startUpload().
  Mutex m_workers_lock;
  std::list<Processor> m_workers;

  /// Number of workers to spawn
  size_t m_nworkers;

  /// When directories are added to a watch list, we must memorise
  /// them so that we can efficiently run a backup traversing only the
  /// on-disk directories under which things have changed.
  struct wnode_t {
    /// Construct with parent (or 0 if root) and name
    wnode_t(wnode_t *, const std::string &);
    ~wnode_t();

    /// If our notification fails and we don't know what has been
    /// touched, we call this method on the root wnode (m_wroot) to
    /// mark the full tree as touched.
    void touchAll();

    /// The CDP monitor will call this method to have the wnode_t
    /// structure traversed and mark the specified object as touched,
    /// and all its parents as having a child object that is touched.
    void markTouched();

    /// This method will move the 'touched' status into the 'queued'
    /// status and reset the 'touched' status. It is called before
    /// initiating a backup so that the backup routines can focus on
    /// backing up anything that was 'queued' for backup, while the
    /// CDP change monitor can still mark objects as 'touched'
    /// regardless of the progress of the current backup.
    void queueTouched();

    /// For change notification (CDP), we track whether this object
    /// was touched or whether it has children that were touched.
    //
    /// When setting this (using markTouched()) we maintain the
    /// invariant that all ascendents of an object that has
    /// m_cdp_touched set, will also have m_cdp_touched set.
    bool m_cdp_touched;

    /// Before starting a backup job, we transfer the touched state
    /// into the queued state variable - then, if changes occur while
    /// we are processing, the _touched variable can safely be updated
    /// without affecting the _queued variablee. The _queued variable
    /// are reset as we process the backup.
    bool m_cdp_queued;

#if defined(__linux__)
    /// On linux we remember our watch descriptor
    int m_wd;
#endif

    /// We have a parent or 0
    wnode_t *m_parent;

    /// The watched object has a name
    std::string name;

    /// The watched object has children
    std::vector<wnode_t*> children;

    /// Absolute path name
    std::string absName() const;

    /// Insert path under wnode_t. The nofs is our offset into the
    /// name string. This method returns a reference to the wnode_t
    /// inserted.
    wnode_t &insert(const std::string &name, size_t nofs = 0);

    /// Locate (and optionally create) child object for given child
    /// name.
    wnode_t &getChild(const std::string &name);

    /// Diagnostic - trace the tree
    void tracetree(size_t indent);
  private:
    wnode_t(const wnode_t&);
    wnode_t &operator=(const wnode_t&);
  };

  /// This is our device name on the back end - we need it when
  /// uploading a new backup root
  const std::string m_device_name;

  /// This is the (usually absolute) path to the root of the backup
  /// set
  const std::string m_backup_root;

  LatestSnapshotInfo m_latestSnapshotInfo;
  Mutex m_latestSnapshotInfoLock;

  /// This is the root of the watched directory tree
  Mutex m_wroot_lock;
  wnode_t m_wroot;

#if defined(__linux__)
  /// In order to monitor directories changes in linux we have to scan recursively
  /// and add every one of them to inotify instance.
  const BindF1Base<int,const std::string&> *m_addWatchDelegate;
#endif
};

/// Upload Manager class aggregates multiple Upload instances
/// that are supposed to backup different base paths (drives/volumes).
/// Every instanse will do backup and create corresponding root chunk for given base path.
/// Once root chunk is created in any Upload object it will be reported to Upload Manager
/// that in turn will create meta root chunk - i.e. combination of root chunk from Upload objects
class UploadManager {
public: //UploadManager
  /// Arguments are stored and used for creation of Upload instances
    
///  #if defined (__APPLE__)
    UploadManager(FSCache &cache, ServerConnection &conn, const std::string &deviceID, const std::string &userID);
////  #else
    UploadManager(FSCache &cache, ServerConnection &conn,
                  const std::string &deviceName);
///  #endif

  /// We must destroy allocated structures
  ~UploadManager();

  /// Create new Upload instance for defined path/root
  /// The path should include slash "/" or "\" at the end
  void addUploadRoot(const std::string &fullPath);

  /// Create path monitor and associate it with already added Upload instance.
  /// I.e. path to be monitored should be the child of one of the upload root paths
  void addPathMonitor(const std::string &fullPath);

  /// Initiate backup at all Upload instances
  void startUploadAllRoots();

  /// Initiate backup at Upload instances where changes were detected
  bool startUploadTouchedRoots();

  /// Include a filter for exclude filtering. This closure is applied
  /// on every file system object we encounter, and if it returns
  /// false the object is skipped.
  //
  /// Note; the filter closure will be call concurrently by several
  /// worker threads. It must be re-entrant.
  UploadManager &setFilter(const BindF1Base<bool,const std::string&> &);

  /// Progress feedback; when we traverse the tree, we can log
  /// progress information to a closure passed here. Beware that the
  /// callback will be called from the worker threads.
  UploadManager &setProgressLog(const BindF1Base<void,UploadManager&>&);
  
  /// Get status of backup working threads for all Upload instances
  const std::vector<Upload::threadstatus_t>& getProgressInfo();

  /// Is a backup currently in progress?
  bool isWorking();


#if defined(__APPLE__) || defined(_WIN32) 
  /// This routine is externally called every second
  void oneSecondTimer();
  /// Called by external timer to initiate upload of scheduled files
  void scheduleExpiredForUpload();
#endif

protected:
  /// Called by DirMonitor if any folder under monitored path has changed
  void handleChangeNotification(DirMonitor &dirMonitor);
  
  /// Called when any of Upload instances prepared and uploaded its root chunk
  void handleSnapshotNotification(Upload &upload);

  /// Called when backup status changes at any of Upload instances
  void handleProgressNotification(Upload &upload);

  /// Helper routine
  void prepareForUpload(const DirMonitor::FileChangeEvent_t &event);

private:  //UploadManager
  /// Our FSCache
  FSCache &m_cache;
  
  /// Our ServerConnection
  ServerConnection &m_conn;
  
  /// This is our device name on the back end - we need it when
  /// uploading a new backup root
  const std::string m_deviceName;
    
///#if defined(__APPLE__) 
    ///we need this for history transactions
    const std::string m_userID;
///#endif
    
  /// Container of Upload instances for different roots/volumes
  typedef std::list<Upload*> uploads_t;
  uploads_t m_uploads;

  /// Association to quicly find corresponding Upload instance
  /// for file changed at monitored path
  typedef std::map<std::string, Upload*> monitor2Upload_t;
  monitor2Upload_t m_monitor2Upload;

  /// Serialise access to handleSnapshotNotification()
  Mutex m_snapshotNotificationLock;

  /// Directory changes monitor, calls handleChangeNotification() on file/folder change
  DirMonitor m_dirMonitor;

  /// Our exclude filter
  const BindF1Base<bool,const std::string&> *m_filter;

  /// Our progress logger
  const BindF1Base<void,UploadManager&> *m_progress;
  
  /// We elicit progress info from every Upload instance and store it in aggregated form
  std::vector<Upload::threadstatus_t> m_progressInfo;

  /// List of roots for which changes were detected
  uploads_t m_touchedRoots;

#if defined(__APPLE__) || defined(_WIN32)
  /// Grey list container and mutex
  typedef std::map<DirMonitor::FileChangeEvent_t,Time> greylist_t;
  greylist_t m_glist;
  Mutex m_glistLock;
#endif

  /// For change notifications, we want to delay our actions
  /// slightly. This scheduler will accept notifications and it will
  /// attempt initiating a backup shortly thereafter.
  class Scheduler : public Thread {
  public:
    Scheduler(UploadManager &);
    ~Scheduler();
  
    /// Called by the change notification handler on file change
    /// notification. This method will schedule a backup initiation
    /// some seconds from 'now' and attempt to start a backup then.
    //
    /// If a backup is already scheduled, it will do nothing. If the
    /// backup initiation fails (because a backup is already running)
    /// then it will re-schedule.
    void notifyChange(const std::string&);
  
  protected:
    void run();

  private:
    UploadManager &m_parent;
  
    /// Mutex that protects m_timeout
    Mutex m_tout_lock;
    
    /// Either END_OF_TIME or the time when the backup should be
    /// scheduled.
    Time m_timeout;
  
    /// Thread event signal
    Semaphore m_signal;
  
    /// Thread run mode
    enum { TRNormal, TRQuit } m_runmode;
  
  } m_scheduler;

}; //UploadManager

#endif
