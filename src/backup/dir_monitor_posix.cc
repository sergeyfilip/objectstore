///
/// Posix implementation of Directories change monitor
//
// $Id: $
//

#if defined(__linux__)
#include <sys/inotify.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#endif

DirMonitor::DirMonitor()
  : m_changeNotify(0)
#if defined(__linux__)
  , m_inotify_fd(-1)
#endif
{
#if defined(__linux__)
  // Set up a wake pipe
  if (pipe(m_wakepipe))
    throw syserror("pipe", "creating ChangeManager wake pipe");
  // Get an inotify fd
  if (m_inotify_fd == -1) {
    m_inotify_fd = inotify_init();
    if (m_inotify_fd == -1) {
      // Log error, but don't throw. Simply let cdp be disabled.
      MTrace(t_dm, trace::Warn, "Failed initialising inotify: "
             << strerror(errno) << ". CDP disabled.");
    } else {
      if (fcntl(m_inotify_fd, F_SETFL, O_NONBLOCK) < 0)
        throw syserror("fcntl", "setting inotify fd to non blocking");
    }
  }
  MTrace(t_dm, trace::Info, "inotify initialized and ready");
#endif
  // start working thread
  start();
#if defined(__APPLE__)
  // wait for worker thread to get started,
  // because we need m_monitorRunLoop to be initializad there
  m_monitorThreadSem.decrement();
#endif
}

DirMonitor::~DirMonitor()
{
#if defined(__APPLE__)
  for (PathsWatchers_t::iterator it = m_pathsWatchers.begin();
       it != m_pathsWatchers.end(); ++it) {
    delete (*it);
  }
#endif

  stop();

#if defined(__linux__)
  // Close inotify - this will wake our poll too
  if (m_inotify_fd != -1) {
    close(m_inotify_fd);
    m_inotify_fd = -1;
  }
  // Close wake pipe ends
  close(m_wakepipe[0]);
  close(m_wakepipe[1]);
#endif

  delete m_changeNotify;
}

void DirMonitor::run()
{
#if defined(__APPLE__)
  runMac();
#endif
#if defined(__linux__)
  runLinux();
#endif
}

#if defined(__APPLE__)
static void dummyRunloopFunc(void *info)
{
}

void DirMonitor::runMac()
{
  m_monitorRunLoop = CFRunLoopGetCurrent();
  // Unblock main thread (see constructor)
  m_monitorThreadSem.increment();

  // Add dummy source to prevent runloop from exiting
  CFRunLoopSourceContext sourceContext;
  bzero(&sourceContext, sizeof(sourceContext));
  sourceContext.perform = dummyRunloopFunc;
  CFRunLoopSourceRef runLoopDummySource = CFRunLoopSourceCreate(NULL, 0, &sourceContext);
  ON_BLOCK_EXIT(CFRelease, runLoopDummySource);
  CFRunLoopAddSource(CFRunLoopGetCurrent(), runLoopDummySource, kCFRunLoopCommonModes);

  MTrace(t_dm, trace::Debug, "Entered monitor Runloop");
  CFRunLoopRun();
  MTrace(t_dm, trace::Debug, "Exited monitor Runloop");
}
#endif

#if defined(__linux__)
void DirMonitor::runLinux()
{
  while (m_inotify_fd != -1) {
    struct pollfd ent[2];
    ent[0].fd = m_inotify_fd;
    ent[0].events = POLLIN;
    ent[0].revents = 0;
    ent[1].fd = m_wakepipe[0];
    ent[1].events = POLLIN;
    ent[1].revents = 0;

    int rc;
    do {
      rc = poll(ent, 2, -1);
    } while (rc == -1 && errno == EINTR);

    if (rc < 0) {
      MTrace(t_dm, trace::Warn, "Error polling inotify fd: "
             << strerror(errno) << " - disabling CDP");
      break;
    }
  
    // Poll should not return without having either some events, or
    // having an error.
    if (rc == 0) {
      MTrace(t_dm, trace::Warn,
             "ChangeManager poll returned without events. Retrying");
      continue;
    }
  
    // If this is the wake pipe, we're closing
    if (ent[1].revents) {
      MTrace(t_dm, trace::Debug, "ChangeManager got wakepipe event - ending...");
      return;
    }
  
    // Fine, let's treat the events then
    if (ent[0].revents & POLLIN) {
      // Read event
      std::vector<uint8_t> rdbuf(1024);
      while (true) {
        int rc;
        while (-1 == (rc = read(m_inotify_fd, &rdbuf[0], rdbuf.size()))
               && errno == EINTR);
        // If buffer too small, double
        if (rc == 0 || (rc == -1 && errno == EINVAL)) {
          rdbuf.resize(rdbuf.size() * 2);
          continue;
        }
        // Otherwise, fail on error
        if (rc == -1)
          throw syserror("read", "reading inotify event");
        // Success then
        if (rc > 0) {
          rdbuf.resize(rc);
          break;
        }
      }

      size_t numEvents = 0;
      {
        MutexLock l(m_fileChangeEventsLock);
        // Parse events from buffer - buffer holds one or more events.
        for (size_t offset = 0; offset != rdbuf.size(); ) {
          struct inotify_event *evt
          = reinterpret_cast<inotify_event*>(&rdbuf[offset]);
          // Treat event
          m_fileChangeEvents.push(FileChangeEvent_t(evt->wd, evt->name));
          numEvents++;
      
          // Now increment offset to next event
          offset += sizeof(struct inotify_event) + evt->len;
          MTrace(t_dm, trace::Debug, "New offset is " << offset
                 << " - buffer size is " << rdbuf.size());
        }
      }
      // Notify so that someone can consume events from our queue
      if (m_changeNotify) {
        (*m_changeNotify)(*this);
      }
      continue;
    }
  }
}
#endif

void DirMonitor::stop()
{
  if(active()) {
#if defined(__APPLE__)
    CFRunLoopStop(m_monitorRunLoop);
#endif
#if defined(__linux__)
    // Signal thread to close
    { char tmp(42);
      while (-1 == write(m_wakepipe[1], &tmp, sizeof tmp)
             && errno == EINTR);
    }
#endif
    join_nothrow();
  }
}

int DirMonitor::addDir(const std::string &path)
{
#if defined(__APPLE__)
  PathWatcher* pathWatcher = new PathWatcher(*this, path);
  m_pathsWatchers.insert(pathWatcher);
  return 0;
#endif
#if defined(__linux__)
  int rc = inotify_add_watch(m_inotify_fd,
                             path.c_str(),
                             // File attribute changes
                             IN_ATTRIB
                             // Files appearing
                             | IN_CREATE | IN_MOVED_TO
                             // Files being modified
                             | IN_MODIFY
                             // Files disappearing
                             | IN_DELETE | IN_MOVED_FROM
                             // Don't follow sym-links
                             | IN_DONT_FOLLOW);
  if (rc == -1 && errno == ENOSPC) {
    // User needs to adjust /proc/sys/fs/inotify/max_user_watches
    //
    // We should log this and disable CDP
    throw error("Exceeded inotify watch limit. "
                "Please increase /proc/sys/fs/inotify/max_user_watches. "
                "CDP disabled.");
  }
  if (rc == -1 && errno == ENOENT) {
    // Something was added for watching but disappeared under us. That
    // is normal.
    MTrace(t_dm, trace::Info, "Skipping " << path
           << " for inotify because it disappeared");
    return -1;
  }
  // Handle misc. errors
  if (rc == -1)
    throw syserror("inotify_add_watch", "adding directory to watch list");
  MTrace(t_dm, trace::Debug, "inotify of \"" << path << "\" got wd " << rc);
  return rc;
#endif
}

#if defined(__APPLE__)
DirMonitor::PathWatcher::PathWatcher(DirMonitor &aDirMonitor, const std::string &path)
: dirMonitor(aDirMonitor)
, m_path(path)
{
  {
    FSEventStreamContext context = {0, this, NULL, NULL, NULL};
    CFStringRef pathRef = CFStringCreateWithCString(kCFAllocatorDefault, path.c_str(), kCFStringEncodingUTF8);
    ON_BLOCK_EXIT(CFRelease, pathRef);
    CFArrayRef pathToWatch = CFArrayCreate(kCFAllocatorDefault, (const void **) &pathRef, 1, &kCFTypeArrayCallBacks);
    ON_BLOCK_EXIT(CFRelease, pathToWatch);
    
    FSEventStreamEventId sinceWhen = kFSEventStreamEventIdSinceNow;
    CFTimeInterval latency = 1;
    m_fsEventStream = FSEventStreamCreate(NULL,
                                        &PathWatcher::macFsEventsCallback,
                                        &context,
                                        pathToWatch,
                                        sinceWhen,
                                        latency,
                                        kFSEventStreamCreateFlagNone
                                        );
    
  }

  FSEventStreamScheduleWithRunLoop(m_fsEventStream, dirMonitor.m_monitorRunLoop, kCFRunLoopDefaultMode);
  FSEventStreamStart(m_fsEventStream);
}

DirMonitor::PathWatcher::~PathWatcher()
{
  FSEventStreamStop(m_fsEventStream); //Stop getting events
  FSEventStreamInvalidate(m_fsEventStream); //Unschedule from all runloops
  FSEventStreamRelease(m_fsEventStream);
}

void DirMonitor::PathWatcher::macFsEventsCallback(ConstFSEventStreamRef streamRef,
                                     void *contextData,
                                     size_t numEvents,
                                     void *eventPaths,
                                     const FSEventStreamEventFlags eventFlags[],
                                     const FSEventStreamEventId eventIds[])
{
  PathWatcher* pathWatcher = reinterpret_cast<PathWatcher*>(contextData);

  {
    MutexLock l(pathWatcher->dirMonitor.m_fileChangeEventsLock);
    for(size_t i = 0; i < numEvents; i++) {
      const char *eventPath = ((const char **)eventPaths)[i];
      pathWatcher->dirMonitor.m_fileChangeEvents.push(FileChangeEvent_t(pathWatcher->m_path,
                                                                        std::string(eventPath).substr(pathWatcher->m_path.length())));
    }
  }

  // Notify so that someone can consume events from our queue
  if (pathWatcher->dirMonitor.m_changeNotify) {
    (*pathWatcher->dirMonitor.m_changeNotify)(pathWatcher->dirMonitor);
  }
}
#endif
