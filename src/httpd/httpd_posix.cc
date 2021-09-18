//
//! \file httpd/httpd_posix.cc
//! POSIX implementation of the httpd routines
//
// $Id: httpd_posix.cc,v 1.10 2013/06/25 07:19:35 joe Exp $
//

#include "httpd.hh"
#include "common/trace.hh"
#include "common/ssl.hh"

#include <vector>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

namespace {
  //! Trace path for POSIX specific HTTPd code
  trace::Path t_httpp("/HTTPd/posix");

  //! Trace path for peer connections
  trace::Path t_peer("/HTTPd/peer");

  //! Depth of listen queue
  const size_t g_listen_queue(32);
}


HTTPd &HTTPd::addListener(uint16_t port)
{
  MutexLock lock(m_conf_mutex);
  // Create an IPv4 stream socket
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0)
    throw syserror("socket", "adding HTTPd listener");
  m_listeners.push_back(fd);

  // Set this socket to be non-blocking
  if (fcntl(fd, F_SETFL, O_NONBLOCK) < 0)
    throw syserror("fcntl", "setting socket to non-blocking mode");

  // Set this socket to be reusable
  { int on = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(SOL_SOCKET)) < 0)
      throw syserror("setsockopt", "setting socket to reuse");
  }

  // Bind to the requested port
  { struct sockaddr_in in;
    memset(&in, 0, sizeof(in));
    in.sin_family = AF_INET;
    in.sin_port = htons(port);
    in.sin_addr.s_addr = INADDR_ANY;
    if (bind(fd, reinterpret_cast<struct sockaddr *>(&in), sizeof(in)) < 0)
      throw syserror("bind", "Unable to bind to port");
  }

  // and set it to listen for incomming connections
  if (listen(fd, g_listen_queue) < 0)
    throw syserror("listen", "Unable to listen on socket");

  MTrace(t_httpp, trace::Debug, "Added listener on port " << port);
  // We want our poll routine to reconsider which fds to poll
  restartPoll();

  return *this;
}

void HTTPd::stop()
{
  // Clear our listener list to signal to the handler thread that
  // there is no more work for it
  std::list<int> listeners;
  { MutexLock lock(m_conf_mutex);
    m_exiting = true;
    listeners.swap(m_listeners);
  }

  // Wake up the handler thread from its poll call and wait for its
  // termination
  { restartPoll();
    m_handler.join_nothrow();
  }

  // If we had listeners, deal with that
  if (!listeners.empty()) {
    // Now close our listeners
    while (!listeners.empty()) {
      close(listeners.front());
      listeners.pop_front();
    }
  }

  //
  // Wake up a worker - our getRequest() method will construct stop
  // messages for the workers for as long as needed
  //
  MTrace(t_httpp, trace::Debug, "No more listeners; waking up a worker "
         "to have stop events generated");
  m_requests_sem.increment();
}

HTTPd &HTTPd::startSSL(const std::string &certfile,
                       const std::string &keyfile)
{
  if (m_ctx)
    return *this;

  init_openssl_library();

  // Create context
  m_ctx = SSL_CTX_new(SSLv23_server_method());

  // Set certificate and key file
  if (!SSL_CTX_use_certificate_chain_file(m_ctx, certfile.c_str())) {
    SSL_CTX_free(m_ctx);
    throw error("Unable to use certificate file " + certfile);
  }

  // Set up private key file
  if (!SSL_CTX_use_PrivateKey_file(m_ctx, keyfile.c_str(),
                                   SSL_FILETYPE_PEM)) {
    SSL_CTX_free(m_ctx);
    throw error("Unable to use key file " + keyfile);
  }

  return *this;
}


//
// ------------------------------------------------------------
// HTTPd::HandlerThread
// ------------------------------------------------------------
//

HTTPd::HandlerThread::~HandlerThread()
{
  // Now close our open connections
  while (!m_connections.empty()) {
    removeProcessor(m_connections.begin()->first);
  }
}

void HTTPd::HandlerThread::run()
{
  MTrace(t_httpp, trace::Debug, "HTTPd handler thread started");
  while (true) {
    // See if we are shutting down...
    if (m_parent.m_exiting) {
      MTrace(t_httpp, trace::Debug, "HTTPd handler thread exiting");
      return;
    }
    // Construct poll fd array
    std::vector<struct pollfd> pollfds;
    { MutexLock lock2(m_parent.m_conf_mutex);
      for (std::list<int>::const_iterator i = m_parent.m_listeners.begin();
           i != m_parent.m_listeners.end(); ++i) {
        struct pollfd ent;
        ent.fd = *i;
        ent.events = POLLIN; // new connection ready
        ent.revents = 0;
        pollfds.push_back(ent);
      }
    }
    //
    // Now add our connections too
    //
    for (std::map<int,Processor>::iterator i = m_connections.begin();
         i != m_connections.end(); ) {
      // If we should close this connection, close it.
      if (i->second.shouldClose()) {
        removeProcessor((i++)->first);
        continue;
      }
      MTrace(t_httpp, trace::Debug, "Connection " << i->second.toString());
      // Add the connection then
      struct pollfd ent;
      ent.fd = i->first;
      ent.events
        = (i->second.shouldRead() ? POLLIN : 0)
        | (i->second.shouldWrite() ? POLLOUT : 0);
      ent.revents = 0;
      pollfds.push_back(ent);
      ++i;
    }
    //
    // Last but not least, monitor the wake pipe
    //
    { struct pollfd ent;
      ent.fd = m_parent.m_wakepipe[0];
      ent.events = POLLIN;
      ent.revents = 0;
      pollfds.push_back(ent);
    }
    //
    // Fine, now call poll() on our non-empty pollfds
    //
    int rc;
    do {
      rc = poll(&pollfds[0], pollfds.size(), -1);
    } while (rc == -1 && errno == EINTR);
    MTrace(t_httpp, trace::Debug, "poll returned with rc=" << rc);
    if (!rc)
      throw error("Poll without timeout timed out");
    if (rc < 0)
      throw syserror("poll", "listen socket poll");
    // Successful return - we need to treat a socket
    for (size_t i = 0; i != pollfds.size(); ++i) {
      if (!pollfds[i].revents)
        continue;
      //
      // OK, so this entry had events. See if it is one of our
      // connections
      //
      std::map<int,Processor>::iterator conn_i
        = m_connections.find(pollfds[i].fd);
      if (conn_i != m_connections.end()) {
        MTrace(t_httpp, trace::Debug, "Treating event on connection fd "
               << conn_i->first);
        try {
          // Deal with reads, if any
          if (pollfds[i].revents & POLLIN)
            conn_i->second.read(pollfds[i].fd);
          // Deal with writes, if any
          if (pollfds[i].revents & (POLLOUT | POLLERR | POLLHUP))
            conn_i->second.write(pollfds[i].fd);
        } catch (error &e) {
          MTrace(t_httpp, trace::Info, "Connection error: " << e.toString()
                 << " - will close");
          removeProcessor(conn_i->first);
        }
        // Done with this fd.
        continue;
      }
      //
      // Is it the wake pipe?
      //
      if (pollfds[i].fd == m_parent.m_wakepipe[0]) {
        uint8_t buf[1024];
        int rc;
        do { rc = read(m_parent.m_wakepipe[0], buf, sizeof buf); }
        while (rc == -1 && errno == EINTR);
        if (rc < 0)
          throw syserror("read", "reading from wake pipe");
        // Done. The only purpose it serves it to wake us from
        // poll(). Simply discard read data.
        MTrace(t_httpp, trace::Debug, "Woken by means of wake pipe");
        continue;
      }
      //
      // This was not a connection we have and not the wake pipe; it
      // must be a listening socket then.
      //
      MTrace(t_httpp, trace::Debug, "Accepting new connection on fd "
             << pollfds[i].fd);
      // Accept new connection
      struct sockaddr addr;
      memset(&addr, 0, sizeof addr);
      socklen_t addr_len = sizeof addr;
      const int fd = accept(pollfds[i].fd, &addr, &addr_len);
      // Accept errors are usually EINTR or buffer errors or other
      // temporary stuff. We will get notified again so don't bother
      // dealing with it.
      if (fd < 0) {
        MTrace(t_httpp, trace::Debug, "accept failed with error "
               << strerror(errno));
        continue;
      }
      if (fcntl(fd, F_SETFL, O_NONBLOCK) < 0) {
        close(fd);
        throw syserror("fcntl", "setting accepted fd to non-blocking mode");
      }
      // Are we above the connection limit?
      if (m_connections.size() == m_parent.m_max_connections) {
        close(fd);
        MTrace(t_httpp, trace::Info, "Dropping incoming connection - at limit ("
               << m_parent.m_max_connections << ")");
        continue;
      }
      // Fine, start a new processor for this fd
      std::pair<m_connections_t::iterator, bool>
        ires = m_connections.insert(std::make_pair(fd,Processor(*this)));
      MAssert(ires.second, "Doubly registered fd on active connections");
      ires.first->second.setFD(fd);
      MTrace(t_httpp, trace::Debug, "Instantiated Processor for fd=" << fd);

      { char adrbuf[INET_ADDRSTRLEN + INET6_ADDRSTRLEN];
        bool success = false;
        switch (addr.sa_family) {
        case AF_INET: {
          struct sockaddr_in *s = reinterpret_cast<sockaddr_in*>(&addr);
          success = inet_ntop(addr.sa_family, &s->sin_addr, adrbuf, sizeof adrbuf);
          break;
        }
        case AF_INET6: {
          struct sockaddr_in6 *s = reinterpret_cast<sockaddr_in6*>(&addr);
          success = inet_ntop(addr.sa_family, &s->sin6_addr, adrbuf, sizeof adrbuf);
          break;
        }
        default:
          MTrace(t_peer, trace::Warn, "Client connected - with unknown AF");
        }

        if (success)
          MTrace(t_peer, trace::Info, "Client connected from " << adrbuf);
        else
          MTrace(t_peer, trace::Warn, "Client connected - but cannot print addr");
      }
    }
  }

}


void HTTPd::HandlerThread::removeProcessor(int fd)
{
  MTrace(t_httpp, trace::Debug, "Removing processor for fd="<< fd);

  m_connections_t::iterator i = m_connections.find(fd);
  MAssert(i != m_connections.end(),
          "Cannot find processor for fd in removeProcessor");
  // First of all, we want to ask the processor (while we have it)
  // which request ids it is not yet serialized a final reply
  // for. Now, worker threads may be working on changing that as we
  // work here, but the worker threads will only *remove* requests. We
  // (the handler that receives requests from the network) are the
  // only one who can actually add requests.
  //
  // So, the worst that can happen is that we get a list of "too many"
  // ids. This is no problem - we must just guarantee that we get them
  // all.
  std::deque<uint64_t> active;
  i->second.getActiveRequestIds(active);

  MutexLock lock(m_id_procs_mutex);
  while (!active.empty()) {
    MTrace(t_httpp, trace::Debug, " req id " << active.front()
           << " disassociated from processor for fd " << fd);
    m_id_procs.erase(active.front());
    active.pop_front();
  }

  // Close the connection
  close(i->first);


  // Remove all request-id-to-processor mappings we may have for
  // requests that were not completely responded to
  for (m_id_procs_t::iterator m = m_id_procs.begin();
       m != m_id_procs.end(); )
    if (m->second == &i->second)
      m_id_procs.erase(m++);
    else
      ++m;

  // Remove the fd/processor pair.
  m_connections.erase(i);

}

