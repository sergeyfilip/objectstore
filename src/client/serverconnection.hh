//
// Simple server connection
//

#ifndef DEMOCLI_SERVERCONNECTION_HH
#define DEMOCLI_SERVERCONNECTION_HH

#include "common/optional.hh"

#include <string>
#include <vector>
#include <map>
#include <stdint.h>

#include <openssl/ssl.h>
#include <openssl/bio.h>

#if defined(_WIN32)
# include <windows.h>
#endif

namespace xml {
  class IDocument;
}

class ServerConnection {
public:
  //! Set up connection parameters for connection to remote HTTP or
  //! HTTPS server
  //
  //! Note: The constructor does not initiate the connection - actual
  //! connection failure is typically temporary in nature and is
  //! therefore transparently performed and re-tried when the
  //! connection is actually utilized.
  //
  //! server: hostname of server to connect to
  //! port: port to connect to
  //! ssl: enable or disable SSL
  ServerConnection(const std::string &server, uint16_t port, bool ssl = false);

  //! Clean up
  ~ServerConnection();

  //! Copy construction - this will copy the connection parameters -
  //! the connection will be established when needed.
  ServerConnection(const ServerConnection &o);


  //! In some use cases we want to set a default set of credentials
  //! which will can used for all requests passed through the
  //! connection if the setBasicAuth() method is called on the
  //! request.
  void setDefaultBasicAuth(const std::string &user, const std::string &pass);

  //! We can clear the default credentials too
  void clearDefaultBasicAuth();

  //! Extract basic auth credentials - will throw if they were not set
  void getDefaultBasicAuth(std::string &uuser, std::string &pass) const;


  enum method_t { mHEAD, mGET, mPOST};

  class Request {
  public:
    Request(method_t method, const std::string &URI);

    /// Serialise request
    const std::vector<uint8_t> &serialise(const std::string &host) const;

    /// Type of request
    method_t m_method;

    /// Add body data
    Request &setBody(const std::vector<uint8_t> &body);

    /// Render an XML document into the body and set appropriate
    /// content-type header
    Request &setBody(const xml::IDocument &body);

    /// Add a basic auth header
    Request &setBasicAuth(const std::string &u, const std::string &p);

    /// Add a basic auth header using the connection default
    /// credentials - will throw if no default credentials were set on
    /// the connection
    Request &setBasicAuth(const ServerConnection&);

    /// For debug purposes - print request summary
    std::string toString() const;
    
    /// \param key   key of header: will be lower-cased before insertion
    /// \param value value of header.
    void addHeader(const std::string &key,
                     const std::string &value);

  private:
    /// m_req: the request line
    std::vector<uint8_t> m_req;
    /// m_headers: all headers except content-length
    std::vector<uint8_t> m_headers;
    /// m_body: the request body
    std::vector<uint8_t> m_body;
    /// Serialisation buffer
    mutable std::vector<uint8_t> m_cache;
  };

  class Reply {
  public:
    Reply();
    /// Attempt parsing headers. Returns true on success. If
    /// unsuccessful, read more data into buffer and re-try the
    /// parse. This *may* seem inefficient - but in fact we will
    /// usually receive the full headers in the first recv(), so retry
    /// is unlikely.
    bool consumeHeaders(std::vector<uint8_t> &d);

    /// Consume body. Returns true on completion, false if there is
    /// more body data to be consumed.
    bool consumeBody(std::vector<uint8_t> &d);

    /// Return status code
    unsigned getCode() const;

    /// For debug purposes - print reply summary
    std::string toString() const;
      
      //For reading content-length
      std::string readHeader() const;

    /// Reference the body buffer
    std::vector<uint8_t> &refBody();
  private:
    /// status code
    unsigned m_code;

    /// Map of headers
    typedef std::map<std::string,std::string> headers_t;
    headers_t m_headers;

    /// Body data
    std::vector<uint8_t> m_body;

    /// Parser utility. Searches "haystack" from position "pos" and
    /// increments "pos" to the position immediately following
    /// "needle". Returns the data up until (but not including) the
    /// "needle". Throws in case "needle" does not exist in "haystack"
    /// at or after "pos".
    std::string consumeUntil(const std::string &needle,
                             const std::string &haystack,
                             std::string::const_iterator &pos);
  };

  /// Blocking request/response
  Reply execute(const Request&);

  /// Get host name of server
  const std::string &getHostname() const;

  /// Print statistics - this is traced with INFO level to
  /// /ServerConnection
  void traceStatistics() const;

private:
  /// Protect against assignment
  ServerConnection &operator=(const ServerConnection&);

#if defined(__unix__) || defined(__APPLE__)
  /// Socket - if -1 we're not connected
  int m_sock;
#endif

#if defined(_WIN32)
  SOCKET m_sock;
#endif

  /// SSL State if SSL is in use
  SSL *m_ssl;
  /// SSL BIO
  BIO *m_bio;

  /// Remember the host name
  const std::string m_hostname;

  /// Remember the port
  const uint16_t m_port;

  /// Remember whether or not to use SSL
  const bool m_use_ssl;

  /// We keep our connection receive data buffer per connection - this
  /// is to allow for parsing of pipe-lined requests.
  std::vector<uint8_t> m_data;

  /// Send all data from buffer over network
  void tx(const std::vector<uint8_t> &data);

  /// Receive at least n bytes from network - append to given data
  /// buffer
  void rx(size_t n, std::vector<uint8_t> &data);

  /// Statistics for connection; counts of requests
  ///
  /// reqstat[0] - HEAD requests
  /// reqstat[1] - GET requests
  /// reqstat[2] - POST requests
  size_t m_reqstat[3];

  /// If we have a set of default credentials for Basic auth, these
  /// are set.
  Optional<std::string> m_defaultbasicuser;
  Optional<std::string> m_defaultbasicpass;


  //! For connection management - used by the execute() method. Report
  //! whether we are connected to a server.
  bool isConnected() const;

  //! Connect to server. If already connected, does nothing.
  void doConnect();

  //! Drop connection to server. If already not connected, does nothing.
  void dropConnection();



};

#endif
