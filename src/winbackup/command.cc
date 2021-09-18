///
/// Implementation of command processing
///
// $Id: command.cc,v 1.0 2013/09/19 13:50:14 sf Exp $
///
#pragma warning(push)
#pragma warning(disable: 4995)
// #include <locale>         // std::locale, std::isspace
#include "common/trace.hh"
#include <QMessageBox>
#include <QString>
#include "command.hh"
#include <stdio.h>
#include <ctype.h>
#pragma warning(pop)

namespace {
  trace::Path t_comm("/comm");
}

Command::Command() {
	for(int i =0; i < INSTANCES; i++) {

		Pipe[i] = PIPEINST();

	}
	
	MTrace(t_comm, trace::Info, "Tray command server started.");
}

Command::Command(Upload *eng, SvcCfg cf, LogOut* logout):engine(eng), m_cfg(cf),m_logout(logout)
{
	for(int i =0; i < INSTANCES; i++) {

		Pipe[i] = PIPEINST();

	}
	connect(this, SIGNAL(logout()), logout, SLOT(doLogout()));
	MTrace(t_comm, trace::Info, "Tray command server started.");
}


Command::~Command() {

}
void Command::run() {
   DWORD i, dwWait, cbRet, dwErr; 
   BOOL fSuccess; 
   LPTSTR lpszPipename = TEXT("\\\\.\\pipe\\mynamedpipe"); 
 
// The initial loop creates several instances of a named pipe 
// along with an event object for each instance.  An 
// overlapped ConnectNamedPipe operation is started for 
// each instance. 
 
   for (i = 0; i < INSTANCES; i++) 
   { 
 
   // Create an event object for this instance. 
 
      hEvents[i] = CreateEvent( 
         NULL,    // default security attribute 
         TRUE,    // manual-reset event 
         TRUE,    // initial state = signaled 
         NULL);   // unnamed event object 

      if (hEvents[i] == NULL) 
      {
         throw error("CreateEvent failed with %d.\n" + GetLastError()); 
         return;
      }
 
      Pipe[i].oOverlap.hEvent = hEvents[i]; 
 
      Pipe[i].hPipeInst = CreateNamedPipe( 
         lpszPipename,            // pipe name 
         PIPE_ACCESS_DUPLEX |     // read/write access 
         FILE_FLAG_OVERLAPPED,    // overlapped mode 
         PIPE_TYPE_MESSAGE |      // message-type pipe 
         PIPE_READMODE_MESSAGE |  // message-read mode 
         PIPE_WAIT,               // blocking mode 
         INSTANCES,               // number of instances 
         BUFSIZE*sizeof(TCHAR),   // output buffer size 
         BUFSIZE*sizeof(TCHAR),   // input buffer size 
         PIPE_TIMEOUT,            // client time-out 
         NULL);                   // default security attributes 

      if (Pipe[i].hPipeInst == INVALID_HANDLE_VALUE) 
      {
         throw error("CreateNamedPipe failed with %d.\n" + GetLastError());
         return;
      }
 
   // Call the subroutine to connect to the new client
 
      Pipe[i].fPendingIO = ConnectToNewClient( 
         Pipe[i].hPipeInst, 
         &Pipe[i].oOverlap); 
 
      Pipe[i].dwState = Pipe[i].fPendingIO ? 
         CONNECTING_STATE : // still connecting 
         READING_STATE;     // ready to read 
		 
   } 
 
   while (1) 
   { 
   // Wait for the event object to be signaled, indicating 
   // completion of an overlapped read, write, or 
   // connect operation. 
 
      dwWait = WaitForMultipleObjects( 
         INSTANCES,    // number of event objects 
         hEvents,      // array of event objects 
         FALSE,        // does not wait for all 
         INFINITE);    // waits indefinitely 
 
   // dwWait shows which pipe completed the operation. 
 
      i = dwWait - WAIT_OBJECT_0;  // determines which pipe 
      if (i < 0 || i > (INSTANCES - 1)) 
      {
         MTrace(t_comm, trace::Info,"Index out of range.\n"); 
         return;
      }
 
   // Get the result if the operation was pending. 
              
      if (Pipe[i].fPendingIO) 
      { 
         fSuccess = GetOverlappedResult( 
            Pipe[i].hPipeInst, // handle to pipe 
            &Pipe[i].oOverlap, // OVERLAPPED structure 
            &cbRet,            // bytes transferred 
            FALSE);            // do not wait 
 
         switch (Pipe[i].dwState) 
         { 
         // Pending connect operation 
            case CONNECTING_STATE: 
               if (! fSuccess) 
               {
                   MTrace(t_comm, trace::Info,"Error %d.\n" <<  GetLastError()); 
                   return;
               }
               Pipe[i].dwState = READING_STATE; 
               break; 
 
         // Pending read operation 
            case READING_STATE: 
               if (! fSuccess || cbRet == 0) 
               { 
                  DisconnectAndReconnect(i); 
                  continue; 
               }
			   
               Pipe[i].cbRead = cbRet;
               Pipe[i].dwState = WRITING_STATE; 
               break; 
 
         // Pending write operation 
            case WRITING_STATE: 
               if (! fSuccess || cbRet != Pipe[i].cbToWrite) 
               { 
                  DisconnectAndReconnect(i); 
                  continue; 
               } 
               Pipe[i].dwState = READING_STATE; 
               break; 
 
            default: 
            {
               throw error("Invalid pipe state.\n"); 
               return;
            }
         }  
      } 
 
   // The pipe state determines which operation to do next. 
 TCHAR *c=0; 
 
      switch (Pipe[i].dwState) 
      { 
      // READING_STATE: 
      // The pipe instance is connected to the client 
      // and is ready to read a request from the client. 
         
         case READING_STATE: 
            fSuccess = ReadFile( 
               Pipe[i].hPipeInst, 
               Pipe[i].chRequest, 
			   BUFSIZE*sizeof(TCHAR), 
               &Pipe[i].cbRead, 
		       &Pipe[i].oOverlap); 
      c = Pipe[i].chRequest;
	  
         // The read operation completed successfully. 
 
            if (fSuccess && Pipe[i].cbRead != 0) 
            { 
               
			   
			   Pipe[i].fPendingIO = FALSE; 
               Pipe[i].dwState = WRITING_STATE; 
			   continue; 
            } 
 
         // The read operation is still pending. 
 
            dwErr = GetLastError(); 
            if (! fSuccess && (dwErr == ERROR_IO_PENDING)) 
            { 
				
               Pipe[i].fPendingIO = TRUE; 
			   continue; 
            } 
 
         // An error occurred; disconnect from the client. 
 
            DisconnectAndReconnect(i); 
            break; 
 
      // WRITING_STATE: 
      // The request was successfully read from the client. 
      // Get the reply data and write it to the client. 
 
         case WRITING_STATE: 
               
			 
			 GetAnswerToRequest(&Pipe[i]); 
 
            fSuccess = WriteFile( 
               Pipe[i].hPipeInst, 
               Pipe[i].chReply, 
               Pipe[i].cbToWrite, 
               &cbRet, 
               &Pipe[i].oOverlap); 
 
         // The write operation completed successfully. 
 
            if (fSuccess && cbRet == Pipe[i].cbToWrite) 
            { 
               Pipe[i].fPendingIO = FALSE; 
               Pipe[i].dwState = READING_STATE; 
			   continue; 
            } 
 
         // The write operation is still pending. 
 
            dwErr = GetLastError(); 
            if (! fSuccess && (dwErr == ERROR_IO_PENDING)) 
            { 
               Pipe[i].fPendingIO = TRUE; 
			   continue; 
            } 
 
         // An error occurred; disconnect from the client. 
 
            DisconnectAndReconnect(i); 
            break; 
 
         default: 
         {
            throw error("Invalid pipe state.\n"); 
            return;
         }
      } 
  } 
 
  return; 
} 
 
 
// DisconnectAndReconnect(DWORD) 
// This function is called when an error occurs or when the client 
// closes its handle to the pipe. Disconnect from this client, then 
// call ConnectNamedPipe to wait for another client to connect. 
 
void Command::DisconnectAndReconnect(DWORD i) 
{ 
// Disconnect the pipe instance. 
 
   if (! DisconnectNamedPipe(Pipe[i].hPipeInst) ) 
   {
      MTrace(t_comm, trace::Info, "DisconnectNamedPipe failed with %d.\n" << GetLastError());
   }
 
// Call a subroutine to connect to the new client. 
 
   Pipe[i].fPendingIO = ConnectToNewClient( 
      Pipe[i].hPipeInst, 
      &Pipe[i].oOverlap); 
 
   Pipe[i].dwState = Pipe[i].fPendingIO ? 
      CONNECTING_STATE : // still connecting 
      READING_STATE;     // ready to read 
} 
 
// ConnectToNewClient(HANDLE, LPOVERLAPPED) 
// This function is called to start an overlapped connect operation. 
// It returns TRUE if an operation is pending or FALSE if the 
// connection has been completed. 
 
BOOL Command::ConnectToNewClient(HANDLE hPipe, LPOVERLAPPED lpo) 
{ 
   BOOL fConnected, fPendingIO = FALSE; 
 
// Start an overlapped connection for this pipe instance. 
   fConnected = ConnectNamedPipe(hPipe, lpo); 
 
// Overlapped ConnectNamedPipe should return zero. 
   if (fConnected) 
   {
      MTrace(t_comm, trace::Info, "ConnectNamedPipe failed with %d.\n" << GetLastError()); 
      return 0;
   }
 
   switch (GetLastError()) 
   { 
   // The overlapped connection in progress. 
      case ERROR_IO_PENDING: 
         fPendingIO = TRUE; 
         break; 
 
   // Client is already connected, so signal an event. 
 
      case ERROR_PIPE_CONNECTED: 
         if (SetEvent(lpo->hEvent)) 
            break; 
 
   // If an error occurs during the connect operation... 
      default: 
      {
         MTrace(t_comm, trace::Info, "ConnectNamedPipe failed with %d.\n" << GetLastError());
         return 0;
      }
   } 
 
   return fPendingIO; 
}

void Command::GetAnswerToRequest(PIPEINST *pipe)
{
	
	Processor *ps = new Processor(*this,pipe->chRequest);
			   ps->processInput();
			   delete(ps);
	_tprintf( "[%d] %s  %s\n", pipe->hPipeInst, pipe->chRequest, answer  );
	StringCchCopy( pipe->chReply, BUFSIZE, pipe->chRequest);                               //TEXT("gdefault answer") );
   pipe->cbToWrite = (lstrlen(pipe->chReply)+1)*sizeof(TCHAR);
   
  
}
void Command::add_answer(TCHAR *s) {

	wcscpy_s(answer,4096,s);
}

void Command::logoutt() {

	emit logout();
}
Command::Processor::Processor(Command &p, TCHAR* buffer)
	: m_parent(p)
{
  
	std::wstring str(buffer);
	std::string s((const char*)&str[0], sizeof(wchar_t)/sizeof(char)*str.size());
	m_cmdbuf = s;
	;
	//respond("Type \"?\" or \"help\" for help");
}

void Command::Processor::respond(TCHAR* s1)
{
	m_parent.add_answer(s1);
}

void Command::Processor::processInput()
{
  std::vector<std::string> tokens;
  std::locale loc;
  { 
QMessageBox msgBox;
QString ss(m_cmdbuf.c_str());
msgBox.setText(ss);
int ret = msgBox.exec();
	  size_t end = m_cmdbuf.length();
    if (end == 0)
      return;

    // Got command!
    std::string command = m_cmdbuf.substr(0, end);
    m_cmdbuf.erase(0, end + 1);

    // split up into tokens
    while (!command.empty()) {
      // Skip space
		
      while (!command.empty() && isspace(command[0]))
        command.erase(0, 1);

      // Now copy until space or eol
      std::string tmp;
      while (!command.empty() && !isspace(command[0])) {
        tmp.push_back(command[0]);
        command.erase(0, 1);
      }

      tokens.push_back(tmp);
    }
  }

  if (tokens.empty())
    return;

  //
  // Treat commands
  //

  if (tokens[0] == "quit") QApplication::quit();
  //  m_parent.shutdown();

  if (tokens[0] == "help" || tokens[0] == "?")
    cmdHelp(tokens);
  
  if (tokens[0] == "logout")
    cmdLogout(tokens);
 // if (tokens[0] == "login")
 //   cmdLogin(tokens);

  //if (tokens[0] == "status")
 //   cmdStatus(tokens);

  
}

void Command::Processor::cmdHelp(std::vector<std::string> &tokens)
{

	respond(L"help is ready");
}

void Command::Processor::cmdLogout(std::vector<std::string> &tokens) {

m_parent.logoutt();
}