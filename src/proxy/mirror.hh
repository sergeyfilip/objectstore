///
/// Simple mirrored object server abstraction
///
//
/// $Id: mirror.hh,v 1.1 2013/08/29 13:37:12 joe Exp $
//

#ifndef PROXY_MIRROR_HH
#define PROXY_MIRROR_HH

#include "httpd/httpclient.hh"
#include <list>

class SvcConfig;

/// The OSMirror class will execute HEAD, GET and POST requests
/// against the storage servers and post the appropriate response
//
/// Depending on the type of request, it must be forwarded to the
/// storage servers differently.
//
class OSMirror {
public:
  OSMirror(const SvcConfig &);
  ~OSMirror();

  /// Execute request and post back appropriate response
  //
  /// The HTTPRequest must not have its "host" header set, as this
  /// will be set when the request is forwarded to a specific host.
  HTTPReply execute(const HTTPRequest &);

private:
  /// Each mirror
  typedef std::list<HTTPclient> mirror_t;
  mirror_t m_mirror;

  /// HEAD processing
  HTTPReply phead(const HTTPRequest &);
  /// GET processing
  HTTPReply pget(const HTTPRequest &);
  /// POST processing
  HTTPReply ppost(const HTTPRequest &);

};

#endif
