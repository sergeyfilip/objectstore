//
//! \file httpd/processor.cc
//! Implementation of the connection processor
//
// $Id: processor.cc,v 1.17 2013/05/10 09:32:48 joe Exp $

#include "httpd.hh"

#include "common/trace.hh"
#include "common/scopeguard.hh"

#if defined(__unix__) || defined(__APPLE__)
# include <sys/types.h>
# include <sys/socket.h>
# include <signal.h>
# include <errno.h>
# include <openssl/err.h>
#endif

namespace {
  //! Trace path for processor operations
  trace::Path t_proc("/HTTPd/Processor");
  //! Specific trace for SSL related routines
  trace::Path t_ssl("/HTTPd/SSL");
}

HTTPd::HandlerThread::Processor::Processor(HandlerThread &handler)
  : m_handler(handler)
  , m_readmore(true)
  , m_rawstate(R_ReadingRequest)
  , m_bodyleft(0)
  , m_ssl(0)
  , m_ssl_accepting(m_ssl)
  , m_ssl_needs_write(false)
  , m_ssl_needs_read(false)
  , m_ssl_in_shutdown(false)
  , m_close_after_id(-1)
{
}

HTTPd::HandlerThread::Processor::Processor(const Processor &o)
  : m_handler(o.m_handler)
  , m_readmore(o.m_readmore)
  , m_rawstate(o.m_rawstate)
  , m_bodyleft(o.m_bodyleft)
  , m_ssl(0)
  , m_ssl_accepting(m_ssl)
  , m_ssl_needs_write(false)
  , m_ssl_needs_read(false)
  , m_ssl_in_shutdown(false)
  , m_close_after_id(-1)
{
  if (o.m_ssl)
    throw error("Cannot copy construct a processor with SSL state");
}

HTTPd::HandlerThread::Processor::~Processor()
{
  if (m_ssl)
    SSL_free(m_ssl);
}

void HTTPd::HandlerThread::Processor::setFD(int fd)
{
  // If SSL is enabled, clone the master context
  if (m_handler.getCTX()) {
    m_ssl_accepting = true;
    m_ssl = SSL_new(m_handler.getCTX());
    if (!m_ssl)
      throw error("SSL state allocation failed");
    // Associate file descriptor with SSL state
    if (!SSL_set_fd(m_ssl, fd))
      throw error(ERR_error_string(ERR_get_error(), 0));
    MTrace(t_proc, trace::Debug, "Set up SSL state with fd");
    // Set auto retry
    SSL_set_mode(m_ssl, SSL_MODE_AUTO_RETRY);
    // Accept processing will start on first read(). Peer will send to
    // us to initiate handshake.
  }
}

void HTTPd::HandlerThread::Processor::closeAfter(uint64_t id)
{
  // We will not override the "connection: close" request id -
  // however, it would be really weird for a client to pipeline two
  // requests where both had a close header...
  if (m_close_after_id != size_t(-1)) {
    MTrace(t_proc, trace::Info, "Client pipelined multiple non-persistent "
           "requests - ignoring");
    return;
  }
  // Fine, set id
  m_close_after_id = id;
}

bool HTTPd::HandlerThread::Processor::shouldClose() const
{
  MutexLock lock(m_outbound_mutex);
  // We are done when
  // 1) We do not expect to get more requests
  // 2) We have no more outstanding reply data to send
  // 3) There are no received requests for which we have not
  //    serialized a final reply
  return !m_readmore && m_outbound.empty() && m_req_wo_final.empty()
    && !m_ssl_in_shutdown;
}

void HTTPd::HandlerThread::Processor::setClose()
{
  m_readmore = false;

  // If we are running with SSL and we have nothing more to write,
  // start the shutdown
  if (m_ssl) {
    MTrace(t_ssl, trace::Debug, "Set shutdown in setClose");
    m_ssl_in_shutdown = true;
  }
}

bool HTTPd::HandlerThread::Processor::shouldRead() const
{
  return (!m_ssl_accepting && m_readmore) || (m_ssl && m_ssl_needs_read)
    || (m_ssl && m_ssl_accepting && !m_ssl_needs_read && !m_ssl_needs_write);
}

bool HTTPd::HandlerThread::Processor::shouldWrite() const
{
  MutexLock lock(m_outbound_mutex);
  return !m_outbound.empty() || (m_ssl && m_ssl_needs_write)
    || (m_ssl && m_ssl_in_shutdown && !m_ssl_needs_read && !m_ssl_needs_write);
}

bool HTTPd::HandlerThread::Processor::outqueued() const
{
  MutexLock lock(m_outbound_mutex);
  return !m_outbound.empty();
}

void HTTPd::HandlerThread::Processor::requestActivated(uint64_t id)
{
  MutexLock lock(m_outbound_mutex);
  m_req_wo_final.push_back(id);
}

void HTTPd::HandlerThread::Processor::postReply(const HTTPReply &response)
{
  MTrace(t_proc, trace::Debug, "Processor got posted reply (final="
         << response.isFinal() << ") for id "
         << response.getId());

  { // Note; we are called from a worker thread.
    MutexLock lock(m_outbound_mutex);

    // If this response is a 5xx series error, we close the connection.
    if (response.getStatus() >= 500) {
      MTrace(t_proc, trace::Info, "Processor will close connection after "
             "sending reply, due to error");
      setClose();
    }

    // If this is a final response and we have been asked to close the
    // connection after this request id, start closing now then.
    if (m_close_after_id == response.getId()
        && response.isFinal()) {
      MTrace(t_proc, trace::Debug, "Processor will close connection "
             "because final reply to non-persistent connection request "
             "was posted");
      setClose();
    }

    // Insert the reply properly into the sequence of replies to that
    // peer
    { std::list<HTTPReply>::iterator ipos = m_outqueue.begin();
      while (ipos != m_outqueue.end()
             && ipos->getId() <= response.getId())
        ++ipos;
      m_outqueue.insert(ipos, response);
    }
    // Serialize replies in the reply sequence as long as the lowest
    // numbered reply matches the lowest numbered outstanding request
    // (this is necessary for correct HTTP 1.1 pipe-lining)
    while (!m_outqueue.empty()
           && !m_req_wo_final.empty()
           && m_outqueue.front().getId() == m_req_wo_final.front()) {
      // Serialise request
      m_outqueue.front().serialize(m_outbound);

      // If this is final, remove the id from the req_wo_final structure
      if (m_outqueue.front().isFinal()) {
        MTrace(t_proc, trace::Debug, " - processor got final response to id "
               << response.getId());
        m_req_wo_final.pop_front();
      }

      // Remove this request from our out queue now that it is
      // serialised
      m_outqueue.pop_front();
    }

    // Sanity check; if we have no requests without final, but we have
    // stuff in the outqueue, our logic is broken
    if (m_req_wo_final.empty())
      MAssert(m_outqueue.empty(),
              "Processor has no active requests, but data in out queue when posting "
              + response.toString());
  }

  // Notify the poll loop that stuff changed
  MTrace(t_proc, trace::Debug, " - notifying poll loop");
  m_handler.m_parent.restartPoll();
}


void HTTPd::HandlerThread::Processor
::getActiveRequestIds(std::deque<uint64_t> &active)
{
  MutexLock lock(m_outbound_mutex);
  active = m_req_wo_final;
}


std::string HTTPd::HandlerThread::Processor::toString() const
{
  std::ostringstream s;
  if (m_ssl) {
    s << "SSL(nW=" << m_ssl_needs_write << ",nR="
      << m_ssl_needs_read << ",acc=" << m_ssl_accepting << ") ";
  }
  s << "sW=" << shouldWrite() << ",sR=" << shouldRead()
    << " outbuf=" << m_outbound.size()
    << " outq=" << m_outqueue.size();
  return s.str();
}

void HTTPd::HandlerThread::Processor::outboundConsumed(size_t s)
{
  MAssert(!m_outbound.empty() || !s,
          "Consumed something when we had nothing");
  // If we sent the whole buffer, just remove it
  if (s == m_outbound.front().size()) {
    m_outbound.pop_front();
  } else {
    MAssert(s < m_outbound.front().size(),
            "Consumed " << s << " with " << m_outbound.front().size()
            << " in outbound");
    // No, slow path - erase inside vector
    m_outbound.front().erase(m_outbound.front().begin(),
                             m_outbound.front().begin() + s);
  }
}

const uint8_t *HTTPd::HandlerThread::Processor::outboundNextData() const
{
  return !m_outbound.empty() && !m_outbound.front().empty()
    ? &m_outbound.front()[0]
    : 0;
}

size_t HTTPd::HandlerThread::Processor::outboundNextSize() const
{
  return !m_outbound.empty() && !m_outbound.front().empty()
    ? m_outbound.front().size()
    : 0;
}

void HTTPd::HandlerThread::Processor::processSSLAccept()
{
  //
  // If we are accepting, continue with accept handshake. Otherwise
  // get out.
  //
  if (!m_ssl_accepting)
    return;

  // Reset need state - we will build a new one
  m_ssl_needs_write = false;
  m_ssl_needs_read = false;
  // Attempt accept
  int res = SSL_accept(m_ssl);
  int err = SSL_get_error(m_ssl, res);
  if (res < 0) {
    // If we simply blocked on a read or write problem, deal with
    // it.
    if (err == SSL_ERROR_WANT_WRITE) {
      m_ssl_needs_write = true;
      return;
    }
    if (err == SSL_ERROR_WANT_READ) {
      m_ssl_needs_read = true;
      return;
    }
    // Some other error...
    m_readmore = false;
    m_ssl_accepting = false;
    throw error("SSL accept sequence failed: "
                + std::string(ERR_error_string(ERR_get_error(), 0)));
  } if (res == 0) {
    m_readmore = false;
    m_ssl_accepting = false;
    throw error("Unsuccessful SSL accept: " + std::string(ERR_error_string(err, 0)));
  } else {
    // We're done! Accept sequence complete.
    MTrace(t_proc, trace::Debug, "SSL accept sequence complete");
    m_ssl_accepting = false;
    // We *may* have read data during the negotiation...
    if (!m_inbound.empty())
      processInbound();
  }
}

void HTTPd::HandlerThread::Processor::processSSLShutdown()
{
  MTrace(t_ssl, trace::Debug, "Shutdown entered; sis = " << m_ssl_in_shutdown);
  //
  // If we are shutting down, continue with shutdown. Otherwise get
  // out.
  //
  if (!m_ssl_in_shutdown)
    return;

  // Reset need state - we will build a new one
  m_ssl_needs_write = false;
  m_ssl_needs_read = false;
  // Attempt shutdown
  int res = SSL_shutdown(m_ssl);
  // Now shutdown may return 0 which means we need to call it again in
  // order for bidirectional shutdown to complete.
  if (!res) {
    MTrace(t_ssl, trace::Debug, "Bi-directional shutdown called");
    res = SSL_shutdown(m_ssl);
  }
  int err = SSL_get_error(m_ssl, res);
  if (res < 0) {
    // If we simply blocked on a read or write problem, deal with
    // it.
    if (err == SSL_ERROR_WANT_WRITE) {
      m_ssl_needs_write = true;
      MTrace(t_ssl, trace::Debug, "Shutdown need write");
      return;
    }
    if (err == SSL_ERROR_WANT_READ) {
      MTrace(t_ssl, trace::Debug, "Shutdown need read");
      m_ssl_needs_read = true;
      return;
    }
    // Some other error...
    m_readmore = false;
    m_ssl_in_shutdown = false;
    throw error("SSL shutdown sequence failed: "
                + std::string(ERR_error_string(ERR_get_error(), 0)));
  } if (res == 0) {
    m_readmore = false;
    m_ssl_in_shutdown = false;
    throw error("Second shutdown returned zero: "
                + std::string(ERR_error_string(err, 0)));
  } else {
    // We're done! Shutdown sequence complete.
    MTrace(t_ssl, trace::Debug, "SSL shutdown sequence complete");
    m_ssl_in_shutdown = false;
  }
}


#if defined(__unix__) || defined(__APPLE__)
void HTTPd::HandlerThread::Processor::read(int fd)
{
  if (m_ssl) {
    // Keep accept moving
    if (m_ssl_accepting) {
      processSSLAccept();
      return;
    }

    // Keep shutdown moving
    if (m_ssl_in_shutdown) {
      processSSLShutdown();
      return;
    }

    //
    // SSL read
    //
    uint8_t buf[8192];
    int res = SSL_read(m_ssl, buf, sizeof buf);
    int err = SSL_get_error(m_ssl, res);

    // We just serviced a read...
    m_ssl_needs_read = false;

    switch (err) {
    case SSL_ERROR_NONE:
      // Successful read of 'res' bytes
      m_inbound.append(buf, buf + res);
      MTrace(t_proc, trace::Debug, "Got " << res << " bytes of SSL payload "
             << "- inbound buffer now " << m_inbound.size() << " bytes");
      // Now empty the buffer for pending data
      if (int avail = SSL_pending(m_ssl)) {
        const size_t start = m_inbound.size();
        m_inbound.resize(start + avail);
        res = SSL_read(m_ssl, &m_inbound[start], avail);
        if (res != avail)
          throw error("SSL read from pending failed");
        MTrace(t_proc, trace::Debug, "Emptied " << avail << " bytes of pending data"
               " into buffer - now " << m_inbound.size() << " bytes");
      }
      break;
    case SSL_ERROR_ZERO_RETURN:
      // End of stream
      MTrace(t_proc, trace::Info, "SSL Connection closed");
      m_readmore = false;
      break;
    case SSL_ERROR_WANT_READ:
      // SSL requires a write...
      MTrace(t_proc, trace::Debug, "SSL read blocked on read");
      break;
    case SSL_ERROR_WANT_WRITE:
      // SSL requires a write...
      MTrace(t_proc, trace::Debug, "SSL read blocked on write");
      m_ssl_needs_write = true;
      break;
    default:
      // See if SSL has more info for us then
      if (long err_err = ERR_get_error())
        throw error(ERR_error_string(err_err, 0));
      else if (err == SSL_ERROR_SYSCALL) {
        // If res == 0, it means we got an EOS while reading - which
        // violates the protocol. However, we do not treat it as an
        // error, we simply treat it as if the client closed the
        // connection.
        if (res == 0) {
          MTrace(t_proc, trace::Debug, "SSL non-conforming connection close");
          m_readmore = false;
          break;
        }
        // If res == -1 then we got some other syscall error
        if (res == -1)
          throw syserror("SSL_read", "reading client data");
      }
      // In all other cases, some other error happened
      throw error("Non-syscall error in SSL_read");
    }

  } else {
    //
    // Non-SSL read
    //
    uint8_t buf[8192];
    const ssize_t rc = recv(fd, buf, sizeof buf, 0);
    // Ignore common error that we just retry later
    if (rc < 0 && errno == EINTR)
      return;
    // Report real errors
    if (rc < 0) {
      m_readmore = false;
      throw syserror("recv", "processor read");
    }
    // On connection close...
    if (!rc) {
      MTrace(t_proc, trace::Debug, "Connection closed");
      m_readmore = false;
      return;
    }
    // Fine, success - add data
    m_inbound.append(buf, buf + rc);
  }

  // Call our common code for processing this
  if (!m_ssl_accepting)
    processInbound();
}
#endif

#if defined(__unix__) || defined(__APPLE__)
void HTTPd::HandlerThread::Processor::write(int fd)
{
  MutexLock lock(m_outbound_mutex);

  if (m_ssl) {
    if (m_ssl_accepting) {
      processSSLAccept();
      return;
    }

    if (m_ssl_in_shutdown && m_outbound.empty()) {
      processSSLShutdown();
      return;
    }

    //
    // SSL write
    //
    MTrace(t_ssl, trace::Debug, "Write " << m_outbound.size() <<  " bytes");
    int res = SSL_write(m_ssl, outboundNextData(), outboundNextSize());
    int err = SSL_get_error(m_ssl, res);

    // We just had a write, so assume that is good enough
    m_ssl_needs_write = false;

    switch (err) {
    case SSL_ERROR_NONE:
      // Successful transmit.
      outboundConsumed(res);
      MTrace(t_ssl, trace::Debug, "Processor wrote " << res
             << " bytes over SSL");
      break;
    case SSL_ERROR_WANT_WRITE:
      // SSL requires a write...
      MTrace(t_ssl, trace::Debug, "SSL write blocked on write");
      break;
    case SSL_ERROR_WANT_READ:
      // SSL layer wants a read...
      m_ssl_needs_read = true;
      MTrace(t_ssl, trace::Debug, "SSL write blocked on read");
      break;
    default:
      // See if SSL has more info for us then
      if (long err_err = ERR_get_error())
        throw error(ERR_error_string(err_err, 0));
      else if (err == SSL_ERROR_SYSCALL) {
        // If ret is zero, an EOF that violoated protocol was
        // observed.
        if (res == 0)
          throw error("SSL write encountered EOF in violation of protocol");
        // If res == -1 then we got some other syscall error
        if (res == -1)
          throw syserror("SSL_write", "writing to client");
      }
      // In all other cases, some other error happened
      throw error("Non-syscall error in SSL_write");
    }

  } else {
    //
    // Non-SSL write
    //
    // Write all we can from our buffer
    const ssize_t rc = send(fd, outboundNextData(), outboundNextSize(), 0);

    // Ignore common error we just retry later
    if (rc < 0 && errno == EINTR)
      return;
    // Report real error
    if (rc < 0)
      throw syserror("send", "processor write");
    // If we consumed data, tidy up the buffer
    outboundConsumed(rc);
    MTrace(t_proc, trace::Debug, "Processor wrote " << rc << " bytes "
           "to fd " << fd);
  }
}
#endif

