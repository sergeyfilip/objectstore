///
/// The proxy.
///
// $Id: main.cc,v 1.103 2013/10/07 08:16:32 joe Exp $
//

#include "main.hh"
#include "common/error.hh"
#include "common/string.hh"
#include "common/string.hcc"
#include "common/scopeguard.hh"
#include "xml/xmlio.hh"
#include "common/base64.hh"
#include "version.hh"
#include "client/serverconnection.hh"
#include "common/JSONParser.hh"
#include "common/JSONValue.hh"

#include <fstream>
#include <algorithm>

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

trace::Path t_api("/api");
trace::Path t_cache("/cache");

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

  SvcConfig conf(argv[1]);

  trace::StreamDestination logstream(std::cerr);
  trace::SyslogDestination logsys("proxy");

  trace::Path::addDestination(trace::Warn, "*", logstream);
  trace::Path::addDestination(trace::Warn, "*", logsys);
  trace::Path::addDestination(trace::Info, "*", logsys);

  MTrace(t_api, trace::Info, "Proxy " << g_getVersion() << " starting...");

  // Set up the web server
  HTTPd httpd;

  // If SSL is requested, activate it
  if (conf.ssl_certfile.isSet() && conf.ssl_keyfile.isSet())
    httpd.startSSL(conf.ssl_certfile.get(), conf.ssl_keyfile.get());

  // Add listener on configured port
  httpd.addListener(conf.bindPort);

  // Instantiate credentials cache
  CredCache credcache(conf.cacheTTL);

  // Start worker threads
  std::vector<MyWorker> workers(conf.workerThreads,
                                MyWorker(httpd, conf, credcache));
  for (size_t i = 0; i != workers.size(); ++i)
    workers[i].start();

  MTrace(t_api, trace::Info, "Proxy servicing requests on port "
         << conf.bindPort);

#if defined(__unix__) || defined(__APPLE__)
  { struct sigaction act;
    memset(&act, 0, sizeof act);
    act.sa_handler = sig_exit_hnd;
    if (sigaction(SIGINT, &act, 0))
      throw syserror("sigaction", "configuring termination handling");
    while (!sig_exit) {
      sleep(10);
      // trace; system status/utilisation/...
      //
      // We want to print how many requests are queued, how much
      // response data is queued and how many workers are busy
      //
      MTrace(t_api, trace::Info, "Workqueue length: "
             << httpd.getQueueLength() << " jobs");
    }
  }
#endif

  MTrace(t_api, trace::Info, "Shutting down");

  // Stop the server (will begin issuing stop requests to workers)
  httpd.stop();

  // Wait until all workers have exited
  for (size_t i = 0; i != workers.size(); ++i)
    workers[i].join_nothrow();

} catch (error &e) {
  std::cerr << e.toString() << std::endl;
  return 1;
}



///////////////////////////////////////////////////////////////
// Configuration loader
///////////////////////////////////////////////////////////////


SvcConfig::SvcConfig(const char *fname)
  : bindPort(0)
  , workerThreads(0)
{
  // Define configuration document schema
  using namespace xml;
  const IDocument &confdoc
    = mkDoc(Element("proxy")
            (Element("bindPort")(CharData<uint16_t>(bindPort))
             & Element("workerThreads")(CharData<size_t>(workerThreads))
             & *Element("osapi")
             (Element("host")(CharData<std::string>(hOSAPI.tmp_name))
              & Element("port")(CharData<uint16_t>(hOSAPI.tmp_port)))
             [ papply(&hOSAPI, &cOSAPI::add) ]
             & Element("documentRoot")(CharData<std::string>(docRoot))
             & Element("documentIndex")(CharData<std::string>(docIndex))
             & Element("connString")(CharData<std::string>(connString))
             & Element("authRealm")(CharData<std::string>(authRealm))
             & !Element("sslCert")(CharData<Optional<std::string> >(ssl_certfile))
             & !Element("sslKey")(CharData<Optional<std::string> >(ssl_keyfile))
             & Element("cacheTTL")(CharData<DiffTime>(cacheTTL))
             & *Element("mime")
             (Element("ext")(CharData<std::string>(hMime.tmp_ext))
              & Element("type")(CharData<std::string>(hMime.tmp_mime)))
             [papply(&hMime, &cMime::add)]
             & !Element("mimedefault")(CharData<Optional<std::string> >
                                       (defaultMimeType))
             & !Element("docShare404")(CharData<std::string>(docShare404))));

  // Load configuration
  std::ifstream file(fname);
  if (!file)
    throw error("Cannot open configuration file: " + std::string(fname));
  XMLexer lexer(file);
  confdoc.process(lexer);
}

bool SvcConfig::cOSAPI::add()
{
  m_hosts.push_back(std::make_pair(tmp_name, tmp_port));
  return true;
}

bool SvcConfig::cMime::add()
{
  m_map.insert(std::make_pair(tmp_ext, tmp_mime));
  return true;
}


///////////////////////////////////////////////////////////////
// Request worker
///////////////////////////////////////////////////////////////


MyWorker::MyWorker(HTTPd &httpd, const SvcConfig &conf, CredCache &cc)
  : m_httpd(httpd)
  , m_cfg(conf)
  , m_cc(cc)
  , m_osapi(conf)
  , m_db(conf.connString)
  , m_account_id(-1)
  , m_access_id(-1)
  , m_access_type(CredCache::AT_None)
  , hObject(*this)
  , hTokens(*this)
  , hToken(*this)
  , hDevices(*this)
  , hDevice(*this)
  , hHistory(*this)
  , hDevStatus(*this)
  , hDevAttr(*this)
  , hDevAuthCode(*this)
  , hUsers(*this)
  , hUser(*this)
  , hResources(*this)
  , hContacts(*this)
  , hContact(*this)
  , hSubUsers(*this)
  , hDownFile(*this)
  , hQueue(*this)
  , hQueueEvent(*this)
  , hStatus(*this)
  , hFavourites(*this)
  , hFavourite(*this)
  , hShares(*this)
  , hShare(*this)
  , hSPath(*this)
{
}

MyWorker::MyWorker(const MyWorker &o)
  : m_httpd(o.m_httpd)
  , m_cfg(o.m_cfg)
  , m_cc(o.m_cc)
  , m_osapi(o.m_osapi)
  , m_db(o.m_cfg.connString)
  , m_account_id(-1)
  , m_access_id(-1)
  , m_access_type(CredCache::AT_None)
  , hObject(*this)
  , hTokens(*this)
  , hToken(*this)
  , hDevices(*this)
  , hDevice(*this)
  , hHistory(*this)
  , hDevStatus(*this)
  , hDevAttr(*this)
  , hDevAuthCode(*this)
  , hUsers(*this)
  , hUser(*this)
  , hResources(*this)
  , hContacts(*this)
  , hContact(*this)
  , hSubUsers(*this)
  , hDownFile(*this)
  , hQueue(*this)
  , hQueueEvent(*this)
  , hStatus(*this)
  , hFavourites(*this)
  , hFavourite(*this)
  , hShares(*this)
  , hShare(*this)
  , hSPath(*this)
{
}

MyWorker::~MyWorker()
{
}

void MyWorker::run() try
{
  // Process requests until we are told to exit
  while (true) {
    // Before serving anything, reset our local variables.
    m_account_id = uint64_t(-1);
    m_access_id = uint64_t(-1);
    m_access_type = CredCache::AT_None;

    // Also the misc. variables for endpoint handlers
    m_objectid.clear();
    m_token_aname.clear();
    m_device_id.clear();
    m_userid.clear();
    m_contacttype.clear();
    m_downdir.clear();
    m_downfile.clear();
    m_queuename.clear();
    m_eventid.clear();
    m_favourite.clear();
    m_share.clear();
    m_spath.clear();

    // Block until we get a request from the queue
    HTTPRequest req(m_httpd.getRequest());
    if (req.isExitMessage()) {
      break;
    }

    try {
      // Now process the request
      MTrace(t_api, trace::Debug, "Got request: " << req.toString());
      processRequest(req);

    } catch (error &e) {
      //
      // Common errors are reported nicely to the client - if,
      // however, a malformed request or some server side error is
      // causing us to fail processing, we will log it and report a
      // 500 error to the client.
      //
      MTrace(t_api, trace::Warn, "Worker: " + e.toString());
      if (m_httpd.outstanding(req.m_id)) {
        m_httpd.postReply(HTTPReply(req.m_id, true, 500,
                                    HTTPHeaders().add("content-type", "text/plain"),
                                    "Proxy internal processing error:\n"
                                    + e.toString() + "\n"));
      }
    }
  }
} catch (error &e) {
  std::cerr << "Worker caught: " << e.toString() << std::endl;
}


void MyWorker::processRequest(HTTPRequest &req)
{
  MTrace(t_api, trace::Debug, "Processing id " << req.getId()
         << ": " << req.toString());
  if (!(UF("tokens")[hTokens] / UD(m_token_aname)[hToken]
        | UF("users")[hUsers] / UD(m_userid)[hUser]
        / (UF("resources")[hResources]
           | UF("contacts")[hContacts] / UD(m_contacttype)[hContact]
           | UF("users")[hSubUsers]
           | UF("devices")[hDevices] / UD(m_device_id)[hDevice]
           / ( UF("history")[hHistory]
               | UF("status")[hDevStatus]
               | UF("attributes") / UD(m_attributename)[hDevAttr]
               | UF("auth_code")[hDevAuthCode] ) )
        | UF("object") / UD(m_objectid)[hObject]
        | UF("download") / UD(m_downdir) / UD(m_downfile)[hDownFile]
        | UF("queue") / UD(m_queuename)[hQueue] / UD(m_eventid)[hQueueEvent]
        | UF("status")[hStatus]
        | UF("favourites")[hFavourites] / UD(m_favourite)[hFavourite]
        | UF("share")[hShares] / UD(m_share)[hShare] / UF("root") / UP(m_spath)[hSPath]
        ).process(req)) {
    //
    // No match.
    //
    // Next step is to see if we have a valid local file. This can be
    // done without authentication.
    //
    if (!processLocal(req)) {
      // Not processed - tell client we don't know what it is talking
      // about
      m_httpd.postReply(HTTPReply(req.m_id, true, 404,
                                  HTTPHeaders().add("content-type", "text/plain"),
                                  "Requested resource not found\n"));
    }
  }

  // If for some reason we never posted a reply to the requested URI,
  // tell the client
  if (m_httpd.outstanding(req.getId())) {
    MTrace(t_api, trace::Info, "Never posted a response to request id "
           << req.getId() << " - returning 500");
    m_httpd.postReply(HTTPReply(req.m_id, true, 500,
                                HTTPHeaders().add("content-type", "text/plain"),
                                "URI matched but no reply generated\n"));
  }
}


bool MyWorker::authenticate(const HTTPRequest &req, size_t tt, reqmode_t authmode)
{
  // Before we do anything, make sure this is an authenticated
  // request. We require an Authorization header
  if (!req.hasHeader("authorization")) {
    m_httpd.postReply(HTTPReply(req.m_id, true, authFailCode(req),
                                HTTPHeaders()
                                .add("WWW-Authenticate",
                                     "Basic realm=\"" + m_cfg.authRealm + "\"")
                                .add("content-type", "text/plain"),
                                "User authentication required\n"));
    return false;
  }

  // Save the authentication string {name}:{pass}
  std::string authstr;

  // Fine, now see that we are given Basic authentication
  // credentials and authorise them
  try {
    std::string creds(req.getHeader("authorization"));
    // The creds must be "Basic" followed by space and credentials
    if (creds.find("Basic"))
      throw std::string("Basic authorization required");
    creds.erase(0, 5);
    while (!creds.empty() && isspace(creds[0]))
      creds.erase(0, 1);
    // Fine, we now have the credentials left
    if (creds.empty())
      throw std::string("Empty credentials given");
    //
    // Locate the user-name and password
    //
    std::string uname;
    std::string upass;
    { std::vector<uint8_t> raw;
      base64::decode(raw, creds);
      authstr = std::string(raw.begin(), raw.end());
      std::vector<uint8_t>::iterator cpos
        = std::find(raw.begin(), raw.end(), ':');
      if (cpos == raw.end())
        throw std::string("Expected colon in decoded credentials");
      uname = std::string(raw.begin(), cpos);
      upass = std::string(cpos + 1, raw.end());
    }

    // If we have this cached and this is a NORMAL authmode request
    // and the request has no impersonation header, we can stop here.
    if (authmode == NORMAL
        && !req.hasHeader("impersonate")
        && m_cc.isValid(authstr, m_account_id, m_access_id, m_access_type)) {
      MTrace(t_api, trace::Debug, "Cached auth");
      return requireAccessType(req, tt);
    }

    // In case we lost our database connection, set it up again.
    m_db.reconnect();

    //
    // Now see if we have such an access token that grants access
    // to an account.
    //
    sql::query q(m_db,
                 "SELECT a.target, a.id, a.ttype, a.atype FROM access a"
                 " WHERE a.aname = $1::text"
                 "   AND a.apass = $2::text");
    q.add(uname).add(upass);

    if (!q.fetch())
      throw std::string("Credentials not valid");

    char ttype;
    char atype;
    q.get(m_account_id).get(m_access_id).get(ttype).get(atype);

    // Does this token grant account access?
    if (ttype != 'a')
      throw std::string("Valid authorization does not grant account access");

    // Convert token type
    //
    // a:AnonymousParent, p:PartnerParent, u:User, d:Device
    //
    switch (atype) {
    case 'a': m_access_type = CredCache::AT_AnonymousParent; break;
    case 'p': m_access_type = CredCache::AT_PartnerParent; break;
    case 'u': m_access_type = CredCache::AT_User; break;
    case 'd': m_access_type = CredCache::AT_Device; break;
    default:
      MTrace(t_api, trace::Warn, "Access token "
             << m_access_id << " under account "
             << m_account_id << " has unrecognised type "
             << atype);
      m_httpd.postReply(HTTPReply(req.m_id, true, 500,
                                  HTTPHeaders()
                                  .add("content-type", "text/plain"),
                                  "Access token has unrecognised type.\n"));
      return false;
    }

  } catch (std::string &s) {
    // To counter brute-force attacks, we delay the 401 response half
    // a second
    usleep(500000);
    // Report error to user
    m_httpd.postReply(HTTPReply(req.m_id, true, authFailCode(req),
                                HTTPHeaders()
                                .add("WWW-Authenticate",
                                     "Basic realm=\"" + m_cfg.authRealm + "\"")
                                .add("content-type", "text/plain"),
                                "User authentication error:\n "
                                + s + "\n"));
    return false;
  }

  // Fine, peer is authentic
  MTrace(t_api, trace::Debug, "Request authenticated: account id is "
         << m_account_id << " access token id is " << m_access_id);

  // We need to see if the account is enabled or disabled, and we need
  // to see if it is a system user (which can allow impersonation)
  { sql::query q(m_db, "SELECT enabled, sysusr FROM account WHERE id = $1::BIGINT");
    q.add(m_account_id);
    if (!q.fetch()) throw error("Cannot extract account data");
    bool enabled;
    bool sysusr;
    q.get(enabled).get(sysusr);
    if (!enabled) {
      m_httpd.postReply(HTTPReply(req.m_id, true, 403,
                                  HTTPHeaders()
                                  .add("content-type", "text/plain"),
                                  "Account disabled.\n"));
      return false;
    }
    //
    // If we are required to authenticate a system user, make sure we
    // did
    //
    if (authmode == REQSYS && !sysusr) {
      m_httpd.postReply(HTTPReply(req.m_id, true, 403,
                                  HTTPHeaders()
                                  .add("content-type", "text/plain"),
                                  "Special account required for endpoint.\n"));
      return false;
    }

    //
    // If this is a system user, see that the peer IP is from our
    // trusted network
    //
    // XXXX later

    //
    // If we have an impersonate header, require a system account.
    //
    if (req.hasHeader("impersonate")) {
      if (!sysusr) {
        MTrace(t_api, trace::Info, "Impersonation attempt denied");
        m_httpd.postReply(HTTPReply(req.m_id, true, 403,
                                    HTTPHeaders()
                                    .add("content-type", "text/plain"),
                                    "Impersonation not allowed.\n"));
        return false;
      }
      // Fine, we have a system account. Fetch impersonation account
      // and impersonate
      sql::query q2(m_db, "SELECT id FROM account WHERE guid = $1::TEXT");
      q2.add(req.getHeader("impersonate"));
      if (!q2.fetch()) {
        m_httpd.postReply(HTTPReply(req.m_id, true, 403,
                                    HTTPHeaders()
                                    .add("content-type", "text/plain"),
                                    "Impersonation account does not exist.\n"));
        return false;
      }
      q2.get(m_account_id);
      MTrace(t_api, trace::Info, "Impersonation attempt succeeded");
    }
  }

  // Fine, request is authentic and account is not disabled
  m_cc.cacheOk(authstr, m_account_id, m_access_id, m_access_type);
  return requireAccessType(req, tt);
}


bool MyWorker::processLocal(HTTPRequest &req)
{
  //
  // If a '?' is in the URI, then we strip off that and anything that
  // follows. Whatever extra stuff is in the URI is between javascript
  // and the browser - it concerns us not.
  //
  std::string uri = req.getURI();
  uri = uri.substr(0, uri.find('?'));

  // First we want to ensure we only read valid file characters:
  //
  // We support a-z, 0-9, '.', '-', '/', '_'
  //
  // Special rules apply to '.': only one at a time is allowed.
  //
  // Special rules apply to '/': only one at a time is allowed.
  //
  // If '/' is requested, request documentRoot/docIndex instead.
  //
  //

  if (uri.find("//") != uri.npos)
    throw error("Double slash in URI");
  if (uri.find("..") != uri.npos)
    throw error("Double dot in URI");
  for (size_t i = 0; i != uri.size(); ++i)
    if ((uri[i] < 'a' || uri[i] > 'z')
	&& (uri[i] < 'A' || uri[i] > 'Z')
        && (uri[i] < '0' || uri[i] > '9')
        && (uri[i] != '-')
        && (uri[i] != '_')
        && (uri[i] != '.')
        && (uri[i] != '/'))
      throw error("Invalid character in URI");
  // Ensure first character is a slash
  if (uri.empty() || uri[0] != '/')
    throw error("URI must start with slash");

  // Ok fine, the URI is ok. Now see if we end in a slash. If so,
  // append the docIndex name.
  if (*uri.rbegin() == '/')
    uri += m_cfg.docIndex;

  // Post reply
  return postFileReply(req, 200, uri);
}

bool MyWorker::postFileReply(const HTTPRequest &req, short status,
                             const std::string &file)
{
  //  - see if we have the file:
  MTrace(t_api, trace::Info, "Accessing local file " << m_cfg.docRoot << file);
  std::ifstream ifile((m_cfg.docRoot + file).c_str());
  if (!ifile.good()) {
    MTrace(t_api, trace::Info, " Cannot read local file " << m_cfg.docRoot << file);
    return false;
  }

  // So, attempt to process the request.

  // First, fill out the headers
  m_httpd.postReply(HTTPReply(req.m_id, false, status,
                              HTTPHeaders().add("content-type",
                                                mimeTypeFromExt(file)),
                              std::string()));
  // Now send chunks
  while (true) {
    char segment[0x10000];
    ifile.read(segment, sizeof segment);
    const size_t read = ifile.gcount();
    const bool final = ifile.eof();
    m_httpd.postReply(HTTPReply(req.m_id, final,
                                std::string(segment, read)));
    if (!final && !read)
      throw error("Error reading from file " + file);
    if (final)
      break;
  }
  return true;
}


std::string MyWorker::mimeTypeFromExt(const std::string &name) const
{
  try {
    const size_t extp = name.rfind('.');
    if (extp == name.npos)
      throw error("Unknown mime type from file without extension");
    const std::string ext = name.substr(extp, name.npos);
    if (ext.empty())
      throw error("Cannot derive mime type from file with empty extension");
    // Look up in configuration
    std::map<std::string,std::string>::const_iterator i = m_cfg.hMime.m_map.find(ext);
    if (i == m_cfg.hMime.m_map.end())
      throw error("Unknown mime type for extension '" + ext + "'");

    return i->second;
  } catch (...) {
    // If we have a default mime type, return that
    if (m_cfg.defaultMimeType.isSet())
      return m_cfg.defaultMimeType.get();
    // Otherwise re-throw.
    throw;
  }
}


short MyWorker::authFailCode(const HTTPRequest &req) const
{
  if (req.hasHeader("authentication-failure-code")) {
    const short ret = string2Any<short>(req.getHeader("authentication-failure-code"));
    if (ret < 400 || ret > 499)
      throw error("Authentication-failure-code header out of range");
    return ret;
  }
  return 401;
}

bool MyWorker::requireAccessType(const HTTPRequest &req, size_t c)
{
  // If c is AT_None (0) then we have no requirements... Otherwise,
  // make sure one of our required types is met.
  if (c && !(m_access_type & c)) {
    m_httpd.postReply(HTTPReply(req.m_id, true, 403,
                                HTTPHeaders()
                                .add("content-type", "text/plain"),
                                "Method not allowed for token type.\n"));
    return false;
  }
  return true;
}


bool MyWorker::cacheBypassRequest(const HTTPRequest &req)
{
  // If we have an if-none-match header, deal with it
  if (req.hasHeader("if-none-match")) {
    std::list<std::string> tags = req.m_headers.getValues("if-none-match");
    // See if "*" was given or if we match our constant entity-tag
    bool match = false;
    for (std::list<std::string>::const_iterator t = tags.begin();
         !match && t != tags.end(); ++t)
      if (*t == "*") match = true;
      else if (*t == "\"0\"") match = true;
    if (match) {
      MTrace(t_cache, trace::Info, "if-none-match: tag matched");
      // Treat GET and HEAD
      if (req.getMethod() == HTTPRequest::mGET
          || req.getMethod() == HTTPRequest::mHEAD) {
        // GET and HEAD must, on match, respond with "304 not modified"
        // and include the constant entity tag
        HTTPReply rep(req.getId(), true, 304, HTTPHeaders(), std::string());
        cacheTagReply(req, rep);
        m_httpd.postReply(rep);
        return true;
      } else {
        // All other methods must respond with "412 precondition failed"
        HTTPReply rep(req.getId(), true, 412, HTTPHeaders(), std::string());
        m_httpd.postReply(rep);
        return true;
      }
    } else {
      // No match. This is rather strange because we use a constant
      // tag so any tag we ever returned should match unless a user
      // agent passes made-up tags...
      MTrace(t_cache, trace::Info, "if-none-match: tag not matched "
             "- broken user agent");
      return false;
    }
  }

  // If we have an if-match header, deal with it
  if (req.hasHeader("if-match")) {
    std::list<std::string> tags = req.m_headers.getValues("if-match");
    // See if "*" was given or if we match our constant entity-tag
    bool match = false;
    for (std::list<std::string>::const_iterator t = tags.begin();
         !match && t != tags.end(); ++t)
      if (*t == "*") match = true;
      else if (*t == "\"0\"") match = true;
    // If nothing matched, return 412 precondition failed
    if (!match) {
      MTrace(t_cache, trace::Info, "if-match: tag not matched");
      HTTPReply rep(req.getId(), true, 412, HTTPHeaders(), std::string());
      m_httpd.postReply(rep);
      return true;
    } else {
      MTrace(t_cache, trace::Info, "if-match: tag matched");
      return false;
    }
  }

  // Fine, we cannot bypass request
  return false;
}

void MyWorker::cacheTagReply(const HTTPRequest &req, HTTPReply &rep)
{
  // If request was a GET and reply is successful, add our constant
  // entity tag and a max-age cache control directive
  if (req.getMethod() == HTTPRequest::mGET
      && rep.getStatus() == 200) {
    rep.refHeaders().add("etag", "\"0\"")
      .add("cache-control", "max-age=172800"); // 48 hours
  }
}


bool MyWorker::isAncestorOf(uint64_t ancestor, uint64_t descendant)
{
  // Check our descendant initially, then look up ancestors from
  // there.
  uint64_t match = descendant;
  do {
    // Locate the parent of 'match'
    sql::query q(m_db,
                 "SELECT parent FROM account"
                 " WHERE id = $1::BIGINT AND parent IS NOT NULL");
    q.add(match);

    // If we can't fetch a non-null parent, then we did not match.
    if (!q.fetch())
      return false;

    // Ok, get the parent and move on
    q.get(match);

  } while (match != ancestor);
  // Ok we got it!
  return true;
}

bool MyWorker::getUserPathId(uint64_t req_id, uint64_t &ownerid)
{
  // First, look up the guid and get the user id
  { sql::query q(m_db,
                 "SELECT id FROM account"
                 " WHERE guid = $1::TEXT");
    q.add(m_userid);
    if (!q.fetch()) {
      m_httpd
        .postReply(HTTPReply(req_id, true, 404,
                             HTTPHeaders().add("content-type", "text/plain"),
                             "Account not found.\n"));
      return false;
    }
    q.get(ownerid);
  }

  // Then, see if we are equal to or ancestor of the user id
  if (ownerid == m_account_id
      || isAncestorOf(m_account_id, ownerid))
    return true;

  // No - we cannot allow access to non-descendant accounts
  m_httpd.postReply(HTTPReply(req_id, true, 403,
                              HTTPHeaders().add("content-type", "text/plain"),
                              "Specified account not descendant of "
                              "authenticated account.\n"));
  return false;
}

void MyWorker::cObject::handle(const HTTPRequest &req) const
{
  // Authenticate OS/API access
  //
  // AnonymousParent: Must not access data
  // PartnerParent: Must not access data
  // User: May access data
  // Device: May access data
  if (!m_parent.authenticate(req, CredCache::AT_User | CredCache::AT_Device))
    return;

  // Deal with entity-tag preconditions
  if (m_parent.cacheBypassRequest(req))
    return;

  // Treat
  HTTPRequest fwd;
  fwd.m_method = req.m_method;
  fwd.m_uri = "/object/" + m_parent.m_objectid;
  fwd.m_body = req.m_body;
  MTrace(t_api, trace::Debug, "Forwarding " << fwd.toString());
  HTTPReply rep = m_parent.m_osapi.execute(fwd);
  // Set up reply with original request id
  rep.setId(req.getId());
  rep.setFinal(true);
  // If we actually returned an object, add an entity tag too
  m_parent.cacheTagReply(req, rep);
  // Post the reply
  m_parent.m_httpd.postReply(rep);
  MTrace(t_api, trace::Debug, "Posted reply " << rep.toString());
}


void MyWorker::cTokens::handle(const HTTPRequest &req) const
{
  switch (req.getMethod()) {
  case HTTPRequest::mGET: {
    // Authenticate token access
    //
    // AnonymousParent: Can create trial login
    // PartnerParent: Can create user login
    // User: Can create new tokens (for devices)
    // Device: Cannot manage tokens
    //
    if (!m_parent.authenticate(req, CredCache::AT_AnonymousParent
                               | CredCache::AT_PartnerParent
                               | CredCache::AT_User))
      return;

    std::string descr;
    std::string aname;
    Time ctime;
    Optional<Time> etime;

    // Setup fetch of tokens
    sql::query q(m_parent.m_db, "SELECT descr, aname, created, expires FROM access "
                 "WHERE ttype = 'a' AND target = $1::BIGINT");
    q.add(m_parent.m_account_id)
      .receiver(descr)
      .receiver(aname)
      .receiver(ctime)
      .receiver(etime);

    using namespace xml;
    const IDocument &tokendoc
      = mkDoc(Element("tokens")
              (*Element("token")
               (Element("descr")(CharData<std::string>(descr))
                & Element("aname")(CharData<std::string>(aname))
                & Element("created")(CharData<Time>(ctime))
                & !Element("expires")(CharData<Optional<Time> >(etime)))
               [ papply(&q, &sql::query::fetch) ]));

    m_parent.m_httpd.postReply(req.m_id, tokendoc);
    return;
  }
  case HTTPRequest::mPOST: {
    // Authenticate token access
    //
    // AnonymousParent: Can create trial login
    // PartnerParent: Can create user login
    // User: Can create new tokens (for devices)
    // Device: Cannot manage tokens
    //
    if (!m_parent.authenticate(req, CredCache::AT_AnonymousParent
                               | CredCache::AT_PartnerParent
                               | CredCache::AT_User))
      return;

    std::string descr, type, aname, apass;

    // Parse the body
    using namespace xml;
    const IDocument &tokendoc
      = mkDoc(Element("token")
              (Element("descr")(CharData<std::string>(descr))
               & Element("type")(CharData<std::string>(type))
               & Element("aname")(CharData<std::string>(aname))
               & Element("apass")(CharData<std::string>(apass))));
    { std::istringstream body(req.m_body);
      XMLexer lexer(body);
      tokendoc.process(lexer);
    }

    char atype;
    if (type == "AnonymousParent")
      atype = 'a';
    else if (type == "PartnerParent")
      atype = 'p';
    else if (type == "User")
      atype = 'u';
    else if (type == "Device")
      atype = 'd';
    else {
      m_parent.m_httpd
        .postReply(HTTPReply(req.m_id, true, 400,
                             HTTPHeaders().add("content-type", "text/plain"),
                             "Unknown token type \"" + type + "\".\n"));
      return;
    }

    MTrace(t_api, trace::Info, "Will create access token ");
    sql::exec s(m_parent.m_db,
                "INSERT INTO access (descr,aname,apass,created_by,ttype,target,atype) "
                "VALUES ($1::text, $2::text, $3::text, $4::BIGINT, 'a', "
                "$5::BIGINT, $6::CHAR)");
    s.add(descr).add(aname).add(apass)
      .add(m_parent.m_access_id).add(m_parent.m_account_id).add(atype);

    try {
      s.execute();
    } catch (error &e) {
      // Insert fails on conflict only
      m_parent.m_httpd
        .postReply(HTTPReply(req.m_id, true, 409,
                             HTTPHeaders().add("content-type", "text/plain"),
                             "Access token aname already exists.\n"));
      return;
    }

    // Send back 201 Created with location header
    m_parent.m_httpd
      .postReply(HTTPReply(req.m_id, true, 201,
                           HTTPHeaders().add("location", "/tokens/" + aname)
                           .add("access-control-allow-origin", "*")
                           .add("access-control-allow-headers",
                                "authorization, authentication-failure-code, location")
                           .add("access-control-expose-headers",
                                "location"),
                           std::string()));
    return;
  }
  case HTTPRequest::mOPTIONS: {
    //
    // Simply return the CORS headers
    //
    // Since this is a preflight check (as documented in
    // http://www.w3.org/TR/cors/#preflight-request), we must respond
    // with a 200 code even though we return no response body.
    //
    m_parent.m_httpd
      .postReply(HTTPReply(req.m_id, true, 200,
                           HTTPHeaders()
                           .add("access-control-allow-origin", "*")
                           .add("access-control-allow-headers",
                                "authorization, authentication-failure-code, location")
                           .add("access-control-expose-headers",
                                "location"),
                           std::string()));
    return;
  }
  default:
    m_parent.m_httpd
      .postReply(HTTPReply(req.m_id, true, 405,
                           HTTPHeaders().add("allow", "GET, POST, OPTIONS")
                           .add("content-type", "text/plain"),
                           "Methods:\n"
                           " GET: Retrieve all tokens for account\n"
                           " POST: Create a new access token\n"
                           " OPTIONS: Service CORS preflight check\n"));
    return;
  }
}


void MyWorker::cToken::handle(const HTTPRequest &req) const
{
  // Authenticate token access
  //
  // AnonymousParent: Cannot manipulate tokens
  // PartnerParent: Cannot manipulate tokens
  // User: May update own password
  // Device: Cannot manipulate tokens
  //
  if (!m_parent.authenticate(req, CredCache::AT_User))
    return;

  // All checks and optionally updates must occur in a single
  // transaction
  sql::transaction trans(m_parent.m_db);

  // Now retrieve the token id that we are going to manipulate. Only
  // retrieve the token if it grants access to the authenticated user
  // account
  uint64_t token_id;
  std::string old_apass;
  if (!sql::query(m_parent.m_db,
                  "SELECT id, apass FROM access "
                  "WHERE ttype = 'a' AND target = $1::BIGINT AND aname = $2::TEXT")
      .add(m_parent.m_account_id)
      .add(m_parent.m_token_aname)
      .receiver(token_id)
      .receiver(old_apass)
      .fetch()) {
    // Cannot locate given token (under this account at least - but we
    // do not distinguish between unauthorised access to other peoples
    // tokens and access to a non-existing token)
    m_parent.m_httpd
      .postReply(HTTPReply(req.m_id, true, 404,
                           HTTPHeaders().add("content-type", "text/plain"),
                           "This account has no such access token.\n"));
    return;
  }

  // Fine, now manipulate
  switch (req.getMethod()) {
  case HTTPRequest::mPUT: {
    Optional<std::string> aname;
    Optional<std::string> apass;

    // Parse request document
    using namespace xml;
    const IDocument &ddoc
      = mkDoc(Element("token_update")
              (!Element("aname")(CharData<Optional<std::string> >(aname))
               & !Element("apass")(CharData<Optional<std::string> >(apass))));
    { std::istringstream body(req.m_body);
      XMLexer lexer(body);
      ddoc.process(lexer);
    }



    // If rename was requested, perform
    if (aname.isSet()) {
      try {
        sql::exec(m_parent.m_db, "UPDATE access SET aname = $1::TEXT "
                  "WHERE id = $2::BIGINT")
          .add(aname.get())
          .add(token_id)
          .execute();
      } catch (error &e) {
        MTrace(t_api, trace::Info, "Error on token rename: " << e.toString());
        // Error is most likely a conflict
        m_parent.m_httpd
          .postReply(HTTPReply(req.m_id, true, 409,
                               HTTPHeaders().add("content-type", "text/plain"),
                               "Token aname already in use.\n"));
        return;
      }
    }

    // If new password given, update
    if (apass.isSet()) {
      sql::exec(m_parent.m_db, "UPDATE access SET apass = $1::TEXT "
                "WHERE id = $2::BIGINT")
        .add(apass.get())
        .add(token_id)
        .execute();
    }

    // Commit and report success
    trans.commit();

    // Invalidate credentials cache for this token
    { std::string authline(m_parent.m_token_aname + ":" + old_apass);
      std::string authstr;
      base64::encode(authstr, std::vector<uint8_t>(authline.begin(), authline.end()));
      MTrace(t_api, trace::Debug, "Invalidating auth: " << authstr
             << " based on auth string " << authline);
      m_parent.m_cc.invalidate(authstr);
    }

    m_parent.m_httpd
      .postReply(HTTPReply(req.m_id, true, 200,
                           HTTPHeaders().add("content-type", "text/plain"),
                           "Successfully updated access token.\n"));
    return;
  }
  default:
    m_parent.m_httpd
      .postReply(HTTPReply(req.m_id, true, 405,
                           HTTPHeaders().add("allow", "PUT")
                           .add("content-type", "text/plain"),
                           "Methods:\n"
                           " PUT: Modify given access token\n"));
    return;
  }


}

void MyWorker::cDevices::handle(const HTTPRequest &req) const
{
  // Authenticate devices access
  //
  // AnonymousParent: No device access
  // PartnerParent: May list/create devices
  // User: May list/create devices
  // Device: May list/create devices
  //
  if (!m_parent.authenticate(req, CredCache::AT_PartnerParent
                             | CredCache::AT_User | CredCache::AT_Device))
    return;

  // Get the id of the user from the path. If this method returns
  // false, it already posted an error to the client and we should
  // just return.
  //
  // On success, it means we are either the specified user id or we
  // are an ancestor.
  uint64_t owner_id;
  if (!m_parent.getUserPathId(req.m_id, owner_id))
    return;

  switch (req.getMethod()) {
  case HTTPRequest::mGET: {
    // Set up query to retrieve all PC devices for the given account
    sql::query pcq(m_parent.m_db,
                   "SELECT guid, descr FROM device "
                   "WHERE account = $1::BIGINT AND kind = 'p'");
    pcq.add(owner_id);

    // Set up query to retrieve all cloud devices for the given account
    sql::query cq(m_parent.m_db,
                  "SELECT guid, descr, ctype, uri, login, password FROM device "
                  "WHERE account = $1::BIGINT AND kind = 'c'");
    cq.add(owner_id);

    // Set up recipient variable for device name
    std::string pcguid, pcname;
    pcq.receiver(pcguid).receiver(pcname);

    std::string cguid, cname, ctype, curi, clogin, cpassword;
    cq.receiver(cguid).receiver(cname).receiver(ctype)
      .receiver(curi).receiver(clogin).receiver(cpassword);

    MTrace(t_api, trace::Debug, "Fetching device list for account "
           << owner_id);

    // Generate output document by fetching until nothing more to
    // fetch
    using namespace xml;
    const IDocument &ddoc
      = mkDoc(Element("devices")
              (*Element("pc")
               (Element("guid")(CharData<std::string>(pcguid))
                & Element("name")(CharData<std::string>(pcname)))
               [ papply(&pcq, &sql::query::fetch) ]
               & *Element("cloud")
               (Element("guid")(CharData<std::string>(cguid))
                & Element("name")(CharData<std::string>(cname))
                & Element("type")(CharData<std::string>(ctype))
                & Element("uri")(CharData<std::string>(curi))
                & Element("login")(CharData<std::string>(clogin))
                & Element("password")(CharData<std::string>(cpassword)))
               [ papply(&cq, &sql::query::fetch) ]));

    m_parent.m_httpd.postReply(req.m_id, ddoc);
    return;
  }
  case HTTPRequest::mPOST: {
    // Fetch a GUID to use for insertion
    std::string device_id;
    sql::query(m_parent.m_db, "SELECT guidstr()")
      .fetchone().get(device_id);

    // Set up SQL statement for PC device insertion
    std::string devname;
    sql::exec pce(m_parent.m_db,
                  "INSERT INTO device (guid, account, kind, descr) "
                  "VALUES ($1::TEXT, $2::BIGINT, 'p', $3::TEXT)");
    pce.sender(device_id).sender(owner_id).sender(devname);

    // Set up SQL statement for cloud device insertion
    std::string ctype;
    Optional<std::string> curi, clogin, cpass;
    sql::exec ce(m_parent.m_db,
                 "INSERT INTO device "
                 "(guid,account, kind, descr, ctype, uri, login, password) "
                 "VALUES "
                 "($1::TEXT, $2::BIGINT, 'c', $3::TEXT, $4::TEXT, $5::TEXT,"
                 " $6::TEXT, $7::TEXT)");
    ce.sender(device_id).sender(owner_id)
      .sender(devname).sender(ctype).sender(curi).sender(clogin).sender(cpass);

    // Yep. Parse request body
    using namespace xml;
    const IDocument &ddoc
      = mkDoc(Element("pc")
              (Element("name")(CharData<std::string>(devname)))
              [ papply(&pce, &sql::exec::execute) ]
              | Element("cloud")
              (Element("name")(CharData<std::string>(devname))
               & Element("type")(CharData<std::string>(ctype))
               & !Element("uri")(CharData<Optional<std::string> >(curi))
               & !Element("login")(CharData<Optional<std::string> >(clogin))
               & !Element("password")(CharData<Optional<std::string> >(cpass)))
              [papply(&ce, &sql::exec::execute)]);
    try {
      std::istringstream body(req.m_body);
      XMLexer lexer(body);
      ddoc.process(lexer);
    } catch (error &e) {
      // If we got an insertion error, then we must report 409. If we
      // got a parse error, we must report 400.
      MTrace(t_api, trace::Info, e.toString());
      m_parent.m_httpd
        .postReply(HTTPReply(req.m_id, true, 409,
                             HTTPHeaders().add("content-type", "text/plain"),
                             "Device already exists.\n"));
      return;
    }

    MTrace(t_api, trace::Info, "Created device \"" << device_id
           << "\" with name \"" << devname << "\" under account " << owner_id);

    // Send back 201 created with location header
    m_parent.m_httpd
      .postReply(HTTPReply(req.m_id, true, 201,
                           HTTPHeaders().add("location",
                                             "/users/" + m_parent.m_userid
                                             + "/devices/" + device_id),
                           std::string()));
    return;
  }
  default:
    m_parent.m_httpd
      .postReply(HTTPReply(req.m_id, true, 405,
                           HTTPHeaders().add("allow", "GET, POST")
                           .add("content-type", "text/plain"),
                           "Methods:\n"
                           " GET: Request list of devices\n"
                           " POST: Create device\n"));
    return;
  }
}

namespace {
  // Simple helper for type assignment in document parser for PUT
  // /devices/{device}
  template <typename T>
  bool assign(T &lhs, T rhs) { lhs = rhs; return true; }

}


void MyWorker::cDevice::handle(const HTTPRequest &req) const
{
  // Authenticate devices access
  //
  // AnonymousParent: No device access
  // PartnerParent: No device access
  // User: May update and delete devices
  // Device: No device access (compromised devices must not delete others)
  //
  if (!m_parent.authenticate(req, CredCache::AT_User))
    return;

  // Get id of user account that owns this device
  uint64_t owner_id;
  if (!m_parent.getUserPathId(req.m_id, owner_id))
    return;

  switch (req.getMethod()) {
  case HTTPRequest::mPUT: {
    // 1: Load existing device properties
    // 2: Parse update document (overriding properties)
    // 3: Update database with new properties
    //
    sql::transaction trans(m_parent.m_db);

    uint64_t d_id;
    char d_kind;
    std::string d_descr;
    Optional<std::string> d_ctype;
    Optional<std::string> d_uri;
    Optional<std::string> d_login;
    Optional<std::string> d_password;

    // 1: Load properties
    sql::query(m_parent.m_db,
               "SELECT id, kind, descr, ctype, uri, login, password "
               "FROM device "
               "WHERE account = $1::BIGINT AND guid = $2::TEXT")
      .add(owner_id)
      .add(m_parent.m_device_id)
      .fetchone()
      .get(d_id)
      .get(d_kind)
      .get(d_descr)
      .get(d_ctype)
      .get(d_uri)
      .get(d_login)
      .get(d_password);


    // 2: Parse update document
    using namespace xml;
    const IDocument &ddoc
      = mkDoc(Element("pc")
              (!Element("name")(CharData<std::string>(d_descr)))
              [ papply<bool,char&,char>( assign<char>, d_kind, 'p' ) ]
              | Element("cloud")
              (!Element("name")(CharData<std::string>(d_descr))
               & !Element("type")(CharData<Optional<std::string> >(d_ctype))
               & !Element("uri")(CharData<Optional<std::string> >(d_uri))
               & !Element("login")(CharData<Optional<std::string> >(d_login))
               & !Element("password")(CharData<Optional<std::string> >(d_password)))
              [ papply<bool,char&,char>( assign<char>, d_kind, 'c' ) ]);
    { std::istringstream body(req.m_body);
      XMLexer lexer(body);
      ddoc.process(lexer);
    }

    // 3: Update database
    sql::exec(m_parent.m_db,
              "UPDATE device SET kind = $1::CHAR, "
              "descr = $2::TEXT, "
              "ctype = $3::TEXT, "
              "uri = $4::TEXT, "
              "login = $5::TEXT, "
              "password = $6::TEXT "
              "WHERE id = $7::BIGINT")
      .add(d_kind)
      .add(d_descr)
      .add(d_ctype)
      .add(d_uri)
      .add(d_login)
      .add(d_password)
      .add(d_id)
      .execute();


    // Attempt commit
    try {
      trans.commit();
    } catch (error &e) {
      // Must be a conflict
      m_parent.m_httpd
        .postReply(HTTPReply(req.m_id, true, 409,
                             HTTPHeaders().add("content-type", "text/plain"),
                             "Transaction aborted.\n"));
      return;
    }

    // Fine, we succeeded
    m_parent.m_httpd
      .postReply(HTTPReply(req.m_id, true, 200,
                           HTTPHeaders().add("content-type", "text/plain"),
                           "Device updated.\n"));

    return;
  }
  case HTTPRequest::mDELETE: {
    // Attempt deleting device.
    sql::exec e(m_parent.m_db,
                "DELETE FROM device WHERE account = $1::BIGINT "
                "AND guid = $2::TEXT");
    e.add(owner_id)
      .add(m_parent.m_device_id)
      .execute();

    if (e.affectedRows() == 0) {
      m_parent.m_httpd
        .postReply(HTTPReply(req.m_id, true, 404,
                             HTTPHeaders().add("content-type", "text/plain"),
                             "Named device not found.\n"));
      return;
    }

    // Fine, more than zero rows affected. Due to the
    // UNIQUE(account,descr) constraint the affected rows should be
    // one. Return ok.
    m_parent.m_httpd
      .postReply(HTTPReply(req.m_id, true, 200,
                           HTTPHeaders().add("content-type", "text/plain"),
                           "Successfully deleted device.\n"));
    return;
  }
  default:
    m_parent.m_httpd
      .postReply(HTTPReply(req.m_id, true, 405,
                           HTTPHeaders().add("allow", "PUT, DELETE")
                           .add("content-type", "text/plain"),
                           "Methods:\n"
                           " PUT: Update named device\n"
                           " DELETE: Delete named device\n"));
    return;
  }
}



void MyWorker::cHistory::handle(const HTTPRequest &req) const
{
  // Authenticate history access
  //
  // AnonymousParent: No history access
  // PartnerParent: No history access
  // User: May access history
  // Device: May access history
  //
  if (!m_parent.authenticate(req, CredCache::AT_User | CredCache::AT_Device))
    return;

  // Get id of user account that owns this device
  uint64_t owner_id;
  if (!m_parent.getUserPathId(req.m_id, owner_id))
    return;

  // Verify device name first!
  sql::query q(m_parent.m_db, "SELECT id FROM device"
               " WHERE account = $1::BIGINT"
               " AND guid = $2::TEXT");
  q.add(owner_id).add(m_parent.m_device_id);
  uint64_t devid;
  q.receiver(devid);
  if (!q.fetch()) {
    // Return 404 if the device is not found
    m_parent.m_httpd
      .postReply(HTTPReply(req.m_id, true, 404,
                           HTTPHeaders().add("content-type", "text/plain"),
                           "Given device not found\n"));
    return;
  }

  switch (req.getMethod()) {
  case HTTPRequest::mGET: {
    // Report backup history for device
    sql::query q(m_parent.m_db,
                 "SELECT root, tstamp, kind FROM backup "
                 "WHERE device = $1::BIGINT "
                 "ORDER BY tstamp DESC");
    q.add(devid);
    std::string root;
    Time tstamp;
    char kind;
    q.receiver(root).receiver(tstamp).receiver(kind);

    // Define return document
    using namespace xml;
    const IDocument &ddoc
      = mkDoc(Element("history")
              (*Element("backup")
               (Element("tstamp")(CharData<Time>(tstamp))
                & Element("root")(CharData<std::string>(root))
                & Element("type")(CharData<char>(kind)))
               [ papply(&q, &sql::query::fetch) ]));

    m_parent.m_httpd.postReply(req.m_id, ddoc);

    break;
  }
  case HTTPRequest::mPOST: {
    // Create backup history item for device
    using namespace xml;
    Time tstamp;
    std::string root;
    Optional<std::string> type;
    const IDocument &ddoc
      = mkDoc(Element("backup")
              (Element("tstamp")(CharData<Time>(tstamp))
               & Element("root")(CharData<std::string>(root))
               & !Element("type")(CharData<Optional<std::string> >(type))));
    { std::istringstream body(req.m_body);
      XMLexer lexer(body);
      ddoc.process(lexer);
    }

    char kind = type.isSet() && type.get().size() == 1 ? type.get()[0] : 'c';
    if (kind != 'p' && kind != 'c' ) {
      m_parent.m_httpd
      .postReply(HTTPReply(req.m_id, true, 400,
                           HTTPHeaders().add("content-type", "text/plain"),
                           "Unknown backup type \"" + std::string(1, kind) + "\".\n"));
      return;
    }

    MTrace(t_api, trace::Info, "Will create backup " << tstamp
           << " on device " << devid << " under account "
           << m_parent.m_account_id);

    // Create device and respond accordingly
    try {
      sql::exec s(m_parent.m_db, "INSERT INTO backup (device,root,tstamp,kind) "
                  "VALUES ($1::BIGINT, $2::text, $3::TIMESTAMP, $4::CHAR)");
      s.add(devid).add(root).add(tstamp).add(kind);
      s.execute();
    } catch (error &e) {
      // Insert fails on conflict only
      m_parent.m_httpd
        .postReply(HTTPReply(req.m_id, true, 409,
                             HTTPHeaders().add("content-type", "text/plain"),
                             "Backup entry already exists.\n"));
      return;
    }

    // Send back 201 created with location header
    std::string uri;
    { std::ostringstream ts;
      ts << "/users/" << m_parent.m_userid << "/devices/"
         << m_parent.m_device_id << "/history/"
         << tstamp;
      uri = ts.str();
    }

    m_parent.m_httpd
      .postReply(HTTPReply(req.m_id, true, 201,
                           HTTPHeaders().add("location", uri),
                           std::string()));
    return;


  }
  default:
    m_parent.m_httpd
      .postReply(HTTPReply(req.m_id, true, 405,
                           HTTPHeaders().add("allow", "GET, POST")
                           .add("content-type", "text/plain"),
                           "Methods:\n"
                           " GET: Request backup history\n"
                           " POST: Report new backup set\n"));
    return;
  }

}

void MyWorker::cDevStatus::handle(const HTTPRequest &req) const
{
  // Authenticate status access
  //
  // AnonymousParent: No status access
  // PartnerParent: No status access
  // User: May access status
  // Device: May access status
  //
  if (!m_parent.authenticate(req, CredCache::AT_User | CredCache::AT_Device))
    return;

  // Get id of user account that owns this device
  uint64_t owner_id;
  if (!m_parent.getUserPathId(req.m_id, owner_id))
    return;

  // Verify device name first!
  sql::query q(m_parent.m_db, "SELECT id FROM device"
               " WHERE account = $1::BIGINT"
               " AND guid = $2::TEXT");
  q.add(owner_id).add(m_parent.m_device_id);
  uint64_t devid;
  q.receiver(devid);
  if (!q.fetch()) {
    // Return 404 if the device is not found
    m_parent.m_httpd
      .postReply(HTTPReply(req.m_id, true, 404,
                           HTTPHeaders().add("content-type", "text/plain"),
                           "Given device not found\n"));
    return;
  }

  switch (req.getMethod()) {
  case HTTPRequest::mGET: {
    // Report status history from device
    sql::query q(m_parent.m_db,
                 "SELECT tstamp,status FROM devstatus "
                 "WHERE device = $1::BIGINT "
                 "ORDER BY tstamp DESC");
    q.add(devid);
    Time tstamp;
    std::string status;
    q.receiver(tstamp).receiver(status);

    // Define return document
    using namespace xml;
    const IDocument &ddoc
      = mkDoc(Element("devstatus")
              (*Element("entry")
               (Element("tstamp")(CharData<Time>(tstamp))
                & Element("status")(CharData<std::string>(status)))
               [ papply(&q, &sql::query::fetch) ]));

    m_parent.m_httpd.postReply(req.m_id, ddoc);

    break;
  }
  case HTTPRequest::mPOST: {
    // Create backup history item for device
    using namespace xml;
    std::string status;
    const IDocument &ddoc
      = mkDoc(Element("status")(CharData<std::string>(status)));
    { std::istringstream body(req.m_body);
      XMLexer lexer(body);
      ddoc.process(lexer);
    }

    // Create device and respond accordingly
    sql::exec s(m_parent.m_db, "INSERT INTO devstatus (device,status) "
                "VALUES ($1::BIGINT, $2::text)");
    s.add(devid).add(status);
    s.execute();

    // Send back 201 created without location header
    m_parent.m_httpd
      .postReply(HTTPReply(req.m_id, true, 201,
                           HTTPHeaders(),
                           std::string()));
    return;


  }
  default:
    m_parent.m_httpd
      .postReply(HTTPReply(req.m_id, true, 405,
                           HTTPHeaders().add("allow", "GET, POST")
                           .add("content-type", "text/plain"),
                           "Methods:\n"
                           " GET: Request recent status entries\n"
                           " POST: Report new status\n"));
    return;
  }

}

void MyWorker::cDevAttr::handle(const HTTPRequest &req) const
{
  // Authenticate status access
  //
  // AnonymousParent: No status access
  // PartnerParent: No status access
  // User: May access status
  // Device: May access status
  //
  if (!m_parent.authenticate(req, CredCache::AT_User | CredCache::AT_Device))
    return;

  // Get id of user account that owns this device
  uint64_t owner_id;
  if (!m_parent.getUserPathId(req.m_id, owner_id))
    return;

  switch (req.getMethod()) {
    case HTTPRequest::mPUT: {
      // Attempt updating device attributes
      sql::exec(m_parent.m_db,
                "UPDATE device SET attributes = attributes || "
                "HSTORE($1::TEXT,$2::TEXT) "
                "WHERE account = $3::BIGINT AND guid = $4::TEXT")
        .add(m_parent.m_attributename)
        .add(req.m_body)
        .add(owner_id)
        .add(m_parent.m_device_id)
        .execute();

      // Fine, we succeeded
      m_parent.m_httpd
      .postReply(HTTPReply(req.m_id, true, 200,
                           HTTPHeaders().add("content-type", "text/plain"),
                           "Attribute created/updated.\n"));

      return;
    }
    case HTTPRequest::mGET: {
      sql::query q(m_parent.m_db,
                "SELECT attributes->$1::TEXT "
                "FROM device "
                "WHERE account = $2::BIGINT AND guid = $3::TEXT");
      q.add(m_parent.m_attributename)
        .add(owner_id)
        .add(m_parent.m_device_id);

      Optional<std::string> p_attr_value;
      q.receiver(p_attr_value);
      q.fetch();
      if (!p_attr_value.isSet()) {
        // Return 404 if the attribute is not found
        m_parent.m_httpd
        .postReply(HTTPReply(req.m_id, true, 404,
                             HTTPHeaders().add("content-type", "text/plain"),
                             "Given attribute not found\n"));
        return;
      }

      // Fine, we succeeded
      m_parent.m_httpd
      .postReply(HTTPReply(req.m_id, true, 200,
                           HTTPHeaders().add("content-type", "application/octet-stream"),
                           p_attr_value.get()));

      return;
    }
    case HTTPRequest::mDELETE: {
      // Attempt deleting the attribute
      sql::exec(m_parent.m_db,
                "UPDATE device SET attributes = DELETE(attributes, $1::TEXT) "
                "WHERE account = $2::BIGINT AND guid = $3::TEXT")
        .add(m_parent.m_attributename)
        .add(owner_id)
        .add(m_parent.m_device_id)
        .execute();

      // Fine, we succeeded
      m_parent.m_httpd
      .postReply(HTTPReply(req.m_id, true, 200,
                           HTTPHeaders().add("content-type", "text/plain"),
                           "Attribute deleted.\n"));

      return;
    }
    default:
      m_parent.m_httpd
      .postReply(HTTPReply(req.m_id, true, 405,
                           HTTPHeaders().add("allow", "GET, PUT, DELETE")
                           .add("content-type", "text/plain"),
                           "Methods:\n"
                           " PUT: Create/Update named attribute\n"
                           " GET: Get named attribute\n"
                           " DELETE: Delete named attribute\n"));
      return;
  }
}

void MyWorker::cDevAuthCode::handle(const HTTPRequest &req) const
{
  // Authenticate status access
  //
  // AnonymousParent: No status access
  // PartnerParent: No status access
  // User: May access status
  // Device: May access status
  //
  if (!m_parent.authenticate(req, CredCache::AT_User | CredCache::AT_Device))
    return;

  // Get id of user account that owns this device
  uint64_t owner_id;
  if (!m_parent.getUserPathId(req.m_id, owner_id))
    return;

  switch (req.getMethod()) {
    case HTTPRequest::mPOST: {
      using namespace xml;
      std::string p_type;
      std::string p_code;
      std::string p_redirect_uri;

      const IDocument &ddoc
      = mkDoc(Element("auth_code")
              (Element("type")(CharData<std::string>(p_type))
               & Element("code")(CharData<std::string>(p_code))
               & Element("redirect_uri")(CharData<std::string>(p_redirect_uri))));
      { std::istringstream body(req.m_body);
        XMLexer lexer(body);
        ddoc.process(lexer);
      }

      if (p_type == "google") {
        // Handle google's authorization code

        std::stringstream body;
        body << "code=" << str2url(p_code);
        body << "&client_id=" << str2url("515377067248.apps.googleusercontent.com");
        body << "&client_secret=" << str2url("JjaB-bzi1ZRHiZqXVjfStIQy");
        body << "&redirect_uri=" << str2url(p_redirect_uri);
        body << "&grant_type=authorization_code";

        ServerConnection conn("accounts.google.com", 443, true);
        ServerConnection::Request tokenReq(ServerConnection::mPOST, "/o/oauth2/token");
        std::string body_str = body.str();
        tokenReq.setBody(std::vector<uint8_t>(body_str.begin(), body_str.end()));
        tokenReq.addHeader("content-type", "application/x-www-form-urlencoded");

        ServerConnection::Reply rep = conn.execute(tokenReq);

        if (rep.getCode() != 200) {
          m_parent.m_httpd
          .postReply(HTTPReply(req.m_id, true, 500,
                               HTTPHeaders().add("content-type", "text/plain"),
                               "Failed to get access token from google:\n"+rep.toString()));
          return;
        } else {
          JSONParser parser;
          std::string body(rep.refBody().begin(), rep.refBody().end());
          JSON::ObjectValue root = parser.parse(body).getObject();

          std::string token_type;
          std::string access_token;
          std::string refresh_token;
          for(JSON::ObjectIterator iter = root.begin(); iter != root.end(); ++iter) {
            std::string key = iter->first;
            if("access_token" == key) {
              access_token = iter->second.getString();
            } else if("refresh_token" == key) {
              refresh_token = iter->second.getString();
            } else if("token_type" == key) {
              token_type = iter->second.getString();
            }
          }

          if(access_token.empty()) {
            throw error("Failed to parse access_token from google response");
          }

          if(refresh_token.empty()) {
            throw error("Failed to parse refresh_token from google response");
          }

          if(token_type.empty()) {
            throw error("Failed to parse token_type from google response");
          }

          // Store tokens in device attributes KV
          {
            sql::transaction trans(m_parent.m_db);

            sql::exec(m_parent.m_db,
                      "UPDATE device SET attributes = attributes || "
                      "HSTORE('google_access_token',$1::TEXT) "
                      "WHERE account = $2::BIGINT AND guid = $3::TEXT")
              .add(access_token)
              .add(owner_id)
              .add(m_parent.m_device_id)
              .execute();

            sql::exec(m_parent.m_db,
                      "UPDATE device SET attributes = attributes || "
                      "HSTORE('google_refresh_token',$1::TEXT) "
                      "WHERE account = $2::BIGINT AND guid = $3::TEXT")
              .add(refresh_token)
              .add(owner_id)
              .add(m_parent.m_device_id)
              .execute();

            // Commit and exit
            trans.commit();
          }

          // Send back 201 created without location header
          m_parent.m_httpd
          .postReply(HTTPReply(req.m_id, true, 201,
                               HTTPHeaders(),
                               std::string()));
          return;
        }
      } else {
        m_parent.m_httpd
        .postReply(HTTPReply(req.m_id, true, 400,
                             HTTPHeaders().add("content-type", "text/plain"),
                             "Unknown type of 3rd party authorization code.\n"));
        return;
      }

    }
    default:
      m_parent.m_httpd
      .postReply(HTTPReply(req.m_id, true, 405,
                           HTTPHeaders().add("allow", "POST")
                           .add("content-type", "text/plain"),
                           "Methods:\n"
                           " POST: Report new status\n"));
      return;
  }

}

void MyWorker::cUsers::handle(const HTTPRequest &req) const
{
  //
  // Treat request
  //
  switch (req.getMethod()) {
  case HTTPRequest::mGET: {
    //
    // AnonymousParent: Must be able to get own id
    // PartnerParent: Must be able to get own id
    // User:  Must be able to get own id
    // Device: Must be able to get own id
    //
    if (!m_parent.authenticate(req, CredCache::AT_AnonymousParent
                               | CredCache::AT_PartnerParent
                               | CredCache::AT_User
                               | CredCache::AT_Device))
      return;

    // Which account did we authenticate as?

    // Extract the account guid
    std::string p_id;
    { sql::query uq(m_parent.m_db,
                    "SELECT guid FROM account WHERE id = $1::BIGINT");
      uq.add(m_parent.m_account_id);
      if (!uq.fetch())
        throw error("Authenticated account does not exist?!?");
      uq.get(p_id);
    }

    using namespace xml;
    const IDocument &ddoc
      = mkDoc(Element("user")
              (Element("id")(CharData<std::string>(p_id))));
    m_parent.m_httpd.postReply(req.m_id, ddoc);
    return;
  }
  case HTTPRequest::mOPTIONS: {
    //
    // Simply return the CORS headers
    //
    // Since this is a preflight check (as documented in
    // http://www.w3.org/TR/cors/#preflight-request), we must respond
    // with a 200 code even though we return no response body.
    //
    m_parent.m_httpd
      .postReply(HTTPReply(req.m_id, true, 200,
                           HTTPHeaders()
                           .add("access-control-allow-origin", "*")
                           .add("access-control-allow-headers",
                                "authorization, authentication-failure-code")
                           .add("access-control-expose-headers",
                                "location"),
                           std::string()));
    return;
  }
  default:
    m_parent.m_httpd
      .postReply(HTTPReply(req.m_id, true, 405,
                           HTTPHeaders().add("allow", "GET, OPTIONS")
                           .add("content-type", "text/plain")
                           .add("access-control-allow-origin", "*"),
                           "Methods:\n"
                           " GET: Retrieve current user details\n"
                           " OPTIONS: Get CORS headers\n"));

    return;
  }
}


void MyWorker::cUser::handle(const HTTPRequest &req) const
{
  //
  // Look up the user account given in the request string
  //
  uint64_t account_id;
  uint64_t parent_id;
  { sql::query q(m_parent.m_db,
                 "SELECT id, parent FROM account"
                 " WHERE guid = $1::TEXT");
    q.add(m_parent.m_userid);
    if (!q.fetch()) {
      m_parent.m_httpd
        .postReply(HTTPReply(req.m_id, true, 404,
                             HTTPHeaders().add("content-type", "text/plain"),
                             "Account not found\n"));
      return;
    }
    q.get(account_id).get(parent_id);
  }

  //
  // Treat request
  //
  switch (req.getMethod()) {
  case HTTPRequest::mPUT: {
    // Authenticate user access
    //
    // AnonymousParent: No user manipulation
    // PartnerParent: Will use this to disable/enable child users
    // User: Will use this to enable/disable child users
    // Device: No user manipulation
    //
    if (!m_parent.authenticate(req, CredCache::AT_User | CredCache::AT_PartnerParent))
      return;

    // Make sure that the authenticated account is an ancestor of the
    // account we are modifying
    if (!m_parent.isAncestorOf(m_parent.m_account_id, account_id)) {
      m_parent.m_httpd
        .postReply(HTTPReply(req.m_id, true, 403,
                             HTTPHeaders().add("content-type", "text/plain"),
                             "Specified account not descendant of "
                             "authenticated account\n"));
      return;
    }

    // Edit user.
    //
    using namespace xml;
    bool p_enabled;

    const IDocument &ddoc
      = mkDoc(Element("user_update")
              (Element("enabled")(CharData<bool>(p_enabled))));
    { std::istringstream body(req.m_body);
      XMLexer lexer(body);
      ddoc.process(lexer);
    }

    // Should we enable/disable account?
    MTrace(t_api, trace::Info, "Will set account-enabled = "
           << p_enabled << " for account " << account_id);
    sql::exec(m_parent.m_db,
              "UPDATE account SET enabled = $1::BOOLEAN"
              " WHERE id = $2::BIGINT")
      .add(p_enabled).add(account_id).execute();

    // Commit and report success
    m_parent.m_httpd
      .postReply(HTTPReply(req.m_id, true, 200,
                           HTTPHeaders().add("content-type", "text/plain"),
                           "Account updated successfully\n"));
    return;
  }
  case HTTPRequest::mDELETE: {
    // Authenticate user access
    //
    // AnonymousParent: No user manipulation
    // PartnerParent: Will use this to delete child users
    // User: Will use this to delete child users
    // Device: No user manipulation
    //
    if (!m_parent.authenticate(req, CredCache::AT_User | CredCache::AT_PartnerParent))
      return;

    // Make sure that the authenticated account is an ancestor of the
    // account we are modifying
    if (!m_parent.isAncestorOf(m_parent.m_account_id, account_id)) {
      m_parent.m_httpd
        .postReply(HTTPReply(req.m_id, true, 403,
                             HTTPHeaders().add("content-type", "text/plain"),
                             "Specified account not descendant of "
                             "authenticated account\n"));
      return;
    }

    //
    // Delete account and everything associated with it...
    //
    MTrace(t_api, trace::Info, "Will delete account id "
           << account_id << " on request from account id "
           << m_parent.m_account_id);

    { sql::transaction trans(m_parent.m_db);
      sql::exec(m_parent.m_db,
                "DELETE FROM account WHERE id = $1::BIGINT")
        .add(account_id)
        .execute();
      // Access tokens do not directly reference the account table and
      // must be deleted separately
      sql::exec(m_parent.m_db,
                "DELETE FROM access WHERE ttype = 'a' AND target = $1::BIGINT")
        .add(account_id)
        .execute();

      // Commit and exit
      trans.commit();
    }

    m_parent.m_httpd
      .postReply(HTTPReply(req.m_id, true, 200,
                           HTTPHeaders().add("content-type", "text/plain"),
                           "Account deleted successfully\n"));
    return;
  }
  default:
    m_parent.m_httpd
      .postReply(HTTPReply(req.m_id, true, 405,
                           HTTPHeaders().add("allow", "PUT, DELETE")
                           .add("content-type", "text/plain"),
                           "Methods:\n"
                           " PUT: Update user account\n"
                           " DELETE: Delete user account\n"));
    return;
  }
}


void MyWorker::cResources::handle(const HTTPRequest &req) const
{
  // Authenticate resource access
  //
  // AnonymousParent: No resource data
  // PartnerParent: Must read out customer resource consumption
  // User: Must read out own consumption
  // Device: Must read out own consumption
  //
  if (!m_parent.authenticate(req, CredCache::AT_PartnerParent
                             | CredCache::AT_User
                             | CredCache::AT_Device))
    return;

  //
  // First see that the authenticated peer is allowed access to the
  // requested account (verify that it is a parent of the account, or
  // the account itself)
  //


  //  uint64_t user_account;

  //
  // Treat request
  //
  switch (req.getMethod()) {
  case HTTPRequest::mGET: {
    //
    // We want to report:
    //
    // 1) Total number of accounts created under this account
    //
    // 2) Total number of devices created on this account and all
    //    accounts under it
    //
    // 3) Total number of bytes in most recent backup set on all
    //    devices (directly and indirectly) under this account
    //
    uint64_t child_accts = 0;
    uint64_t child_devs = 0;
    uint64_t active_bytes = 0;


    using namespace xml;
    const IDocument &ddoc
      = mkDoc(Element("resources")
              (Element("accounts")(CharData<uint64_t>(child_accts))
               & Element("devices")(CharData<uint64_t>(child_devs))
               & Element("active_bytes")(CharData<uint64_t>(active_bytes))));

    m_parent.m_httpd.postReply(req.m_id, ddoc);
    return;
  }
  default:
    m_parent.m_httpd
      .postReply(HTTPReply(req.m_id, true, 405,
                           HTTPHeaders().add("allow", "GET")
                           .add("content-type", "text/plain"),
                           "Methods:\n"
                           " GET: Get user resource consumption\n"));
    return;
  }
}


void MyWorker::cContacts::handle(const HTTPRequest &req) const
{
  // Authenticate for contact access
  //
  // AnonymousParent: Can create contact
  // PartnerParent: Can create contact
  // User: Can get/create contacts
  // Device: No contact access
  //
  if (!m_parent.authenticate(req, size_t(-1))) // authenticate to get
                                               // m_account_id, but do
                                               // not discriminate on
                                               // account types yet
    return;

  //
  // Look up the user account given in the request string
  //
  uint64_t query_root;
  if (!m_parent.getUserPathId(req.m_id, query_root))
    return;

  //
  // Treat request
  //
  switch (req.getMethod()) {
  case HTTPRequest::mGET: {
    if (!m_parent.authenticate(req, CredCache::AT_User))
      return;
    // Retrieve all contacts for the given user
    std::string type;
    sql::query tq(m_parent.m_db,
                  "SELECT ctype FROM contact "
                  "WHERE account = $1::BIGINT");
    tq.add(query_root).receiver(type);

    MTrace(t_api, trace::Debug, "Fetching contact type list for account "
           << query_root);

    using namespace xml;
    const IDocument &ddoc
      = mkDoc(Element("contacts")
              (*Element("type")(CharData<std::string>(type))
               [ papply(&tq, &sql::query::fetch) ]));

    m_parent.m_httpd.postReply(req.m_id, ddoc);
    return;
  }
  case HTTPRequest::mPOST: {
    if (!m_parent.authenticate(req, CredCache::AT_User | CredCache::AT_PartnerParent
                               | CredCache::AT_AnonymousParent))
      return;

    // Parse request document
    std::string p_type;
    Optional<std::string> p_email, p_companyname, p_fullname,
      p_phone, p_street1, p_street2, p_city, p_state, p_zipcode, p_country;

    using namespace xml;
    const IDocument &ddoc
      = mkDoc(Element("contact")
              (Element("type")(CharData<std::string>(p_type))
               & !Element("email")(CharData<Optional<std::string> >(p_email))
               & !Element("companyname")(CharData<Optional<std::string > >(p_companyname))
               & !Element("fullname")(CharData<Optional<std::string> >(p_fullname))
               & !Element("phone")(CharData<Optional<std::string> >(p_phone))
               & !Element("street1")(CharData<Optional<std::string> >(p_street1))
               & !Element("street2")(CharData<Optional<std::string> >(p_street2))
               & !Element("city")(CharData<Optional<std::string> >(p_city))
               & !Element("state")(CharData<Optional<std::string> >(p_state))
               & !Element("zipcode")(CharData<Optional<std::string> >(p_zipcode))
               & !Element("country")(CharData<Optional<std::string> >(p_country))));

    { std::istringstream body(req.m_body);
      XMLexer lexer(body);
      ddoc.process(lexer);
    }

    // Verify that type is "p"
    if (p_type != "p") {
      m_parent.m_httpd
        .postReply(HTTPReply(req.m_id, true, 400,
                             HTTPHeaders().add("content-type", "text/plain"),
                             "Only primary (type 'p') contact supported for now.\n"));
      return;
    }

    // p_type must always be one char.
    MAssert(p_type.size() == 1, "Pre-checks on p_type not correct");

    // Now create new contact
    try {
      sql::exec(m_parent.m_db,
                "INSERT INTO contact (email, companyname, fullname"
                ", phone, street1, street2, city, state, zipcode"
                ", country, account"
                ", ctype) VALUES ($1::TEXT, $2::TEXT, $3::TEXT"
                ", $4::TEXT, $5::TEXT, $6::TEXT, $7::TEXT, $8::TEXT, $9::TEXT"
                ", $10::TEXT, $11::BIGINT"
                ", $12::CHAR)")
        .add(p_email).add(p_companyname)
        .add(p_fullname).add(p_phone).add(p_street1)
        .add(p_street2).add(p_city).add(p_state)
        .add(p_zipcode).add(p_country)
        .add(query_root).add(p_type[0])
        .execute();
    } catch (error &e) {
      MTrace(t_api, trace::Info, "Contact create failed: "
             << e.toString());
      m_parent.m_httpd
        .postReply(HTTPReply(req.m_id, true, 409,
                             HTTPHeaders().add("content-type", "text/plain"),
                             "This contact type already exists for user.\n"));
      return;
    }

    // Fine, report success
    m_parent.m_httpd
      .postReply(HTTPReply(req.m_id, true, 201,
                           HTTPHeaders().add("location", "/users/"
                                             + m_parent.m_userid
                                             + "/contacts/" + p_type)
                           .add("access-control-allow-origin", "*")
                           .add("access-control-allow-headers",
                                "authorization, authentication-failure-code, location")
                           .add("access-control-expose-headers",
                                "location"),
                           std::string()));


    return;
  }
  case HTTPRequest::mOPTIONS: {
    //
    // Simply return the CORS headers
    //
    // Since this is a preflight check (as documented in
    // http://www.w3.org/TR/cors/#preflight-request), we must respond
    // with a 200 code even though we return no response body.
    //
    m_parent.m_httpd
      .postReply(HTTPReply(req.m_id, true, 200,
                           HTTPHeaders()
                           .add("access-control-allow-origin", "*")
                           .add("access-control-allow-headers",
                                "authorization, authentication-failure-code, location")
                           .add("access-control-expose-headers",
                                "location"),
                           std::string()));
    return;
  }
  default:
    m_parent.m_httpd
      .postReply(HTTPReply(req.m_id, true, 405,
                           HTTPHeaders().add("allow", "GET, POST, OPTIONS")
                           .add("content-type", "text/plain"),
                           "Methods:\n"
                           " GET: Retrieve list of contacts\n"
                           " POST: Create contact\n"
                           " OPTIONS: Service CORS preflight check\n"));
    return;
  }
}

void MyWorker::cContact::handle(const HTTPRequest &req) const
{
  // Authenticate for contact access
  //
  // AnonymousParent: No contact access
  // PartnerParent: No contact access
  // User: May get,create and edit details
  // Device: No contact access
  //
  if (!m_parent.authenticate(req, CredCache::AT_User))
    return;

  //
  // Look up the user account given in the request string
  //
  uint64_t query_root;
  if (!m_parent.getUserPathId(req.m_id, query_root))
    return;

  //
  // Treat request
  //
  switch (req.getMethod()) {
  case HTTPRequest::mGET: {

    // Set up query
    std::string p_type;
    Optional<std::string> p_email, p_companyname, p_fullname,
      p_phone, p_street1, p_street2, p_city, p_state, p_zipcode, p_country;

    sql::query cq(m_parent.m_db,
                  "SELECT ctype, email, companyname, fullname,"
                  " phone, street1, street2, city, state, zipcode, country "
                  " FROM contact WHERE account = $1::BIGINT AND ctype = $2::TEXT");
    cq.add(query_root).add(m_parent.m_contacttype);

    // Verify that contact exists - or return 404
    if (!cq.fetch()) {
      m_parent.m_httpd
        .postReply(HTTPReply(req.m_id, true, 404,
                             HTTPHeaders().add("content-type", "text/plain"),
                             "Contact not found\n"));
      return;
    }

    // Get results and generate return document
    cq.get(p_type).get(p_email).get(p_companyname).get(p_fullname)
      .get(p_phone).get(p_street1).get(p_street2).get(p_city)
      .get(p_state).get(p_zipcode).get(p_country);

    using namespace xml;
    const IDocument &ddoc
      = mkDoc(Element("contact")
               (Element("type")(CharData<std::string>(p_type))
                & !Element("email")(CharData<Optional<std::string> >(p_email))
                & !Element("companyname")(CharData<Optional<std::string > >(p_companyname))
                & !Element("fullname")(CharData<Optional<std::string> >(p_fullname))
                & !Element("phone")(CharData<Optional<std::string> >(p_phone))
                & !Element("street1")(CharData<Optional<std::string> >(p_street1))
                & !Element("street2")(CharData<Optional<std::string> >(p_street2))
                & !Element("city")(CharData<Optional<std::string> >(p_city))
                & !Element("state")(CharData<Optional<std::string> >(p_state))
                & !Element("zipcode")(CharData<Optional<std::string> >(p_zipcode))
                & !Element("country")(CharData<Optional<std::string> >(p_country))));
    m_parent.m_httpd.postReply(req.m_id, ddoc);

    return;
  }
  case HTTPRequest::mPUT: {

    // Parse update document....

    Optional<std::string> p_email, p_companyname, p_fullname,
      p_phone, p_street1, p_street2, p_city, p_state, p_zipcode, p_country;

    using namespace xml;
    const IDocument &ddoc
      = mkDoc(Element("contact")
              (!Element("email")(CharData<Optional<std::string> >(p_email))
               & !Element("companyname")(CharData<Optional<std::string > >(p_companyname))
               & !Element("fullname")(CharData<Optional<std::string> >(p_fullname))
               & !Element("phone")(CharData<Optional<std::string> >(p_phone))
               & !Element("street1")(CharData<Optional<std::string> >(p_street1))
               & !Element("street2")(CharData<Optional<std::string> >(p_street2))
               & !Element("city")(CharData<Optional<std::string> >(p_city))
               & !Element("state")(CharData<Optional<std::string> >(p_state))
               & !Element("zipcode")(CharData<Optional<std::string> >(p_zipcode))
               & !Element("country")(CharData<Optional<std::string> >(p_country))));

    { std::istringstream body(req.m_body);
      XMLexer lexer(body);
      ddoc.process(lexer);
    }

    // Now update the given contact
    sql::exec ex(m_parent.m_db,
                 "UPDATE contact SET email = $1::TEXT, companyname = $2::TEXT, "
                 "fullname = $3::TEXT, phone = $4::TEXT, street1 = $5::TEXT, "
                 "street2 = $6::TEXT, city = $7::TEXT, state = $8::TEXT, "
                 "zipcode = $9::TEXT, country = $10::TEXT "
                 "WHERE account = $11::BIGINT AND ctype = $12::TEXT");
    ex.add(p_email).add(p_companyname)
      .add(p_fullname).add(p_phone).add(p_street1)
      .add(p_street2).add(p_city).add(p_state)
      .add(p_zipcode).add(p_country)
      .add(query_root).add(m_parent.m_contacttype)
      .execute();

    // If no rows were affected, the contact didn't exist
    if (!ex.affectedRows()) {
      m_parent.m_httpd
        .postReply(HTTPReply(req.m_id, true, 404,
                             HTTPHeaders().add("content-type", "text/plain"),
                             "Contact not found\n"));
      return;
    }

    // Ok fine, we succeeded.
    m_parent.m_httpd
      .postReply(HTTPReply(req.m_id, true, 200,
                           HTTPHeaders().add("content-type", "text/plain"),
                           "Successfully updated contact details.\n"));

    return;
  }
  default:
    m_parent.m_httpd
      .postReply(HTTPReply(req.m_id, true, 405,
                           HTTPHeaders().add("allow", "GET")
                           .add("content-type", "text/plain"),
                           "Methods:\n"
                           " GET: Retrieve contact details\n"
                           " PUT: Update contact details\n"));
    return;
  }
}


void MyWorker::cSubUsers::handle(const HTTPRequest &req) const
{
  // Authenticate for child user access
  //
  // AnonymousParent: No access to create child accounts
  // PartnerParent: Must be able to see customers
  // User: Must be able to see and create child accounts
  // Device: No child users access
  //
  if (!m_parent.authenticate(req, size_t(-1))) // no particular type yet
    return;

  //
  // Look up the user account given in the request string
  //
  uint64_t query_root;
  if (!m_parent.getUserPathId(req.m_id, query_root))
    return;

  //
  // Treat request
  //
  switch (req.getMethod()) {
  case HTTPRequest::mGET: {
    if (!m_parent.authenticate(req, CredCache::AT_PartnerParent | CredCache::AT_User))
      return;
    //
    // Now retrieve list of users and report it
    //
    std::string guid;
    sql::query gq(m_parent.m_db,
                  "SELECT guid FROM account WHERE parent = $1::BIGINT");
    gq.add(query_root)
      .receiver(guid);

    MTrace(t_api, trace::Debug, "Fetching list of child accounts under "
           << query_root);

    using namespace xml;
    const IDocument &ddoc
      = mkDoc(Element("users")
              (*Element("id")(CharData<std::string>(guid))
               [papply(&gq, &sql::query::fetch)]));

    m_parent.m_httpd.postReply(req.m_id, ddoc);
    return;
  }
  case HTTPRequest::mPOST: {
    // Create user.
    //
    // AnonymousParent: Creates trial accounts
    // PartnerParent: Creates real accounts
    // User: Creates sub users
    // Device: Must not do user management
    //
    if (!m_parent.authenticate(req, CredCache::AT_AnonymousParent
                               | CredCache::AT_PartnerParent
                               | CredCache::AT_User))
      return;

    // We create an enabled account with the parent set to the
    // received user id. However, the credentials used in this
    // connection must grant access to the given parent account - so
    // the account our peer uses must be the - or an ancestor to the -
    // parent account given.
    //
    // We use the given email address to create an access token with
    // the aname as the e-mail address (and the apass as the supplied
    // password).
    //
    // We also create a "primary" contact record with the email
    // address (and, if received, the user fullname).
    //
    using namespace xml;
    std::string p_login;
    std::string p_password;
    std::string p_extid;

    const IDocument &ddoc
      = mkDoc(Element("user_create")
              (Element("login")(CharData<std::string>(p_login))
               & Element("password")(CharData<std::string>(p_password))
               & !Element("external_id")(CharData<std::string>(p_extid))));
    { std::istringstream body(req.m_body);
      XMLexer lexer(body);
      ddoc.process(lexer);
    }

    // Create user account
    { sql::transaction trans(m_parent.m_db);
      // First the account
      sql::query q(m_parent.m_db, "SELECT nextval('account_id_seq')");
      if (!q.fetch()) throw error("Cannot get account id");
      uint64_t new_account_id;
      q.get(new_account_id);
      // Fine, insert
      sql::exec(m_parent.m_db,
                "INSERT INTO account (id,parent)"
                " VALUES ($1::BIGINT,$2::BIGINT)")
        .add(new_account_id).add(query_root)
        .execute();
      // Log it
      MTrace(t_api, trace::Info, "Creating user account " << new_account_id
             << " with parent " << query_root);

      try {
        //
        // Also create a set of credentials for this user account. We
        // use the supplied e-mail address as the login.
        //
        sql::exec(m_parent.m_db,
                  "INSERT INTO access (descr, aname, apass, created_by, "
                  "ttype, target, atype) "
                  "VALUES ($1::TEXT, $2::TEXT, $3::TEXT, $4::BIGINT, "
                  "'a', $5::BIGINT, 'u')")
          .add("Primary user login token")
          .add(p_login).add(p_password).add(m_parent.m_access_id)
          .add(new_account_id)
          .execute();
      } catch (error &e) {
        // Insert fails on conflict only
        MTrace(t_api, trace::Info, "Could not create credentials with login " << p_login
               << " : " << e.toString());
        m_parent.m_httpd
          .postReply(HTTPReply(req.m_id, true, 409,
                               HTTPHeaders()
                               .add("content-type", "text/plain"),
                               "An account with this login name already exists.\n"));
        return;
      }

      //
      // If an external id is given, attach it to this account
      //
      // Remember, the "parent" field in this table is not necessarily
      // the parent account of the newly created account - it is the
      // (possibly indirect parent) partner account for which the
      // external id must be unique.
      //
      if (!p_extid.empty()) {
        sql::exec(m_parent.m_db,
                  "INSERT INTO account_extern_id (account,parent,extern)"
                  " VALUES ($1::BIGINT, $2::BIGINT, $3::TEXT)")
          .add(new_account_id).add(m_parent.m_account_id).add(p_extid)
          .execute();
      }

      //
      // Extract the user guid for our reply...
      //
      std::string user_guid;
      { sql::query q(m_parent.m_db, "SELECT guid FROM account WHERE id = $1::BIGINT");
        q.add(new_account_id);
        if (!q.fetch()) throw error("Unable to retrieve account we just created");
        q.get(user_guid);
      }

      // Commit and get out of here
      trans.commit();
      // Fine, let user know we did this.
      m_parent.m_httpd
        .postReply(HTTPReply(req.m_id, true, 201,
                             HTTPHeaders()
                             .add("location", "/users/" + user_guid)
                             .add("access-control-allow-origin", "*")
                             .add("access-control-allow-headers",
                                  "authorization, authentication-failure-code")
                             .add("access-control-expose-headers",
                                  "location"),
                             std::string()));
      return;
    }
  }
  default:
    m_parent.m_httpd
      .postReply(HTTPReply(req.m_id, true, 405,
                           HTTPHeaders().add("allow", "GET, POST")
                           .add("content-type", "text/plain"),
                           "Methods:\n"
                           " GET: Retrieve list of child user accounts\n"
                           " POST: Create user account\n"));
    return;
  }
}




void MyWorker::cDownFile::handle(const HTTPRequest &req) const
{
  //
  // About authentication: Using this endpoint requires knowledge of a
  // directory object hash. Thus, we do not strictly need
  // authentication. We may wish to have authentication for auditing
  // or later on if we store the owning user id in the directory we
  // would want to match the authenticated user against that. But
  // right now, we can do downloads without authentication.
  //

  //
  // Treat request
  //
  switch (req.getMethod()) {
  case HTTPRequest::mGET: {
    //
    // First, locate and download the directory object
    //
    objseq_t filedata;
    { HTTPRequest dirget;
      dirget.m_method = HTTPRequest::mGET;
      dirget.m_uri = "/object/" + m_parent.m_downdir;
      MTrace(t_api, trace::Debug, "Fetching directory object " << m_parent.m_downdir);
      HTTPReply rep = m_parent.m_osapi.execute(dirget);
      if (rep.getStatus() == 404) {
        m_parent.m_httpd.postReply(HTTPReply(req.m_id, true, 404,
                                             HTTPHeaders()
                                             .add("content-type", "text/plain"),
                                             "Directory object does not exist\n"));
        return;
      }
      if (rep.getStatus() != 200)
        throw error("Cannot get directory object: " + rep.toString());
      // Fine, we have the directory object. Now search the LoM for
      // the given file-name
      const std::vector<uint8_t> object(rep.refBody().begin(),
                                        rep.refBody().end());
      size_t ofs = 0;
      //
      // Offset 0:  1 byte, version 0
      if (0 != des<uint8_t>(object, ofs))
        throw error("Unexpected object version");
      // Offset 1: 1 byte object type - expect 0xdd or 0xde for directory entry
      uint8_t objType= des<uint8_t>(object, ofs);
      if (0xdd != objType && 0xde != objType)
        throw error("Object not directory entry");
      // Offset 2: 8 byte tree-size
      ofs += 8;
      // Offset 10: LoR
      std::vector<objseq_t> lor;
      const size_t lor_len = des<uint32_t>(object, ofs);
      for (size_t i = 0; i != lor_len; ++i)
        lor.push_back(des<objseq_t>(object, ofs));
      // LoM - same number of entries as LoR
      bool found = false;
      for (size_t i = 0; i != lor_len; ++i) {
        const std::string name = lom_entry_extract_name(object, ofs);
        if (name == m_parent.m_downfile) {
          MTrace(t_api, trace::Debug, " Located downfile=" << name);
          filedata = lor[i];
          found = true;
          break;
        }
      }
      if (!found) {
        MTrace(t_api, trace::Debug, " No file named " << m_parent.m_downfile);
        m_parent.m_httpd.postReply(HTTPReply(req.m_id, true, 404,
                                             HTTPHeaders()
                                             .add("content-type", "text/plain"),
                                             "File object does not exist\n"));
        return;
      }
    }
    //
    // So, since the directory object references the file data
    // objects, it is safe to assume that they are there. Just go
    // ahead and download them.
    //
    m_parent.generateFileDownloadReply(req,
                                       m_parent.m_downfile,
                                       filedata, true);

    return;
  }
  default:
    m_parent.m_httpd
      .postReply(HTTPReply(req.m_id, true, 405,
                           HTTPHeaders().add("allow", "GET")
                           .add("content-type", "text/plain"),
                           "Methods:\n"
                           " GET: Initiate file download\n"));
    return;
  }
}


void MyWorker::cQueue::handle(const HTTPRequest &req) const
{
  switch (req.getMethod()) {
  case HTTPRequest::mGET: {
    //
    // For GET requests we require a special system privilege user.
    //
    // We care use a device type token for this.
    //
    if (!m_parent.authenticate(req, CredCache::AT_Device, REQSYS))
      return;

    // GET on a queue means; retrieve the events with the oldest
    // expiry time older than 'now'. If no such event exists, return
    // 'temporarily unavailable'. If an event is returned it is also
    // de-queued and re-scheduled in case it is a recurring event.
    {
      sql::transaction trans(m_parent.m_db);
      sql::query q(m_parent.m_db,
                   "SELECT s.id, s.identifier,"
                   "       COALESCE(s.period, '0 seconds'::INTERVAL),"
                   "       a.guid"
                   " FROM schedule s, account a"
                   " WHERE s.queue = $1::TEXT"
                   "   AND s.expire <= now()"
                   "   AND s.account = a.id"
                   " ORDER BY expire ASC LIMIT 1");
      q.add(m_parent.m_queuename);
      if (!q.fetch()) {
        // No event expired yet.
        m_parent.m_httpd
          .postReply(HTTPReply(req.m_id, true, 404,
                               HTTPHeaders().add("content-type", "text/plain"),
                               "No event expired yet in this queue\n"));
        return;
      }

      uint64_t id;
      std::string identifier;
      DiffTime period;
      std::string account;
      q.get(id).get(identifier).get(period).get(account);

      if (period > DiffTime(0, 0)) {
        // If this is a recurring event, re-schedule it.
        sql::exec(m_parent.m_db,
                  "UPDATE schedule SET expire = now() + period"
                  " WHERE id = $1::BIGINT")
          .add(id).execute();
      } else {
        // Fine, non-recurring so delete it.
        sql::exec(m_parent.m_db,
                  "DELETE FROM schedule WHERE id = $1::BIGINT")
          .add(id).execute();
      }

      // Commit transaction and return event.
      trans.commit();

      using namespace xml;
      const IDocument &ddoc
        = mkDoc(Element("event")
                (Element("identifier")(CharData<std::string>(identifier))
                 & Element("account")(CharData<std::string>(account))));
      m_parent.m_httpd.postReply(req.m_id, ddoc);
    }
    return;
  }
  case HTTPRequest::mPOST: {
    // POST on a queue will queue a new event (possibly a recurring
    // event). It will fail if the event identifier already exists
    // in this queue.

    // POST is authenticated - we need to use the authenticated user
    // account id.
    // We use a device or user token.
    // (user token is needed for the case when update scheduled
    //  after O365/FTP device creation in Web client)
    //
    if (!m_parent.authenticate(req, CredCache::AT_User | CredCache::AT_Device))
      return;

    // Now parse the event description document
    std::string identifier;
    Time expire(Time::BEGINNING_OF_TIME);
    DiffTime period(0,0);
    using namespace xml;
    const IDocument &ddoc
      = mkDoc(Element("event")
              (Element("identifier")(CharData<std::string>(identifier))
               & !Element("expire")(CharData<Time>(expire))
               & !Element("period")(CharData<DiffTime>(period))));
    { std::istringstream body(req.m_body);
      XMLexer lexer(body);
      ddoc.process(lexer);
    }

    // Fine, add event then
    sql::exec(m_parent.m_db,
              "INSERT INTO schedule (account, queue, expire, period, identifier)"
              " VALUES ($1::BIGINT, $2::TEXT, $3::TIMESTAMP, $4::INTERVAL, $5::TEXT)")
      .add(m_parent.m_account_id)
      .add(m_parent.m_queuename)
      .add(expire)
      .add(period)
      .add(identifier)
      .execute();

    // Send back 201 created with location header
    m_parent.m_httpd
      .postReply(HTTPReply(req.m_id, true, 201,
                           HTTPHeaders()
                           .add("location",
                                "/queue/" + m_parent.m_queuename
                                + "/" + str2url(identifier)),
                           std::string()));
    return;
  }
  default:
    m_parent.m_httpd
      .postReply(HTTPReply(req.m_id, true, 405,
                           HTTPHeaders().add("allow", "GET, POST")
                           .add("content-type", "text/plain"),
                           "Methods:\n"
                           " GET: Fetch oldest expired event\n"
                           " POST: Queue new event\n"));
    return;
  }
}


void MyWorker::cQueueEvent::handle(const HTTPRequest &req) const
{
  // Authenticate - we need the user id for the event deletion
  //
  // We use a Device or User token for this
  //
  if (!m_parent.authenticate(req, CredCache::AT_Device | CredCache::AT_User))
    return;

  switch (req.getMethod()) {
  case HTTPRequest::mDELETE: {

    // Fine, delete event if we can find it
    sql::exec cmd(m_parent.m_db,
                  "DELETE FROM schedule"
                  " WHERE account = $1::BIGINT"
                  "   AND queue = $2::TEXT"
                  "   AND identifier = $3::TEXT");
    cmd.add(m_parent.m_account_id)
      .add(m_parent.m_queuename)
      .add(m_parent.m_eventid)
      .execute();

    if (cmd.affectedRows() == 0) {
      // Nothing deleted. We have to report 404
      m_parent.m_httpd
        .postReply(HTTPReply(req.m_id, true, 404,
                             HTTPHeaders().add("content-type", "text/plain"),
                             "Matching event not found.\n"));
    } else {
      // We deleted something then
      m_parent.m_httpd
        .postReply(HTTPReply(req.m_id, true, 200,
                             HTTPHeaders().add("content-type", "text/plain"),
                             "Event successfully deleted.\n"));
    }

    return;
  }
  default:
    m_parent.m_httpd
      .postReply(HTTPReply(req.m_id, true, 405,
                           HTTPHeaders().add("allow", "DELETE")
                           .add("content-type", "text/plain"),
                           "Methods:\n"
                           " DELETE: Delete event from queue\n"));
    return;
  }

}


void MyWorker::cStatus::handle(const HTTPRequest &req) const
{
  switch (req.getMethod()) {
  case HTTPRequest::mGET: {

    std::string version(g_getVersion());
    Time p_time(Time::now());
    size_t p_queue(m_parent.m_httpd.getQueueLength());

    cm hm(*this);

    using namespace xml;
    const IDocument &ddoc
      = mkDoc(Element("status")
              (Element("version")(CharData<std::string>(version))
               & Element("time")(CharData<Time>(p_time))
               & Element("request-queue")(CharData<size_t>(p_queue))
               & *Element("mirror")
               (Element("host")(CharData<std::string>(hm.host))
                & Element("port")(CharData<uint16_t>(hm.port))
                & SubDocument(hm.m_status))[ papply(&hm, &cm::getNext) ]));

    // Process and output document
    m_parent.m_httpd.postReply(req.m_id, ddoc);
    return;
  }
  default:
    m_parent.m_httpd
      .postReply(HTTPReply(req.m_id, true, 405,
                           HTTPHeaders().add("allow", "GET")
                           .add("content-type", "text/plain"),
                           "Methods:\n"
                           " GET: Get current API server status\n"));
    return;
  }

}

MyWorker::cStatus::cm::cm(const MyWorker::cStatus &p)
  : m_parent(p)
  , m_curr(p.m_parent.m_cfg.hOSAPI.m_hosts.begin())
{
}

bool MyWorker::cStatus::cm::getNext()
{
  if (m_curr == m_parent.m_parent.m_cfg.hOSAPI.m_hosts.end())
    return false;

  host = m_curr->first;
  port = m_curr->second;
  m_status.clear();

  // Fetch /status document
  try {
    HTTPclient c(host, port);
    HTTPRequest req;
    req.m_method = HTTPRequest::mGET;
    req.m_uri = "/status";
    req.m_headers.add("host", host);
    HTTPReply rep = c.execute(req);
    if (rep.getStatus() == 200) {
      using namespace xml;
      const IDocument &ddoc = mkDoc(SubDocument(m_status));
      std::istringstream istr(rep.refBody());
      XMLexer lexer(istr);
      ddoc.process(lexer);
    } else {
      m_status = "<!-- Non-200 status -->";
    }

  } catch (error &e) {
    m_status = "<!-- Cannot fetch /status (" + e.toString() + " -->";
  }

  ++m_curr;
  return true;
}


void MyWorker::cFavourites::handle(const HTTPRequest &req) const
{
  // Authenticate
  //
  // AnonymousParent: No favourites access
  // PartnerParent: No favourites access
  // User: Can create favourites
  // Device: Can create favourites (auto-favourites of oft-used targets)
  //
  if (!m_parent.authenticate(req, CredCache::AT_User | CredCache::AT_Device))
    return;

  // Fine, POST (add) or GET (list) favourites
  switch (req.getMethod()) {
  case HTTPRequest::mPOST: {
    const std::string label(randStr(8));
    std::string path;

    using namespace xml;
    const IDocument &ddoc
      = mkDoc(Element("favourite")
              (Element("path")(CharData<std::string>(path))));
    { std::istringstream body(req.m_body);
      XMLexer lexer(body);
      ddoc.process(lexer);
    }

    // Parse path
    std::string devname;
    std::string partpath;
    try { splitPath(devname, partpath, path); }
    catch (error&) {
      m_parent.m_httpd
        .postReply(HTTPReply(req.m_id, true, 400,
                             HTTPHeaders().add("content-type", "text/plain"),
                             "Path must be on form:  /{devname}/{path-to-object}.\n"));
      return;
    }

    // Start transaction for our two-stage insert
    sql::transaction trans(m_parent.m_db);

    // Locate device id
    uint64_t devid;
    sql::query q(m_parent.m_db,
                 "SELECT id FROM device "
                 "WHERE account = $1::BIGINT "
                 "AND descr = $2::TEXT");
    q.add(m_parent.m_account_id).add(devname);
    if (!q.fetch()) {
      m_parent.m_httpd
        .postReply(HTTPReply(req.m_id, true, 404,
                             HTTPHeaders().add("content-type", "text/plain"),
                             "No device named \"" + devname + "\".\n"));
      return;
    }
    q.get(devid);

    // Insert.
    sql::exec(m_parent.m_db,
              "INSERT INTO favourites (account, label, device, path)"
              " VALUES ($1::BIGINT, $2::TEXT, $3::BIGINT, $4::TEXT)")
      .add(m_parent.m_account_id)
      .add(label).add(devid).add(partpath).execute();

    // Commit the transaction
    trans.commit();

    // Send back a 201 created with a location header
    m_parent.m_httpd
      .postReply(HTTPReply(req.m_id, true, 201,
                           HTTPHeaders()
                           .add("location",
                                "/favourites/" + str2url(label)),
                           std::string()));
    return;
  }
  case HTTPRequest::mGET: {
    std::string label;
    std::string path;

    sql::query q(m_parent.m_db,
                 "SELECT f.label, '/' || d.descr || f.path "
                 "FROM favourites f, device d "
                 "WHERE f.account = $1::BIGINT "
                 "AND d.account = $1::BIGINT " // safety measure only
                 "AND f.device = d.id");
    q.add(m_parent.m_account_id)
      .receiver(label).receiver(path);

    using namespace xml;
    const IDocument &ddoc
      = mkDoc(Element("favourites")
              (*Element("favourite")
               (Element("label")(CharData<std::string>(label))
                & Element("path")(CharData<std::string>(path)))
               [papply(&q, &sql::query::fetch)]));
    m_parent.m_httpd.postReply(req.m_id, ddoc);
    return;
  }
  default:
    m_parent.m_httpd
      .postReply(HTTPReply(req.m_id, true, 405,
                           HTTPHeaders().add("allow", "POST, GET")
                           .add("content-type", "text/plain"),
                           "Methods:\n"
                           " POST: Add favourite\n"
                           " GET: Get favourites\n"));
    return;
  }
}


void MyWorker::cFavourites::splitPath(std::string &dev, std::string &partial,
                                      const std::string &full) const
{
  // Path is '/' {device-name} '/' {path-to-object}
  if (full.find('/') != 0)
    throw error("Path must start with slash");
  // Locate end of device name
  size_t devend = full.find('/', 1);
  if (devend == std::string::npos)
    throw error("No slash after device name");
  // Extract device name
  dev = full.substr(1, devend - 1);
  // Extract partial path (path without device name)
  partial = full.substr(devend);
}


void MyWorker::cFavourite::handle(const HTTPRequest &req) const
{
  // Authenticate
  //
  // AnonymousParent: No favourites access
  // PartnerParent: No favourites access
  // User: Can delete favourites
  // Device: No favourite deletion
  //
  if (!m_parent.authenticate(req, CredCache::AT_User))
    return;

  // Fine, DELETE specific favourite
  switch (req.getMethod()) {
  case HTTPRequest::mDELETE: {

    sql::exec(m_parent.m_db,
              "DELETE FROM favourites "
              "WHERE account = $1::BIGINT "
              "AND label = $2::TEXT")
      .add(m_parent.m_account_id)
      .add(m_parent.m_favourite)
      .execute();

    m_parent.m_httpd
      .postReply(HTTPReply(req.m_id, true, 200,
                           HTTPHeaders().add("content-type", "text/plain"),
                           "Favourite successfully deleted.\n"));
    break;
  }
  default:
    m_parent.m_httpd
      .postReply(HTTPReply(req.m_id, true, 405,
                           HTTPHeaders().add("allow", "DELETE")
                           .add("content-type", "text/plain"),
                           "Methods:\n"
                           " DELETE: Remove favourite\n"));
    return;
  }
}




void MyWorker::cShares::handle(const HTTPRequest &req) const
{
  // Authenticate
  //
  // AnonymousParent: No share access
  // PartnerParent: No share access
  // User: Can create shares
  // Device: Can create shares
  //
  if (!m_parent.authenticate(req, CredCache::AT_User | CredCache::AT_Device))
    return;

  // Fine, POST (add) a share
  switch (req.getMethod()) {
  case HTTPRequest::mPOST: {
    std::string device;
    std::string path;
    Time expires(Time::now() + DiffTime::iso("P5DT")); // expire in 5 days

    using namespace xml;
    const IDocument &ddoc
      = mkDoc(Element("share")
              (Element("device")(CharData<std::string>(device))
               & Element("path")(CharData<std::string>(path))
               & !Element("expires")(CharData<Time>(expires))));
    { std::istringstream body(req.m_body);
      XMLexer lexer(body);
      ddoc.process(lexer);
    }

    // Match device
    uint64_t dev_id;
    { sql::query dq(m_parent.m_db,
                    "SELECT id FROM device "
                    "WHERE account = $1::BIGINT"
                    "  AND descr = $2::TEXT");
      dq.add(m_parent.m_account_id)
        .add(device);
      if (!dq.fetch()) {
        m_parent.m_httpd
          .postReply(HTTPReply(req.m_id, true, 400,
                               HTTPHeaders().add("content-type", "text/plain"),
                               "No device with matching name found.\n"));
        return;
      }
      dq.get(dev_id);
    }

    // Fine, now see that path starts with a slash.
    if (path.empty() || path[0] != '/') {
      m_parent.m_httpd
        .postReply(HTTPReply(req.m_id, true, 400,
                             HTTPHeaders().add("content-type", "text/plain"),
                             "Given path must start with slash.\n"));
      return;
    }

    // Generate guid string
    std::string guidstr;
    if (!sql::query(m_parent.m_db, "SELECT guidstr()")
        .receiver(guidstr).fetch()) {
      m_parent.m_httpd
        .postReply(HTTPReply(req.m_id, true, 500,
                             HTTPHeaders().add("content-type", "text/plain"),
                             "Unable to generate share GUID string.\n"));
      return;
    }

    // Insert.
    sql::exec(m_parent.m_db,
              "INSERT INTO share (account, access, guid, device, path, expires) "
              "VALUES ($1::BIGINT, $2::BIGINT, $3::TEXT, $4::BIGINT, $5::TEXT, "
              "$6::TIMESTAMP)")
      .add(m_parent.m_account_id).add(m_parent.m_access_id)
      .add(guidstr).add(dev_id).add(path).add(expires)
      .execute();

    MTrace(t_api, trace::Info, "Account id " << m_parent.m_account_id
           << " shares " << path << " on device " << device
           << " as /share/" << guidstr);

    // Send back a 201 created with a location header
    m_parent.m_httpd
      .postReply(HTTPReply(req.m_id, true, 201,
                           HTTPHeaders()
                           .add("location",
                                "/share/" + guidstr),
                           std::string()));
    return;
  }
    // GET /shares/ - returns document with list of shares
  case HTTPRequest::mGET: {
    // element shares {
    //  element share {
    //   element guid { text }
    //   & element path { text }
    //   & element expires { timestamp }
    //   & element device { text }
    //  }*
    // }
    std::string guid;
    std::string path;
    Time expires;
    std::string device;
    sql::query fq(m_parent.m_db,
                  "SELECT s.guid, s.path, s.expires, d.descr "
                  "FROM share s, device d "
                  "WHERE s.device = d.id "
                  "AND s.account = $1::BIGINT "
                  "AND s.expires > now()");
    fq.add(m_parent.m_account_id);
    fq.receiver(guid).receiver(path).receiver(expires).receiver(device);

    using namespace xml;
    const IDocument &ddoc
      = mkDoc(Element("shares")
              (*Element("share")
               (Element("guid")(CharData<std::string>(guid))
                & Element("path")(CharData<std::string>(path))
                & Element("expires")(CharData<Time>(expires))
                & Element("device")(CharData<std::string>(device)))
               [papply(&fq, &sql::query::fetch)]));
    m_parent.m_httpd.postReply(req.m_id, ddoc);
    return;
  }
  default:
    m_parent.m_httpd
      .postReply(HTTPReply(req.m_id, true, 405,
                           HTTPHeaders().add("allow", "POST, GET")
                           .add("content-type", "text/plain"),
                           "Methods:\n"
                           " POST: Add share\n"
                           " GET: List shares\n"));
    return;
  }
}


void MyWorker::cShare::handle(const HTTPRequest &req) const
{
  //
  // Access to a specific share is always unauthenticated
  //
  switch (req.getMethod()) {
  case HTTPRequest::mGET: {
    // Verify that share exists and hasn't expired.
    if (!sql::query(m_parent.m_db,
                    "SELECT id FROM share WHERE guid = $1::TEXT "
                    "AND expires > now()")
        .add(m_parent.m_share).fetch()) {
      // No, no such share.
      // Remove it, if it simply expired
      sql::exec(m_parent.m_db,
                "DELETE FROM share WHERE guid = $1::TEXT "
                "AND expires < now()").add(m_parent.m_share).execute();
      // Respond accordingly
      //
      // It will be quite common that shares have expired or that
      // users introduce errors in their copy/paste. Therefore we have
      // the ability to present a stylish 404 page for this scenario.
      //
      if (m_parent.m_cfg.docShare404.empty()
          || !m_parent.postFileReply(req, 404, m_parent.m_cfg.docShare404)) {
        m_parent.m_httpd
          .postReply(HTTPReply(req.m_id, true, 404,
                               HTTPHeaders().add("content-type", "text/plain"),
                               "Share not found - it may have expired.\n"));
      }
      return;
    }

    // Get the index document under /share/.
    HTTPRequest nreq(req);
    nreq.m_uri = "/share/";
    if (!m_parent.processLocal(nreq)) {
      //
      // We do NOT present a stylish 404 page for this scenario,
      // because if this error ever occurs it means that we have
      // misconfigured our API front-end servers. In other words, this
      // should never happen.
      //
      m_parent.m_httpd
        .postReply(HTTPReply(req.m_id, true, 404,
                             HTTPHeaders().add("content-type", "text/plain"),
                             "Index file for share not found.\n"));
    }
    return;
  }
  case HTTPRequest::mDELETE: {
    // Deletion must be authenticated
    //
    // AnonymousParent: No share access
    // PartnerParent: No share access
    // User: Can delete shares
    // Device: Can delete shares
    //
    if (!m_parent.authenticate(req, CredCache::AT_User | CredCache::AT_Device))
      return;

    // So let's see if the authenticated user is the owner of the
    // share
    uint64_t share_owner_id;
    sql::transaction trans(m_parent.m_db);
    if (!sql::query(m_parent.m_db,
                    "SELECT account FROM share WHERE guid = $1::TEXT")
        .add(m_parent.m_share)
        .receiver(share_owner_id)
        .fetch()) {
      m_parent.m_httpd
        .postReply(HTTPReply(req.m_id, true, 404,
                             HTTPHeaders().add("content-type", "text/plain"),
                             "Share not found.\n"));
      return;
    }

    // Is the authenticated user the owner?
    if (share_owner_id != m_parent.m_account_id) {
      m_parent.m_httpd
        .postReply(HTTPReply(req.m_id, true, 403,
                             HTTPHeaders().add("content-type", "text/plain"),
                             "You must be share owner to delete it.\n"));
      return;
    }

    // So we own the share - delete it
    sql::exec(m_parent.m_db,
              "DELETE FROM share WHERE guid = $1::TEXT "
              "AND account = $2::BIGINT")
      .add(m_parent.m_share)
      .add(m_parent.m_account_id)
      .execute();

    // And commit...
    trans.commit();

    // We're good
    m_parent.m_httpd
      .postReply(HTTPReply(req.m_id, true, 200,
                           HTTPHeaders().add("content-type", "text/plain"),
                           "Share successfully deleted.\n"));

    return;
  }
  default:
    m_parent.m_httpd
      .postReply(HTTPReply(req.m_id, true, 405,
                           HTTPHeaders().add("allow", "GET, DELETE")
                           .add("content-type", "text/plain"),
                           "Methods:\n"
                           " GET: Access share browser\n"
                           " DELETE: Delete share\n"));
    return;
  }
}


void MyWorker::cSPath::handle(const HTTPRequest &req) const
{
  //
  // Access to a sub-path under a share is always unauthenticated
  //
  switch (req.getMethod()) {
  case HTTPRequest::mGET: {
    //
    // Extract device id and root-share-path from share
    //
    uint64_t dev_id;
    std::string rootpath;
    if (!sql::query(m_parent.m_db,
                    "SELECT device, path FROM share WHERE guid = $1::TEXT "
                    "AND expires > now()")
        .add(m_parent.m_share).receiver(dev_id).receiver(rootpath).fetch()) {
      // Remove it, if it simply expired
      sql::exec(m_parent.m_db,
                "DELETE FROM share WHERE guid = $1::TEXT "
                "AND expires < now()").add(m_parent.m_share).execute();
      // Respond accordingly
      if (m_parent.m_cfg.docShare404.empty()
          || !m_parent.postFileReply(req, 404, m_parent.m_cfg.docShare404)) {
        m_parent.m_httpd
          .postReply(HTTPReply(req.m_id, true, 404,
                               HTTPHeaders().add("content-type", "text/plain"),
                               "Share not found - it may have expired.\n"));
      }
      return;
    }

    //
    // The path requested is in m_spath.
    //
    // First, locate the root object of the most recent backup set of
    // the shared device
    //
    std::string rootobj;
    if (!sql::query(m_parent.m_db,
                    "SELECT root FROM backup WHERE device = $1::BIGINT "
                    "ORDER BY tstamp DESC LIMIT 1")
        .add(dev_id).receiver(rootobj).fetch()) {
      // Unable to locate most recent backup on device...
      m_parent.m_httpd
        .postReply(HTTPReply(req.m_id, true, 404,
                             HTTPHeaders().add("content-type", "text/plain"),
                             "No snapshots on shared device.\n"));
      return;
    }

    MTrace(t_api, trace::Info, "Allowing access to root path "
           << rootpath << " on device id " << dev_id);

    //
    // We simply prepend the root path to the access path
    //
    std::string spath = rootpath + m_parent.m_spath;
    //
    // Fine, now we simply traverse the path...
    //
    std::string filename; // Non-empty if m_spath points to a file
    objseq_t curobj;
    curobj.push_back(sha256::parse(rootobj));
    while (!spath.empty()) {
      // Strip leading slash(es)
      while (!spath.empty() && spath[0] == '/')
        spath.erase(0, 1);
      if (spath.empty()) break;
      // Fetch next component
      const std::string next(spath.substr(0, spath.find("/")));
      spath.erase(0, next.size());
      // Load the current directory
      FSDir curr(papply(&m_parent, &MyWorker::fetchObject), curobj);
      // Locate the entry to traverse into
      for (FSDir::dirents_t::const_iterator i = curr.dirents.begin();
           i != curr.dirents.end(); ++i) {
        if (i->name == next) {
          curobj = i->hash;
          if (i->type == FSDir::dirent_t::UNIXFILE
              || i->type == FSDir::dirent_t::WINFILE)
            filename = i->name;
          goto next_component;
        }
      }
      // Unable to find path component
      m_parent.m_httpd
        .postReply(HTTPReply(req.m_id, true, 404,
                             HTTPHeaders().add("content-type", "text/plain"),
                             "Component " + next + " not found\n"));
      return;
    next_component:;
    }
    // Fine, no unresolved component. Our curobj is now either
    // pointing to a sequence of directory or file data objects.
    if (!filename.empty()) {
      // Fine, it is a file and we should download it.  The URI for a
      // share is not data-constant so we tell
      // generateFileDownloadReply to not add our constant entity-tag
      // header.
      m_parent.generateFileDownloadReply(req,
                                         filename,
                                         curobj, false);
      return;
    }
    // Ok, it is a directory listing we need then
    FSDir dir(papply(&m_parent, &MyWorker::fetchObject), curobj);

    FSDir::dirent_t f;
    FSDir::dirents_t::iterator f_i = dir.dirents.end();
    FSDir::dirents_t::iterator d_i = dir.dirents.end();
    using namespace xml;
    const IDocument &ddoc
      = mkDoc(Element("directory")
              (*(Element("file")
                 (Element("name")(CharData<std::string>(f.name))
                  & Element("size")(CharData<uint64_t>(f.size)))
                 )[ papply<bool,cSPath,FSDir::dirent_t&,
                    FSDir::dirents_t::iterator&, FSDir::dirents_t&>
                    (this, &cSPath::sendFile, f, f_i, dir.dirents) ]
               & *(Element("directory")
                   (Element("name")(CharData<std::string>(f.name))
                    & Element("treesize")(CharData<uint64_t>(f.size))))
               [ papply<bool,cSPath,FSDir::dirent_t&,
                 FSDir::dirents_t::iterator&, FSDir::dirents_t&>
                 (this, &cSPath::sendDir, f, d_i, dir.dirents) ]));

    m_parent.m_httpd.postReply(req.m_id, ddoc);
    return;
  }
  default:
    m_parent.m_httpd
      .postReply(HTTPReply(req.m_id, true, 405,
                           HTTPHeaders().add("allow", "GET")
                           .add("content-type", "text/plain"),
                           "Methods:\n"
                           " GET: Access shared object\n"));
    return;
  }
}

bool MyWorker::cSPath::sendFile(FSDir::dirent_t &dst,
                                FSDir::dirents_t::iterator &i,
                                FSDir::dirents_t &ents) const
{
  // Initialise or increment
  if (i == ents.end())
    i = ents.begin();
  else
    ++i;

  // Skip non-file entries
  while (i != ents.end()
         && i->type != FSDir::dirent_t::UNIXFILE
         && i->type != FSDir::dirent_t::WINFILE)
    ++i;
  if (i != ents.end())
    dst = *i;
  return i != ents.end();
}

bool MyWorker::cSPath::sendDir(FSDir::dirent_t &dst,
                               FSDir::dirents_t::iterator &i,
                               FSDir::dirents_t &ents) const
{
  // Initialise or increment
  if (i == ents.end())
    i = ents.begin();
  else
    ++i;

  // Skip non-directory entries
  while (i != ents.end()
         && i->type != FSDir::dirent_t::UNIXDIR
         && i->type != FSDir::dirent_t::WINDIR)
    ++i;
  if (i != ents.end())
    dst = *i;
  return i != ents.end();
}
