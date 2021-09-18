///
/// Implementation of the object server abstraction
///
//
/// $Id: mirror.cc,v 1.1 2013/08/29 13:37:12 joe Exp $

#include "mirror.hh"
#include "main.hh"

#include "common/trace.hh"
#include "common/error.hh"

namespace {
  trace::Path t_mirror("/mirror");
}


OSMirror::OSMirror(const SvcConfig &c)
{
  for (std::list<std::pair<std::string,uint16_t> >::const_iterator i
         = c.hOSAPI.m_hosts.begin();
       i != c.hOSAPI.m_hosts.end(); ++i)
    m_mirror.push_back(HTTPclient(i->first, i->second));
}

OSMirror::~OSMirror()
{
}

HTTPReply OSMirror::execute(const HTTPRequest &req)
{
  switch (req.getMethod()) {
  case HTTPRequest::mHEAD:
    return phead(req);
  case HTTPRequest::mGET:
    return pget(req);
  case HTTPRequest::mPOST:
    return ppost(req);
  default:
    throw error("Cannot mirror method");
  }
}

HTTPReply OSMirror::phead(const HTTPRequest &req)
{
  //
  // The proxy, when receiving a HEAD request, must forward the
  // request to both storage servers.
  //
  // We group these requests in three categories, depending on how the
  // request goes:
  //
  //    Confirmation - 204 response received
  //    Denial - 404 response received
  //    Failure - no response or error
  //
  // If any storage server returns Confirmation, that response is
  // forwarded to the user agent.
  //
  // If at least one server returns Denial, that response is forwarded
  // to the user agent (please note; this may cause us to deny the
  // existence of an object that resides on a currently failed storage
  // server, if the server failed too quickly after the write and
  // therefore did not mirror it in time). This is fine though - the
  // HEAD request is intended to be used to detect the presence of an
  // object for the purpose of de-duplication - we respond with Denial
  // in the described situation, all that happens is that we get an
  // extra upload of the object. No harm done.
  //
  // If the two above cases did not match, it must mean that no
  // servers responded or that they all failed. In this case, we
  // respond with a "503 Service Unavailable" error.
  //
  HTTPReply denial;

  for (mirror_t::iterator m = m_mirror.begin(); m != m_mirror.end(); ++m) {
    try {
      // Attempt executing the request
      HTTPRequest fwd(req);
      fwd.m_headers.add("host", m->refHost());

      HTTPReply rep = m->execute(fwd);
      rep.setId(req.getId());
      rep.setFinal(true);
      if (rep.getStatus() == 204) {
        MTrace(t_mirror, trace::Debug, "HEAD 204 from " << m->refHost()
               << ":" << m->getPort() << " - returning");
        return rep;
      } else if (rep.getStatus() == 404) {
        denial = rep;
        MTrace(t_mirror, trace::Debug, "HEAD 404 from " << m->refHost()
               << ":" << m->getPort() << " - continuing");
      } else {
        MTrace(t_mirror, trace::Info, "Forwarded HEAD returned "
               << rep.toString() << " from " << m->refHost()
               << ":" << m->getPort());
      }
    } catch (error &e) {
      // Some error occurred - retry request on next server
      MTrace(t_mirror, trace::Info, "HEAD fwd to " << m->refHost()
             << ":" << m->getPort() << " failed");
    }
  }

  // If we are here, then none of our mirror hosts returned
  // Confirmation. If we got a Denial, report that.
  if (denial.getStatus() == 404)
    return denial;

  // So, no mirror host returned Confirmation and none returned
  // Denial. We have to report to the user that we cannot service this
  // request at this time.
  MTrace(t_mirror, trace::Info, "HEAD got neither Confirmation nor Denial");
  return HTTPReply(req.getId(), true, 503, HTTPHeaders(), std::string());
}

HTTPReply OSMirror::pget(const HTTPRequest &req)
{
  // The proxy, when receiving a GET request, will forward it to one
  // of the storage servers.
  //
  // If we cannot contact the server, we re-try the request against
  // the next server - unless there are no more servers, in which case
  // we return "503 Service Unavailable".
  //
  // If a server responds with "200", that response is sent back to
  // the user agent.
  //
  // If a server responds with 404, we re-try the request against the
  // next server - unless there are no more servers in which case we
  // return "404 Not Found". We re-try on 404 response because this
  // may simply happen when mirroring has fallen behind.
  //
  // In any other case a 503 "service unavailable" error is sent back to the client.
  //
  HTTPReply denial;
  size_t denials = 0;

  for (mirror_t::iterator m = m_mirror.begin(); m != m_mirror.end(); ++m) {
    try {
      // Attempt executing the request
      HTTPRequest fwd(req);
      fwd.m_headers.add("host", m->refHost());

      HTTPReply rep = m->execute(fwd);
      rep.setId(req.getId());
      rep.setFinal(true);
      if (rep.getStatus() == 200) {
        MTrace(t_mirror, trace::Debug, "GET 200 from " << m->refHost()
               << ":" << m->getPort() << " - returning");
        return rep;
      } else if (rep.getStatus() == 404) {
        denial = rep;
        denials ++;
        MTrace(t_mirror, trace::Debug, "GET 404 from " << m->refHost()
               << ":" << m->getPort() << " - continuing");
      } else {
        MTrace(t_mirror, trace::Info, "Forwarded HEAD returned "
               << rep.toString() << " from " << m->refHost()
               << ":" << m->getPort());
      }
    } catch (error &e) {
      // Some error occurred - retry request on next server
      MTrace(t_mirror, trace::Info, "GET fwd to " << m->refHost()
             << ":" << m->getPort() << " failed");
    }
  }

  // Did we get exactly as many denials as we have servers? In that
  // case, return the denial.
  if (denial.getStatus() == 404
      && denials == m_mirror.size()) {
    MTrace(t_mirror, trace::Debug, "All mirrors returned 404 - returning");
    return denial;
  }

  // No server confirmed the object and we did not get unanimous
  // denial, therefore we tell the client to come back later.
  MTrace(t_mirror, trace::Info, "GET could not confirm nor unanimously deny");
  return HTTPReply(req.getId(), true, 503, HTTPHeaders(), std::string());
}

HTTPReply OSMirror::ppost(const HTTPRequest &req)
{
  // The proxy will forward the POST request to one storage
  // server. The storage server will log this object as "original" in
  // its replication do-to log and write the object locally. The
  // replication service on the storage server will then replicate
  // this data block to the other storage server shortly after,
  // asynchronously. If the other storage server is down, that
  // replication can happen much later in time.
  HTTPReply err4xx;

  for (mirror_t::iterator m = m_mirror.begin(); m != m_mirror.end(); ++m) {
    try {
      // Attempt executing the request
      HTTPRequest fwd(req);
      fwd.m_headers.add("host", m->refHost());

      HTTPReply rep = m->execute(fwd);
      rep.setId(req.getId());
      rep.setFinal(true);
      if (rep.getStatus() == 201) {
        MTrace(t_mirror, trace::Debug, "POST 201 from " << m->refHost()
               << ":" << m->getPort() << " - returning");
        return rep;
      } else {
        if (rep.getStatus() >= 400 && rep.getStatus() < 500)
          err4xx = rep;
        MTrace(t_mirror, trace::Info, "Forwarded POST returned "
               << rep.toString() << " from " << m->refHost()
               << ":" << m->getPort());
      }
    } catch (error &e) {
      // Some error occurred - retry request on next server
      MTrace(t_mirror, trace::Info, "POST fwd to " << m->refHost()
             << ":" << m->getPort() << " failed");
    }
  }

  // If we are here, then posting failed. If we received a 4xx error
  // from one of the object servers, relay that back (in lack of a
  // better error). Otherwise, report that service is unavailable.
  if (err4xx.getStatus() >= 400 && err4xx.getStatus() < 500) {
    MTrace(t_mirror, trace::Debug, "POST failed - forwarding 4xx error");
    return err4xx;
  }

  MTrace(t_mirror, trace::Info, "POST failed against all mirrors");
  return HTTPReply(req.getId(), true, 503, HTTPHeaders(), std::string());
}
