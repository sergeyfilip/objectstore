//
// Object store daemon
//

#include "httpd/httpd.hh"
#include "common/error.hh"
#include "common/trace.hh"
#include "common/hash.hh"
#include "common/scopeguard.hh"
#include "objparser/objparser.hh"
#include "xml/xmlio.hh"
#include "version.hh"
#include "mirror.hh"

#include "main.hh"

#include <iostream>
#include <sstream>
#include <iomanip>
#include <string>
#include <stdio.h>
#include <stdint.h>
#include <fstream>

#if defined(__unix__) || defined(__APPLE__)
# include <sys/types.h>
# include <sys/time.h>
# include <sys/resource.h>
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
  //! Trace path for main server operations
  trace::Path t_stord("/stord");
}

//! Processing statistics
namespace stats {
  //! Hold this when updating/reading stats
  Mutex lock;
  //! We keep structure for GET, HEAD and POST
  enum stat_e { GET = 0, HEAD = 1, POST = 2 };
  //! Our structures for GET, HEAD and POST
  std::vector<std::deque<Time> > history(3);

  //! Trim list so it only contains entries for 1 minute - assumes
  //! lock is held!
  void l_trim(stat_e h) {
    static const DiffTime m(DiffTime::iso("PT1M"));
    const Time now(Time::now());
    while (!history[h].empty() && now - history[h].back() > m)
      history[h].pop_back();
  }

  //! Retrieve size of requested list
  size_t get1m(stat_e h) {
    MutexLock l(lock);
    l_trim(h);
    return history[h].size();
  }

  //! Pre-pend entry to requested list, trim off entries to maintain 1
  //! minute history
  void put1m(stat_e h) {
    MutexLock l(lock);
    const Time now(Time::now());
    history[h].push_front(now);
    l_trim(h);
  }

}


class SvcConfig {
public:
  //! Load and parse configuration from file
  SvcConfig(const char *fname);

  //! Property: port to bind to
  uint16_t bindPort;

  //! Property: number of worker threads
  size_t workerThreads;

  //! Property: root of data directory
  std::string root;

  //! Property: Maximum number of incoming connections
  size_t maxConnections;

  //! Property: Type of directory entry treesize/reference check:
  //! "full" or "simple". "full" incurs a significant number of lstat
  //! and open/read calls on every single directory object
  //! POST. "simple" is free but only performs the most rudimentary
  //! treesize check and no object reference checking.
  std::string dirCheck;

  //! Handling of optional mirror configuration
  struct cMirror {
    cMirror() : mirror(0) { }
    ~cMirror() { delete mirror; }
    //! Temporary variables for use during parsing
    struct mirror_t {
      std::string host;
      uint16_t port;
      std::string tododir;
      size_t threads;
    } tmp;
    mirror_t *mirror;
    //! Callback for setting the mirror
    bool set();
  } hMirror;


  //! Our mirror server - who do we replicate to?
  std::string mirrorHost;
  uint16_t mirrorPort;

  //! Our non-replicated objects database directory. This directory
  //! holds text files with object names as 64-character hexadecimal
  //! strings, one name per line. Each file holds up to 100
  //! names. When all objects in a file has been synchronised, the
  //! file is deleted.
  std::string replicaTodoDir;

  //! Number of replication worker threads
  size_t replicatorThreads;
};


class MyWorker : public Thread {
public:
  MyWorker(HTTPd &httpd, const SvcConfig &cfg,
           refcount_ptr<mirror::Supervisor> m);
  virtual ~MyWorker();

  //! Sets a notification callback for before we write original
  //! objects. The worker object is taking ownership of the binder
  MyWorker &setPreWriteNotifier(BindF1Base<void,const sha256&> *);

  //! Sets a sync-mirror callback for after we write original objects
  MyWorker &setPostWriter(BindF1Base<void,const sha256&>*);

protected:
  //! Our actual worker thread
  void run();
private:
  //! Handle GET /status - status output
  void statusGET(const HTTPRequest &req);
  //! Handle HEAD requests - existence of object
  void handleHEAD(const HTTPRequest &req);
  //! Handle GET requests - retrieval of object
  void handleGET(const HTTPRequest &req);
  //! Handle POST requests - creation of object
  void handlePOST(const HTTPRequest &req);

  //! Fetching of objects
  std::vector<uint8_t> localObjectFetch(const sha256&) const;

  //! Fetching of local object size
  uint64_t localObjectSize(const sha256&) const;

  //! URI parser - parse the URI and see that it is a valid SHA256 hash
  std::string getHash(const HTTPRequest &req);

  //! Reference to the httpd (needed for getting requests and posting
  //! replies)
  HTTPd &m_httpd;

  //! Reference to our configuration
  const SvcConfig &m_cfg;

  //! Our mirroring logic (may not be set!)
  refcount_ptr<mirror::Supervisor> m_mirror;

  //! When we write non-replicated objects we call this callback to
  //! log the object for mirroring
  BindF1Base<void,const sha256&> *m_pre_orig_write;
  BindF1Base<void,const sha256&> *m_post_orig_write;
};



int main(int argc, char **argv) try
{
  if (argc != 2) {
    std::cerr << "usage: " << argv[0] << " {service configuration file}" << std::endl;
    return 1;
  }

# if defined(__unix__) || defined(__APPLE__)
  struct sigaction nact;
  struct sigaction pact;
  memset(&nact, 0, sizeof nact);
  nact.sa_handler = SIG_IGN;
  sigaction(SIGPIPE, &nact, &pact);
  ON_BLOCK_EXIT(sigaction, SIGPIPE, &pact, &nact);
# endif

  trace::StreamDestination logstream(std::cerr);
  trace::SyslogDestination logsys("stord");

  trace::Path::addDestination(trace::Warn, "*", logstream);
  trace::Path::addDestination(trace::Warn, "*", logsys);
  trace::Path::addDestination(trace::Info, "*", logsys);

  SvcConfig conf(argv[1]);

  MTrace(t_stord, trace::Info, "OS/API " << g_getVersion() << " starting...");

  // We need to set the max open files limit to at least:
  // 1: our number of threads times two (1 for a file, 1 for database fd)
  // 2: plus one fd for each incoming HTTP connection
  // 3: plus two fds for each mirroring thread
  // 4: plus some overhead.
  { struct rlimit lim;
    if (getrlimit(RLIMIT_NOFILE, &lim))
      throw syserror("getrlimit", "getting file descriptor limit");
    const size_t wanted
      = 2 * conf.workerThreads
      + conf.maxConnections
      + 2 * (conf.hMirror.mirror ? conf.hMirror.mirror->threads : 0)
      + 50;
    // See if we want more than the hard limit
    if (wanted > lim.rlim_max)
      throw error("Hard file limit too low for our number of threads");
    // Only set limit if we need more than we have
    if (wanted > lim.rlim_cur) {
      lim.rlim_cur = wanted;
      if (setrlimit(RLIMIT_NOFILE, &lim))
        throw syserror("setrlimit", "setting file descriptor limit");
      MTrace(t_stord, trace::Info, "Increased file descriptor limit to "
             << wanted);
    }
  }

  try {
    // Start up the mirroring supervisor if mirroring is configured
    // (and have it automatically destroyed on exit by using a
    // reference counted pointer)
    refcount_ptr<mirror::Supervisor> mirror;
    if (conf.hMirror.mirror) {
      mirror = new mirror::Supervisor(conf.hMirror.mirror->host,
                                      conf.hMirror.mirror->port,
                                      conf.hMirror.mirror->tododir,
                                      conf.hMirror.mirror->threads,
                                      conf.root);
    }

    // Set up web server, listening to configured port
    HTTPd httpd;
    httpd.setMaxConnections(conf.maxConnections);
    httpd.addListener(conf.bindPort);

    // Start worker threads
    std::vector<MyWorker> workers(conf.workerThreads, MyWorker(httpd, conf, mirror));
    for (size_t i = 0; i != workers.size(); ++i) {
      // Add the notification callback for the mirror
      if (mirror)
        workers[i]
          .setPreWriteNotifier( papply<void,mirror::Supervisor,const sha256&>
                                (&*mirror, &mirror::Supervisor::logObjectReplication)
                                .clone() )
          .setPostWriter(papply<void,mirror::Supervisor,const sha256&>
                         (&*mirror, &mirror::Supervisor::replicateObject)
                         .clone());
      // Start the worker
      workers[i].start();
    }

    MTrace(t_stord, trace::Info, "OS/API Servicing requests on port "
           << conf.bindPort);

    // Hang here until we should exit
#if defined(__unix__) || defined(__APPLE__)
    { struct sigaction act;
      memset(&act, 0, sizeof act);
      act.sa_handler = sig_exit_hnd;
      if (sigaction(SIGINT, &act, 0))
        throw syserror("sigaction", "configuring termination handling");
      while (!sig_exit) {
        sleep(10);
        // trace; system status/utilisation/...
        MTrace(t_stord, trace::Info, "Workqueue length: "
               << httpd.getQueueLength() << " jobs");
      }
    }
#endif

    MTrace(t_stord, trace::Info, "Shutting down...");

    // Stop the server (will begin issuing stop requests to workers)
    httpd.stop();

    // Wait until all workers have exited
    for (size_t i = 0; i != workers.size(); ++i)
      workers[i].join_nothrow();
  } catch (error &e) {
    MTrace(t_stord, trace::Warn, e.toString());
  }

} catch (error &e) {
  std::cerr << e.toString() << std::endl;
  return 1;
}


///////////////////////////////////////////////////////////////
// Configuration loader
///////////////////////////////////////////////////////////////


SvcConfig::SvcConfig(const char *fname)
  : bindPort(0)
  , workerThreads(2)
  , maxConnections(20)
  , dirCheck("simple")
{
  // Define configuration document schema
  using namespace xml;
  const IDocument &confdoc
    = mkDoc(Element("stord")
            (Element("bindPort")(CharData<uint16_t>(bindPort))
             & Element("workerThreads")(CharData<size_t>(workerThreads))
             & Element("root")(CharData<std::string>(root))
             & Element("maxConnections")(CharData<size_t>(maxConnections))
             & Element("dirCheck")(CharData<std::string>(dirCheck))
             & !Element("mirror")
             (Element("host")(CharData<std::string>(hMirror.tmp.host))
              & Element("port")(CharData<uint16_t>(hMirror.tmp.port))
              & Element("todoDir")(CharData<std::string>(hMirror.tmp.tododir))
              & Element("threads")(CharData<size_t>(hMirror.tmp.threads)))
             [ papply(&hMirror, &cMirror::set) ]));

  // Load configuration
  std::ifstream file(fname);
  if (!file)
    throw error("Cannot open configuration file: " + std::string(fname));
  XMLexer lexer(file);
  confdoc.process(lexer);

  if (dirCheck != "full" && dirCheck != "simple")
    throw error("dirCheck property must be \"full\" or \"simple\"");
}


bool SvcConfig::cMirror::set()
{
  MAssert(!mirror, "Mirror already set");
  mirror = new mirror_t(tmp);
  MTrace(t_stord, trace::Debug, "Set mirror configuration ["
         << tmp.host << ":" << tmp.port << "] " << tmp.tododir
         << " (" << tmp.threads << " threads)");
  return true;
}


///////////////////////////////////////////////////////////////
// Request worker
///////////////////////////////////////////////////////////////

MyWorker::MyWorker(HTTPd &httpd, const SvcConfig &cfg,
                   refcount_ptr<mirror::Supervisor> m)
  : m_httpd(httpd)
  , m_cfg(cfg)
  , m_mirror(m)
  , m_pre_orig_write(0)
  , m_post_orig_write(0)
{
}

MyWorker::~MyWorker()
{
  delete m_post_orig_write;
  delete m_pre_orig_write;
}

MyWorker &MyWorker::setPreWriteNotifier(BindF1Base<void,const sha256&> *n)
{
  delete m_pre_orig_write;
  m_pre_orig_write = n;
  return *this;
}

MyWorker &MyWorker::setPostWriter(BindF1Base<void,const sha256&> *n)
{
  delete m_post_orig_write;
  m_post_orig_write = n;
  return *this;
}

void MyWorker::run() try
{
  // Process requests until we are told to exit
  while (true) {
    HTTPRequest req(m_httpd.getRequest());
    if (req.isExitMessage()) {
      break;
    }

    try {
      // See if we match /status
      if (req.consumeComponent("/status")) {
        switch (req.getMethod()) {
        case HTTPRequest::mGET: statusGET(req); break;
        default:
          m_httpd.postReply(HTTPReply(req.m_id, true, 405,
                                      HTTPHeaders().add("allow", "GET"),
                                      std::string()));
        }
        continue;
      }

      // See if we match /object
      if (!req.consumeComponent("/object")) {
        m_httpd.postReply(HTTPReply(req.m_id, true, 404,
                                    HTTPHeaders()
                                    .add("content-type", "plain/text"),
                                    "No such name space"));
        continue;
      }

      // Fine, treat
      switch (req.getMethod()) {
      case HTTPRequest::mGET:
        stats::put1m(stats::GET);
        handleGET(req);
        break;
      case HTTPRequest::mPOST:
        stats::put1m(stats::POST);
        handlePOST(req);
        break;
      case HTTPRequest::mHEAD:
        stats::put1m(stats::HEAD);
        handleHEAD(req);
        break;
      default:
        m_httpd.postReply(HTTPReply(req.m_id, true, 405,
                                    HTTPHeaders()
                                    .add("allow", "HEAD, GET, POST"),
                                    std::string()));
        continue;
      }
    } catch (error &e) {
      MTrace(t_stord, trace::Warn, "Error processing request "
             << req.toString() << ": " << e.toString());
      m_httpd.postReply(HTTPReply(req.m_id, true, 500,
                                  HTTPHeaders().add("content-type", "text/plain"),
                                  e.toString() + "\n"));
    }

  }
} catch (error &e) {
  std::cerr << "Worker caught: " << e.toString() << std::endl;
}


void MyWorker::handleHEAD(const HTTPRequest &req)
{
  const std::string name = m_cfg.root + splitName(getHash(req));

  // See whether the file exists
  bool exists = false;
#if defined(__unix__) || defined(__APPLE__)
  if (!access(name.c_str(), F_OK)) {
    // If access succeeds, the file exists.
    exists = true;
  } else {
    // In case access fails, we validate that it fails because the
    // file isn't there. If it fails for any other reason, we want to
    // fail the request.
    if (errno != ENOENT)
      throw syserror("access", "existence checking");
  }
#endif

  // So, respond accordingly..
  if (exists)
    m_httpd.postReply(HTTPReply(req.m_id, true, 204, HTTPHeaders(), std::string()));
  else
    m_httpd.postReply(HTTPReply(req.m_id, true, 404, HTTPHeaders(), std::string()));
}

void MyWorker::statusGET(const HTTPRequest &req)
{
  // Output our status
  std::string version(g_getVersion());
  Time p_time(Time::now());
  size_t p_queue(m_httpd.getQueueLength());
  size_t mqueue(m_mirror ? m_mirror->getQueueLength() : 0);
  size_t g1m(stats::get1m(stats::GET));
  size_t h1m(stats::get1m(stats::HEAD));
  size_t p1m(stats::get1m(stats::POST));

  using namespace xml;
  const IDocument &ddoc
    = mkDoc(Element("status")
            (Element("version")(CharData<std::string>(version))
             & Element("time")(CharData<Time>(p_time))
             & Element("request-queue")(CharData<size_t>(p_queue))
             & Element("mirror-queue")(CharData<size_t>(mqueue))
             & Element("GET-1m")(CharData<size_t>(g1m))
             & Element("HEAD-1m")(CharData<size_t>(h1m))
             & Element("POST-1m")(CharData<size_t>(p1m))));

  m_httpd.postReply(req.m_id, ddoc);
}

void MyWorker::handleGET(const HTTPRequest &req)
{
  const std::string name = m_cfg.root + splitName(getHash(req));

  // Open the file. If we fail, return 404.
#if defined(__unix__) || defined(__APPLE__)
  {
    int orc = open(name.c_str(), O_RDONLY);
    if (orc == -1) {
      // Verify that we failed because the file does not exist. If we
      // failed for any other reason, we must fail this request.
      if (errno != ENOENT)
        throw syserror("open", "reading object");
      // Ok good, return 404
      m_httpd.postReply(HTTPReply(req.m_id, true, 404, HTTPHeaders(), std::string()));
      return;
    }
    // Good, file exists. Don't leak the handle!
    ON_BLOCK_EXIT(close, orc);

    // Set headers
    m_httpd.postReply(HTTPReply(req.m_id, false, 200, HTTPHeaders(), std::string()));

    // Now read the file and post the replies
    uint8_t buf[8192];
    while (true) {
      ssize_t res = read(orc, buf, sizeof buf);
      switch (res) {
      case -1: // error
        throw syserror("read", "reading object");
      case 0: // end of file
        m_httpd.postReply(HTTPReply(req.m_id, true, std::string()));
        return;
      default: // got regular data - send and continue
        MAssert(res > 0, "read returned negative non -1");
        m_httpd.postReply(HTTPReply(req.m_id, false,
                                    std::string(buf, buf + res)));
      }
    }
    // Done!
  }
#endif
}

std::vector<uint8_t> MyWorker::localObjectFetch(const sha256 &obj) const
{
  const std::string name = m_cfg.root + splitName(obj.m_hex);

#if defined(__unix__) || defined(__APPLE__)
  int orc;
  while (-1 == (orc = open(name.c_str(), O_RDONLY))
         && errno == EINTR);
  if (orc == -1)
    throw syserror("open", "fetching local object");
  // Good, file exists. Don't leak the handle!
  ON_BLOCK_EXIT(close, orc);

  // Read data...  We use a buffer larger than our chunk size, to
  // ensure we can read the full file without resizing our buffer.
  std::vector<uint8_t> chunk(ng_chunk_size * 2);
  size_t ofs = 0;
  while (true) {
    size_t rd = chunk.size() - ofs;
    ssize_t res;
    while (-1 == (res = read(orc, &chunk[ofs], rd))
           && errno == EINTR);
    if (res == -1)
      throw syserror("read", "reading local object");
    if (res == 0) {
      // Done!
      chunk.resize(ofs);
      break;
    }
    // We got data. Move offset and make sure we have more space
    ofs += res;
    if (ofs == chunk.size())
      chunk.resize(chunk.size() * 2);
  }
  return chunk;
#endif
}

uint64_t MyWorker::localObjectSize(const sha256 &obj) const
{
  const std::string name = m_cfg.root + splitName(obj.m_hex);

#if defined(__unix__) || defined(__APPLE__)
  struct stat buf;
  int lrc;
  while (-1 == (lrc = lstat(name.c_str(), &buf))
         && errno == EINTR);
  if (lrc == -1)
    throw syserror("lstat", "retrieving local object size ("
                   + name + ")");
  return buf.st_size;
#endif
}


void MyWorker::handlePOST(const HTTPRequest &req)
{
  // Get the hash
  const std::string hash = getHash(req);

  // If hashes deviate, request is not valid
  if (sha256::hash(req.m_body).m_hex != hash) {
    m_httpd.postReply(HTTPReply(req.m_id, true, 400,
				HTTPHeaders().add("content-type", "text/plain"),
				"Hash and content data do not match\n"));
    return;
  }

  //
  // See if we have a "redundancy" header. If we do and it is set to
  // "replica" then we will not replicate this write further. In any
  // other case we will log the object for replication.
  //
  const bool must_replicate
    = !req.hasHeader("redundancy")
    || req.getHeader("redundancy") != "replica";

  // If these members are not set it is a basic setup error in the
  // calling code
  if (must_replicate) {
    if (!m_pre_orig_write || !m_post_orig_write)
      throw error("Received object to replicate in a non-replica setup");
  }

  // Perform rudimentary object validation on normal objects. We skip
  // validation on replica objects we receive from our mirror peer
  // because those will typically arrive out of order.
  //
  // If this is a directory entry object we want some basic dirsize
  // validation.
  //
  if (must_replicate) {
    size_t ofs = 0;
    // version - must be zero
    if (0 != des<uint8_t>(req.m_body, ofs)) {
      MTrace(t_stord, trace::Info, "Rejecting version not-0 object");
      m_httpd.postReply(HTTPReply(req.m_id, true, 400,
                                  HTTPHeaders().add("content-type", "text/plain"),
                                  "Object version must be zero.\n"));
      return;
    }
    // Only perform validation on directory entries
    uint8_t objType= des<uint8_t>(req.m_body, ofs);
    if (0xdd == objType || 0xde == objType) {
      MTrace(t_stord, trace::Debug, "Will validate uploaded directory entry "
             << hash);
      // We will sum up the tree size...
      uint64_t tsize = req.m_body.size();

      // Fine, parse this object to see what it references
      FSDir thisobj(std::vector<uint8_t>(req.m_body.begin(), req.m_body.end()));

      //
      // If full checking is enabled, perform that
      //
      if (m_cfg.dirCheck == "full") {
        // We keep a 'tab' on the sizes for thorough error reporting.
        std::ostringstream tab;
        tab << "Directory treesize computation:" << std::endl
            << "------------------------------------------" << std::endl
            << "Self: " << tsize << " bytes" << std::endl;
        //
        // We now perform two checks; we see that all referenced objects
        // actually exist, and, we validate the tree size.
        //
        for (FSDir::dirents_t::const_iterator i = thisobj.dirents.begin();
             i != thisobj.dirents.end(); ++i) {
          switch (i->type) {
          case FSDir::dirent_t::UNIXFILE:
          case FSDir::dirent_t::WINFILE:
            // For a regular file we just need the sizes of the
            // referenced objects
            for (objseq_t::const_iterator c = i->hash.begin();
                 c != i->hash.end(); ++c) {
              try {
                const uint64_t s(localObjectSize(*c));
                tsize += s;
                tab << "File " << i->name << ": +" << s << " bytes" << std::endl;
              } catch (error &e) {
                MTrace(t_stord, trace::Info, "Child file " << i->name
                       << " size fetch error: " << e.toString());
                m_httpd.postReply(HTTPReply
                                  (req.m_id, true, 400,
                                   HTTPHeaders().add("content-type", "text/plain"),
                                   "Cannot fetch size of child file (" + i->name
                                   + ").\n"));
                return;
              }
            }
            break;
          case FSDir::dirent_t::UNIXDIR:
          case FSDir::dirent_t::WINDIR: {
            // For directories, we fetch the directory, parse it, and
            // read out its tree size
            try {
              FSDir child(papply(this,&MyWorker::localObjectFetch), i->hash);
              tsize += child.dirsize;
              tab << "Dir  " << i->name << ": +" << child.dirsize
                  << " bytes" << std::endl;
            } catch (error &e) {
              MTrace(t_stord, trace::Info, "Child directory " << i->name
                     << " parse error: " << e.toString());
              m_httpd.postReply(HTTPReply
                                (req.m_id, true, 400,
                                 HTTPHeaders().add("content-type", "text/plain"),
                                 "Child directory (" + i->name + ") error: "
                                 + e.toString() + "\n"));
              return;
            }
            break;
          }
          default:
            MTrace(t_stord, trace::Info, "Unknown object type "
                   << i->type << " encountered in directory entry validation of "
                   "entry named " << i->name);
            m_httpd.postReply(HTTPReply(req.m_id, true, 400,
                                        HTTPHeaders().add("content-type", "text/plain"),
                                        "Unknown child entry type.\n"));
            return;
          }
        }

        tab << "------------------------------------------" << std::endl
            << "Sum of sizes: " << tsize << " bytes" << std::endl
            << "==========================================" << std::endl;
        //
        // Do the tree sizes add up?
        //
        if (tsize != thisobj.dirsize) {
          MTrace(t_stord, trace::Info, "Rejecting directory entry with treesize "
                 << thisobj.dirsize << ", but referenced objects add up to "
                 << tsize << " bytes");
          m_httpd.postReply(HTTPReply(req.m_id, true, 400,
                                      HTTPHeaders().add("content-type", "text/plain"),
                                      "Object treesize invalid.\n\n"
                                      + tab.str()));
          return;
        }
      }

      //
      // If only rudimentary checking is enabled, just verify that the
      // treesize is at least as big as the object itself
      //
      if (m_cfg.dirCheck == "simple") {
        if (thisobj.dirsize < tsize) {
          MTrace(t_stord, trace::Info, "Rejecting directory of size "
                 << tsize << " with treesize " << thisobj.dirsize);
          m_httpd.postReply(HTTPReply(req.m_id, true, 400,
                                      HTTPHeaders().add("content-type", "text/plain"),
                                      "Directory treesize smaller than directory.\n"));
          return;
        }
        // Ok, the treesize is at least as big as the directory
        // entry. That is enough for the basic check.
      }
    }

  }

  //
  // Deal with replication logging BEFORE we write the object! This is
  // important because if the replication logging fails we want to
  // fail the request altogether, thereby forcing the client to
  // re-try.
  //

  // If we must replicate this object, log that it must be replicated
  if (must_replicate) {
    (*m_pre_orig_write)(sha256::parse(hash));
  }

  // Create directory tree - we don't care if the individual mkdirs
  // succeed or fail - most will fail because the directory is already
  // there, but that is just fine!  We don't care if it was there
  // before or not - we just want to make sure it is there now.
#if defined(__unix__) || defined(__APPLE__)
  const std::string sep("/");
#endif
  const std::string n_a = m_cfg.root + sep + hash.substr(0, 2);
  const std::string n_b = n_a + sep + hash.substr(2, 2);
  const std::string n_c = n_b + sep + hash.substr(4, 2);
  const std::string n_d = n_c + sep + hash.substr(6, 58);
  mkdir(n_a.c_str(), 0700);
  mkdir(n_b.c_str(), 0700);
  mkdir(n_c.c_str(), 0700);

  // Create a temporary file for writing - we use the hash plus request id in hex
  std::string tmpname;
  { std::ostringstream name;
    name << n_d << "." << std::hex << req.m_id;
    tmpname = name.str();
  }


  { int tfile;
    // Retry the open until it is not interrupted
    while (-1 == (tfile = open(tmpname.c_str(),
                               O_WRONLY  // We only want to write
                               | O_CREAT // We expect to create the file
                               | O_EXCL // It is an error if the file already
                               // exists
                               , 00600))
           && errno == EINTR);

    // If we failed, fail the request
    if (tfile == -1)
      throw syserror("open", "creating tmpfile for object data");

    // Fine, success, don't leak the handle
    ON_BLOCK_EXIT(close, tfile);

    // Now write the file (retry while we're being interrupted)
    ssize_t res;
    while (-1 == (res = write(tfile, req.m_body.data(), req.m_body.size()))
           && errno == EINTR);
    if (res == -1)
      throw syserror("write", "writing object data");
    MAssert(res >= 0, "write returned negative non -1");
    if (size_t(res) != req.m_body.size())
      throw error("Write wrote too little");
  }

  // And finally rename the temporary into the final name - if this
  // fails, it only means that someone else beat us to it. This is not
  // an error, as such. Therefore, error on rename is ignored.
  (void)rename(tmpname.c_str(), n_d.c_str());

  // Report 201 created.
  m_httpd.postReply(HTTPReply(req.m_id, true, 201,
                              HTTPHeaders(), std::string()));

  // Notify mirroring logic that the object has hit the disk and is
  // ready to be mirrored
  if (must_replicate)
    (*m_post_orig_write)(sha256::parse(hash));
}

std::string MyWorker::getHash(const HTTPRequest &req)
{
  // The request URI should be on the form: A slash followed by a
  // hexadecimal (lower-case) representation of the SHA256.
  //
  // In other words, the length must be 1 + plus the length of the hash.
  if (req.getURI().size() != 1 + 64)
    throw error("Expected slash and 64 byte name, got \"" + req.getURI() + "\"");

  // Validate leading slash
  if (req.getURI().find("/") != 0)
    throw error("Expected leading /");

  // Validate lower-case hex string
  std::string res = req.getURI().substr(1, std::string::npos);
  if (std::string::npos != res.find_first_not_of("0123456789abcdef"))
    throw error("Expected lower-case hexadecimal hash");

  return res;
}

std::string splitName(const std::string &n)
{
#if defined(__unix__) || defined(__APPLE__)
  const std::string sep("/");
#endif
  MAssert(n.size() == 64, "Bad size of name");
  return sep + n.substr(0, 2) + sep
    + n.substr(2, 2) + sep
    + n.substr(4, 2) + sep
    + n.substr(6, 58);
}

