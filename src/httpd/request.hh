//
//! \file httpd/request.hh
//! Definition of our received HTTP request
//
// $Id: request.hh,v 1.9 2013/08/08 08:46:23 joe Exp $
//

#ifndef HTTPD_REQUEST_HH
#define HTTPD_REQUEST_HH

#include "headers.hh"

#include <string>
#include <stdint.h>

//! \class HTTPRequest
//
//! This is a HTTP request as parsed by the HTTPd. This request is
//! pushed by the HTTPd onto the request queue to be processed by
//! worker threads.
//
//! Note; this request structure also contains the body data (if any)
//! associated with the request, so it is not purely just a request in
//! the strict rfc2616 sense of the word. It is, however, exactly what
//! we need in order to queue work for our work processing threads.
//
class HTTPRequest {
public:
  enum method_t { mGET, mPOST, mPUT, mDELETE, mHEAD, mOPTIONS, mNONE };

  //! The empty HTTPRequest is used to notify a worker thread that it
  //! should exit.
  HTTPRequest();

  //! The normal HTTPRequest is constructed by the HTTPd and pushed
  //! onto the request queue. This call will parse the given
  //! request/header data.
  //
  //! \param id     The id of the request - used for reply posting
  //! \param data   The full request part of the data, not including \r\n\r\n
  HTTPRequest(uint64_t id,
              const std::string &data);

  //! During request URI parsing it is useful to "consume" URI
  //! components from the start of the URI and down. This naturally
  //! modifies the request. This function attempts to consume a URI
  //! component, and, if successful, removes it from m_uri and returns
  //! true. If unsuccessful, the HTTPRequest is unaltered and the
  //! function returns false.
  bool consumeComponent(const std::string &c);

  //! Like consumeComponent, except this function does not consume on
  //! match. It leaves the request unaltered.
  bool matchComponent(const std::string &c) const;

  //! We may also just want to consume the next component and return
  //! it, no matter what it is. This routine will consume the next
  //! component and return it.  We consume either until the end of the
  //! URI or until we meet a slash. We return everything up to but not
  //! including the slash.
  std::string getNextComponent();

  //! Some times one might want to save the current URI and
  //! potentially restore it. Use an object of this class to
  //! accomplish this
  class Backup {
  public:
    Backup(HTTPRequest& r) : m_rem(r.m_uri), m_req(r) { }
    void restore() const { m_req.m_uri = m_rem; }
  private:
    Backup(const Backup&);
    std::string m_rem;
    HTTPRequest& m_req;
  };

  //! Workers can call this to see if the request is an exit message
  bool isExitMessage() const;

  //! Call this method to append body data to the request
  void addBody(const std::string &data);

  //! Query whether given header (lower-case) exists in request
  bool hasHeader(const std::string &key) const;

  //! Request the value of a given header assuming it exists
  //
  //! \throws error if queried about non-existing header
  std::string getHeader(const std::string &key) const;

  //! Request the id of the request
  uint64_t getId() const;

  //! Request the URI of the request
  const std::string &getURI() const;

  //! Request the method of the request
  method_t getMethod() const;

  //! For parsers it can be useful to "pop" off a component of the
  //! URI. This method will modify the request by returning the string
  //! leading up to the first "/", and removing this.
  std::string popComponent();

  //! For diagnostics
  std::string toString() const;

  //! This method is called from the constructor and simply parses
  //! options ('?' followed by key=value pairs or stand-alone keys)
  //! and enters them in the options map.
  void parseOptions();

  //! Returns whether a given option was set
  bool hasOption(const std::string &key) const;

  //! Returns the value of the option - potentially the empty string
  //! if the option was given with no assignment or assigned an empty
  //! string (for 'foo' either "?foo=&bar=1" or "?foo&bar=1")
  //
  //! Throws error if queried about non-existing option.
  std::string getOption(const std::string &key) const;


  //! The id of the request
  uint64_t m_id;
  //! Remote-user of request
  std::string m_user;
  //! Method in request
  method_t m_method;
  //! URI upon which method is invoked
  std::string m_uri;
  //! Our headers
  HTTPHeaders m_headers;
  //! Body of request
  std::string m_body;

private:
  //! Request URI options map
  std::map<std::string,std::string> m_options;
};


//! A handler class for a URI endpoint. This one is used in the
//! Request URI parsing framework below.
class Endpoint {
public:
  virtual ~Endpoint() { }

  //! Handle a given request on this endpoint
  virtual void handle(const HTTPRequest &) const = 0;
};


template <typename T>
class UHandler;

//! Parser - the basic parser construct.
//
//! A Parser can consume data from the Request and cause an action
//! to be executed on successful parse.
template <class Derived>
class Parser {
public:
  virtual ~Parser() { }

  //! Process input. Returns true on successful processing,
  //! indicating that the request has been served. Returns false
  //! otherwise, indicating that the request has not been served.
  //! Note; this method will consume data from the Request if the
  //! processing is successful. If processing fails, no data is
  //! consumed from the Request.
  virtual bool process(HTTPRequest& in) const = 0;

  //! Compile-time up-cast (CRTP) used when combining parsers
  Derived& derived() {
    return *static_cast<Derived*>(this);
  }

  //! Compile-time up-cast (CRTP) used when combining parsers
  const Derived& derived() const {
    return *static_cast<const Derived*>(this);
  }

  //! Assign a handler to this URI component.
  UHandler<Derived> operator[](const Endpoint& handler) const {
    return UHandler<Derived>(derived(), handler);
  }

};


//! The UHandler class inherits from either UF or UD. It
//! acts like those two, but if process() succeeds it executes some
//! method.
template <class ParserT>
class UHandler : public Parser<UHandler<ParserT> > {
public:
  UHandler(const ParserT& p, const Endpoint& h)
    : parser(p), handler(h) { }

  //! This calls the parser we extend, to have the token
  //! processed. Then, if it is successfully processed, we will see
  //! if it was the last token on the URI. If that was the case,
  //! then we execute the handler we have.
  bool process(HTTPRequest& s) const;

private:
  const ParserT& parser;
  const Endpoint& handler;
};


//! Parser specialization that parses a sequence; "A/B"
template <class A, class B>
class Sequence : public Parser<Sequence<A, B> > {
public:
  Sequence(const A& a_, const B& b_) : a(a_), b(b_) { }

  //! A sequence has been successfully processed if its left and
  //! right components process.
  bool process(HTTPRequest& s) const;

private:
  const A a;
  const B b;
};


//! Parser specialization that parses an option; "A | B"
template <class A, class B>
class Option : public Parser<Option<A, B> > {
public:
  Option(const A& a_, const B& b_) : a(a_), b(b_) { }

  //! An Option has been successfully processed if its left
  //! component processes. If the left does not, the right is
  //! attempted, and the Option is successful if the right
  //! successfully processes. Otherwise the Option is not
  //! successful.
  bool process(HTTPRequest& s) const;

private:
  const A a;
  const B b;
};


//! Parser specialization used to match a Fixed URI element.
class UF : public Parser<UF> {
public:
  //! The argument is the fixed string we must match
  UF(const std::string& match_) : match(match_) { }

  //! See if current token matches the fixed match
  bool process(HTTPRequest& in) const;

private:
  //! The string we can match
  const std::string match;
};


//! Parser specialization used to match a Dynamic URI element.
class UD : public Parser<UD> {
public:
  //! The argument is the destination variable to which we will
  //! assign our match
  UD(std::string& dest_) : dest(dest_) { }

  //! Simply assign current token to destination - fail if we got no
  //! token...
  bool process(HTTPRequest& in) const;

private:
  //! Destination variable for whatever we match
  std::string& dest;
};

//! Parser specialization used to match an arbitrary (and possibly
//! empty) Path
class UP : public Parser<UP> {
public:
  //! The argument is the destination variable to which we will
  //! assign our match
  UP(std::string& dest_) : dest(dest_) { }

  //! Consume all tokens and append to destination.
  bool process(HTTPRequest& in) const;

private:
  //! Destination variable for whatever we match
  std::string& dest;
};


//! Utility operator; in order to create a sequence of URI
//! components, one can simply write "A / B"
template <class Left, class Right>
Sequence<Left,Right> operator/(const Parser<Left>&, const Parser<Right>&);


//! Utility operator; in order to create a option between URI
//! components, one can simply write "A | B"
template <class Left, class Right>
Option<Left,Right> operator|(const Parser<Left>&, const Parser<Right>&);

# include "request.hcc"

#endif
