///
/// Application updater
///
//
/// $Id: appupdater.cc,v 1.4 2013/07/23 15:07:46 vg Exp $

#include "appupdater.hh"
#include "common/trace.hh"
#include "xml/xmlio.hh"
#include "version.hh"

#include <fstream>

namespace {
  trace::Path t_update("/AppUpdater");
}

AppUpdater::AppUpdater(ServerConnection &c)
  : m_conn(c)
  , m_quit(0)
{
}

AppUpdater::~AppUpdater()
{
  m_quitsem.increment();
  join_nothrow();
  delete m_quit;
}

AppUpdater &AppUpdater::setQuit(BindBase<void> &q)
{
  delete m_quit;
  m_quit = q.clone();
  return *this;
}

namespace {
  bool pback(std::list<std::string> &l, const std::string &f) {
    l.push_back(f);
    return true;
  }
}

void AppUpdater::run()
{
  MTrace(t_update, trace::Debug, "Application update thread started");
  while (!m_quitsem.decrement(Time::now() + DiffTime::iso("PT1M"))) {
    MTrace(t_update, trace::Debug, "Time to check for update...");

    // Request latest package.xml for our architecture
    std::string uri = "/files/download/updates/desktop/pc/";
    uri += g_getArchitecture();
    uri += "-latest/package.xml";
    ServerConnection::Request req(ServerConnection::mGET, uri);
    ServerConnection::Reply rep = m_conn.execute(req);
    if (rep.getCode() != 200) {
      MTrace(t_update, trace::Info, "Cannot fetch update description at " << uri
             << ": " << rep.toString());
      continue;
    }

    // Fine, parse document
    uint32_t buildno;
    std::string file;
    std::list<std::string> files;
    using namespace xml;
    const IDocument &doc = mkDoc
      (Element("package")
       (Element("build")(CharData<uint32_t>(buildno))
        & *Element("file")(CharData<std::string>(file))
        [  papply<bool,std::list<std::string>&,const std::string&>( pback, files, file ) ]));

    try {
      std::istringstream s(std::string(rep.refBody().begin(), rep.refBody().end()));
      XMLexer lexer(s);
      doc.process(lexer);
    } catch (error &e) {
      MTrace(t_update, trace::Info, "Package parse error: " << e.toString());
      continue;
    }

    // Compare - do we need to upgrade?
    MTrace(t_update, trace::Debug, "Most recent update is build " << buildno
           << "; running build is " << g_getBuild());
    if (buildno <= g_getBuild())
      continue;

    // We must upgrade!

    // Step 1: Copy ourselves to a temporary location
    std::string copy(getCurrentModuleDirectory() + "\\update.exe");
    if (!CopyFile(utf8_to_utf16(getCurrentModule()).c_str(),
                  utf8_to_utf16(copy).c_str(), false))
      throw syserror("CopyFile", "copying module for upgrade");

    // Step 2: Execute the copy with the "-u" switch and our PID for
    // upgrade
    wchar_t buf[MAX_PATH];
    { std::ostringstream str;
      str << copy << " -u " << GetCurrentProcessId()
          << " " << buildno;
      utf8_to_utf16(str.str()).copy(buf, sizeof buf / sizeof buf[0] - 1, 0);
      buf[str.str().size()] = 0;
    }

    STARTUPINFO start;
    memset(&start, 0, sizeof start);
    start.cb = sizeof start;
    
    PROCESS_INFORMATION info;
    if (!CreateProcess(utf8_to_utf16(copy).c_str(), buf, 0, 0, false,
                       NORMAL_PRIORITY_CLASS, 0, 0, &start, &info))
      throw syserror("CreateProcess", "spawning update handler " + copy);

    // Step 3: Quit
    MTrace(t_update, trace::Info, "Spawned updater - exiting.");
    (*m_quit)();

  }
  // Quit!
}

bool downloadFile(const uint32_t &buildno, const std::string &file, const std::string &host)
{
  std::ostringstream uri;
  uri <<  "/files/download/updates/desktop/pc/"
      << g_getArchitecture()
      << "-" << buildno << "/"
      << file;

  MTrace(t_update, trace::Debug, "Downloading URI: "
         << uri.str());

  ServerConnection conn(host, 443, true);
  ServerConnection::Request req(ServerConnection::mGET, uri.str());
  ServerConnection::Reply rep = conn.execute(req);

  // Simply write file
  if (rep.getCode() != 200)
    throw error("Cannot fetch update file at " + uri.str()
                + ": " + rep.toString());

  MTrace(t_update, trace::Debug, "Writing file " << file << " of size "
         << rep.refBody().size() << " bytes");

  std::string outname(getCurrentModuleDirectory() + "\\" + file);

  std::ofstream f(outname.c_str(), std::ios::binary | std::ios::trunc);
  f.write(reinterpret_cast<char*>(rep.refBody().data()), rep.refBody().size());
  
  return true;
}

void runUpgrade(DWORD pid, uint32_t buildno, const std::string host)
{
  // Step 0: Wait for pid to exit
  while (HANDLE proc = OpenProcess(0, false, pid)) {
    CloseHandle(proc);
    MTrace(t_update, trace::Info, "Waiting for caller to exit");
    Sleep(1000);
  }
  MTrace(t_update, trace::Debug, "Caller exited, proceed with upgrade");

  try {
    // Step 1: Load the package.xml document
    std::ostringstream uri;
    uri <<  "/files/download/updates/desktop/pc/"
        << g_getArchitecture()
        << "-" << buildno << "/package.xml";

    ServerConnection conn(host, 443, true);
    ServerConnection::Request req(ServerConnection::mGET, uri.str());
    ServerConnection::Reply rep = conn.execute(req);
  
    // Step 2: Download all files
    if (rep.getCode() != 200)
      throw error("Cannot fetch update package at " + uri.str()
                   + ": " + rep.toString());

    using namespace xml;
    uint32_t buildno;
    std::string file;
    const IDocument &doc = mkDoc
      (Element("package")
       (Element("build")(CharData<uint32_t>(buildno))
        & *Element("file")(CharData<std::string>(file))
        [  papply<bool,const uint32_t&,const std::string&, const std::string&>( &downloadFile, buildno, file, host ) ]));

    MTrace(t_update, trace::Info, "Downloading upgrade package to build "
           << buildno);

    std::istringstream s(std::string(rep.refBody().begin(), rep.refBody().end()));
    XMLexer lexer(s);
    doc.process(lexer);
  
  } catch (error &e) {
    MTrace(t_update, trace::Warn, "Upgrade failed: " << e.toString());
  }

  // Step 3: Re-start keepitw
  std::string orig(getCurrentModuleDirectory() + "\\keepitw.exe");

  STARTUPINFO start;
  memset(&start, 0, sizeof start);
  start.cb = sizeof start;
    
  PROCESS_INFORMATION info;
  if (!CreateProcess(utf8_to_utf16(orig).c_str(), 0, 0, 0, false,
                     NORMAL_PRIORITY_CLASS, 0, 0, &start, &info))
    throw syserror("CreateProcess", "spawning keepitw " + orig);

}

std::string getCurrentModule()
{
  wchar_t buf[32768];
  const size_t elms = sizeof buf / sizeof buf[0];
  SetLastError(ERROR_SUCCESS);
  DWORD rc = GetModuleFileName(0, buf, elms);
  if (rc == elms || !rc) // Yes the return code is rather
    // ambiguous on error...
    throw syserror("GetModuleFileName", "getting module path for upgrade");
  return utf16_to_utf8(buf);
}

std::string getCurrentModuleDirectory()
{
  const std::string c(getCurrentModule());
  return c.substr(0, c.find_last_of("\\"));
}

