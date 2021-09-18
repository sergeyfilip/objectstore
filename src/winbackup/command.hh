
/// Command h file
///
//
// $Id: command.hh,v 1.6 2013/09/18 11:48:02 sf Exp $
//

#ifndef WINBACKUP_COMMAND_H
#define WINBACKUP_COMMAND_H
#include <windows.h> 
#include <stdio.h>
#include <tchar.h>
#include <strsafe.h>
#include <iostream>
#include <vector>
#include <stdio.h>
#include <tchar.h>
#include <map>
#include "backup\upload.hh" 
#include "common/thread.hh"
#include "common/mutex.hh"
#include "svccfg.hh"
#include "tray.hh"
#include "logout.hh"
#include <QObject>
#include <QThread>
#include <QApplication>
#define CONNECTING_STATE 0 
#define READING_STATE 1 
#define WRITING_STATE 2 
#define INSTANCES 4 
#define PIPE_TIMEOUT 5000
#define BUFSIZE 4096



//structure for manage pipes

struct PIPEINST
{ 
   
   OVERLAPPED oOverlap; 
   HANDLE hPipeInst; 
   TCHAR chRequest[BUFSIZE]; 
   DWORD cbRead;
   TCHAR chReply[BUFSIZE];
   DWORD cbToWrite; 
   DWORD dwState; 
   BOOL fPendingIO; 
   

} ;
//clacc for geting remote commands (tokens) and processindthem

class Command: public QThread {
Q_OBJECT
public:

  PIPEINST Pipe[INSTANCES]; 
  HANDLE hEvents[INSTANCES]; 
  LPTSTR lpszPipename; 
	
  Command();
  Command(Upload *e, SvcCfg cfg,LogOut *lo);
  ~Command();
  void run();
  bool shouldExit() const;
  void add_answer(TCHAR *);

 signals:
	  void logout();
 private:
  
  TCHAR answer[4096];
  void DisconnectAndReconnect(DWORD); 
  BOOL ConnectToNewClient(HANDLE, LPOVERLAPPED); 
  void GetAnswerToRequest(PIPEINST *);
  void logoutt();
//! Should we exit?
  bool m_shouldexit;
  LogOut * m_logout;
 Upload * engine;
 SvcCfg m_cfg;
//! Client processor
  class Processor {
  public:
    Processor(Command &parent, TCHAR* buffer);
    void read(int fd);

    //! Called by poll loop when there is data to be written for this
    //! processor
    void write(int fd);
	//! Call this method internally to respond with text to the user -
    //! this will do the necessary locking of outbuf.
    void respond(TCHAR*);
	//! Command processor - see if we have a full command and process
    //! it
    void processInput();
  private:
    //! Reference to our parent
    Command &m_parent;

    //! Our current command buffer that is being read into
    
    std::string m_cmdbuf;

    //! Our current command output buffer that we post back
    
    std::string m_outbuf;

    

    

    //! Command: login
    void cmdLogin(std::vector<std::string> &);

    //! Command: help
    void cmdHelp(std::vector<std::string> &);

    //! Command: status
    void cmdStatus(std::vector<std::string> &);

    //! Command: newdev
    void cmdNewdev(std::vector<std::string> &);

	void cmdLogout(std::vector<std::string> &);

  };



};

#endif
