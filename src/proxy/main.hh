///
/// Configuration and Worker class definitions for our API front end
/// server
///
//
/// $Id: main.hh,v 1.6 2013/10/07 08:16:32 joe Exp $

#ifndef PROXY_MAIN_HH
#define PROXY_MAIN_HH

#include "credcache.hh"
#include "mirror.hh"
#include "common/trace.hh"
#include "common/optional.hh"
#include "common/thread.hh"
#include "httpd/httpd.hh"
#include "httpd/httpclient.hh"
#include "sql/sql.hh"
#include "objparser/objparser.hh"

//! Trace path for main server operations
extern trace::Path t_api;
//! Trace path for caching operations
extern trace::Path t_cache;

class SvcConfig {
public:
  //! Load and parse configuration from file
  SvcConfig(const char *fname);

  //! Property: port to bind to
  uint16_t bindPort;

  //! Property: number of worker threads
  size_t workerThreads;

  //! Each mirror is a number of hosts each with name and port
  struct cOSAPI {
    cOSAPI() : tmp_port(-1) { }
    std::string tmp_name;
    uint16_t tmp_port;

    /// Call to add tmp to list - always returns true
    bool add();

    std::list<std::pair<std::string,uint16_t> > m_hosts;
  } hOSAPI;

  //! Property: database connection string
  std::string connString;

  //! Property: HTTP authentication realm
  std::string authRealm;

  //! Property: static file root
  std::string docRoot;

  //! Index file if "/" is requested
  std::string docIndex;

  //! If SSL is enabled, the certificate file
  Optional<std::string> ssl_certfile;

  //! If SSL is enabled, the key file
  Optional<std::string> ssl_keyfile;

  //! Credentials cache entry TTL
  DiffTime cacheTTL;

  //! Handler for mime types
  struct cMime {
    /// For parsing - load temporary extension and mimetype into here
    std::string tmp_ext;
    std::string tmp_mime;

    /// Call to add tmp into map - always returns true
    bool add();

    /// Map from extension into mime type
    std::map<std::string,std::string> m_map;
  } hMime;

  //! Default mime type
  Optional<std::string> defaultMimeType;

  //! Special 404 error page for public shares
  std::string docShare404;
};


class MyWorker : public Thread {
public:
  MyWorker(HTTPd &httpd, const SvcConfig &cfg, CredCache &cc);
  MyWorker(const MyWorker &);
  ~MyWorker();
protected:
  //! Our actual worker thread
  void run();
private:
  //! Protect against assignment
  MyWorker &operator=(const MyWorker&);

  //! Reference to the httpd (needed for getting requests and posting
  //! replies)
  HTTPd &m_httpd;

  //! Reference to our service configuration
  const SvcConfig &m_cfg;

  //! Credentials cache
  CredCache &m_cc;

  //! Our OS/API connection handler
  OSMirror m_osapi;

  //! Our database connection
  sql::Connection m_db;

  //! Actually process a request
  void processRequest(HTTPRequest &req);

  //! Authentication mode
  enum reqmode_t {
    NORMAL,  // Normal user authentication. Impersonation only allowed
             // if authenticated user is a system user
    REQSYS   // Require system user, optionally allow impersonation
  };
  //! Authenticate connection - read authorization header and check up
  //! against user database. Returns true if all is ok, to allow
  //! further processing by the caller. In case authorization does not
  //! check out, the function will post a 401 error and return false.
  //
  //! The tt argument is a bitwise OR of the allowed Access Token
  //! types (defined in the AT_x constants below). If the
  //! authenticated access token is not of one of the allowed types,
  //! authentication also fails.
  bool authenticate(const HTTPRequest &req, size_t tt, reqmode_t rm = NORMAL);

  //! Process request as a local file request. Returns true on
  //! success, false if nothing was processed.
  bool processLocal(HTTPRequest &req);

  //! This method posts back a response with a given status code and
  //! file data from the given file name under the document root.
  bool postFileReply(const HTTPRequest &req, short status, const std::string &file);

  //! Return mime type for file (based on file extension)
  std::string mimeTypeFromExt(const std::string &name) const;

  //! Post a 200 OK reply with mime type and optionally a content
  //! disposition set to 'attachment' suggesting the given file name,
  //! and send the file data from the given objseq.
  //
  //! Some URIs contain the request object hash in them, or the hash
  //! of a parent object. Such URIs can never return anything else
  //! than the data they return now (because if the data changes the
  //! hash changes - both in the object and its parents).
  void generateFileDownloadReply(const HTTPRequest &req,
                                 const std::string &filename,
                                 const objseq_t &filedata,
                                 bool data_constant_uri);

  //! Download a given object from the object store. This routine will
  //! retry a limited number of times to allow us some resilience to
  //! object server restarts.
  std::vector<uint8_t> fetchObject(const sha256&);

  //! Compute which response code to use for an authentication
  //! failure. According to rfc2616 the only right answer is code 401
  //! (Authentication Failed) - but browsers intercept this error (and
  //! only this error) and display their hideous authentication
  //! pop-up. Therefore an AJAX GUI may wish to request an alternative
  //! error code for failed authentication, now that the browsers make
  //! it impossible to use the code mandated by the standard.
  short authFailCode(const HTTPRequest &) const;


  //! Before calling processRequest, the user account and password
  //! supplied in the authentication header is validated. If the
  //! request is authentic, the account_id and used access token id is
  //! stored in these two variables.
  uint64_t m_account_id;
  uint64_t m_access_id;

  //! Authenticated account access type
  CredCache::access_type_t m_access_type;

  //! This method returns true if the authenticated access token is of
  //! one of the types required in the argument (bitwise OR of AT_x
  //! types above). It returns false if it has posted a 403 Forbidden.
  //
  //! This method is called internally by authenticate() and should
  //! not be needed outside of that.
  bool requireAccessType(const HTTPRequest &, size_t);

  //! This method is to be used see if an "if-match" or
  //! "if-none-match" header is present, and then either post a "412
  //! precondition failed" or "304 not modified" status and return
  //! true, or, post nothing and return false (to allow normal
  //! processing of the request).
  //
  //! This simple method is sufficient on requests on our objects
  //! because a URI that refers to an object contains the object
  //! checksum in it, or the checksum of a parent object. Therefore, a
  //! given URI can never return other data. We therefore always
  //! return an entity-tag of "0", and we always match against that
  //! tag.
  bool cacheBypassRequest(const HTTPRequest&);

  //! This method is used to optionally add our constant entity-tag on
  //! a reply. It will add the entity-tag if the request was a GET and
  //! the reply is successful (200)
  void cacheTagReply(const HTTPRequest&, HTTPReply&);

  //! This method will follow the parent column in the User table to
  //! determine whether one account is an ancestor of another
  //! account. The method returns true if \param ancestor is an
  //! ancestor of \param descendant.
  bool isAncestorOf(uint64_t ancestor, uint64_t descendant);

  //! When accessing /object/{objectid} this stores the objectid
  std::string m_objectid;

  //! Endpoint handler for /object/{objectid}
  class cObject : public Endpoint {
  public:
    cObject(MyWorker &p) : m_parent(p) { }
    void handle(const HTTPRequest &) const;
  private:
    MyWorker &m_parent;
  } hObject;

  //! Endpoint handler for /tokens/
  class cTokens : public Endpoint {
  public:
    cTokens(MyWorker &p) : m_parent(p) { }
    void handle(const HTTPRequest &) const;
  private:
    MyWorker &m_parent;
  } hTokens;

  //! Duuring URI parsing we may read a token {aname} - this is kept
  //! here
  std::string m_token_aname;

  //! Endpoint handler for /tokens/{aname}
  class cToken : public Endpoint {
  public:
    cToken(MyWorker &p) : m_parent(p) { }
    void handle(const HTTPRequest &) const;
  private:
    MyWorker &m_parent;
  } hToken;

  //! Endpoint handler for /users/{user-id}/devices/
  class cDevices : public Endpoint {
  public:
    cDevices(MyWorker &p) : m_parent(p) { }
    void handle(const HTTPRequest &) const;
  private:
    MyWorker &m_parent;
  } hDevices;

  //! During URI parsing we may read a {device-id} - that will be kept
  //! here
  std::string m_device_id;

  //! Endpoint handler for /users/{user-id}/devices/{device-id}
  class cDevice : public Endpoint {
  public:
    cDevice(MyWorker &p) : m_parent(p) { }
    void handle(const HTTPRequest &) const;
  private:
    MyWorker &m_parent;
  } hDevice;

  //! Endpoint handler for
  //! /users/{user-id}/devices/{device-id}/history
  class cHistory : public Endpoint {
  public:
    cHistory(MyWorker &p) : m_parent(p) { }
    void handle(const HTTPRequest &) const;
  private:
    MyWorker &m_parent;
  } hHistory;

  //! Endpoint handler for /users/{user-id}/devices/{device-id}/status
  class cDevStatus : public Endpoint {
  public:
    cDevStatus(MyWorker &p) : m_parent(p) { }
    void handle(const HTTPRequest &) const;
  private:
    MyWorker &m_parent;
  } hDevStatus;

  //! During URI parsing we may read a {attr} - that will be kept here
  std::string m_attributename;

  //! Endpoint handler for
  //! /users/{user-id}/devices/{device-id}/attributes/{attr}
  class cDevAttr : public Endpoint {
  public:
    cDevAttr(MyWorker &p) : m_parent(p) { }
    void handle(const HTTPRequest &) const;
  private:
    MyWorker &m_parent;
  } hDevAttr;

  //! Endpoint handler for /users/{user-id}/devices/{device-id}/auth_code
  class cDevAuthCode : public Endpoint {
  public:
    cDevAuthCode(MyWorker &p) : m_parent(p) { }
    void handle(const HTTPRequest &) const;
  private:
    MyWorker &m_parent;
  } hDevAuthCode;

  //! Endpoint handler for /users
  class cUsers : public Endpoint {
  public:
    cUsers(MyWorker &p) : m_parent(p) { }
    void handle(const HTTPRequest &) const;
  private:
    MyWorker &m_parent;
  } hUsers;

  //! During URI parsing we may read a userid
  std::string m_userid;

  //! This method will validate whether the authenticated client is an
  //! ancestor (or equal) to the m_userid parsed. It will return true
  //! in that case. It will post an error message and return false
  //! otherwise.
  bool getUserPathId(uint64_t req_id, uint64_t &ownerid);

  //! Endpoint handler for /users/{user-id}
  class cUser : public Endpoint {
  public:
    cUser(MyWorker &p) : m_parent(p) { }
    void handle(const HTTPRequest &) const;
  private:
    MyWorker &m_parent;
  } hUser;

  //! Endpoint handler for /users/{user-id}/resources
  class cResources : public Endpoint {
  public:
    cResources(MyWorker &p) : m_parent(p) { }
    void handle(const HTTPRequest &) const;
  private:
    MyWorker &m_parent;
  } hResources;

  //! Endpoint handler for /users/{user-id}/contacts
  class cContacts : public Endpoint {
  public:
    cContacts(MyWorker &p) : m_parent(p) { }
    void handle(const HTTPRequest &) const;
  private:
    MyWorker &m_parent;
  } hContacts;

  //! During URI parsing we may need a contact type
  std::string m_contacttype;

  //! Endpoint handler for /users/{user-id}/contacts/{contact-type}
  class cContact : public Endpoint {
  public:
    cContact(MyWorker &p) : m_parent(p) { }
    void handle(const HTTPRequest &) const;
  private:
    MyWorker &m_parent;
  } hContact;

  //! Endpoint handler for /users/{user-id}/users
  class cSubUsers : public Endpoint {
  public:
    cSubUsers(MyWorker &p) : m_parent(p) { }
    void handle(const HTTPRequest &) const;
  private:
    MyWorker &m_parent;
  } hSubUsers;

  //! When we do specific dirhash/file downloads, we store the
  //! directory hash and file name here
  std::string m_downdir;
  std::string m_downfile;

  class cDownFile : public Endpoint {
  public:
    cDownFile(MyWorker &p) : m_parent(p) { }
    void handle(const HTTPRequest &) const;
  private:
    MyWorker &m_parent;
  } hDownFile;

  //! For queue management, the name of the queue we operate on
  std::string m_queuename;

  //! Queue endpoint handler
  class cQueue : public Endpoint {
  public:
    cQueue(MyWorker &p) : m_parent(p) { }
    void handle(const HTTPRequest &) const;
  private:
    MyWorker &m_parent;
  } hQueue;

  //! For queued event management, the name of the event we operate on
  std::string m_eventid;

  //! Queued event endpoint handler
  class cQueueEvent : public Endpoint {
  public:
    cQueueEvent(MyWorker &p) : m_parent(p) { }
    void handle(const HTTPRequest &) const;
  private:
    MyWorker &m_parent;
  } hQueueEvent;

  //! Status endpoint handler
  class cStatus : public Endpoint {
  public:
    cStatus(MyWorker &p) : m_parent(p) { }
    void handle(const HTTPRequest &) const;
  private:
    MyWorker &m_parent;

    //! Handler for mirror /status requests
    struct cm {
      /// init
      cm(const cStatus &);
      /// get next mirror - returns true while successful
      bool getNext();
      /// reference to cStatus parent
      const cStatus &m_parent;
      /// current mirror config
      std::list<std::pair<std::string,uint16_t> >::const_iterator m_curr;
      /// Current host
      std::string host;
      /// Current port
      uint16_t port;
      /// current mirror /status output document
      std::string m_status;
    };

  } hStatus;

  //! Favourites endpoint handler
  class cFavourites : public Endpoint {
  public:
    cFavourites(MyWorker &p) : m_parent(p) { }
    void handle(const HTTPRequest &) const;
  private:
    MyWorker &m_parent;
    // parse a full (host+path) path - throw on error
    void splitPath(std::string &dev, std::string &partial,
                   const std::string &full) const;
  } hFavourites;

  //! Specific favourite endpoint handler
  class cFavourite : public Endpoint {
  public:
    cFavourite(MyWorker &p) : m_parent(p) { }
    void handle(const HTTPRequest &) const;
  private:
    MyWorker &m_parent;
  } hFavourite;

  //! If dealing with a specific favourite, this is the one
  std::string m_favourite;

  //! Shares endpoint handler
  class cShares : public Endpoint {
  public:
    cShares(MyWorker &p) : m_parent(p) { }
    void handle(const HTTPRequest &) const;
  private:
    MyWorker &m_parent;
  } hShares;

  //! Specific share endpoint handler
  class cShare : public Endpoint {
  public:
    cShare(MyWorker &p) : m_parent(p) { }
    void handle(const HTTPRequest &) const;
  private:
    MyWorker &m_parent;
  } hShare;

  //! If dealing with a specific share, this is the one
  std::string m_share;

  //! Share sub-path endpoint handler
  class cSPath : public Endpoint {
  public:
    cSPath(MyWorker &p) : m_parent(p) { }
    void handle(const HTTPRequest &) const;
  private:
    MyWorker &m_parent;
    bool sendFile(FSDir::dirent_t&, FSDir::dirents_t::iterator&,
                  FSDir::dirents_t&) const;
    bool sendDir(FSDir::dirent_t&, FSDir::dirents_t::iterator&,
                 FSDir::dirents_t&) const;
  } hSPath;

  //! If dealing with a sub-path under a share, this is the one
  std::string m_spath;

};


#endif
