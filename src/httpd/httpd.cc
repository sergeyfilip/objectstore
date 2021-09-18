//
//! \file httpd/httpd.cc
//! Implementation of our basic HTTP 1.1 server
//
// $Id: httpd.cc,v 1.14 2013/08/26 08:44:12 joe Exp $
//

#include "httpd.hh"
#include "common/error.hh"
#include "common/trace.hh"

#if defined(__unix__) || defined(__APPLE__)
# include <openssl/err.h>
# include "httpd_posix.cc"
#endif

namespace {
  //! Trace path for common HTTPd code
  trace::Path t_http("/HTTPd/common");

  //! Trace path for request logging
  trace::Path t_req("/HTTPd/request");
}

HTTPd::HTTPd()
  : m_exiting(false)
  , m_handler(*this)
  , m_ctx(0)
  , m_max_connections(-1) // practically unlimited
{
#if defined(__unix__) || defined(__APPLE__)
  int rc = pipe(m_wakepipe);
  if (rc < 0)
    throw syserror("pipe", "creation of wake pipe");
#endif
  m_handler.start();
}

HTTPd::~HTTPd()
{
  // Clear context, if any
  if (m_ctx)
    SSL_CTX_free(m_ctx);

  // Close our listeners and stop the handling thread, if not done
  // already.
  if (!m_exiting)
    stop();

  // Destroy the wake pipe
  close(m_wakepipe[0]);
  close(m_wakepipe[1]);
}

HTTPd &HTTPd::setMaxConnections(size_t conns)
{
  m_max_connections = conns;
  return *this;
}

HTTPRequest HTTPd::getRequest()
{
  // First, wait until we can grab a request
  m_requests_sem.decrement();

  // If we have been shut down, stop processing actual requests and
  // instead return stop events to the workers
  { MutexLock lock(m_conf_mutex);
    if (m_listeners.empty()) {
      MTrace(t_http, trace::Debug, "Returning stop event");
      m_requests_sem.increment(); // keep doing this - we may have
                                  // many workers
      return HTTPRequest();
    }
  }

  HTTPRequest ret;

  // Now, dequeue a request holding the lock
  { MutexLock lock(m_requests_mutex);
    MAssert(!m_requests.empty(),
            "No requests after decrementing request semaphore");
    ret = m_requests.back();
    m_requests.pop_back();
    m_outstanding.insert(std::make_pair(ret.getId(), ret.toString()));
  }

  // If this request has "connection: close" set, don't expect to read
  // any more from that client
  if (ret.hasHeader("connection")
      && ret.getHeader("connection") == "close") {
    m_handler.setNonpersistent(ret.getId());
  }

  return ret;
}

void HTTPd::postReply(const HTTPReply &response)
{
  m_handler.postReply(response);
  // Clear from set of outstanding requests if the response is final
  if (response.isFinal()) {
    MutexLock lock(m_requests_mutex);
    m_outstanding.erase(response.getId());
  }
}

void HTTPd::postReply(uint64_t req_id, const xml::IDocument &doc)
{
  using namespace xml;

  std::ostringstream out;
  XMLWriter writer(out);
  doc.output(writer);

  postReply(HTTPReply(req_id, true, 200,
                      HTTPHeaders().add("content-type", "application/xml")
                      .add("cache-control", "no-cache"),
                      out.str()));
}

size_t HTTPd::getQueueLength()
{
  MutexLock lock(m_requests_mutex);
  return m_requests.size();
}

bool HTTPd::outstanding(uint64_t id)
{
  MutexLock lock(m_requests_mutex);
  return m_outstanding.count(id);
}

bool HTTPd::outqueued(uint64_t id)
{
  return m_handler.outqueued(id);
}

bool HTTPd::HandlerThread::outqueued(uint64_t id)
{
  MutexLock lock(m_id_procs_mutex);
  // Locate the processor that is treating this request id. If we
  // cannot find it, it is empty
  m_id_procs_t::iterator proc = m_id_procs.find(id);
  if (proc == m_id_procs.end())
    return false;
  // Ask processor if anything is queued
  return proc->second->outqueued();
}

void HTTPd::HandlerThread::setNonpersistent(uint64_t id)
{
  MutexLock lock(m_id_procs_mutex);
  m_id_procs_t::iterator proc = m_id_procs.find(id);
  if (proc != m_id_procs.end())
    proc->second->closeAfter(id);
}

void HTTPd::pushRequest(const HTTPRequest &req)
{
  MTrace(t_http, trace::Debug, "HTTPd pushing request id "
         << req.getId());
  // Queue the request holding the lock
  { MutexLock lock(m_requests_mutex);
    m_requests.push_front(req);
  }
  // Notify those who wait
  m_requests_sem.increment();
}

SSL_CTX *HTTPd::getCTX() const
{
  return m_ctx;
}

void HTTPd::restartPoll()
{
  uint8_t tmp(42);
  int rc;
  do {
    rc = write(m_wakepipe[1], &tmp, sizeof tmp);
  } while (rc == -1 && errno == EINTR);
  if (rc != sizeof tmp)
    throw syserror("write", "waking HTTPd poll");
}


HTTPd::HandlerThread::HandlerThread(HTTPd &parent)
  : m_parent(parent)
  , m_request_id(0)
{
}

uint64_t HTTPd::HandlerThread::nextId()
{
  MutexLock lock(m_request_id_mutex);
  return m_request_id++;
}

SSL_CTX *HTTPd::HandlerThread::getCTX() const
{
  return m_parent.getCTX();
}

void HTTPd::HandlerThread::postReply(const HTTPReply &response)
{
  MTrace(t_http, trace::Debug, "Handler got posted reply for id "
         << response.getId() << " - final=" << response.isFinal());

  // Log, if this is the status-code-containing first response to a
  // given outstanding request
  if (response.isInitial()) {
    MutexLock l(m_parent.m_requests_mutex);
    MTrace(t_req, trace::Info,
           m_parent.m_outstanding[response.getId()] << " " << response.getStatus());
  }

  MutexLock lock(m_id_procs_mutex);
  // Locate the processor that is treating this request id. If we
  // cannot find it, the connection must have closed while we were
  // processing...
  m_id_procs_t::iterator proc = m_id_procs.find(response.getId());
  if (proc == m_id_procs.end()) {
    MTrace(t_http, trace::Debug, "Discarding response because there "
           "is no longer any processor for request id "
           << response.getId());
    return;
  }

  proc->second->postReply(response);

  // If this response was a final response, we can remove the
  // request-id-to-processor mapping
  if (response.isFinal())
    m_id_procs.erase(proc);
}

void HTTPd::HandlerThread::pushRequest(const HTTPRequest &req, Processor *proc)
{
  MTrace(t_http, trace::Debug, "Handler thread pushing request id "
         << req.getId());

  // Now associate id with processor
  MutexLock lock(m_id_procs_mutex);
  m_id_procs[req.getId()] = proc;

  // Tell the processor this request is now active
  proc->requestActivated(req.getId());

  // Tell the HTTPd about this
  m_parent.pushRequest(req);
}


void HTTPd::HandlerThread::Processor::processInbound()
{
  //
  // We must continue to consume data until we meet 'CR-LF-CR-LF' in
  // the buffer. Once we see that, we must stop consumption and parse
  // the request line and the headers from what we have read so far.
  //
  // Only then can we know, whether any additional data is the request
  // body, or a new (pipelined) requested.
  //
  if (m_rawstate == R_ReadingRequest) {
    // Now we must search for CRLFCRLF to see if we have a full
    // request
    const size_t endreqpos = m_inbound.find("\r\n\r\n");

    // If we do not have a full request, continue reading...
    if (endreqpos == m_inbound.npos) {
      MTrace(t_http, trace::Debug, "No full request yet. Waiting.");
      return;
    }

    MTrace(t_http, trace::Debug, "We have a full request - will process");

    // We have our request string. Start building a request - and
    // allocate it a request id
    m_request = HTTPRequest(m_handler.nextId(), m_inbound.substr(0, endreqpos + 4));
    m_inbound.erase(0, endreqpos + 4);

    // If the request has the content-length or transfer-encoding
    // headers set, we must read the body of the request
    //
    // This is stated in rfc2616 section 4.3:
    // -------------------------------------------
    // The presence of a message-body in a request is signaled by the
    // inclusion of a Content-Length or Transfer-Encoding header field
    // in the request's message-headers. A message-body MUST NOT be
    // included in a request if the specification of the request
    // method (section 5.1.1) does not allow sending an entity-body in
    // requests.
    // -------------------------------------------
    //
    // Further, it states in section 4.4-2:
    // -------------------------------------------
    // If a Transfer-Encoding header field (section 14.41) is present
    // and has any value other than "identity", then the
    // transfer-length is defined by use of the "chunked"
    // transfer-coding (section 3.6), unless the message is terminated
    // by closing the connection.
    // -------------------------------------------
    //
    // Further, it states in section 4.4-3:
    // -------------------------------------------
    // If a message is received with both a Transfer-Encoding header
    // field and a Content-Length header field, the latter MUST be
    // ignored.
    // -------------------------------------------
    //
    if (m_request.hasHeader("transfer-encoding")
        && m_request.getHeader("transfer-encoding") != "identity") {
      // Fine, set up for chunked reading then
      m_rawstate = R_ReadingBodyTE;
      m_bodyleft = 0; // read chunk header
      MTrace(t_http, trace::Debug, "Reading transfer-encoding body");
    } else if (m_request.hasHeader("content-length")) {
      m_rawstate = R_ReadingBodyCL;
      { std::istringstream decconv(m_request.getHeader("content-length"));
        decconv >> std::dec >> m_bodyleft;
        if (decconv.fail())
          throw error("Invalid content-length - cannot parse");
      }
      MTrace(t_http, trace::Debug, "Reading content-length="
             << m_bodyleft << " body");
    } else if (m_request.hasHeader("transfer-encoding")) {
      throw error("No content-length given for "
                  "identity transfer-encoding");
    } else {
      MTrace(t_http, trace::Debug, "Request without body - complete.");
      // Nothing.
      m_rawstate = R_ReadingRequest;
      m_handler.pushRequest(m_request, this);
      m_request = HTTPRequest();
    }
  }

  //
  // If we need to read the body from a content-length definition,
  // simply read bytes until we have what we need
  //
  if (m_rawstate == R_ReadingBodyCL) {
    const size_t to_read = std::min(m_inbound.size(), m_bodyleft);
    m_request.addBody(m_inbound.substr(0, to_read));
    m_bodyleft -= to_read;
    m_inbound.erase(0, to_read);

    // Are we done yet?
    if (!m_bodyleft) {
      MTrace(t_http, trace::Debug, "Completed content-length body read");
      m_rawstate = R_ReadingRequest;
      m_handler.pushRequest(m_request, this);
      m_request = HTTPRequest();
    }
  }

  //
  // If we are reading the body from a chunked transfer-encoding then
  // we either need to read a chunk header or chunk data.
  //
  if (m_rawstate == R_ReadingBodyTE) {
    //
    // If we have a current chunk, read more from that
    //
    if (m_bodyleft) {
      const size_t to_read = std::min(m_inbound.size(), m_bodyleft);
      m_request.addBody(m_inbound.substr(0, to_read));
      m_bodyleft -= to_read;
      m_inbound.erase(0, to_read);
    }
    //
    // If we do not have a current chunk, make sure we get one... Read
    // the chunk header.
    //
    if (!m_bodyleft) {
      // See if we have a chunk header (ends on \r\n).
      const size_t crlfpos = m_inbound.find("\r\n");
      if (crlfpos == m_inbound.npos) {
        // No, better luck next time
      } else {
        std::string chunkhead = m_inbound.substr(0, crlfpos);
        m_inbound.erase(0, crlfpos + 2);
        // If there are chunk extensions, skip them.  We thus ignore
        // all chunk extensions in accordance with rfc 2616 section
        // 3.6.1.
        const size_t ext_separator_pos = chunkhead.find(';');
        if (ext_separator_pos != chunkhead.npos)
          chunkhead.erase(ext_separator_pos);
        // Now parse the size of the chunk - the size can be followed
        // by any number of chunk-extensions, but these are ignored by
        // the hex number parser.
        { std::istringstream hexconv(chunkhead);
          hexconv >> std::hex >> m_bodyleft;
          if (hexconv.fail())
            throw error("Bad chunk header - cannot parse");
        }
        // If the size of the chunk is 0, this is the end of the body!
        if (!m_bodyleft) {
          MTrace(t_http, trace::Debug, "Completed chunked body read");
          m_rawstate = R_ReadingRequest;
          m_handler.pushRequest(m_request, this);
          m_request = HTTPRequest();
        }
      }
    }
  }
}
