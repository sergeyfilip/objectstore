//
//! \file httpd/test_httpd.cc
//! Regression test of the HTTPd functionality
//

#include "common/error.hh"
#include "common/trace.hh"
#include "common/thread.hh"
#include "common/mutex.hh"
#include "httpd.hh"
#include <iostream>
#include <cctype>
#include <string.h>
#include <unistd.h>

#ifdef __unix__
# include <sys/types.h>
# include <sys/socket.h>
# include <netinet/in.h>
# include <arpa/inet.h>
#endif

bool g_failed = false;

class MyWorker : public Thread {
public:
  MyWorker(HTTPd &httpd);
protected:
  //! Our actual worker thread
  void run();
private:
  //! Reference to the httpd (needed for getting requests and posting
  //! replies)
  HTTPd &m_httpd;
};

class MyClient : public Thread {
public:
  MyClient(uint16_t port);
  ~MyClient();
protected:
  //! Our client worker thread
  void run();
private:
  //! port to connect to
  uint16_t m_port;

  //! A simple test with a small mixed-case string where we just test
  //! if case is reversed
  void test_simple_nodelay();

  //! The same simple test as above, only with a delay
  void test_simple_delay();

  //! This method will send a request to the server. It will open the
  //! connection if we have none and it will re-use the existing
  //! connection if we have it.
  void send(const std::string &data);

  //! This method will POST the given body data to "/"
  void post_body(const std::string &data, int delay = 0);

  //! This method will sort-of parse the HTTP reply and return the
  //! body data. We are NOT fully HTTP 1.1 compliant here, we just
  //! need to parse what our own server sends...
  std::string receive_body();

  //! This method will perform a read and append data to m_inbound.
  void readmore();

#ifdef __unix__
  //! If we have an open connection, this is the fd. Otherwise it is
  //! -1.
  int m_fd;
#endif

  //! This is the buffer of unconsumed data we have read from the
  //! peer.
  std::string m_inbound;
};


int main(int argc, char **argv) try
{
  if (argc != 2) {
    std::cerr << "usage: test_httpd {port}" << std::endl;
    return 1;
  }

  trace::StreamDestination logstream(std::cerr);
  //  trace::Path::addDestination(trace::Debug, "*", logstream);
  trace::Path::addDestination(trace::Info, "*", logstream);
  trace::Path::addDestination(trace::Warn, "*", logstream);

  uint16_t port;
  { std::istringstream istr(argv[1]);
    istr >> port;
  }

  std::cerr << "HTTPd test" << std::endl;

  // Start HTTP server
  HTTPd httpd;
  httpd.addListener(port);

  // Start worker threads
  std::vector<MyWorker> workers(5, MyWorker(httpd));
  for (size_t i = 0; i != workers.size(); ++i)
    workers[i].start();

  // Start our test clients
  std::vector<MyClient> clients(10, MyClient(port));
  for (size_t i = 0; i != clients.size(); ++i)
    clients[i].start();

  std::cerr << "Clients running - waiting until they are done." << std::endl;

  // Wait until the clients exit
  for (size_t i = 0; i != clients.size(); ++i)
    clients[i].join_nothrow();

  // Stop the server (will begin issuing stop requests to workers)
  std::cerr << "Stopping httpd..." << std::endl;
  httpd.stop();

  // Wait until all workers have exited
  std::cerr << "Waiting for workers to shut down..." << std::endl;
  for (size_t i = 0; i != workers.size(); ++i)
    workers[i].join_nothrow();

  if (g_failed)
    std::cerr << "FAILED." << std::endl;
  else
    std::cerr << "PASSED." << std::endl;

} catch (error &e) {
  std::cerr << e.toString() << std::endl;
  return 1;
}



MyWorker::MyWorker(HTTPd &httpd)
  : m_httpd(httpd)
{
}

void MyWorker::run() try
{
  // Process requests until we are told to exit
  while (true) {
    HTTPRequest req(m_httpd.getRequest());
    if (req.isExitMessage()) {
      break;
    }
    std::cerr << ".";

    // Fine, process the request.
    //
    // 1: If request contains header "X-Sleep" then the worker must
    //    sleep for the specified number of microseconds
    //
    // 2: The case is reversed on all characters in the request and
    //    put back as response
    //
    if (req.hasHeader("X-Sleep")) {
      std::istringstream parser(req.getHeader("X-Sleep"));
      unsigned delay;
      parser >> delay;
      if (parser.fail())
        throw error("Cannot parse sleep header");
      usleep(delay);
    }

    std::string data;
    for (size_t i = 0; i != req.m_body.size(); ++i) {
      char c = req.m_body[i];
      if (isupper(c)) c = tolower(c);
      else if (islower(c)) c = toupper(c);
      data += c;
    }

    m_httpd.postReply(HTTPReply(req.m_id, true, 200, HTTPHeaders(), data));
  }
} catch (error &e) {
  std::cerr << "Worker caught: " << e.toString() << std::endl;
}


MyClient::MyClient(uint16_t port)
  : m_port(port)
  , m_fd(-1)
{
}

MyClient::~MyClient()
{
  if (m_fd != -1)
    close(m_fd);
}

void MyClient::run() try
{
  // Run a sequence of requests
  for (size_t i = 0; i != 100; ++i) {
    switch (i % 2) {
    case 0:
      test_simple_nodelay();
      break;
    case 1:
      test_simple_delay();
      break;
    }

  }
} catch (error &e) {
  g_failed = true;
  std::cerr << "Client failed: " << e.toString() << std::endl;
}

void MyClient::test_simple_nodelay()
{
  post_body("MiXeD CaSeS");

  const std::string reply = receive_body();
  if (reply != "mIxEd cAsEs")
    throw error("Simple test got bad reply (\""
                + reply + "\"");
}

void MyClient::test_simple_delay()
{
  post_body("MiXeD CaSeS deLAY", 10000); // sleep for 10ms

  const std::string reply = receive_body();
  if (reply != "mIxEd cAsEs DElay")
    throw error("Simple delay test got bad reply (\""
                + reply + "\"");
}

void MyClient::post_body(const std::string &data, int delay)
{
  std::ostringstream out;
  out << "POST / HTTP/1.1\r\n"
      << "host: foo\r\n"
      << "content-length: " << data.size() << "\r\n";
  if (delay)
    out << "x-sleep: " << delay << "\r\n";
  out << "\r\n"
      << data;
  send(out.str());
}

void MyClient::send(const std::string &data)
{
#ifdef __unix__
  if (m_fd == -1) {
    // Reset inbound too
    m_inbound.clear();
    // Create client socket
    m_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (m_fd < 0)
      throw syserror("socket", "creating client socket");
    // Connect to server
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(m_port);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    int rc = connect(m_fd, reinterpret_cast<struct sockaddr*>(&addr),
                     sizeof addr);
    if (rc < 0)
      throw syserror("connect", "connecting to server");
  }
  int rc = ::send(m_fd, data.data(), data.size(), 0);
  if (rc < 0)
    throw syserror("send", "sending request to server");
  if (size_t(rc) != data.size())
    throw error("Unable to send data to server");
#endif
}

std::string MyClient::receive_body()
{
  // Response looks like:
  //
  // HTTP/1.1 200 OK \r\n
  // content-length: 1234 \r\n
  // \r\n
  // body...

  // Read until we at least have response code
  while (m_inbound.find("\r\n") == m_inbound.npos)
    readmore();

  // Parse version
  { const std::string version
      = m_inbound.substr(0, m_inbound.find(' '));
    m_inbound.erase(0, version.size());
    if (version != "HTTP/1.1")
      throw error("Client received bad version");
  }

  // Parse status and skip rest of line
  { unsigned status;
    std::istringstream stin(m_inbound);
    stin >> status;
    if (stin.fail())
      throw error("Client cannot parse status code");
    if (status != 200)
      throw error("Got non-200 status code");
    m_inbound.erase(0, m_inbound.find("\r\n") + 2);
  }

  size_t body_size = 0;

  // Parse all headers
  while (true) {
    if (m_inbound.find("\r\n") == m_inbound.npos)
      readmore();

    std::string hline = m_inbound.substr(0, m_inbound.find("\r\n"));
    m_inbound.erase(0, hline.size() + 2);

    if (hline.empty())
      break;

    const std::string key = hline.substr(0, hline.find(':'));
    hline.erase(0, key.size() + 1);
    while (!hline.empty() && hline[0] == ' ')
      hline.erase(0, 1);

    // Is this the content-length?
    if (key == "content-length") {
      std::istringstream clparse(hline);
      clparse >> body_size;
      if (clparse.fail())
        throw error("Client cannot parse content length header");
    }

  }

  // Make sure we have the full body read
  while (m_inbound.size() < body_size)
    readmore();

  const std::string body(m_inbound.substr(0, body_size));
  m_inbound.erase(0, body_size);
  return body;
}


void MyClient::readmore()
{
#ifdef __unix__
  if (m_fd == -1)
    throw error("Cannot receive body when we have no connection");

  uint8_t buffer[8192];
  int rc = recv(m_fd, buffer, sizeof buffer, 0);

  if (rc < 0)
    throw syserror("recv", "receiving response from server");

  if (rc == 0)
    throw error("Server closed connection on us");

  m_inbound.append(buffer, buffer + rc);
#endif
}
