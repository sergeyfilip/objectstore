//
//! \file httpd/reply.hh
//! Definition of the HTTP reply that can be posted back
//
// $Id: reply.hh,v 1.9 2013/08/22 09:33:23 joe Exp $
//

#ifndef HTTPD_REPLY_HH
#define HTTPD_REPLY_HH

#include "headers.hh"

#include <string>
#include <list>
#include <vector>
#include <deque>
#include <stdint.h>

//! \class HTTPReply
//
//! This is the reply sent to a request.
//
//! Note, we may want to be able to be able to send data continually,
//! for example as a response to a Server-Sent Event GET. Therefore, a
//! reply that has a zero status code but content is used as a
//! "continuation" of a previously begun reply. Such a reply is not
//! valid as the first response after a request - we must have a
//! response first which defines the headers like the mime type and
//! status code.
//
class HTTPReply {
public:
  //! The default constructed reply has no use other than as sentinel
  //! in STL data structures.
  HTTPReply();

  //! The normal HTTPReply contains a status code, a mime type and
  //! content data.
  //
  //! \param id       The id of the request for which this is a response
  //! \param is_final This reply completes the response to the request id
  //! \param status   The HTTP status code (200=ok, ...)
  //! \param headers  Headers (status etc.)
  //! \param content  Body content
  HTTPReply(uint64_t id,
            bool is_final,
            uint16_t status,
            const HTTPHeaders &headers,
            const std::string &content);

  //! A continuation HTTPReply just contains body data. It must be
  //! preceded by a normal HTTPReply.
  //
  //! \param id      The id of the request for which this is a response
  //! \param is_final This reply completes the response to the request id
  //! \param content Body content (continued)
  HTTPReply(uint64_t id,
            bool is_final,
            const std::string &content);

  //! Return the id of the request we are a reply to
  uint64_t getId() const;

  //! Set the ID of the request
  void setId(uint64_t);

  //! Serialize reply data onto buffer (pushes a new buffer to the
  //! back of the given dequue)
  void serialize(std::deque<std::vector<uint8_t> > &) const;

  //! Returns whether or not this is the initial response (the one
  //! that contains the status code)
  bool isInitial() const;

  //! Returns whether or not this is a final request
  bool isFinal() const;

  //! Sets whether reply is final or not
  void setFinal(bool);

  //! Returns the status code
  uint16_t getStatus() const;

  //! Clear the status code - used when we post a reply we got as a
  //! client, as part of a chunked body we proxy back as a server
  void clearStatus();

  //! Reading: consume headers from given buffer - return true on
  //! success.
  bool consumeHeaders(std::vector<uint8_t> &);

  //! Reading: consume body from given buffer - return true on
  //! success.
  bool consumeBody(std::vector<uint8_t> &);

  //! Reading: Search haystack for needle, starting at haystack
  //! position pos. The pos will be incremented if needle is found,
  //! otherwise an exception is thrown.
  std::string consumeUntil(const std::string &needle,
                           const std::string &haystack,
                           std::string::const_iterator &pos);


  //! For diagnostics
  std::string toString() const;

  //! Reference the reply body
  const std::string &refBody() const;

  //! Reference to the reply headers
  HTTPHeaders &refHeaders();

private:
  //! id of the request to which we are a reply
  uint64_t m_id;

  //! Whether or not we are the final reply to the request id
  bool m_is_final;

  //! Status code
  uint16_t m_status;

  //! The headers
  HTTPHeaders m_headers;

  //! The body
  std::string m_body;
};



#endif
