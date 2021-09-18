//
/// Backup engine
//
// $Id: engine.hh,v 1.9 2013/10/06 11:48:08 vg Exp $
//

#ifndef SERVERBACKUP_ENGINE_HH
#define SERVERBACKUP_ENGINE_HH

#include "config.hh"
#include "common/thread.hh"
#include "common/semaphore.hh"
#include "backup/upload.hh"
#include "backup/dir_monitor.hh"

#include <set>

class Engine {
public:
  Engine(Config &config);

  //! Supported engine commands
  enum cmd {
    CBACKUP, /// start backup
    CSTOP    /// stop current operation
  };

  //! Engine command submission
  Engine &submit(cmd);

  //! Same as above, only with plain English string result
  std::string status_en();

private:
  //! Our configuration object
  Config &m_cfg;

  //! Utility routine for logging engine status - in case logging to
  //! server fails, it will log a warning locally.
  void logStatus(const std::string &);

  //! A backup worker thread.
  class Backup : private Thread {
  public:
    Backup(Engine &);
    ~Backup();

#if defined(__linux__)
    /// If a full backup has been made already, a set of watch
    /// descriptors can be supplied to the backup logic. This will
    /// cause us to run a much faster backup where we only consider
    /// the directories mentioned in the set of watch descriptors.
    void startPartial(std::set<int> &);
#endif

    /// Call this method to start a backup
    void startBackup();

    /// Call this method to stop a backup
    void stopBackup();

    /// Call this to enquire if backup worker is busy
    bool isBusy() const;

  protected:
    /// Our backup worker thread
    void run();

    /// callback when from Upload instance when it finished working
    void handleUploadCompletion(Upload &upload);

    /// callback from dirmonitor when file changes 
    void handleChangeNotification(DirMonitor &dirMonitor);

  private:
    /// Our engine parent
    Engine &m_parent;

    /// Semaphore used to wait for job start
    Semaphore m_ssem;

    /// Command passed to thread
    enum {
      CNONE,
      CSTART,
      CSTOP,
      CSHUTDOWN
    } m_cmd;

    /// Thread state
    bool m_busy;

    /// Our filter method - it is called by the uploader and should
    /// return true if a file system object is to be included, false
    /// if it is to be excluded
    bool filter(const std::string &);

    /// Call this routine to add all mounted file systems of types in
    /// the given skiptypes set to our m_skip_filesystems set
    void blacklistFSTypes(const std::set<std::string>&);

    /// Mutex on skip list
    Mutex m_skip_lock;
    /// Our blacklist of filesystems to skip
    std::set<std::string> m_skip_filesystems;

    /// Reference to Upload instance in backup worker
    Upload *m_pUpload;

    /// Reference to upload completion semaphore in backup worker
    Semaphore * m_pWaitSem;
  } m_backup;


  //! Our scheduler thread. It initiates timed backups and keeps track
  //of file system notification events.
  class Scheduler : private Thread {
  public:
    Scheduler(Engine &);
    ~Scheduler();

    /// When the upload engine sees a changed file, we're called. We
    /// will schedule a CDP initiated backup if not already done.
    void scheduleCDPBackup(const std::string &path);

  protected:
    /// Our scheduler worker thread
    void run();

  private:
    /// Protect against copying
    Scheduler(const Scheduler&);

    /// Protect against assignment
    Scheduler &operator=(const Scheduler&);

    /// Our engine parent
    Engine &m_parent;

    /// Set when we should shut down
    bool m_shutdown;

#if defined(__unix__)
    /// We have a wake pipe for waking the scheduling thread from
    /// sleep
    int m_wakepipe[2];
    /// Call this to wake the sleeping thread
    void wakeThread();
#endif

    /// If set, this is the time where we want to run a CDP initiated
    /// backup next
    Optional<Time> m_cdp_backup_init;

  } m_scheduler;

};


#endif
