//
/// Command processor
//
// $Id: command.hh,v 1.4 2013/10/16 08:49:25 sf Exp $
//

#ifndef SERVERBACKUP_COMMAND_HH
#define SERVERBACKUP_COMMAND_HH

#include "config.hh"
#include "engine.hh"

#include "common/thread.hh"
#include "common/mutex.hh"

#include <map>
#include <vector>

class Command : public Thread {
public:
  Command(Config &cfg, Engine &eng);
  ~Command();

  //! Signal command processor to exit
  Command &shutdown();

  //! Returns true if the main application should exit
  bool shouldExit() const;
  //! Configuration reference
  Config &m_cfg;

protected:
  //! Our command processor
  void run();

private:

  //! Engine reference
  Engine &m_eng;

  //! Our socket fd
  int m_sock;

  //! Out wake pipe for waking up poll
  int m_wakepipe[2];

  //! Should we exit?
  bool m_shouldexit;


  //! Client processor
  class Processor {
  public:
    Processor(Command &parent);

    //! Returns true when processor is ready to read
    bool shouldRead() const;

    //! Returns true when processor is ready to write
    bool shouldWrite() const;

    //! Called by poll loop when there is data to be read for this
    //! processor
    void read(int fd);

    //! Called by poll loop when there is data to be written for this
    //! processor
    void write(int fd);
    std::string checkForExistingDevice(const std::string& devn);
	std::string replaceStringWithString(const std::string& replaceableStr, const std::string& replacingWord, const std::string& replacingStr);

  private:
    //! Reference to our parent
    Command &m_parent;

    //! Our current command buffer that is being read into
    Mutex m_cmdbuf_mutex;
    std::string m_cmdbuf;

    //! Our current command output buffer that we post back
    Mutex m_outbuf_mutex;
    std::string m_outbuf;

    //! Call this method internally to respond with text to the user -
    //! this will do the necessary locking of outbuf.
    void respond(const std::string &str);

    //! Command processor - see if we have a full command and process
    //! it
    void processInput();

    //! Command: login
    void cmdLogin(std::vector<std::string> &);

    //! Command: help
    void cmdHelp(std::vector<std::string> &);

    //! Command: status
    void cmdStatus(std::vector<std::string> &);

    //! Command: newdev
    void cmdNewdev(std::vector<std::string> &);

  };

  //! Connected clients - map from socket fd to processor
  typedef std::map<int,Processor> clients_t;
  clients_t m_clients;


};



#endif
