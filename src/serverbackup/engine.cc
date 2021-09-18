//
/// Backup engine implementation
//
// $Id: engine.cc,v 1.25 2013/10/17 14:29:20 sf Exp $
//

#include "engine.hh"
#include "common/error.hh"
#include "common/trace.hh"
#include "common/string.hh"
#include "common/scopeguard.hh"
#include "xml/xmlio.hh"
#include "client/serverconnection.hh"
#include "backup/upload.hh"

#include <sstream>
#include <sys/vfs.h>
#include <stdio.h>
#include <mntent.h>

#include <poll.h>
#include <unistd.h>
#include <fcntl.h>

namespace {
  //! Tracer for engine operations
  trace::Path t_eng("/kservd/engine");

  //! Tracer for cdp related operations
  trace::Path t_cdp("/kservd/cdp");
}

Engine::Engine(Config &conf)
  : m_cfg(conf)
  , m_backup(*this)
  , m_scheduler(*this)
{
}

Engine &Engine::submit(cmd c)
{
  switch (c) {
  case CBACKUP:
    m_backup.startBackup();
    break;
  case CSTOP:
    m_backup.stopBackup();
    break;
  }
  return *this;
}

std::string Engine::status_en()
{
  if (m_backup.isBusy())
    return "Backup running";
  return "Idle";
}

void Engine::logStatus(const std::string &str)
{
  // We want to have a server connection
  std::string apihost, token, pass, devname, cachename;
  { // We access config data
    MutexLock cfglock(m_cfg.m_lock);
    apihost = m_cfg.m_apihost;
    token = m_cfg.m_token.get();
    pass = m_cfg.m_password.get();
    devname = m_cfg.m_device.get();
  }
  ServerConnection conn(apihost, 443, true);
  conn.setDefaultBasicAuth(token, pass);

  // Now log status
  MTrace(t_eng, trace::Debug, "Logging to server: " << str);
  try {
    // POST status
    ServerConnection::Request req(ServerConnection::mPOST, "/devices/"
                                  + str2url(devname) + "/status");
    req.setBasicAuth(conn);

    using namespace xml;
    std::string cstr(str);
    const IDocument &doc = mkDoc(Element("status")(CharData<std::string>(cstr)));
    req.setBody(doc);

    ServerConnection::Reply rep = conn.execute(req);
    if (rep.getCode() != 201) {
      MTrace(t_eng, trace::Warn, "Log to server response: " << rep.toString());
      return;
    }
  } catch (error &e) {
    // Log a warning locally
    MTrace(t_eng, trace::Warn, "Log to server failed: "
           << e.toString());
  }
}

Engine::Backup::Backup(Engine &p)
  : m_parent(p)
  , m_cmd(CNONE)
  , m_busy(false)
  , m_pUpload(0)
  , m_pWaitSem(0)
{
}

Engine::Backup::~Backup()
{
  // Tell the thread to stop, then exit
  if (m_cmd != CNONE) {
    m_cmd = CSHUTDOWN;
    m_ssem.increment();
    join_nothrow();
  }
}

void Engine::Backup::startBackup()
{
  if (m_cmd == CNONE) {
    // Start the worker thread - it should wait for something to do
    start();
  }
  m_cmd = CSTART;
  m_ssem.increment();
}

void Engine::Backup::stopBackup()
{
  m_cmd = CSTOP;
}

bool Engine::Backup::isBusy() const
{
  return m_busy;
}

void Engine::Backup::run()
{
    m_parent.m_cfg.m_device = "f";
    m_parent.m_cfg.m_device_id = "f";
  // Let's connect to the server
  std::string apihost, token, pass, devname, cachename,device_id, id;
  size_t nworkers;
  { // We access config data
    MutexLock cfglock(m_parent.m_cfg.m_lock);
    // We have some absolute excludes that we will not run
    // without. Make sure those are there
#if defined(__linux__)
    m_parent.m_cfg.m_skiptypes.insert("tmpfs");
    m_parent.m_cfg.m_skiptypes.insert("proc");
    m_parent.m_cfg.m_skiptypes.insert("sysfs");
    m_parent.m_cfg.m_skiptypes.insert("devpts");
    m_parent.m_cfg.m_skiptypes.insert("rpc_pipefs");
#endif
    // Get config
    apihost = m_parent.m_cfg.m_apihost;
    token = m_parent.m_cfg.m_token.get();
    pass = m_parent.m_cfg.m_password.get();
    devname = m_parent.m_cfg.m_device.get();
  //  device_id = m_parent.m_cfg.m_device_id.get();
  //  id = m_parent.m_cfg.m_user_id.get();
    cachename = m_parent.m_cfg.m_cachename;
    nworkers = m_parent.m_cfg.m_workers;
    
  }
  ServerConnection conn(apihost, 443, true);
  conn.setDefaultBasicAuth(token, pass);

  // Initialise cache
  FSCache cache(cachename);

  DirMonitor dirMonitor;
  dirMonitor.setChangeNotification(papply(this, &Engine::Backup::handleChangeNotification));

  // Set up upload
  Upload* upload;
 if(apihost.find("ws.keepit.com") != std::string::npos) {
 MTrace(t_eng, trace::Info, "Log to old server and want to start upload: " );
 upload = new Upload(cache, conn, devname, "/");
} 
 else
 // upload = new Upload(cache, conn, device_id, id);
  upload = new Upload(cache, conn, devname, "/");
  MTrace(t_eng, trace::Info, "Log to new server and want to start upload: " );
  m_pUpload = upload;

  // Set number of workers
  upload->setWorkers(nworkers);

  // Set up exclude filtering
  upload->setFilter(papply(this, &Engine::Backup::filter));

  upload->setCompletionNotification(papply(this, &Engine::Backup::handleUploadCompletion));

  upload->setAddWatchDelegate(papply(&dirMonitor, &DirMonitor::addDir));

  // Wait until we're told to start
  while (true) {
    // Close the db if it is open... it will automatically re-open.
    cache.quiesce();

    MTrace(t_eng, trace::Info, "Backup thread awaiting command");
    m_busy = false;
    m_ssem.decrement();

    // Got command, now go!
    m_busy = true;
    if (m_cmd == CSHUTDOWN) {
      MTrace(t_eng, trace::Info, "Backup thread exiting.");
      return;
    }

    MTrace(t_eng, trace::Info, "Backup starting...");

    try {
      // Initialise our blacklist for the filtering
      { MutexLock cfglock(m_parent.m_cfg.m_lock);
        MutexLock l(m_skip_lock);
        // Skip blacklisted file system types
        blacklistFSTypes(m_parent.m_cfg.m_skiptypes);
        // Skip specified file systems
        for (std::set<std::string>::const_iterator i = m_skip_filesystems.begin();
             i != m_skip_filesystems.end(); ++i)
          MTrace(t_eng, trace::Debug, "Skip: \"" << *i << "\"");
        m_skip_filesystems.insert(m_parent.m_cfg.m_skipdirs.begin(),
                                  m_parent.m_cfg.m_skipdirs.end());
        for (std::set<std::string>::const_iterator i = m_skip_filesystems.begin();
             i != m_skip_filesystems.end(); ++i)
          MTrace(t_eng, trace::Debug, "Skip: \"" << *i << "\"");
        // Skip the cache too
        m_skip_filesystems.insert(cachename);
        for (std::set<std::string>::const_iterator i = m_skip_filesystems.begin();
             i != m_skip_filesystems.end(); ++i)
          MTrace(t_eng, trace::Debug, "Skip: \"" << *i << "\"");
      }

      m_parent.logStatus("Backup started");
      MTrace(t_eng, trace::Info, "Backup started");

      // Set up wait condition
      Semaphore wait;
      m_pWaitSem = &wait;

      // Perform full backup
      Time tstamp(Time::now());
      // Start backup
      bool res = upload->startUpload();
      MAssert(res, "performUpload failed starting upload");
      // Wait for completion
      wait.decrement();
      m_pWaitSem = 0;

      m_parent.logStatus("Backup completed");
      MTrace(t_eng, trace::Info, "Backup completed");

    } catch (error &e) {
      MTrace(t_eng, trace::Warn, "Backup failed: " << e.toString());

      m_parent.logStatus("Backup failed: " + e.toString());
    }
    // We're done
  }

}

void Engine::Backup::handleUploadCompletion(Upload &upload)
{
  if (m_pWaitSem) {
    m_pWaitSem->increment();
  }
}

void Engine::Backup::handleChangeNotification(DirMonitor &dirMonitor)
{
  DirMonitor::FileChangeEvent_t event;
  bool rc = dirMonitor.popFileChangeEvent(event);

  while (rc) {
    MTrace(t_eng, trace::Debug, "ChangedWD:" << event.root + " " + event.fileName);
    m_pUpload->touchPathWD(event.root);
    rc = dirMonitor.popFileChangeEvent(event);
  }

  // If CDP is configured, schedule backup
  if (m_parent.m_cfg.m_cdp.isSet()) {
    m_parent.m_scheduler.scheduleCDPBackup("");
  }
}

bool Engine::Backup::filter(const std::string &oi)
{
  MutexLock l(m_skip_lock);

  // If we are told to exit, do so
  if (m_cmd == CSHUTDOWN || m_cmd == CSTOP)
    throw error("Stop requested");

  // See if our given object name string contains a given skip dir
  // (eg. if our current object name is the skip dir or is a child of
  // the skip dir)
  for (std::set<std::string>::const_iterator i = m_skip_filesystems.begin();
       i != m_skip_filesystems.end(); ++i)
    if (oi.find(*i) == 0) {
      MTrace(t_eng, trace::Debug, "Exclude \"" << *i << "\" filters out \""
             << oi << "\"");
      return false;
    }

  return true;
}

void Engine::Backup::blacklistFSTypes(const std::set<std::string> &skiptypes)
{
  m_skip_filesystems.clear();

  FILE *mtab = setmntent("/etc/mtab", "r");
  if (!mtab)
    throw error("Cannot parse /etc/mtab");
  ON_BLOCK_EXIT(endmntent, mtab);

  while (mntent *fs = getmntent(mtab)) {
    if (skiptypes.count(fs->mnt_type)) {
      MTrace(t_eng, trace::Info, "Skipping mount \"" << fs->mnt_dir << "\" type "
             << fs->mnt_type);
      m_skip_filesystems.insert(fs->mnt_dir);
    }
  }
}

Engine::Scheduler::Scheduler(Engine &e)
  : m_parent(e)
  , m_shutdown(false)
{
  if (pipe(m_wakepipe))
    throw syserror("pipe", "creating Scheduler wake pipe");

  // Start the worker thread - it should wait for something to do
  start();
}

Engine::Scheduler::~Scheduler()
{
  m_shutdown = true;
  try { wakeThread(); }
  catch (...) { }
  join_nothrow();
  // Kill wake pipe
  close(m_wakepipe[0]);
  close(m_wakepipe[1]);
}

void Engine::Scheduler::scheduleCDPBackup(const std::string &path)
{
  if (!m_parent.m_cfg.m_cdp.isSet()) {
    MTrace(t_cdp, trace::Warn, "File changed but CDP trigger disabled - ignoring");
    return;
  }
  if (!m_cdp_backup_init.isSet()) {
    m_cdp_backup_init = Time::now() + m_parent.m_cfg.m_cdp.get();
    MTrace(t_cdp, trace::Debug, "Scheduled CDP initiated backup at: "
           << m_cdp_backup_init.get());
    // Wake the scheduler poll thread
    wakeThread();
  } else {
    MTrace(t_cdp, trace::Debug, "Change, but CDP initiated backup already "
           "scheduled at: " << m_cdp_backup_init.get());
  }
}

void Engine::Scheduler::wakeThread()
{
  char tmp(42);
  int rc;
  do {
    rc = write(m_wakepipe[1], &tmp, sizeof tmp);
  } while (rc == -1 && errno == EINTR);
  if (rc == -1)
    throw syserror("write", "waking Scheduler thread");
}

void Engine::Scheduler::run()
{
  while (!m_shutdown) {
    try {
      //
      // We want to sleep until either
      // 1: we reach our next timeout (scheduled event)
      // 2: we get notified about change
      // 3: we get woken by the wake pipe
      //
      std::vector<struct pollfd> pollfds;

      Time expiry = Time::END_OF_TIME;

      // Wake on the wake pipe
      { struct pollfd ent;
        ent.fd = m_wakepipe[0];
        ent.events = POLLIN;
        ent.revents = 0;
        pollfds.push_back(ent);
      }

      // See if a backup was scheduled by CDP
      if (m_cdp_backup_init.isSet()) {
        expiry = std::min(expiry, m_cdp_backup_init.get());
      }

      // So poll...
      int rc;
      do {
        int timeout = -1;
        if (expiry < Time::END_OF_TIME) {
          timeout = ((expiry - Time::now()).to_timet() + 1) * 1000;
          MTrace(t_cdp, trace::Debug, "Poll timeout " << timeout << " ms");
        }
        rc = poll(&pollfds[0], pollfds.size(), timeout);
      } while (rc == -1 && errno == EINTR);

      // Treat timeout
      if (m_cdp_backup_init.isSet()
          && m_cdp_backup_init.get() <= Time::now()) {
        MTrace(t_cdp, trace::Info, "Initiating backup due to CDP event");
        m_cdp_backup_init = Optional<Time>();
        // Start the backup
        m_parent.m_backup.startBackup();
      }

      // Treat fd results
      for (size_t i = 0; i != pollfds.size(); ++i) {
        // Is this the wake pipe?
        if (pollfds[i].fd == m_wakepipe[0]) {
          if (pollfds[i].revents & POLLIN) {
            char c[128];
            while (-1 == read(m_wakepipe[0], c, sizeof c)
                   && errno == EINTR);
          }
          continue;
        }
        // Unknown...
        throw error("Unknown fd in scheduler poll loop");
      }
    } catch (error &e) {
      MTrace(t_cdp, trace::Warn, e.toString());
    }
  }
}

