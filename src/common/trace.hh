//
//! \file common/trace.hh
//! Logging framework for multi-level tracing throughout the code base
//
// $Id: trace.hh,v 1.8 2013/05/31 08:11:57 joe Exp $
//

#ifndef COMMON_TRACE_HH
#define COMMON_TRACE_HH

#include "mutex.hh"

#include <string>
#include <sstream>
#include <list>
#include <vector>


//! Use the MTrace macro for trace points throughout the code.
#define MTrace(path,level,output)                     \
  do {                                                \
    if (path.enabled(level)) {                        \
      std::ostringstream ostr;                        \
      ostr << output;                                 \
      path.log(level, ostr.str());                    \
    }                                                 \
  } while (false)

namespace trace {

  //! \class Destination
  //
  //! This is the declaration of the interface implemented by trace
  //! destinations.
  class Destination {
  public:
    //! Ensure proper destruction
    virtual ~Destination();

    //! A destination is a mechanism for outputting log messages.
    virtual void output(const std::string &str) = 0;
  };


  //! We define three levels: DEBUG, INFO and WARN.
  //
  //! The DEBUG level is used for low level tracing of algorithms and
  //! can be applied on an ad-hoc basis during development. It is
  //! intended for development and debugging use only.
  //
  //! The INFO level is to be used whenever the system makes a
  //! "significant" decision. For example, when the database
  //! initialization code concludes what the largest committed
  //! transaction sequence is.
  //
  //! The WARN level is to be used whenever the system encounters a
  //! situation that could indicate that there is or has been a
  //! problem with the software system as a whole (hardware, software,
  //! environment...). For example the WARN level can be used when the
  //! database initialization code encounters a malformed super block.
  enum level_t { Debug, Info, Warn };

  //! \class Path
  //
  //! This is the declaration of a trace path. Every trace path must
  //! be declared by means of a Path object. The Path object is
  //! referenced in the MTrace() calls and allows for very quick
  //! (in-line) log decisions
  //
  //! The idea is to allow for run time reconfiguration of
  //! logging/tracing so that a client could request all paths under
  //! "/tman/" or request logging of "/dbms/*" etc.
  //
  //! Aside from the path (which is most useful for debug tracing),
  //! the Path also defines a "level" so that a client can request the
  //! logging of "*",WARN.
  class Path {
  public:
    //! All paths are constructed with a '/' separated path, such as:
    //! Path("/tman/init").
    //
    //! \param name     The name of the trace path
    Path(const std::string &name);
    virtual ~Path() { }

    //! Called by the MTrace macro to determine whether anything
    //! should be logged via this path at the given level
    bool enabled(level_t l) const;

    //! Called by the MTrace macro if 'enabled' returned true. This
    //! will actually log the message to whatever clients requested
    //! it.
    void log(level_t l, const std::string &s) const;

    //! Used by the log client register/de-register code. Returns the
    //! m_next pointer of our Path chain.
    Path *getNext() const;

    //! Add a trace destination
    //
    //! \param level   The log level for which to add the destination
    //! \param patrn   The path pattern for which to add the destination
    //! \param dest    The destination to add
    //
    static void addDestination(level_t level, const std::string &patrn,
                               Destination& dest);

  private:
    //! Our local name
    const std::string m_name;

    //! Many threads can be calling trace functions - we protect our
    //! state with this mutex
    mutable Mutex m_conf_mutex;

    //! Chain of all Path objects
    static Path *m_pchain;
    //! Next pointer for next Path object...
    Path *m_next;

    //! Our list of Debug level destinations
    std::list<Destination*> m_debug_dests;
    //! Our list of Info level destinations
    std::list<Destination*> m_info_dests;
    //! Our list of Warn level destinations
    std::list<Destination*> m_warn_dests;

    //! This method returns true if this path matches the given
    //! pattern
    bool matches(const std::string &patrn) const;

    //! Add a destination for the given level
    void addDestination(level_t level, Destination *dest);
  };


  //! \class StreamDestination
  //
  //! This is a trace destination that outputs data on a given std
  //! ostream
  class StreamDestination : public Destination {
  public:
    //! Construct with the given stream (eg. std::cerr)
    StreamDestination(std::ostream &stream);

    //! Logging...
    void output(const std::string &str);
  private:
    //! The stream we output to
    std::ostream &m_stream;
  };



#if defined(__unix__) || defined(__APPLE__)
  // \class SyslogDestination
  //
  //! This is a trace destination that outputs data to syslog
  class SyslogDestination : public Destination {
  public:
    //! Construct with the given application short-name
    SyslogDestination(const std::string &appname);

    //! Cleanup
    ~SyslogDestination();

    //! Logging...
    void output(const std::string &str);

  private:
    //! syslog needs the name to live on in memory
    std::vector<char> namebuf;
  };
# endif

}


# include "trace.hcc"

#endif
