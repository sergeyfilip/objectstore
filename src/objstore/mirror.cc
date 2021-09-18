///
/// Implementation of the mirroring engine
///
//
/// $Id: mirror.cc,v 1.13 2013/09/05 09:29:55 joe Exp $
//

#include "mirror.hh"
#include "main.hh"

#include "common/scopeguard.hh"
#include "common/string.hh"

#include "objparser/objparser.hh"

#include "httpd/request.hh"
#include "httpd/reply.hh"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <unistd.h>

namespace {
  //! Trace path for mirroring operations
  trace::Path t_mirror("/stord/mirror");

  //! When scanning for the next log file to replay into our work log,
  //! sleep this many seconds before each round
  const time_t c_logpoll_delay(5);

  //! Number of elements in work queue above which no more log files
  //! will be replayed
  const size_t c_wqueue_qlimit(1000);

  //! Number of replication log entries we keep in each file
  const size_t c_fileentries(4000);

  //! Maximum age of a replication log - the log is closed after
  //! either c_fileentries has been written, or, the max age reached
  const DiffTime c_maxlogage(DiffTime::iso("PT30S"));

  //! Get previous element
  template <typename I>
  I pred(I i) { I t(i); return --t; }

}

mirror::Supervisor::Supervisor(const std::string &host, uint16_t port,
                               const std::string &tmpdir, size_t threads,
                               const std::string &objdir)
  : m_host(host)
  , m_conn(host, port)
  , m_serial(1) // if we have an empty log, it's max sequence must not
                // be uint64_t(-1)
  , m_exit(false)
  , m_objdir(objdir)
  , m_logdir(tmpdir)
  , m_log(-1)
  , m_log_entries(0)
{
  MTrace(t_mirror, trace::Debug, "Initialising mirror to ["
         << host << ":" << port << "] (" << tmpdir << "), " << threads
         << " threads");

  // Create a new log file (don't bother holding the log - we're
  // single threaded so far)
  l_newlog();

  // Start directory watcher thread
  start();

  // Start workers
  for (size_t i = 0; i != threads; ++i) {
    m_workers.push_back(Worker(*this));
    m_workers.back().start();
  }
}

mirror::Supervisor::~Supervisor()
{
  { MutexLock l(m_work_lock);
    // Clear the work queue and set the exit variable
    m_workqueue.clear();
    m_exit = true;
    m_workitems.increment(); // Unblock waiting threads
  }

  // Wait for our directory watcher thread to exit before we exit - it
  // watches our m_exit variable, so it should know we're exiting.
  join_nothrow();

  // Wait for threads to exit before we exit (so that we do not
  // destroy the queue or exit variable until the threads are gone).
  m_workers.clear();

  // Close our work log
  if (m_log != -1)
    close(m_log);
}

void mirror::Supervisor::logObjectReplication(const sha256 &oid)
{
  MutexLock fl(m_logfile_lock);

  // We should *always* have an active log no matter when and how
  // we're called...
  MAssert(m_log != -1, "logObjectReplication called with no log");

  // Log object to current file
  { std::ostringstream s;
    s << oid.m_hex << "\n";
    int rc;
    do { rc = write(m_log, s.str().data(), s.str().size()); }
    while (rc == -1 && errno == EINTR);
    if (rc == -1)
      throw syserror("write", "writing object id entry to mirroring log");
    ++m_log_entries;
    MTrace(t_mirror, trace::Debug, "Logged object " << oid.m_hex);
  }

  // We should *always* have our active log in the logs list
  MAssert(!m_logs.empty(), "logObjectReplication has no logs");

  // Object is outstanding until written
  m_logs.back().second.insert(oid);

}

void mirror::Supervisor::replicateObject(const sha256 &oid)
{
  // The object has been written locally. It is therefore safe for us
  // to release the current log file if it is sufficiently full or
  // old.
  MutexLock l(m_logfile_lock);
  if (m_log_entries >= c_fileentries) { // yes we can exceed limit by
                                        // our number of worker
                                        // threads minus one!
    MTrace(t_mirror, trace::Info, "Creating new log due to entry limit");
    l_newlog();
  } else if (Time::now() - m_log_ctime > c_maxlogage) {
    MTrace(t_mirror, trace::Info, "Creating new log due to time limit");
    l_newlog();
  }

  // We want to mark our oid as completed - simply try erasing it from
  // each of the log file outstanding sets until we find it
  //
  // We could test whether we successfully delete the object, but we
  // won't because a given object may be written multiple times
  // concurrently, and since we use a set we only keep one instance of
  // the hash, we may well have multiple calls to replicateObject
  // which will not remove any id from the set because it was already
  // removed.
  //
  for (logs_t::iterator l = m_logs.begin(); l != m_logs.end(); ++l)
    if (l->second.erase(oid)) {
      // If set is now empty, and this file is not the currently
      // active file (the back), clear file from list as it no longer
      // has outstanding writes
      if (l != pred(m_logs.end()) && l->second.empty())
        m_logs.erase(l);
      break;
    }

}

size_t mirror::Supervisor::getQueueLength()
{
  MutexLock l(m_work_lock);
  return m_workqueue.size();
}

void mirror::Supervisor::run()
{
  while (!m_exit) {

    // Sleep and then see if the work queue is mostly empty - we can
    // proceed even if the queue is not empty, we want to keep a nice
    // flow.
    sleep(c_logpoll_delay);

    { MutexLock l(m_work_lock);
      if (m_workqueue.size() > c_wqueue_qlimit)
        continue;
    }

    MTrace(t_mirror, trace::Debug, "Looking for logs in " << m_logdir);

    //
    // Fine, work queue is empty.
    //
    MutexLock l2(m_logfile_lock);

    //
    // See if we have a log file that is not the currently open one
    //
    std::string fullname;
    std::string filename;

    // Scan log file directory.
    { DIR *dir = opendir(m_logdir.c_str());
      if (!dir)
        throw syserror("opendir", "Opening log dir " + m_logdir);
      ON_BLOCK_EXIT(closedir, dir);

      struct dirent *de;
      while ((de = readdir(dir))) {
        // Skip dummy entries
        filename = de->d_name;
        if (filename == "." || filename == "..")
          continue;

        // Skip the active logs
        { bool found(false);
          for (logs_t::const_iterator i = m_logs.begin();
               !found && i != m_logs.end(); ++i) {
            if ((found = i->first == de->d_name)) {
              MTrace(t_mirror, trace::Debug, "Skipping log "
                     << i->first << " with " << i->second.size()
                     << " outstanding writes");
            }
          }
          // See if it is already replayed and queued for deletion
          for (inflight_logs_t::const_iterator i = m_inflight_logs.begin();
               !found && i != m_inflight_logs.end(); ++i)
            if ((found = i->second == de->d_name)) {
              MTrace(t_mirror, trace::Debug, "Skipping log "
                     << i->second << " with max. seq# " << i->first);
            }
          // skip if found
          if (found)
            continue;
        }

        // If we're here, we found a log entry we can use!
        MTrace(t_mirror, trace::Info, "Found inactive log: " + filename);
        fullname = m_logdir + "/" + filename;
        break;
      }
    }

    // If no log was found, go back to sleep
    if (fullname.empty())
      continue;

    // We want to be fault tolerant when doing this processing - if we
    // fail in any way, simply log it and go back to processing. It is
    // perfectly fine if the admin is moving files around and we
    // should not crash the storage server just for a temporary file
    // access problem.
    try {

      // Open whatever file we find
      int log = open(fullname.c_str(), O_RDONLY);
      if (log == -1)
        throw syserror("open", "opening log " + fullname + " for reading");
      ON_BLOCK_EXIT(close, log);

      // Read entries - 64 characters followed by newline. Queue each
      // entry.
      while (true) {
        char entry[65];
        int rc;
        do { rc = read(log, entry, sizeof entry); }
        while (rc == -1 && errno == EINTR);
        if (rc == sizeof entry) {
          MutexLock l(m_work_lock);
          m_workqueue
            .push_back(wq_item_t(m_serial++,
                                 sha256::parse(std::string(entry, entry + 64))));
          m_workitems.increment();
          MTrace(t_mirror, trace::Debug, "queued " << m_workqueue.back().objectid.m_hex
                 << " from " << fullname
                 << " with sequence " << m_workqueue.back().serial);
        } else if (rc == -1) {
          throw syserror("read", "reading log entry from " + fullname);
        } else if (rc != 0) {
          throw error("Read of log " + fullname + " caused bad read");
        } else {
          // rc = 0 - end of file.
          MTrace(t_mirror, trace::Info, "Completed queueing of "
                 << fullname << " - work queue now has " << m_workqueue.size()
                 << " elements");
          // Queue file for deletion
          m_inflight_logs.push_back(std::make_pair(m_serial - 1, filename));
          break;
        }
      }
    } catch (error &e) {
      MTrace(t_mirror, trace::Warn, "Log replay: " << e.toString());
    }
    // Finished queuing entries from log - now go back to sleep.
  }
}

bool mirror::Supervisor::getWorkItem(wq_item_t &w)
{
  // Wait until something appears on queue
  m_workitems.decrement();
  // Manipulate work queue
  MutexLock l(m_work_lock);
  // Are we exiting?
  if (m_exit) {
    m_workitems.increment(); // keep ball rolling
    return false;
  }
  // Pick item
  w = m_workqueue.front();
  m_workqueue.pop_front();
  // Add item to the "active" set
  m_active_replicas.insert(w.serial);
  return true;
}

bool mirror::Supervisor::mayContinue()
{
  // Lockless reading - it is only manipulated on construction and
  // exit so we have no atomicity requirements.
  return !m_exit;
}

void mirror::Supervisor::workItemComplete(uint64_t i)
{
  uint64_t lowest_active = uint64_t(-1);

  { MutexLock l(m_work_lock);
    // Mark item as completed
    m_active_replicas.erase(i);
    // Now see if we can skip the oldest log (it will have sequence
    // number uint64_t(-1) if it is the active log so no need to deal
    // with that specially
    // If we have active replications, consider those
    if (!m_active_replicas.empty()) {
      lowest_active = std::min(lowest_active, *m_active_replicas.begin());
      MTrace(t_mirror, trace::Debug, "Lowest active replicate seq# "
             << *m_active_replicas.begin());
    }
    // If we have work items not yet processed, consider those
    if (!m_workqueue.empty()) {
      lowest_active = std::min(lowest_active, m_workqueue.front().serial);
      MTrace(t_mirror, trace::Debug, "Lowest workqueue seq# "
             << m_workqueue.front().serial);
    }
  }

  while (true) {
    // So, if the oldest log file has a max sequence equal to or higher
    // than the lowest active or queued job, do not delete it.
    std::string oldlog_name;
    { MutexLock l(m_logfile_lock);
      if (m_inflight_logs.empty()
          || m_inflight_logs.front().first >= lowest_active)
        return;
      oldlog_name = m_inflight_logs.front().second;
      m_inflight_logs.pop_front();
    }

    // Fine, delete old log
    MTrace(t_mirror, trace::Info, "Deleting old log " << oldlog_name);
    if (-1 == unlink((m_logdir + "/" +  oldlog_name.c_str()).c_str()))
      throw syserror("unlink", "deleting old log " + oldlog_name);
  }
}


mirror::Supervisor::Worker::Worker(mirror::Supervisor &p)
  : m_parent(p)
  , m_conn(p.m_conn)
{
}

mirror::Supervisor::Worker::~Worker()
{
}

void mirror::Supervisor::Worker::run()
{
  MTrace(t_mirror, trace::Debug, "Mirroring worker started");
  //
  // As long as we are not shutting down, get entries from the work
  // queue
  //
  while (m_parent.mayContinue()) {

    // Get work item from queue - or get false return which means we
    // must stop what we're doing
    wq_item_t item;
    if (!m_parent.getWorkItem(item))
      break;

    MTrace(t_mirror, trace::Debug, "Will mirror " << item.objectid.m_hex
           << " with seq# " << item.serial);

    // Good, mirror item
    do {
      try {
        // Construct the mirror POST request
        HTTPRequest req;
        req.m_method = HTTPRequest::mPOST;
        req.m_uri = "/object/" + item.objectid.m_hex;
        req.m_headers
          .add("host", m_parent.m_host)
          .add("redundancy", "replica"); // prevent back-replication

        std::vector<char> buffer(ng_chunk_size);
        // Read data from local disk
        { int fd = open((m_parent.m_objdir + splitName(item.objectid.m_hex)).c_str(),
                        O_RDONLY);
          if (fd == -1) {
            // In case the file simply doesn't exist, then we probably
            // had a race during shutdown which has caused us to log a
            // replication but never actually write it to local
            // disk. We skip such objects.
            if (errno == ENOENT) {
              MTrace(t_mirror, trace::Info, "Skipping replication of "
                     << item.objectid.m_hex << " - missing locally");
              m_parent.workItemComplete(item.serial);
              break;
            }
            throw syserror("open", "opening " + item.objectid.m_hex
                           + " for replication");
          }
          ON_BLOCK_EXIT(close, fd);
          // Read data
          int rc;
          while (-1 == (rc = read(fd, &buffer[0], buffer.size()))
                 && errno == EINTR);
          if (rc == -1)
            throw syserror("read", "reading source object "
                           + item.objectid.m_hex + " for replication");
          // Insert data in body
          req.m_body.assign(&buffer[0], &buffer[0] + rc);
        }

        // Execute request
        HTTPReply rep = m_conn.execute(req);
        if (rep.getStatus() == 201) {
          // Success!
          MTrace(t_mirror, trace::Info, "Replicated "
                 << item.objectid.m_hex << " seq# " << item.serial);
          m_parent.workItemComplete(item.serial);
          break;
        }

        // Any failure will be retried....
        throw error(rep.toString());

      } catch (error &e) {
        MTrace(t_mirror, trace::Info, "Retry mirror: " << e.toString());
        sleep(3);
        // Failure - loop and retry
        continue;
      }
      // Success!
      break;
    } while (m_parent.mayContinue());

  }
  MTrace(t_mirror, trace::Debug, "Mirroring worker exiting");
}

void mirror::Supervisor::l_newlog()
{
  // Close current file, if any
  if (m_log != -1) {
    close(m_log);
    m_log = -1;
  }

  // Create a new one
  m_logs.push_back(std::make_pair(randStr(16),
                                  std::set<sha256>()));
  MTrace(t_mirror, trace::Info, "Starting new log: " << m_logs.back().first);
  // Attempt creating the file (collision is unlikely but if it
  // happens we simply append). We open it with O_SYNC to force all
  // writes to be synchronous because this is what we want on our log
  m_log = open((m_logdir + "/" + m_logs.back().first).c_str(),
               O_APPEND | O_CREAT | O_LARGEFILE | O_SYNC | O_WRONLY,
               S_IRUSR | S_IWUSR);
  if (m_log == -1)
    throw syserror("open", "creation/open of log " + m_logs.back().first);

  // Reset counters
  m_log_entries = 0;
  m_log_ctime = Time::now();
}
