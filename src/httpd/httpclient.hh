///
/// HTTP 1.1 Client
///
// $Id: httpclient.hh,v 1.4 2013/08/29 13:35:43 joe Exp $
//

#ifndef HTTPD_HTTPCLIENT_HH
#define HTTPD_HTTPCLIENT_HH

#include "reply.hh"
#include "request.hh"

class HTTPclient {
public:
  /// Set up connection parameters for connection to HTTP server. This
  /// does not actually initiate a connection and as such the
  /// construction will not fail even when the remote HTTPd is not
  /// responding.
  HTTPclient(const std::string &hostname, uint16_t port);

  /// Cleanup
  ~HTTPclient();

  /// Return name of host
  const std::string &refHost() const;

  /// Return port
  uint16_t getPort() const;

  /// Transmit request and receive the response. If necessary,
  /// establish or re-establish connection to HTTPd. This function
  /// will throw on connection/communication errors.
  HTTPReply execute(const HTTPRequest &request);

  /// Copy construction - we copy the parameters but not the
  /// connection.
  HTTPclient(const HTTPclient &o);

private:
  /// Protect against assignment
  HTTPclient &operator=(const HTTPclient &);

  /// Our peer host name
  std::string m_peer;

  /// Our peer port
  uint16_t m_port;

  /// Our active connection (socket fd), or -1 if none
  int m_sock;

  /// Our receive buffer
  std::vector<uint8_t> m_data;

  /// Utility routine; if we do not have a connection we will attempt
  /// to establish one. Will throw on error. Does nothing if
  /// connection exists already.
  void reconnect();

  /// Utility routine; serialise request and transmit to server
  void transmitRequest(const HTTPRequest &request);

  /// Utility routine; read reply from server and de-serialise
  HTTPReply readReply(uint64_t request_id);

  /// Utility routine for reading - read at least n bytes from peer
  /// and append onto given buffer
  void rx(size_t n, std::vector<uint8_t> &buf);
};



#endif
