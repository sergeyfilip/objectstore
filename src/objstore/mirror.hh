///
/// Mirroring engine
///
//
/// $Id: mirror.hh,v 1.4 2013/09/05 09:29:55 joe Exp $
//

#ifndef OBJSTORE_MIRROR_HH
#define OBJSTORE_MIRROR_HH

#include "common/trace.hh"
#include "common/thread.hh"
#include "common/mutex.hh"
#include "common/hash.hh"
#include "common/semaphore.hh"

#include "httpd/httpclient.hh"

#include <set>
#include <deque>

#include <stdint.h>


namespace mirror {

  /// The Supervisor is started in one single instance for a given
  /// mirror host. Whenever an object needs mirroring, it is sent to
  /// the supervisor.
  //
  /// The supervisor logs this information in the mirroring log files
  /// and puts the information on the work queue.
  //
  //
  /// When the supervisor is notified of an object that needs
  /// replication, a work item is queued on the work queue and written
  /// to the active log file. Every work item has an (ever increasing)
  /// sequence number. The sequence number is local to this storage
  /// server and is not persisted across restarts - it is only used to
  /// track when log files can be cleaned up during operations.
  //
  /// When a worker thread picks an object from the work queue, it
  /// "activates" the sequence number in the supervisor. It notifies
  /// the supervisor when the replication is complete.
  //
  /// The supervisor will regularly close its log file and start a new
  /// one, to keep the size limited. It will remember the maximum
  /// sequence number of each log file that it wrote. When the minimum
  /// sequence number in the work queue *and* all outstanding active
  /// sequence numbers from worker threads are above this maximum
  /// sequence number, the log file is deleted.
  ///
  ///
  class Supervisor : private Thread {
  public:
    /// Constructing the supervisor will make it start up the threads
    /// and get ready for processing. It can be notified of changes
    /// immediately after creation.
    Supervisor(const std::string &host, uint16_t port,
               const std::string &tmpdir, size_t threads,
               const std::string &objdir);

    /// Destroying the supervisor will make it stop its worker threads
    /// and shut down. This is the "normal" way to shut down the
    /// mirroring service.
    virtual ~Supervisor();

    /// This method must be called whenever an "original"
    /// (non-replicated) object is written. When this method returns,
    /// it means the object id has been logged to persistent storage
    /// so that it can get replicated in the event of a system
    /// failure right after the object write.
    //
    /// In case of a problem logging this write, the method will
    /// throw. It is important that the caller calls this method
    /// BEFORE writing the object to disk, so that we can return an
    /// error in case logging fails, and guarantee that we will not
    /// have written this object to disk without guaranteeing its
    /// eventual replication.
    void logObjectReplication(const sha256&);

    /// This method can be called after the object has been written to
    /// local disk.
    void replicateObject(const sha256&);

    /// Return the size of the mirroring queue (takes lock and is thus
    /// not const)
    size_t getQueueLength();

  protected:
    /// This is our mirror log directory watching thread
    void run();

  private:
    /// Mirror server host name
    const std::string m_host;

    /// Connection template for mirror - each worker creates its own
    /// from this template
    HTTPclient m_conn;

    /// When manipulating the workqueue and/or the active serials, we
    /// must hold this lock
    Mutex m_work_lock;

    /// We track which sequence numbers are currently in progress of
    /// being replicated.
    std::set<uint64_t> m_active_replicas;

    struct wq_item_t {
      wq_item_t() : serial(-1) { }
      wq_item_t(uint64_t s, const sha256 &h)
        : serial(s), objectid(h) { }
      uint64_t serial; /// The serial number
      sha256 objectid; /// The object id to replicate
    };

    /// Called by a worker thread to receive a work item - returns
    /// true on success, false if we are shutting down.
    bool getWorkItem(wq_item_t &);

    /// Called by a worker thread on retry - the worker thread will
    /// retry failing mirroring operations indefinitely, so it calls
    /// this method to see if it should be shutting down. This method
    /// returns true if we are in normal operations, false if we are
    /// shutting down.
    bool mayContinue();

    /// When a mirror worker has completed a work item, it notifies us
    /// using this mechanism. This may in turn cause the log file from
    /// which the work item came, to be deleted (if this was the last
    /// work item from that log)
    void workItemComplete(uint64_t);

    /// We have a work queue of objects that need replication
    typedef std::deque<wq_item_t> wq_t;
    wq_t m_workqueue;

    /// Counter semaphore for jobs on queue
    Semaphore m_workitems;

    /// Next serial number to use (ever increasing) (manipulated under
    /// m_work_lock)
    uint64_t m_serial;

    /// This variable is set when we want the worker thread to
    /// exit. This will cause the getWorkItem() method to return
    /// false.
    bool m_exit;

    /// The worker thread picks objects from the work queue and mirrors
    /// the objects. When an object has been mirrored, the supervisor is
    /// notified.
    //
    class Worker : public Thread {
    public:
      Worker(Supervisor &);
      virtual ~Worker();

    protected:
      void run();

    private:
      //! Reference to our parent
      Supervisor &m_parent;
      //! Our own connection object
      HTTPclient m_conn;
    };

    /// Worker threads
    std::list<Worker> m_workers;

    /// The directory under which object data files are placed
    const std::string m_objdir;

    //////////////////////////////////////////////////////////////////
    /// Queueing logic theory of operation
    //////////////////////////////////////////////////////////////////
    ///
    /// logObjectReplication() will optionally create a new log file
    /// if we do not currently have one active.
    ///
    /// replicateObject() will optionally close the currently active
    /// log file if it is either too old or has received the maximum
    /// number of log entries.  The reason it is closed this late
    /// (instead of in logObjectReplication()) is because the actual
    /// object must be written to disk before we close the log -
    /// otherwise we would risk starting a replication of an object
    /// that was not yet on local disk, causing the object to be
    /// skipped and thus never replicated.
    ///
    /// Our thread watches the temporary directory for log files. It
    /// will periodically scan this directory and add replication
    /// object ids to our work queue if the work queue is sufficiently
    /// empty. It will avoid queueing entries from the currently
    /// active log of course.
    ///
    //////////////////////////////////////////////////////////////////

    /// Our log file list management is handled under a separate lock
    /// not to burden the more common operations unnecessarily
    Mutex m_logfile_lock;

    /// While holding the m_logfile_lock, call this method to have a
    /// new log created (and the active - if any - closed)
    void l_newlog();

    /// The supervisor may or may not have an active file for
    /// logging. We keep track on the number of objects in this file
    /// so that we can re-cycle it when needed.
    ///
    /// The file is named randomly.
    ///
    const std::string m_logdir;

    /// file descriptor of current file
    int m_log;

    /// number of entries in current file
    size_t m_log_entries;

    /// Time the current log was opened
    Time m_log_ctime;

    /// Whenever we close a file, there may be logged object ids in it
    /// which are still being written locally. We therefore keep this
    /// list of log files along with their sets of outstanding local
    /// writes. The theoretical maximum size of this list is our
    /// number of worker threads, but in practice this list should
    /// usually only hold one or two elements. The back of the list is
    /// the currently active/open log file, any other elements are
    /// closed files that still have outstanding local writes.
    //
    /// Mapping from file name (with no directory components) into set
    /// of outstanding writes
    typedef std::list<std::pair<std::string,std::set<sha256> > > logs_t;
    logs_t m_logs;

    /// Whenever we replay a file from disk into our workqueue, we
    /// register the file in this list along with the maximum sequence
    /// number. As mirror workers complete replications, we can delete
    /// these files as their maximum sequence number gets surpassed
    /// (meaning, any entry in the file has successfully been
    /// replicated and thus the file is no longer needed)
    //
    /// Pair of max. seq# and file name (without directory components)
    typedef std::list<std::pair<uint64_t, std::string> > inflight_logs_t;
    inflight_logs_t m_inflight_logs;
  };

}


#endif
