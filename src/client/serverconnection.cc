//
// Implementation of the serverconnection
//

#if defined(_WIN32)
# include <winsock2.h>
# include <ws2tcpip.h>
#endif

#include "serverconnection.hh"
#include "common/error.hh"
#include "common/trace.hh"
#include "common/scopeguard.hh"
#include "common/string.hh"
#include "common/base64.hh"
#include "common/ssl.hh"
#include "xml/xmlio.hh"
#include "version.hh"

#if defined(__unix__) || defined(__APPLE__)
# include <signal.h>
# include <unistd.h>
# include <sys/types.h>
# include <sys/socket.h>
# include <netdb.h>
# include <string.h>
# include <errno.h>
#endif

#include <openssl/ssl.h>
#include <openssl/err.h>

#include <sstream>
#include <algorithm>

#if defined(__unix__) || defined(__APPLE__)
# define INVALID_SOCKET -1
#endif
#if defined(_WIN32)
# undef max
# undef min
#endif

namespace {
  //! Tracer for server operations
  trace::Path t_con("/ServerConnection");
  trace::Path t_txn("/ServerConnection/Transaction");

  void short_delay()
  {
#if defined(__unix__) || defined(__APPLE__)
    sleep(1);
#endif
#if defined(_WIN32)
    Sleep(1000);
#endif
  }

  std::string lowercase(const std::string &lowercase)
  {
    std::string ret;
    for (size_t i = 0; i != lowercase.size(); ++i)
      ret += char(isupper(lowercase[i])
                  ? tolower(lowercase[i])
                  : lowercase[i]);
    return ret;
  }

  namespace ssl {
    //! Our context
    SSL_CTX *ctx(0);

    //! Setup mutex
    Mutex setup_lock;

GCC_DIAG_OFF(deprecated-declarations);
    void init()
    {
      init_openssl_library();

      // Lock-less fast path
      if (ctx)
        return;

      MutexLock l(setup_lock);

      // In critical section - now do the proper ctx test
      if (ctx)
        return;

      // Set up context
      ctx = SSL_CTX_new(SSLv23_client_method());
      if (!ctx)
        throw error("Cannot set up SSL context");

      // Load CAs we trust
      if (!SSL_CTX_load_verify_locations(ctx, g_getCAFile(), g_getCAPath())) {
        SSL_CTX_free(ctx);
        ctx = 0;
        throw error("Unable to load CAs");
      }
    }
GCC_DIAG_ON(deprecated-declarations);

  }

}


ServerConnection::ServerConnection(const std::string &host, uint16_t port,
                                   bool use_ssl)
  : m_sock(INVALID_SOCKET)
  , m_ssl(0)
  , m_bio(0)
  , m_hostname(host)
  , m_port(port)
  , m_use_ssl(use_ssl)
{
  // Clear stats
  m_reqstat[0] = m_reqstat[1] = m_reqstat[2] = 0;
}

ServerConnection::~ServerConnection()
{
  dropConnection();
}

ServerConnection::ServerConnection(const ServerConnection &o)
  : m_sock(INVALID_SOCKET)
  , m_ssl(0)
  , m_bio(0)
  , m_hostname(o.m_hostname)
  , m_port(o.m_port)
  , m_use_ssl(o.m_use_ssl)
  , m_defaultbasicuser(o.m_defaultbasicuser)
  , m_defaultbasicpass(o.m_defaultbasicpass)
{
  // Clear stats
  m_reqstat[0] = m_reqstat[1] = m_reqstat[2] = 0;
}


void ServerConnection::setDefaultBasicAuth(const std::string &user,
                                           const std::string &pass)
{
  m_defaultbasicuser = user;
  m_defaultbasicpass = pass;
}


void ServerConnection::clearDefaultBasicAuth()
{
  m_defaultbasicuser = Optional<std::string>();
  m_defaultbasicpass = Optional<std::string>();
}

void ServerConnection::getDefaultBasicAuth(std::string &user,
                                           std::string &pass) const
{
  user = m_defaultbasicuser.get();
  pass = m_defaultbasicpass.get();
}


GCC_DIAG_OFF(deprecated-declarations);
void ServerConnection::tx(const std::vector<uint8_t> &data)
{
  MTrace(t_con, trace::Debug, "Transmitting " << data.size()
         << " bytes");
  if (m_ssl) {
    // Send using BIO
    int res = SSL_write(m_ssl, data.data(), int(data.size()));

    if (res < 0) {
      // Error. Abort.
      int err = SSL_get_error(m_ssl, res);
      throw error(ERR_error_string(err, 0));
    }

    if (!res) {
      // Client closed connection
      throw error("SSL Peer closed connection on write");
    }

    // See if we got everything we wanted
    if (size_t(res) != data.size())
      throw error("did not send expected amount of data to SSL peer");

    MTrace(t_con, trace::Debug, "Sent " << data.size()
           << " bytes over SSL");
    return;
  }

  // Attempt transmission of data
#if defined(__unix__) || defined(__APPLE__)
  ssize_t rc;
  do {
    rc = send(m_sock, data.data(), data.size(), 0);
  } while (rc == -1 && errno == EINTR);
#endif
#if defined(_WIN32)
  SSIZE_T rc;
  do {
    rc = send(m_sock, reinterpret_cast<const char*>(data.data()), int(data.size()), 0);
  } while (rc == -1 && errno == EINTR);
#endif

  if (rc < 0)
    throw syserror("send", "sending to server");

  if (size_t(rc) != data.size())
    throw error("incomplete send to server");

  // Done!
}

void ServerConnection::rx(size_t n, std::vector<uint8_t> &data)
{
  // If we're on SSL, handle the read using the BIO
  if (m_ssl) {
    // We want to read all bytes that are already available, and we
    // want to read at least the number we were requested to.
    const int avail = SSL_pending(m_ssl);
    const int to_read = std::max(avail, int(n));
    const size_t start = data.size();
    data.resize(data.size() + to_read);
    int res = SSL_read(m_ssl, &data[start], to_read);

    if (res < 0) {
      // Error. Abort.
      int err = SSL_get_error(m_ssl, res);
      throw error(ERR_error_string(err, 0));
    }

    if (!res) {
      // Client closed connection
      throw error("SSL Peer closed connection");
    }

    // See if we got everything we wanted
    if (res != to_read)
      throw error("did not receive expected amount of data from SSL peer");

    MTrace(t_con, trace::Debug, "Received " << to_read
           << " SSL payload bytes - added to "
           << start << " byte buffer");

    return;
  }

  // Read at least n bytes. Do this by issuing a blocking read for n
  // (up to our buffer size) bytes followed by a non-blocking read for
  // the rest of our standard buffer size.
  uint8_t buf[8192];

  const size_t block_read = std::min(n, sizeof buf);
  const size_t nb_read = sizeof buf - block_read;

#if defined(__unix__) || defined(__APPLE__)
  ssize_t rres;
  do {
    rres = recv(m_sock, buf, block_read, 0);
  } while (rres == -1 && errno == EINTR);
#endif
#if defined(_WIN32)
  SSIZE_T rres;
  do {
    rres = recv(m_sock, reinterpret_cast<char*>(buf), int(block_read), 0);
  } while (rres == -1 && errno == EINTR);
#endif

  if (rres < 0)
    throw syserror("recv", "reading (blocking) data from server");

  if (size_t(rres) != block_read)
    throw error("did not receive expected amount of data from server");

  // On POSIX we can do a non blocking read easily
#if defined(__unix__) || defined(__APPLE__)
  do {
    rres = recv(m_sock, buf + block_read, nb_read, MSG_DONTWAIT);
  } while (rres == -1 && errno == EINTR);
#endif

#if defined(_WIN32)
  // On Win32 it is slightly more complicated. Use "select" to see if
  // there is data and only read if there is.
  { fd_set fdr, fdw, fde;
    memset(&fdr, 0, sizeof fdr);
    memset(&fdw, 0, sizeof fdw);
    memset(&fde, 0, sizeof fde);
    fdr.fd_count = 1;
    fdr.fd_array[0] = m_sock;
    struct timeval to;
    to.tv_sec = 0; to.tv_usec = 0;
    // If select returns 1, then our socket is ready for reading
    if (1 == select(1, &fdr, &fdw, &fde, &to)) {
      do {
	rres = recv(m_sock, reinterpret_cast<char*>(buf + block_read), int(nb_read), 0);
      } while (rres == -1 && errno == EINTR);
    }
  }
#endif

  // If we failed and it wasn't just because we don't want to block,
  // report error
  if (rres == -1 && errno != EAGAIN)
    throw syserror("recv", "reading (non-blocking) data from server");

  // If we simply didn't get anything extra, leave it at that
  if (rres == -1)
    rres = 0;

  MTrace(t_con, trace::Debug, "Received " << block_read
         << " + " << rres << " bytes - adding to "
         << data.size() << " byte buffer");

  // We don't care if we got 0 or more bytes - anything is a bonus.
  // Append data.
  data.insert(data.end(), buf, buf + block_read + rres);
}
GCC_DIAG_ON(deprecated-declarations);

ServerConnection::Request
::Request(method_t method, const std::string &URI)
  : m_method(method)
{
  std::ostringstream out;

  //
  // Generate request line
  //
  switch (method) {
  case mHEAD: out << "HEAD "; break;
  case mGET: out << "GET "; break;
  case mPOST: out << "POST "; break;
  }
  out << URI << " HTTP/1.1";

  MTrace(t_con, trace::Debug, "Serialised request: "
         << out.str());

  out << "\r\n";
  std::string s = out.str();
  m_req.insert(m_req.end(), s.begin(), s.end());
}


ServerConnection::Request
&ServerConnection::Request::setBody(const std::vector<uint8_t> &body)
{
  m_body = body;
  return *this;
}

ServerConnection::Request
&ServerConnection::Request::setBody(const xml::IDocument &body)
{
  std::ostringstream xmlout;
  xml::XMLWriter xmlwriter(xmlout);
  body.output(xmlwriter);

  std::string ss(xmlout.str());
  m_body.assign(ss.begin(), ss.end());
  return *this;
}

ServerConnection::Request
&ServerConnection::Request::setBasicAuth(const std::string &u, const std::string &p)
{
  std::ostringstream s;
  s << "authorization: ";

  { std::string auth;
    const std::string data = u + ":" + p;
    base64::encode(auth, std::vector<uint8_t>(data.begin(), data.end()));
    s << "Basic " << auth << "\r\n";
  }

  std::string ss(s.str());
  m_headers.insert(m_headers.end(), ss.begin(), ss.end());
  return *this;
}

ServerConnection::Request
&ServerConnection::Request::setBasicAuth(const ServerConnection &c)
{
  std::string u, p;
  c.getDefaultBasicAuth(u, p);
  setBasicAuth(u, p);
  return *this;
}


const std::vector<uint8_t>
&ServerConnection::Request::serialise(const std::string &host) const
{
  m_cache = m_req;
  m_cache.insert(m_cache.end(), m_headers.begin(), m_headers.end());

  // Add missing headers; content-length and host
  { std::ostringstream s;
    s << "content-length: " << m_body.size() << "\r\n"
      << "host: " << host << "\r\n"
      << "\r\n";
    std::string ss(s.str());
    m_cache.insert(m_cache.end(), ss.begin(), ss.end());
  }

  // Add body
  m_cache.insert(m_cache.end(), m_body.begin(), m_body.end());

  // That's it!
  return m_cache;
}

std::string ServerConnection::Request::toString() const
{
  return std::string(m_req.begin(), std::find(m_req.begin(), m_req.end(), '\r'));
}

void ServerConnection::Request::addHeader(const std::string &key,
                           const std::string &value)
{
  std::ostringstream s;
  s << lowercase(key) << ": ";
  s << value << "\r\n";

  std::string ss(s.str());
  m_headers.insert(m_headers.end(), ss.begin(), ss.end());
}

ServerConnection::Reply::Reply()
  : m_code(0)
{
}

ServerConnection::Reply
ServerConnection::execute(const ServerConnection::Request &req)
{
  // Statistics
  switch (req.m_method) {
  case mHEAD: m_reqstat[0]++; break;
  case mGET: m_reqstat[1]++; break;
  case mPOST: m_reqstat[2]++; break;
  }

  size_t retries = 3;
  while (true) {
    try {
      // If we're not connected, connect
      if (!isConnected())
        doConnect();

      // Send request.
      tx(req.serialise(getHostname()));

      MTrace(t_txn, trace::Debug, "TX: " << req.toString());

      ServerConnection::Reply rep;

      // Read headers
      while (!rep.consumeHeaders(m_data))
        rx(1, m_data);

      // If we got a 5xx back and we have retries left, go for a
      // retry.
      if (rep.getCode() >= 500 && retries) {
        retries--;
        MTrace(t_txn, trace::Info, "Received server error - will retry ("
               + rep.toString() + ")");
        short_delay();
        dropConnection();
        continue;
      }

      // So consume body
      while (!rep.consumeBody(m_data))
        rx(1, m_data);

      MTrace(t_txn, trace::Debug, "RX: " << rep.toString());

      return rep;
    } catch (error &e) {
      // If no more retries, stop trying
      if (!retries--)
        throw;
      MTrace(t_txn, trace::Info, "Transaction failed: " << e.toString()
             << " - will reconnect and retry");
      // To prevent hammering the connection, simply wait a bit and
      // re-try
      short_delay();
      // Drop connection to force reconnect.
      dropConnection();
    }
  }
}

std::vector<uint8_t> &ServerConnection::Reply::refBody()
{
  return m_body;
}

const std::string &ServerConnection::getHostname() const
{
  return m_hostname;
}

unsigned ServerConnection::Reply::getCode() const
{
  return m_code;
}

bool ServerConnection::Reply::consumeHeaders(std::vector<uint8_t> &d)
{
  // We simply attempt parsing the full request-line and header block
  // - if we fail, return false.
  //
  // First, simply scan for \r\n\r\n - this is the end of the header
  // block.
  const char *srch = "\r\n\r\n";
  std::vector<uint8_t>::iterator i = std::search(d.begin(), d.end(),
                                                 srch, srch + 4);
  // If we did not find this, fail.
  if (i == d.end())
    return false;

  MTrace(t_con, trace::Debug, "Located end of HTTP reply header block");

  // Good, we have the full header block. Now parse them.
  std::string head(d.begin(), i + 4);
  std::string::const_iterator pos = head.begin();

  MTrace(t_con, trace::Debug, "Header block: " << head);

  d.erase(d.begin(), i + 4);

  // Read the status line - rfc2616 says:
  //
  // Status-Line = HTTP-Version SP Status-Code SP Reason-Phrase CRLF
  //
  const std::string s_httpver = consumeUntil(" ", head, pos);
  m_code = string2AnyInt<uint16_t>(consumeUntil(" ", head, pos));
  const std::string s_reason = consumeUntil("\r\n", head, pos);

  if (s_httpver != "HTTP/1.1")
    throw error("Expected HTTP/1.1, got: " + s_httpver);

  MTrace(t_con, trace::Debug, "Got status-line: code " << m_code
         << " (" << s_reason << ")");

  // Now parse headers
  while (true) {
    try {
      std::string field_name = consumeUntil(":", head, pos);
      std::string field_value = consumeUntil("\r\n", head, pos);

      // We may strip leading and trailing LWS from field_value
      while (!field_value.empty() && field_value[0] == ' ')
        field_value.erase(0, 1);
      while (!field_value.empty() && field_value[field_value.size() - 1] == ' ')
        field_value.erase(field_value.size() - 1, 1);

      m_headers.insert(std::make_pair(lowercase(field_name), field_value));
      //      MTrace(t_con, trace::Debug, "Parsed header '" << field_name
      //             << "' = '" << field_value << "'");

    } catch (error&) {
      // Cannot consume until ":" - possibly because there are no more
      // headers.
      std::string trailer = consumeUntil("\r\n", head, pos);
      if (!trailer.empty())
        throw error("Unexpected trailing data: " + trailer);
      // Ok success -parsed all headers
      return true;
    }
  }
}

bool ServerConnection::Reply::consumeBody(std::vector<uint8_t> &d)
{
  // XXXX this is not a proper reply parser - headers must be case
  // insensitive. Also, some request codes must not have a body...

  // Do we have a content-length header?
  { headers_t::iterator hi = m_headers.find("content-length");
    if (hi != m_headers.end()) {
      // Fine, simple content-length read
      size_t body_size = string2AnyInt<uint32_t>(hi->second);

      // So, if we have the right number of body bytes, consume them
      if (d.size() >= body_size) {
        MTrace(t_con, trace::Debug, "Consumed " << body_size
               << " bytes of reply body");

        m_body = std::vector<uint8_t>(d.begin(), d.begin() + body_size);
        d.erase(d.begin(), d.begin() + body_size);
        return true;
      }
      return false;
    }
  }

  // No content-length header. See if we have a transfer-encoding
  // header.
  { headers_t::iterator hi = m_headers.find("transfer-encoding");
    if (hi != m_headers.end()) {
      // The only transfer-encoding we support is chunked.
      if (hi->second != "chunked")
        throw error("Unsupported transfer encoding: " + hi->second);

      while (true) {
        // See if we have a chunk size. This is a hex number terminated
        // by CRLF
        const char *crlf = "\r\n";
        std::vector<uint8_t>::iterator csend = std::search(d.begin(), d.end(),
                                                           crlf, crlf + 2);
        if (csend == d.end()) {
          MTrace(t_con, trace::Debug, "TE read: no chunk size yet");
          return false; // no chunk size yet
        }

        // Parse chunk size - it is a hex number
        const size_t csize = hex2AnyInt<uint32_t>(std::string(d.begin(), csend));

        // See if we have this much data left (remember CRLF after chunk data)
        std::vector<uint8_t>::iterator data_start = csend + 2;
        MAssert(data_start <= d.end(),
                "start of two-byte needle plus two past end of haystack");
        if (csize + 2 > size_t(d.end() - data_start)) {
          MTrace(t_con, trace::Debug, "TE read: has " << (d.end() - data_start)
                 << " bytes, need " << csize << "+2 for chunk");
          return false; // we do not have the full chunk yet
        }

        // Good, full chunk is here.
        m_body.insert(m_body.end(), data_start, data_start + csize);
        d.erase(d.begin(), data_start + csize + 2);

        MTrace(t_con, trace::Debug, "Consumed " << csize
               << "+2 bytes of chunk data. "
               << d.size() << " bytes left in buffer");

        // If this is the zero chunk, we're done
        if (!csize) {
          MTrace(t_con, trace::Debug, "Received zero chunk. End of body.");
          return true;
        }

        // Read again...
      }
    }
  }

  throw error("No content-length and no transfer encoding");
}

std::string ServerConnection::Reply::consumeUntil(const std::string &needle,
                                                  const std::string &haystack,
                                                  std::string::const_iterator &pos)
{
  std::string::const_iterator i = std::search(pos, haystack.end(),
                                              needle.begin(), needle.end());
  if (i == haystack.end())
    throw error("Expected '" + needle + "' in '" + haystack + "'"
                + " after position '" + std::string(pos, haystack.end()) + "'");

  const std::string res = haystack.substr(pos - haystack.begin(), i - pos);
  pos = i;
  pos += needle.size();

  return res;
}

std::string ServerConnection::Reply::toString() const
{
  std::ostringstream os;
  os << "Code " << m_code << " -> " << m_body.size() << " bytes body";
  if (m_code >= 400) {
    os << std::endl << " => " << std::string(m_body.begin(), m_body.end());
  }
  return os.str();
}

std::string ServerConnection::Reply::readHeader() const
{
    std::string someString;
    std::string iterationString;
    
    headers_t::const_iterator it = m_headers.end();
    it--;
    if (it != m_headers.begin()) {
        iterationString = it->second.c_str();
        size_t found = iterationString.find_last_of("/");
        someString = iterationString.substr(found+1);
    }
    
    return someString;
}


void ServerConnection::traceStatistics() const
{
  MTrace(t_con, trace::Info, "\nConnection statistics:\n"
         << " #HEAD: " << m_reqstat[0] << "\n"
         << " #GET:  " << m_reqstat[1] << "\n"
         << " #POST: " << m_reqstat[2]);
}


bool ServerConnection::isConnected() const
{
  return m_sock != INVALID_SOCKET;
}

GCC_DIAG_OFF(deprecated-declarations);
void ServerConnection::doConnect()
{
  // Op-op if connected
  if (isConnected())
    return;

  // Clear receive buffer - whatever it contains is from an old
  // connection
  m_data.clear();

  // Resolve - we want an address for TCP connections
  struct addrinfo hints;
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;     // IPv4 or v6 - we don't care
  hints.ai_socktype = SOCK_STREAM; // TCP

  std::ostringstream strport;
  strport << m_port;
  struct addrinfo *result;
  if (getaddrinfo(m_hostname.c_str(), strport.str().c_str(), &hints, &result))
    throw error("Cannot resolve host name: " + m_hostname);
  ON_BLOCK_EXIT(freeaddrinfo, result);

  // Attempt connecting
  m_sock = socket(AF_INET, SOCK_STREAM, 0);
  if (m_sock < 0)
    throw syserror("socket", "creating client socket");

  while (result) {
    // Now try connecting...
    MTrace(t_con, trace::Debug, "Attempting connection...");
    int src = connect(m_sock, result->ai_addr, int(result->ai_addrlen));
    if (src == -1) {
      // Try the next address
      result = result->ai_next;
      continue;
    }
    // We're done!
    MTrace(t_con, trace::Debug, "Connected to " << m_hostname << ":" << m_port);
    break;
  }
  if (!result)
    throw syserror("connect", "connecting to server");

  if (m_use_ssl) {
    ssl::init();
    // Set up SSL state
    m_ssl = SSL_new(ssl::ctx);
    // Connect state with socket
    m_bio = BIO_new_socket(m_sock, BIO_NOCLOSE);
    SSL_set_bio(m_ssl, m_bio, m_bio);
    // Server certificate must be valid
    SSL_set_verify(m_ssl, SSL_VERIFY_PEER, 0);
    // We do not want to be bothered with WANT_READ/WANT_WRITE caused
    // by re-negotiations. Instead, let the library deal with this.
    SSL_set_mode(m_ssl, SSL_MODE_AUTO_RETRY);
    // Now handshake for SSL connect
    int res = SSL_connect(m_ssl);
    int err = SSL_get_error(m_ssl, res);
    switch (err) {
    case SSL_ERROR_NONE:
      MTrace(t_con, trace::Debug, "Successfully negotiated SSL");
      break;
    default:
      if (long err_err = ERR_get_error())
        throw error("SSL negotiate with " + m_hostname + ": "
                    + ERR_error_string(err_err, 0));
      else
        throw error("SSL negotiation with " + m_hostname + " failed.");
    }
  }
}

void ServerConnection::dropConnection()
{
  if (m_ssl) {
    SSL_free(m_ssl);
    m_ssl = 0;
  }
  if (m_sock != INVALID_SOCKET) {
#if defined(__unix__) || defined(__APPLE__)
    close(m_sock);
#endif
#if defined(_WIN32)
    closesocket(m_sock);
#endif
    m_sock = INVALID_SOCKET;
  }
}
GCC_DIAG_ON(deprecated-declarations);
