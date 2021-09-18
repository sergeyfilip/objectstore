//
// Demo client - simple backup and restore
//

#include "common/error.hh"
#include "common/trace.hh"
#include "common/scopeguard.hh"
#include "common/partial.hh"
#include "common/string.hh"
#include "xml/xmlio.hh"

#include "backup/metatree.hh"
#include "backup/upload.hh"
#include "client/serverconnection.hh"
#include "fsobject.hh"

#include <iostream>
#include <sstream>
#include <fstream>
#include <algorithm>

#include <errno.h>
#include <stdint.h>

#if defined(__unix__) || defined(__APPLE__)
# include <sys/types.h>
# include <sys/stat.h>
# include <fcntl.h>
# include <unistd.h>
# include <string.h>
# include <signal.h>
namespace {
  bool sig_exit(false);
  void sig_exit_hnd(int) { sig_exit = true; }
}
#endif

namespace {
  //! Tracer for high-level operations
  trace::Path t_hi("/democli");

  //! Tracer for serialisation
  trace::Path t_ser("/democli/ser");

}

//! Client configuration file parser - we store our access token in a
//! configuration file
class Config {
public:
  Config(const std::string& cn);

  //! Read config
  Config &read();

  //! Write config
  Config &write();

  //! Property: access token name
  std::string m_aname;

  //! Property: access token password
  std::string m_apass;

  //! Property: optional; our device name
  std::string m_devname;

  //! Property: optional; file name
  std::string m_fname;

private:
  enum op_t { op_r, op_w };
  //! Document handler - read or write
  Config &doch(op_t);
};


//! Print error and usage information, return 1.
int usage(char **argv, const std::string &error);

void handleLogin(ServerConnection &conn, Config &conf);
void handleListdev(ServerConnection &conn, Config &conf);
void handleNewdev(ServerConnection &conn, Config &conf);
void handleHistory(ServerConnection &conn, Config &conf);
void handleUpload(ServerConnection &conn, const std::string &devname);
void handleCDP(ServerConnection &conn, Config &conf);
void handleList(ServerConnection &conn, Config &conf, const std::string &hash);
void handleRestore(ServerConnection &conn, Config &conf, const std::string &hash);

int main(int argc, char **argv) try
{
# if defined(__unix__) || defined(__APPLE__)
  struct sigaction nact;
  struct sigaction pact;
  memset(&nact, 0, sizeof nact);
  nact.sa_handler = SIG_IGN;
  sigaction(SIGPIPE, &nact, &pact);
  ON_BLOCK_EXIT(sigaction, SIGPIPE, &pact, &nact);
# endif

#if defined(_WIN32)
  { WSADATA tmp;
    if (WSAStartup(0x0202, &tmp))
      throw error("Cannot initialize winsock");
  }
  ON_BLOCK_EXIT(WSACleanup);
#endif

  std::string s_server("ws.keepit.com");
  std::string s_port("443");
  std::string s_hash;
  std::string conf_name;
  bool b_nossl = false;
  enum { M_LOGIN,
         M_LISTDEV, M_NEWDEV,
         M_HISTORY,
         M_UPLOAD, M_CDP, M_RESTORE,
         M_LIST, M_NONE } mode(M_NONE);

  if (argc < 1)
    throw error("Cannot run with non-positive argc");

  for (int i = 1; i != argc; ++i) {
    struct fn {
      fn(int argc_, int &i_, char **argv_)
        : argc(argc_), i(i_), argv(argv_) { }
      std::string fetchNext() {
        if (++i == argc)
          throw error("Expected more arguments");
        return argv[i];
      }
      int argc;
      int& i;
      char **argv;
    } l_fn(argc,i,argv);
    const std::string arg(argv[i]);
    if (arg == "-h") {
      // parse host
      s_server = l_fn.fetchNext();
    } else if (arg == "-p") {
      // parse port
      s_port = l_fn.fetchNext();
    } else if (arg == "login") {
      // mode is login
      mode = M_LOGIN;
    } else if (arg == "listdev") {
      // mode is listdev
      mode = M_LISTDEV;
    } else if (arg == "newdev") {
      // mode is newdev
      mode = M_NEWDEV;
    } else if (arg == "history") {
      // mode is history
      mode = M_HISTORY;
    } else if (arg == "upload") {
      // mode is upload
      mode = M_UPLOAD;
    } else if (arg == "cdp") {
      // mode is cdp
      mode = M_CDP;
    } else if (arg == "list") {
      // parse hash.
      // mode is list
      mode = M_LIST;
      s_hash = l_fn.fetchNext();
    } else if (arg == "restore") {
      // parse hash.
      // mode is restore
      mode = M_RESTORE;
      s_hash = l_fn.fetchNext();
    } else if (arg == "-f") {
      conf_name = l_fn.fetchNext();
    } else if (arg == "--nossl") {
      b_nossl = true;
    } else {
      // Otherwise we failed...
      return usage(argv, "Cannot recognise parameter: " + arg);
    }
  }

  uint16_t port;
  { std::istringstream istr(s_port);
    istr >> port;
    if (istr.fail())
      throw error("Cannot parse port: " + s_port);
    if (!istr.eof())
      throw error("Trailing data after port: " + s_port);
  }

  if (mode == M_NONE)
    return usage(argv, "No mode given");

  trace::StreamDestination logstream(std::cerr);

  trace::Path::addDestination(trace::Warn, "*", logstream);
  trace::Path::addDestination(trace::Info, "*", logstream);
  //  trace::Path::addDestination(trace::Debug, "/upload", logstream);
  //  trace::Path::addDestination(trace::Debug, "/upload/worker", logstream);
  //  trace::Path::addDestination(trace::Debug, "/democli/ser", logstream);
  //  trace::Path::addDestination(trace::Debug, "/cache", logstream);

  { ServerConnection conn(s_server, port, !b_nossl);
    Config conf(conf_name);

    // Unless we are logging in, read our login credentials from the
    // saved configuration
    if (mode != M_LOGIN)
      conf.read();

    switch (mode) {
    case M_LOGIN:
      handleLogin(conn,conf); break;
    case M_LISTDEV:
      handleListdev(conn,conf); break;
    case M_NEWDEV:
      handleNewdev(conn,conf); break;
    case M_HISTORY:
      handleHistory(conn,conf); break;
    case M_UPLOAD:
      conn.setDefaultBasicAuth(conf.m_aname, conf.m_apass);
      handleUpload(conn,conf.m_devname); break;
    case M_CDP:
      handleCDP(conn,conf); break;
    case M_LIST:
      conn.setDefaultBasicAuth(conf.m_aname, conf.m_apass);
      handleList(conn,conf, s_hash); break;
    case M_RESTORE:
      handleRestore(conn,conf, s_hash); break;
    default:
      throw error("Mode disappeared");
    }

    conn.traceStatistics();
  }

} catch (error &e) {
  std::cerr << std::endl
            << e.toString() << std::endl;
  return 1;
}

Config::Config(const std::string& cn)
  : m_fname(cn)
{}

Config &Config::read()
{
  return doch(op_r);
}

Config &Config::write()
{
  return doch(op_w);
}

Config &Config::doch(op_t op)
{
  if(m_fname.empty())
    m_fname = ".democli";

  using namespace xml;
  const IDocument &confdoc
    = mkDoc(Element("democli")
            (Element("aname")(CharData<std::string>(m_aname))
             & Element("apass")(CharData<std::string>(m_apass))
             & !Element("devname")(CharData<std::string>(m_devname))));

  if (op == op_r) {
    // Load configuration
    std::ifstream file(m_fname.c_str());
    if (!file)
      throw error("Cannot read configuration file: " + m_fname);
    XMLexer lexer(file);
    confdoc.process(lexer);
  } else {
    // Write configuration
    std::ofstream file(m_fname.c_str());
    if (!file)
      throw error("Cannot write configuration file: " + m_fname);
    XMLWriter writer(file);
    confdoc.output(writer);
  }
  return *this;
}

int usage(char **argv, const std::string &error)
{
  std::cerr << error << std::endl << std::endl
            << "Usage: " << argv[0] << " "
            << "[-h {host}] "
            << "[-p {port}] "
            << "[--nossl] "
            << "[-f {config file path}] "
            << "{login | listdev | newdev {devname} "
            << "| upload | cdp | list {hash} | restore {hash}}"
            << std::endl << std::endl;
  return 1;
}

void handleLogin(ServerConnection &conn, Config &conf)
{
  //
  // Query user for username and password - we need these to
  // authenticate for token generation, but we will never store these
  // credentials. We only store the token we generate.
  //
  std::string username;
  std::cout << "Username: " << std::flush;
  std::cin >> username;
  std::string userpass;
  std::cout << "Password: " << std::flush;
  std::cin >> userpass;

  MTrace(t_hi, trace::Info, "Creating access token");

  ServerConnection::Request req(ServerConnection::mPOST, "/tokens/");
  req.setBasicAuth(username, userpass);

  { using namespace xml;
    std::string descr("democli client");
    { char hname[1024];
      if (gethostname(hname, sizeof hname))
        throw syserror("gethostname", "retrieving local host name");
      descr +=  "on " + std::string(hname);
    }

    // The access token name is a GUID - see rfc 4122
    conf.m_aname = randStr(16);

    // The access token password is a long random string
    conf.m_apass = randStr(16);

    std::string ttype("Device");

    const IDocument &doc = mkDoc
      (Element("token")
       (Element("descr")(CharData<std::string>(descr))
        & Element("type")(CharData<std::string>(ttype))
        & Element("aname")(CharData<std::string>(conf.m_aname))
        & Element("apass")(CharData<std::string>(conf.m_apass))));

    req.setBody(doc);
  }

  ServerConnection::Reply rep = conn.execute(req);
  if (rep.getCode() == 201) {
    conf.write();
    MTrace(t_hi, trace::Info, "Access token acquired and stored");
  } else if (rep.getCode() == 401) {
    MTrace(t_hi, trace::Warn, "Authentication failed.");
  } else {
    MTrace(t_hi, trace::Warn, "Received error: " << rep.toString());
  }
}



bool gotDevice(const Config &conf, const std::string &devname)
{
  std::cout << (devname == conf.m_devname ? "=> " : "-> ")
            << devname
            << (devname == conf.m_devname ? " <= (this device)" : "")
            << std::endl;
  return true;
}

void handleListdev(ServerConnection &conn, Config &conf)
{
  // Query API for list of devices
  // GET /devices/
  //
  ServerConnection::Request req(ServerConnection::mGET, "/devices/");
  req.setBasicAuth(conf.m_aname, conf.m_apass);
  MTrace(t_hi, trace::Info, "using credentials user=" << conf.m_aname
         << ", pass=" << conf.m_apass);

  ServerConnection::Reply rep = conn.execute(req);
  if (rep.getCode() != 200) {
    MTrace(t_hi, trace::Warn, "Received error: " << rep.toString());
    return;
  }

  std::string devname;
  std::string devtype;
  std::string devuri;
  std::string devlogin;
  std::string devpass;

  // Fine, parse response
  using namespace xml;
  const IDocument &ddoc
    = mkDoc(Element("devices")
            (*Element("pc")
             (Element("name")(CharData<std::string>(devname)))
             [ papply<bool,const Config&,const std::string&>
               (&gotDevice, conf, devname) ])
            & (*Element("cloud")
               (Element("name")(CharData<std::string>(devname))
                & Element("uri")(CharData<std::string>(devuri))
                & Element("type")(CharData<std::string>(devtype))
                & Element("login")(CharData<std::string>(devlogin))
                & Element("password")(CharData<std::string>(devpass)))
               [ papply<bool,const Config&,const std::string&>
                 (&gotDevice, conf, devname) ]));

  std::cout << "List of devices attached to account:" << std::endl;
  std::istringstream s(std::string(rep.refBody().begin(), rep.refBody().end()));
  XMLexer lexer(s);
  ddoc.process(lexer);

}

void handleNewdev(ServerConnection &conn, Config &conf)
{
  std::string devname;
  {
    char dname[1024];
    std::cout << "Device name: " << std::flush;
    std::cin.getline(dname, sizeof dname);
    devname = dname;
  }

  // Now ask API to create PC device
  // POST /devices/

  ServerConnection::Request req(ServerConnection::mPOST, "/devices/");
  req.setBasicAuth(conf.m_aname, conf.m_apass);

  using namespace xml;
  const IDocument &doc = mkDoc
    (Element("pc")
     (Element("name")(CharData<std::string>(devname))));

  req.setBody(doc);

  ServerConnection::Reply rep = conn.execute(req);
  if (rep.getCode() != 201) {
    MTrace(t_hi, trace::Warn, "Got error: " << rep.toString());
    return;
  }

  conf.m_devname = devname;
  conf.write();
  MTrace(t_hi, trace::Info, "Device \"" << devname << "\" created and set.");
}


namespace {
  /// De-serialisation. Given a buffer and an offset, decode data and
  /// increment offset
  template <typename t>
  t des(const std::vector<uint8_t> &b, size_t &ofs);

  template <>
  uint8_t des<uint8_t>(const std::vector<uint8_t> &b, size_t &ofs)
  {
    if (b.size() < ofs + 1)
      throw error("Object ended before uint8_t");
    uint16_t res = b[ofs];
    ofs ++;
    return res;
  }
  template <>
  uint16_t des<uint16_t>(const std::vector<uint8_t> &b, size_t &ofs)
  {
    if (b.size() < ofs + 2)
      throw error("Object ended before uint16_t");
    uint16_t res = (uint16_t(b[ofs]) << 8) | b[ofs + 1];
    ofs += 2;
    return res;
  }

  template <>
  uint32_t des<uint32_t>(const std::vector<uint8_t> &b, size_t &ofs)
  {
    if (b.size() < ofs + 4)
      throw error("Object ended before uint32_t");
    uint32_t res
      = (uint32_t(b[ofs]) << 24)
      | (uint32_t(b[ofs+1]) << 16)
      | (uint32_t(b[ofs+2]) << 8)
      | b[ofs + 3];
    ofs += 4;
    return res;
  }

  template <>
  uint64_t des<uint64_t>(const std::vector<uint8_t> &b, size_t &ofs)
  {
    if (b.size() < ofs + 8)
      throw error("Object ended before uint64_t");
    uint64_t res
      = (uint64_t(b[ofs]) << 56)
      | (uint64_t(b[ofs+1]) << 48)
      | (uint64_t(b[ofs+2]) << 40)
      | (uint64_t(b[ofs+3]) << 32)
      | (uint64_t(b[ofs+4]) << 24)
      | (uint64_t(b[ofs+5]) << 16)
      | (uint64_t(b[ofs+6]) << 8)
      | b[ofs + 7];
    ofs += 8;
    return res;
  }

  template <>
  objseq_t des<objseq_t>(const std::vector<uint8_t> &b, size_t &ofs)
  {
    const uint32_t len = des<uint32_t>(b, ofs);
    if (b.size() < ofs + len * 32)
      throw error("Object ended before objseq_t");
    objseq_t res(len);
    for (size_t i = 0; i != len; ++i) {
      res[i] = sha256::parse(std::vector<uint8_t>(&b[ofs], &b[ofs + 32]));
      ofs += 32;
    }
    return res;
  }

  template <>
  std::string des<std::string>(const std::vector<uint8_t> &b, size_t &ofs)
  {
    const uint32_t len = des<uint32_t>(b, ofs);
    if (b.size() < ofs + len)
      throw error("Object ended before string");
    std::string res(&b[ofs], &b[ofs + len]);
    ofs += len;
    return res;
  }

  /// Backup history printing...
  struct job_t {
    Time tstamp;
    std::string root;
  };

  bool gotBackup(const job_t &job)
  {
    std::cout << "Time: " << job.tstamp << std::endl
              << "Root: " << job.root << std::endl;
    return true;
  }

}


void handleHistory(ServerConnection &conn, Config &conf)
{
  // Ask the server for the backup history on this device
  ServerConnection::Request req(ServerConnection::mGET, "/devices/"
                                + str2url(conf.m_devname) + "/history");
  req.setBasicAuth(conf.m_aname, conf.m_apass);

  ServerConnection::Reply rep = conn.execute(req);
  if (rep.getCode() != 200) {
    MTrace(t_hi, trace::Warn, "Received error: " << rep.toString());
    return;
  }

  // Fine, parse response
  job_t job;
  using namespace xml;
  const IDocument &ddoc
    = mkDoc(Element("history")
            (*Element("backup")
             (Element("tstamp")(CharData<Time>(job.tstamp))
              & Element("root")(CharData<std::string>(job.root)))
             [ papply<bool,const job_t&>(&gotBackup, job) ]));

  std::cout << "List of backups on this device:" << std::endl;
  std::istringstream s(std::string(rep.refBody().begin(), rep.refBody().end()));
  XMLexer lexer(s);
  ddoc.process(lexer);
}

Semaphore* p_wait_sem=0;
  
void handleUploadCompletion(Upload &upload)
{
  if (p_wait_sem) {
    p_wait_sem->increment();
  }
}

void handleUpload(ServerConnection &conn, const std::string &devname)
{
  // Initialise cache
  FSCache cache(".democli-cache.db");

  Time tstamp(Time::now());

  // Perform incremental upload
  MTrace(t_hi, trace::Info, "Will upload changes");

  std::string cwd;
#if defined(__unix__) || defined(__APPLE)
  { std::vector<char> buf(1024);
    char * res;
    while (!(res = getcwd(&buf[0], buf.size()))
           && errno == ENAMETOOLONG)
      buf.resize(buf.size() * 2);
    if (!res)
      throw error("Cannot retrieve current working directory");
    cwd = std::string(buf.begin(), std::find(buf.begin(), buf.end(), 0));
  }
#endif
#if defined(_WIN32)
  { TCHAR buf[32768];
    if (!GetCurrentDirectory(sizeof buf, buf))
      throw error("Cannot retrieve current workingd directory");
    cwd = utf16_to_utf8(buf);
  }
#endif

  // Start upload
  Upload upload(cache, conn, devname, cwd);
  upload
    .setWorkers(4)
    .setCompletionNotification(papply(&handleUploadCompletion));
  
  // Set up wait condition
  Semaphore wait;
  p_wait_sem = &wait;

  // Start backup
  bool res = upload.startUpload();
  MAssert(res, "performUpload failed starting upload");
  // Wait for completion
  wait.decrement();
  p_wait_sem = 0;

  MTrace(t_hi, trace::Info, "Time stamp is: " << tstamp);
}
  
void handleCDP(ServerConnection &conn, Config &conf)
{
  throw error("CDP not yet functional");
#if 0
  // Update tree to match local file system
  MTrace(t_hi, trace::Info, "Building initial meta-data tree");
  root.scanLocal();

  while (true) {
    // Perform incremental upload
    MTrace(t_hi, trace::Info, "Will upload changes");
    root.upload(conn);
    conn.traceStatistics();

    // Print hash of root
    MTrace(t_hi, trace::Info, "Upload complete: "
           << root.getHash().m_hex);

    // Now watch for FS changes.
    //
    // For "small" files, we want to upload changes a few seconds
    // after the file was last changed. This prevents us from having
    // to re-re-re-upload a rapid succession of changes. Still, we
    // want to upload the file regardless, no more than a few handfuls
    // of minutes after {its first change since its last backup}.
    //
    // For "large" files we need to exercise more restraint.
    //
  }
#endif
}

void sublist(size_t indent, ServerConnection &conn, Config &conf, const sha256 &hash)
{
  MTrace(t_hi, trace::Debug, "Loading child at level " << indent);

  std::vector<uint8_t> obj;
  fetchObject(conn, hash, obj);

  size_t ofs = 0;

  // First, see that it is a version 0 object
  if (des<uint8_t>(obj, ofs))
    throw error("Object is not version 0");

  // Then, treat object depending on type
  switch (des<uint8_t>(obj, ofs)) {
  case 0xfd: // 0xfd = file data
    std::cout << std::string(indent, ' ') << "[file data]" << std::endl;
    break;
  case 0xdd: { // 0xdd = partial directory entry
    std::cout << std::string(indent, ' ')
    << "This is partially uploaded directory"
    << std::endl;
  }
  case 0xde: { // 0xde = complete directory entry
    // Cumulative size of this object and all its children
    const uint64_t treesize = des<uint64_t>(obj, ofs);
    std::cout << std::string(indent, ' ')
              << "(total subtree size is " << treesize << " bytes)"
              << std::endl;
    // Next, we have a 32-bit integer with the length of our LoR
    const uint32_t lor_len = des<uint32_t>(obj, ofs);
    // Now read LoR
    std::vector<objseq_t> lor;
    for (size_t i = 0; i != lor_len; ++i)
      lor.push_back(des<objseq_t>(obj, ofs));
    // And read the same number of elements in the LoM
    for (size_t i = 0; i != lor_len; ++i) {
      // Read data type entry
      const uint8_t entrytype = des<uint8_t>(obj, ofs);
      switch (entrytype) {
      case 0x01: { // 0x01 => regular file
        const std::string name = des<std::string>(obj, ofs);
        const std::string user = des<std::string>(obj, ofs);
        const std::string group = des<std::string>(obj, ofs);
        const uint32_t mode = des<uint32_t>(obj, ofs);
        const uint64_t mtime = des<uint64_t>(obj, ofs);
        const uint64_t ctime = des<uint64_t>(obj, ofs);
        const uint64_t size = des<uint64_t>(obj, ofs);
        std::cout << std::string(indent, ' ') << "F:"
                  << name << " (" << user << ":" << group << ") perm="
                  << std::oct << mode << " "
                  << std::dec << size << " bytes" << std::endl;
        (void)mtime;
        (void)ctime;
        break;
      }
      case 0x02: { // 0x02 => directory
        const std::string name = des<std::string>(obj, ofs);
        const std::string user = des<std::string>(obj, ofs);
        const std::string group = des<std::string>(obj, ofs);
        const uint32_t mode = des<uint32_t>(obj, ofs);
        const uint64_t mtime = des<uint64_t>(obj, ofs);
        const uint64_t ctime = des<uint64_t>(obj, ofs);
        std::cout << std::string(indent, ' ') << "D:"
                  << name << " (" << user << ":" << group << ") perm="
                  << std::oct << mode << std::endl;
        (void)mtime;
        (void)ctime;

        // For each entry in our LoR, sublist
        for (size_t sub = 0; sub != lor[i].size(); ++sub)
          sublist(indent + 1, conn, conf, lor[i][sub]);

        break;
      }
      default:
        throw error("Unknown meta data type entry");
      }
    }
    break;
  }
  default:
    throw error("Unknown object entry");
  }
}



void handleList(ServerConnection &conn, Config &conf, const std::string &hash)
{
  // Start at the root
  MTrace(t_hi, trace::Info, "Loading root for listing");
  sublist(0, conn, conf, sha256::parse(hash));
}

void handleRestore(ServerConnection &conn, Config &conf, const std::string &hash)
{
  // Simply download and write data
  MTrace(t_hi, trace::Info, "Loading root for restore");
}



