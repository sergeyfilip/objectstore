///
/// Directories change monitor
//
// $Id: $
//

#ifndef DIR_MONITOR_HH
#define DIR_MONITOR_HH

#include "common/thread.hh"
#include "common/mutex.hh"
#include "common/semaphore.hh"
#include "common/partial.hh"

#include <stdint.h>
#include <string>
#include <set>
#include <queue>
#include <utility>

#if defined(_WIN32)
# include <windows.h>
#endif

#if defined(__APPLE__)
#include <CoreFoundation/CFRunLoop.h>
#include <CoreServices/CoreServices.h>
#endif

/// Component that allows monitoring files/folders changes at givent paths
class DirMonitor : public Thread {
public:
  /// Create instance and start working thread
  DirMonitor();

  /// We must destroy allocated structures
  ~DirMonitor();

  /// Include new directory into monitoring
  /// Note. For linux it returns inotify watch descriptor (0 for other platforms)
  int addDir(const std::string &fullPath);

  struct FileChangeEvent_t {
    FileChangeEvent_t() {}

#if defined(_WIN32) || defined(__APPLE__)
    bool operator<(const FileChangeEvent_t &o) const {
      return root+fileName < o.root+o.fileName;
    }
#endif

#if defined(_WIN32) || defined(__APPLE__)
    FileChangeEvent_t(const std::string& aRoot, const std::string& aFileName)
      : root(aRoot), fileName(aFileName) {}
    std::string root;
#endif

#if defined(__linux__)
    FileChangeEvent_t(int aRoot, const std::string& aFileName)
      : root(aRoot), fileName(aFileName) {}
    int root;
#endif

    std::string fileName;
  };

  /// Pops FileChangeEvent from FilesChangeEvents queue
  /// Returns true on success
  bool popFileChangeEvent(FileChangeEvent_t &event);

  /// We call the given closure with the number of fileChange events recently pushed
  /// into the queue
  DirMonitor &setChangeNotification(const BindF1Base<void,DirMonitor&> &);

protected:
  /// worker thread main routine
  void run();
  
  /// stop worker thread
  void stop();

#if defined(_WIN32) 
  static void __stdcall terminateProc(ULONG_PTR arg);
  static void __stdcall addDirProc(ULONG_PTR arg);
#endif
#if defined(__APPLE__)
  void runMac();
#endif
#if defined(__linux__)
  void runLinux();
#endif

private:
#if defined(_WIN32)
  /// Flags to worker thread that exit was requested
  bool m_outstandingExit;
#endif

  /// Change-notify callback to subscribed listener
  const BindF1Base<void,DirMonitor&> *m_changeNotify;
  
  /// The queue of paths at which changes were detected
  typedef std::queue<FileChangeEvent_t> FileChangeEvents_t;
  FileChangeEvents_t m_fileChangeEvents;
  /// Serialization of access to events queue
  Mutex m_fileChangeEventsLock;

#if defined(__linux__)
  /// On Linux we hold an inotify file descriptor
  int m_inotify_fd;
  /// On Linux we use a wake pipe to be woken for exit
  int m_wakepipe[2];
#endif

#if defined(__APPLE__) || defined(_WIN32)

#if defined(__APPLE__)
  Semaphore m_monitorThreadSem;
  CFRunLoopRef m_monitorRunLoop;
#endif
  
#if defined(_WIN32)
  volatile DWORD m_outstandingReads;
#endif

  class PathWatcher;
  typedef std::set<PathWatcher*> PathsWatchers_t;
  PathsWatchers_t m_pathsWatchers;

  class PathWatcher {
  public:
    PathWatcher(DirMonitor &aDirMonitor, const std::string &path);
    ~PathWatcher();
    /// Open directory to watch
    void init();
    /// Cancel watching
    void cancel();
    /// Begin read directory changes
    void beginRead();
    /// Reference to parent class
    DirMonitor &getDirMonitor();

  protected:
#if defined(_WIN32)
    static void __stdcall dirChangesCallback(
      DWORD dwErrorCode,
      DWORD dwNumberOfBytesTransfered,
      LPOVERLAPPED lpOverlapped);
#endif
#if defined(__APPLE__)
    static void macFsEventsCallback(ConstFSEventStreamRef streamRef,
                                    void *userData,
                                    size_t numEvents,
                                    void *eventPaths,
                                    const FSEventStreamEventFlags eventFlags[],
                                    const FSEventStreamEventId eventIds[]);
#endif

  private:
    /// Reference to parent class
    DirMonitor &dirMonitor;
    /// Path to directory to watch
    std::string m_path;
#if defined(_WIN32)
    ///
    HANDLE m_directoryHandle;
    /// Data structure for overlapped operation 
    OVERLAPPED m_overlapped;
    /// Allocated buffer for ReadDirectoryChangesW()
    uint8_t m_buffer[32768];
#endif
#if defined(__APPLE__)
    FSEventStreamRef m_fsEventStream;
#endif
  };

#endif

};

#endif
