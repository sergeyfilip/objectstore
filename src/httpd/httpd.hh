//
//! \file httpd/httpd.hh
//! Our core HTTP 1.1 server
//
// $Id: httpd.hh,v 1.17 2013/05/03 08:12:47 joe Exp $
//

#ifndef HTTPD_HTTPD_HH
#define HTTPD_HTTPD_HH

#include "request.hh"
#include "reply.hh"

#include "common/semaphore.hh"
#include "common/mutex.hh"
#include "common/thread.hh"
#include "xml/xmlio.hh"

#include <list>
#include <deque>
#include <map>
#include <vector>
#include <stdint.h>

#if defined(__unix__) || defined(__APPLE__)
# include <poll.h>
# include <openssl/ssl.h>
#endif

class HTTPd {
public:
  //! Non-listening httpd setup.
  HTTPd();

  //! Destroy all associated resources
  ~HTTPd();

  //! Add listener on all addresses on specific port
  //
  //! \throws error on failure to bind
  HTTPd &addListener(uint16_t port);

  //! Set up SSL on this listener using the given certificate and key
  //! file
  HTTPd &startSSL(const std::string &certfile,
                  const std::string &keyfile);

  //! Set maximum number of connections
  //
  //! Call this method before starting processing
  HTTPd &setMaxConnections(size_t conns);

  //! Call this method to initiate a shutdown - shutdown messages will
  //! be returned to the worker threads and all connections and
  //! listening sockets will be shut down.
  void stop();

  //! This method is used by the worker threads. The method will block
  //! until a request becomes available. If the request has the HMNone
  //! method, it means that the worker thread should terminate.
  HTTPRequest getRequest();

  //! This method is used by the worker threads to push a response to
  //! a request back to the peer.
  void postReply(const HTTPReply &response);

  //! This method sends a final code 200 reply to a given request id
  //! in the form of an XML document
  void postReply(uint64_t id, const xml::IDocument &doc);

  //! For diagnostics: Return the number of requests in queue
  size_t getQueueLength(); // Not const because we take a mutex

  //! Returns true if a given request id was extracted by getRequest()
  //! but no final reply was posted with postReply()
  bool outstanding(uint64_t id);

  //! This function return true if anything is on the outqueue for the
  //! given request, or false if the outqueue has been emptied - note
  //! that this does not necessarily mean that the peer has actually
  //! received the data, it just means that the data have left our
  //! user space queue and at least entered the kernel network layer.
  bool outqueued(uint64_t id);

private:
  //! Mutex for general configuration information.
  Mutex m_conf_mutex;

  //! Set to true when we are shutting down (to signal the
  //! HandlerThreads to exit)
  bool m_exiting;
#if defined(__unix__) || defined(__APPLE__)
  //! The file descriptors of our listening sockets. Protected by the
  //! m_conf_mutex. The HTTPd may add new fds to this list but only
  //! the handler thread may actually close the fds.
  std::list<int> m_listeners;
  //! A pipe we use to wake up the handler thread. The handler thread
  //! poll() call watches [0] for reads and we can signal [1] to wake
  //! it during stop() or when data has been pushed for outbound.
  int m_wakepipe[2];
  //! Call this method to make the HTTPd exit its poll and re-consider
  //! the fd sets
  void restartPoll();
#endif

  //! Semaphore for our request queue.
  Semaphore m_requests_sem;
  //! Mutex that protects the request queue
  Mutex m_requests_mutex;
  //! Our actual request queue. Push to front, pop from back.
  std::deque<HTTPRequest> m_requests;
  //! This map holds requests that have been de-queued from the
  //! request queue (and therefore should be in processing by a
  //! worker) - the value of the map is the log sub-string for the
  //! request logging we do when the response is posted back.
  std::map<uint64_t,std::string> m_outstanding;

  //! Used internally to post a HTTP requests to our request queue
  //
  //! \param req  The request to push.
  void pushRequest(const HTTPRequest &req);

  //! Return our SSL context if any
  SSL_CTX *getCTX() const;

  //! \class HandlerThread
  //
  //! This is the implementation of our listening event handling
  //! thread.
  class HandlerThread : public Thread {
  public:
    HandlerThread(HTTPd &parent);

    //! This will close all open connections
    ~HandlerThread();

    //! The HTTPd calls this to post a reply to a request served by
    //! one of our processors
    void postReply(const HTTPReply &response);

    //! Whether or not the outqueue for a given request id is empty or
    //! not
    bool outqueued(uint64_t id);

    //! Tell the processor for the given request id to stop expecting
    //! the connection to be persistent (used when we de-queue a
    //! request with connection: close)
    void setNonpersistent(uint64_t id);

  protected:
    //! Our actual handler implementation
    void run();
  private:
    //! Our HTTPd parent
    HTTPd &m_parent;

    //! Mutex that protects the request sequence tracker
    Mutex m_request_id_mutex;
    //! Our request id counter. This is the value of the next request id
    uint64_t m_request_id;
    //! Call this method to allocate a request id
    uint64_t nextId();

    //! Return parent SSL context or 0
    SSL_CTX *getCTX() const;

    //! \class Processor
    //
    //! A request processor. This object parses requests from a
    //! peer. Whenever a complete request is consumed, it will post
    //! the request in the parent request queue.
    class Processor {
    public:
      //! We must be able to request new request-ids, therefore we
      //! need access to the handler thread. We must also be able to
      //! associate the connection fd with our SSL state.
      Processor(HandlerThread &ht);

      //! Copy construction cannot be done in all cases
      Processor(const Processor &);

      //! We may need to clean up an SSL context
      ~Processor();

      //! If we use SSL we will need to associate our SSL state with a
      //! file descriptor.
      void setFD(int fd);

      //! Tells the processor that when the final reply to the given
      //! request id is posted, the connection should start shutdown.
      void closeAfter(uint64_t id);

      //! Returns true if this socket underlying this connection
      //! should be closed
      bool shouldClose() const;

      //! Returns true if we want to be notified of possible reads on
      //! this connection
      bool shouldRead() const;

      //! Returns true if we want to be notified of possible writes on
      //! this connection
      bool shouldWrite() const;

      //! Returns true if anything is on the outqueue (not yet written
      //! to at least the OS network layer buffer)
      bool outqueued() const;

#if defined(__unix__) || defined(__APPLE__)
      //! Called by the handler when a read is possible on the given
      //! fd (which is the fd of this connection of course)
      //
      //! \throws error on connection error
      void read(int fd);

      //! Called by the handler when a write is possible on the given
      //! fd (which is the fd of this connection of course)
      //
      //! \throws error on connection error
      void write(int fd);
#endif

      //! The HandlerThread calls this to notify us that a request has
      //! been received and will be posted for the workers. We must
      //! expect to receive a response (or sequence of responses) to
      //! this id.
      //
      //! Note, the ids passed to this call must be strictly
      //! increasing.
      void requestActivated(uint64_t id);

      //! The HandlerThread calls this to post a reply to a request
      //! served by this processor
      void postReply(const HTTPReply &response);

      //! Call this method to receive a list of which request ids this
      //! processor is handling but has not yet serialized a final
      //! reply for
      void getActiveRequestIds(std::deque<uint64_t> &active);

      //! For diagnostics - print processor details
      std::string toString() const;

    private:
      //! Reference to our HandlerThread
      HandlerThread &m_handler;

      //! This is the buffer for in-bound data. The read() method will
      //! append data to this.
      std::string m_inbound;

      //! This variable is initially true. It tells us if we expect to
      //! receive more requests.
      bool m_readmore;

      //! We protect the outbound, req_wo_final and outqueue members
      //! because they are used both from the HTTPd thread context and
      //! from worker thread context
      mutable Mutex m_outbound_mutex;

      //! This is the buffer for out-bound data. The write() method
      //! will consume data from this.  We pop data from the front and
      //! push data to the back.
      std::deque<std::vector<uint8_t> > m_outbound;

      //! outbound buffer management (must hold m_outbound_mutex
      //! before calling) - remove given number of bytes - which must
      //! be less than or equal to the size of the front() element in
      //! m_outbound.
      void outboundConsumed(size_t);

      //! outbound buffer management (must hold m_outbound_mutex
      //! before calling) - return uint8_t* to next data to send, or 0
      //! if nothing to send
      const uint8_t *outboundNextData() const;

      //! outbound buffer management (must hold m_outbound_mutex
      //! before calling) - return size of next data to send (0 if
      //! nothing)
      size_t outboundNextSize() const;

      //! This variable holds the outstanding request ids that we have
      //! not yet serialized a final reply for
      std::deque<uint64_t> m_req_wo_final;
      //! These are the replies we have not yet serialized (this is
      //! used in HTTP 1.1 pipelining - the replies must be sent
      //! in-order)
      std::list<HTTPReply> m_outqueue;

      //! This method is called by the platform specific read()
      //! whenever we have new data in m_inbound.
      void processInbound();

      //! Where in the HTTP request reading are we?
      enum { R_ReadingRequest,
             R_ReadingBodyCL, // raw read with content-length
             R_ReadingBodyTE  // raw read chunked transfer-encoding
      } m_rawstate;

      //! When loading the body, this variable either holds the number
      //! of bytes left according to content-length, or, the number of
      //! bytes left in the current chunk.
      size_t m_bodyleft;

      //! Currently processing request
      HTTPRequest m_request;

      //! If SSL is enabled, this is our SSL state
      SSL *m_ssl;

      //! When an SSL connection is accepted, we need to run some
      //! handshaking during accept. When this variable is set, this
      //! handshake is not yet done.
      bool m_ssl_accepting;

      //! SSL may need to issue a write before it can service a read
      //! (for handshakes etc.) If this variable is set, the SSL layer
      //! wants to have a write serviced.
      bool m_ssl_needs_write;

      //! SSL may need to issue a read before it can service a write
      //! (for handshakes etc.). If this variable is set, the SSL layer
      //! wants to have a read serviced.
      bool m_ssl_needs_read;

      //! When an SSL connection is being closed, we need to run a
      //! shutdown.
      bool m_ssl_in_shutdown;

      //! This routine will attempt an SSL_accept() and set the
      //! m_ssl_needs_read, m_ssl_needs_write and m_ssl_accepting as
      //! necessary. This routine is called from read() and write().
      void processSSLAccept();

      //! This routine will attempt an SSL_shutdown() and set the
      //! m_ssl_needs_read, m_ssl_needs_write and m_ssl_in_shutdown as
      //! necessary. This routine is called from read() and write() -
      //! and initially from setClose()
      void processSSLShutdown();

      //! Tells the processor to not expect more data from this
      //! connection (will cause SSL shutdown followed by socket close
      //! when last response is sent).
      void setClose();

      //! If we have been asked to shut down the connection after a
      //! given request has finished processing (usually in response
      //! to a "connection: close" header), then we hold the id of the
      //! terminating request here
      uint64_t m_close_after_id;

    };

    //! Used internally to post a HTTP requests to our request queue
    //
    //! \param req  The request to push.
    //! \param proc The processor that handles this
    void pushRequest(const HTTPRequest &req, Processor *proc);

#if defined(__unix__) || defined(__APPLE__)
    //! Map from fd -> processor for all currently open connections
    typedef std::map<int,Processor> m_connections_t;
    m_connections_t m_connections;
    //! Close an fd, remove the fd/processor pair and also remove all
    //! references to the processor from the m_id_procs map.
    void removeProcessor(int fd);
#endif

    //! Worker threads will be posting replies to the processors
    Mutex m_id_procs_mutex;
    //! Map from request id into the processor that is processing said
    //! request id
    typedef std::map<uint64_t,Processor*> m_id_procs_t;
    m_id_procs_t m_id_procs;

  };

  //! Event handling thread
  HandlerThread m_handler;

  //! Our SSL context that all other contexts are cloned from
  SSL_CTX *m_ctx;

  //! Maximum number of live connections we must hold
  size_t m_max_connections;
};

#endif
