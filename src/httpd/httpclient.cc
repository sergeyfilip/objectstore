///
/// Implementation of the HTTP client
///
// $Id: httpclient.cc,v 1.8 2013/08/29 13:35:43 joe Exp $
//

#include "httpclient.hh"
#include "common/scopeguard.hh"
#include "common/error.hh"
#include "common/trace.hh"

#include <sstream>
#include <algorithm>

#if defined(__unix__) || defined(__APPLE__)
# include <signal.h>
# include <unistd.h>
# include <sys/types.h>
# include <sys/socket.h>
# include <netdb.h>
# include <string.h>
# include <errno.h>
#endif

namespace {
  //! Trace path for HTTP client
  trace::Path t_cli("/HTTP/client");
}


HTTPclient::HTTPclient(const std::string &h, uint16_t p)
  : m_peer(h)
  , m_port(p)
  , m_sock(-1)
{
}

HTTPclient::~HTTPclient()
{
  if (m_sock != -1)
    close(m_sock);
}

const std::string &HTTPclient::refHost() const
{
  return m_peer;
}

uint16_t HTTPclient::getPort() const
{
  return m_port;
}

HTTPclient::HTTPclient(const HTTPclient &o)
  : m_peer(o.m_peer)
  , m_port(o.m_port)
  , m_sock(-1)
{
}


HTTPReply HTTPclient::execute(const HTTPRequest &request) try
{
  // Make sure we have a connection
  reconnect();

  // Fine, we have a connection. Issue request.
  transmitRequest(request);

  // Done. Now read reply.
  return readReply(request.m_id);
} catch (...) {
  // If transmit/receive failed, kill connection and re-try
  if (m_sock != -1)
    close (m_sock);
  m_sock = -1;

  MTrace(t_cli, trace::Debug, "Error during HTTPclient execute - will retry");

  // Same as above, but let the error escape
  reconnect();
  transmitRequest(request);
  return readReply(request.m_id);
}


void HTTPclient::reconnect()
{
  if (m_sock != -1)
    return;

  // Kill receive buffer on reconnect
  m_data.clear();

  MTrace(t_cli, trace::Debug, "Initiating connection to "
         << m_peer << ":" << m_port);

  // Resolve - we want an address for TCP connections
  struct addrinfo hints;
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;     // IPv4 or v6 - we don't care
  hints.ai_socktype = SOCK_STREAM; // TCP

  std::ostringstream strport;
  strport << m_port;
  struct addrinfo *result;
  if (getaddrinfo(m_peer.c_str(), strport.str().c_str(), &hints, &result))
    throw syserror("getaddrinfo", "resolving peer " + m_peer);
  ON_BLOCK_EXIT(freeaddrinfo, result);

  // Create a socket
  m_sock = socket(AF_INET, SOCK_STREAM, 0);
  if (m_sock == -1)
    throw syserror("socket", "creating socket for peer connection");

  // Now try connecting...
  int src = connect(m_sock, result->ai_addr, result->ai_addrlen);
  if (src == -1) {
    close(m_sock);
    m_sock = -1;
    throw syserror("connect", "connecting HTTP client socket to peer "
                   + m_peer);
  }
}


void HTTPclient::transmitRequest(const HTTPRequest &request)
{
#if defined(__sun__) || defined(__APPLE__)
  struct sigaction nact;
  struct sigaction pact;
  nact.sa_handler = SIG_IGN;
  sigaction(SIGPIPE, &nact, &pact);
  ON_BLOCK_EXIT(sigaction, SIGPIPE, &pact, &nact);
#endif

  std::ostringstream os;
  switch (request.getMethod()) {
  case HTTPRequest::mGET: os << "GET "; break;
  case HTTPRequest::mPOST: os << "POST "; break;
  case HTTPRequest::mPUT: os << "PUT "; break;
  case HTTPRequest::mDELETE: os << "DELETE "; break;
  case HTTPRequest::mHEAD: os << "HEAD "; break;
  default: throw error("Unhandled request type");
  }
  os << request.getURI() << " HTTP/1.1" << "\r\n";

  // Now headers...
  for (HTTPHeaders::m_headers_t::const_iterator i
         = request.m_headers.getHeaders().begin();
       i != request.m_headers.getHeaders().end(); ++i) {
    if (i->first == "content-length")
      continue;
    if (i->first == "transfer-encoding")
      continue;
    os << i->first << ": " << i->second << "\r\n";
  }

  // End of headers, then body
  os << "content-length: " << request.m_body.size() << "\r\n";

  MTrace(t_cli, trace::Debug, "Request top:" << std::endl
         << os.str());

  os << "\r\n";
  os << request.m_body;

  // Put data in vector for easy sending
  std::vector<uint8_t> outbuf;
  { std::string ob(os.str());
    outbuf.assign(ob.begin(), ob.end());
  }

  MTrace(t_cli, trace::Debug, "Serialised " << outbuf.size()
         << " bytes of request data");

  // Fine, now send the whole shebang
  size_t pos = 0;
  while (pos != outbuf.size()) {
    // Send data in up to 8k at a time
    ssize_t rc;
    do {
#if defined(__sun__) || defined(__APPLE__)
      rc = send(m_sock, &outbuf[pos], std::min(outbuf.size() - pos,
                                               size_t(8192)), 0);
#else
      rc = send(m_sock, &outbuf[pos], std::min(outbuf.size() - pos,
                                               size_t(8192)), MSG_NOSIGNAL);
#endif
    } while (rc == -1 && errno == EINTR);

    if (rc < 0) {
      close(m_sock); m_sock = -1;
      throw syserror("send", "sending to peer " + m_peer);
    }

    if (rc == 0) {
      close(m_sock); m_sock = -1;
      throw error("send sent nothing while writing to peer " + m_peer);
    }

    pos += rc;

    MTrace(t_cli, trace::Debug, "Transmitted " << rc << " bytes to peer, "
           << outbuf.size() - pos << " bytes left");

  }
}


HTTPReply HTTPclient::readReply(uint64_t request_id)
{
  HTTPReply rep(request_id, true, 0, HTTPHeaders(), std::string());

  // Read headers
  while (!rep.consumeHeaders(m_data))
    rx(1, m_data);

  // So consume body
  while (!rep.consumeBody(m_data))
    rx(1, m_data);

  return rep;
}



void HTTPclient::rx(size_t n, std::vector<uint8_t> &data)
{
  // Read at least n bytes. Do this by issuing a blocking read for n
  // (up to our buffer size) bytes followed by a non-blocking read for
  // the rest of our standard buffer size.
  uint8_t buf[8192];

  const size_t block_read = std::min(n, sizeof buf);
  const size_t nb_read = sizeof buf - block_read;

  ssize_t rres;
  do {
    rres = recv(m_sock, buf, block_read, 0);
  } while (rres == -1 && errno == EINTR);

  if (rres < 0)
    throw syserror("recv", "reading (blocking) data from server");

  if (!rres)
    throw error("server closed connection when reading response");

  if (size_t(rres) != block_read)
    throw error("did not receive expected amount of data from server");

  do {
    rres = recv(m_sock, buf + block_read, nb_read, MSG_DONTWAIT);
  } while (rres == -1 && errno == EINTR);

  // If we failed and it wasn't just because we don't want to block,
  // report error
  if (rres == -1 && errno != EAGAIN)
    throw syserror("recv", "reading (non-blocking) data from server");

  // If we simply didn't get anything extra, leave it at that
  if (rres == -1)
    rres = 0;

  // We don't care if we got 0 or more bytes - anything is a bonus.
  // Append data.
  data.insert(data.end(), buf, buf + block_read + rres);
}

