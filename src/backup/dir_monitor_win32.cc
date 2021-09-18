///
/// Win32 implementation of Directories change monitor
//
// $Id: $
//

DirMonitor::DirMonitor()
  : m_outstandingExit(false)
  , m_outstandingReads(0)
  , m_changeNotify(0)
{
}

DirMonitor::~DirMonitor()
{
  stop();

  delete m_changeNotify;
}

void DirMonitor::terminateProc(ULONG_PTR arg)
{
  DirMonitor* dirMonitor = reinterpret_cast<DirMonitor*>(arg);
  for (PathsWatchers_t::iterator it = dirMonitor->m_pathsWatchers.begin();
       it != dirMonitor->m_pathsWatchers.end(); ++it) {
    (*it)->cancel();
  }
}

void DirMonitor::run()
{
  MTrace(t_dm, trace::Debug, "DirMonitor::run()");
  // Process change notifications until we should quit
  while (!m_outstandingExit || m_outstandingReads) {
    DWORD rc = ::SleepEx(INFINITE, true);
    MTrace(t_dm, trace::Debug, "m_outstandingReads:" << m_outstandingReads);
  }
}

void DirMonitor::stop()
{
  m_outstandingExit = true;
  if (getThreadHandle() != INVALID_HANDLE_VALUE) {
    ::QueueUserAPC(DirMonitor::terminateProc, getThreadHandle(), reinterpret_cast<ULONG_PTR>(this));
    join_nothrow();
  }
}

int DirMonitor::addDir(const std::string &path)
{
  if (getThreadHandle() == INVALID_HANDLE_VALUE) {
    start();
  }

  PathWatcher* pathWatcher = new PathWatcher(*this, path);
  ::QueueUserAPC(DirMonitor::addDirProc, getThreadHandle(), reinterpret_cast<ULONG_PTR>(pathWatcher));
  return 0;
}

void DirMonitor::addDirProc(ULONG_PTR arg)
{
  PathWatcher* pathWatcher = reinterpret_cast<PathWatcher*>(arg);
  pathWatcher->getDirMonitor().m_pathsWatchers.insert(pathWatcher);
  try {
    pathWatcher->init();
    pathWatcher->beginRead();
  } catch (error &e) {
    MTrace(t_dm, trace::Warn, "Add dir watch error: " << e.toString());
    pathWatcher->getDirMonitor().m_pathsWatchers.erase(pathWatcher);
    delete pathWatcher;
  }
}

DirMonitor::PathWatcher::PathWatcher(DirMonitor &aDirMonitor, const std::string &path)
  : dirMonitor(aDirMonitor)
  , m_path(path)
  , m_directoryHandle(INVALID_HANDLE_VALUE)
{
  ::ZeroMemory(&m_overlapped, sizeof(OVERLAPPED));
  m_overlapped.hEvent = this;
}

DirMonitor::PathWatcher::~PathWatcher()
{
  MTrace(t_dm, trace::Debug, "~PathWatcher()");
  ::CloseHandle(m_directoryHandle);
}

DirMonitor& DirMonitor::PathWatcher::getDirMonitor()
{
  return dirMonitor;
}

void DirMonitor::PathWatcher::cancel()
{
  ::CancelIo(m_directoryHandle);
}

void DirMonitor::PathWatcher::init()
{
  m_directoryHandle = ::CreateFile(
                   utf8_to_utf16(m_path).c_str(),       // pointer to the file name
                   FILE_LIST_DIRECTORY,   // access (read/write) mode
                   FILE_SHARE_READ        // share mode
                   | FILE_SHARE_WRITE
                   | FILE_SHARE_DELETE,
                   NULL,                  // security descriptor
                   OPEN_EXISTING,         // how to create
                   FILE_FLAG_BACKUP_SEMANTICS // file attributes
                   | FILE_FLAG_OVERLAPPED,
                   NULL);                 // file with attributes to copy

  if (m_directoryHandle == INVALID_HANDLE_VALUE)
    throw syserror("CreateFile", "opening directory to watch: " + m_path);
}

void DirMonitor::PathWatcher::beginRead()
{
  DWORD dwBytes=0;
  // This call needs to be reissued after every APC.
  BOOL rc = ::ReadDirectoryChangesW(
                   m_directoryHandle,
                   &m_buffer,
                   sizeof m_buffer,
                   true,                                // watch entire subtree
                   FILE_NOTIFY_CHANGE_FILE_NAME
                   | FILE_NOTIFY_CHANGE_DIR_NAME
                   | FILE_NOTIFY_CHANGE_ATTRIBUTES
                   | FILE_NOTIFY_CHANGE_SIZE
                   | FILE_NOTIFY_CHANGE_LAST_WRITE
                   | FILE_NOTIFY_CHANGE_CREATION
                   | FILE_NOTIFY_CHANGE_SECURITY,
                   &dwBytes,
                   &m_overlapped,
                   &dirChangesCallback); // completion routine

  if (!rc) {
    throw syserror("ReadDirectoryChangesW", "PathWatcher::beginRead(): " + m_path);
  }
  
  ::InterlockedIncrement(&dirMonitor.m_outstandingReads);
}

void DirMonitor::PathWatcher::dirChangesCallback(
  DWORD dwErrorCode,
  DWORD dwNumberOfBytesTransfered,
  LPOVERLAPPED lpOverlapped)
{
  PathWatcher* pathWatcher = reinterpret_cast<PathWatcher*>(lpOverlapped->hEvent);
  ::InterlockedDecrement(&pathWatcher->dirMonitor.m_outstandingReads);

  if (dwErrorCode == ERROR_OPERATION_ABORTED) {
    MTrace(t_dm, trace::Debug, "Directory changes Read aborted");
    pathWatcher->dirMonitor.m_pathsWatchers.erase(pathWatcher);
    delete pathWatcher;
    return;
  }

  size_t nNewFileChangeEvents = 0;

  if (!dwNumberOfBytesTransfered) {
      MTrace(t_dm, trace::Warn, "Buffer overflow");
  } else {
    MutexLock l(pathWatcher->dirMonitor.m_fileChangeEventsLock);
    // Process notifications
    size_t ofs = 0;
    while (true) {
      FILE_NOTIFY_INFORMATION *p = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(pathWatcher->m_buffer + ofs);
      // Extract object name, relative from parent m_path
      const std::wstring wrelname(p->FileName,
                                  p->FileName + p->FileNameLength / sizeof p->FileName[0]);

      pathWatcher->dirMonitor.m_fileChangeEvents.push(
        FileChangeEvent_t(pathWatcher->m_path, utf16_to_utf8(wrelname))
      );
      nNewFileChangeEvents++;

      // Last change?
      if (!p->NextEntryOffset)
        break;
      ofs += p->NextEntryOffset;
    }
  }

  // Schedule path watcher again
  try {
    pathWatcher->beginRead();
  } catch (error &e) {
    MTrace(t_dm, trace::Warn, "Add dir watch error: " << e.toString());
    pathWatcher->dirMonitor.m_pathsWatchers.erase(pathWatcher);
    delete pathWatcher;
  }

  // Notify so that someone can consume events from our queue
  if (pathWatcher->dirMonitor.m_changeNotify) {
    (*pathWatcher->dirMonitor.m_changeNotify)(pathWatcher->dirMonitor);
  }
}
