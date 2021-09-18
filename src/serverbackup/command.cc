//
/// Command processor implementation
//
// $Id: command.cc,v 1.10 2013/10/17 14:29:20 sf Exp $
//

#include "command.hh"
#include "common/error.hh"
#include "common/trace.hh"
#include "common/string.hh"
#include "client/serverconnection.hh"
#include "xml/xmlio.hh"

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>

#include <vector>

namespace {
  //! Tracer for command parser operations
  trace::Path t_cmd("/kservd/cmd");
}

Command::Command(Config &cfg, Engine &eng)
  : m_cfg(cfg)
  , m_eng(eng)
  , m_sock(-1)
  , m_shouldexit(false)
{
  // Create the wake pipe
  int rc = pipe(m_wakepipe);
  if (rc < 0)
    throw syserror("pipe", "creation of wake pipe");

  // Create the socket
  m_sock = socket(AF_UNIX, SOCK_STREAM, 0);
  if (m_sock == -1)
    throw syserror("socket", "creating command socket");

  // See if path exists and is a socket - in that case, unlink it.
  { struct stat sbuf;
    int rc = lstat(cfg.m_cmdsocket.c_str(), &sbuf);
    if (rc && errno != ENOENT)
      throw syserror("lstat", "looking up command socket \""
                     + cfg.m_cmdsocket + "\"");
    if (!rc && S_ISSOCK(sbuf.st_mode))
      unlink(cfg.m_cmdsocket.c_str());
  }

  // Create socket with permissions 0600
  struct sockaddr_un addr;
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, cfg.m_cmdsocket.c_str(), sizeof addr.sun_path);
  addr.sun_path[sizeof addr.sun_path - 1] = 0; // guarantee zero termination
  rc = bind(m_sock, reinterpret_cast<const sockaddr*>(&addr), sizeof addr);
  if (rc)
    throw syserror("bind", "binding socket to command socket \""
                   + cfg.m_cmdsocket + "\"");

  // Now listen on it
  rc = listen(m_sock, 5);
  if (rc)
    throw syserror("listen", "listening on command socket");
}

Command::~Command()
{
  // Make our thread exit first.
  join_nothrow();

  // Close our socket
  close(m_sock);

  // Destroy the wake pipe
  close(m_wakepipe[0]);
  close(m_wakepipe[1]);
}

bool Command::shouldExit() const
{
  return m_shouldexit;
}

Command &Command::shutdown()
{
  // We want to exit
  m_shouldexit = true;

  // Wake up poll to make thread realise we should exit
  char tmp(42);
  int rc;
  do {
    rc = write(m_wakepipe[1], &tmp, sizeof tmp);
  } while (rc == -1 && errno == EINTR);
  if (rc != sizeof tmp)
    throw syserror("write", "waking Command processor poll");

  return *this;
}


void Command::run()
{
  // Accept connections and process commands...
  while (!shouldExit()) {
    //
    // Set up fd array for poll
    //
    std::vector<struct pollfd> fds;

    // Add listen socket
    fds.push_back(pollfd());
    fds.back().fd = m_sock;
    fds.back().events = POLLIN;
    fds.back().revents = 0;

    // Add wakepipe
    fds.push_back(pollfd());
    fds.back().fd = m_wakepipe[0];
    fds.back().events = POLLIN;
    fds.back().revents = 0;

    // Add socket clients
    for (clients_t::const_iterator i = m_clients.begin();
         i != m_clients.end(); ++i) {
      fds.push_back(pollfd());
      fds.back().fd = i->first;
      fds.back().events = (i->second.shouldWrite() ? POLLOUT : 0)
        | (i->second.shouldRead() ? POLLIN : 0);
      fds.back().revents = 0;
    }

    // Now poll
    int prc;
    do { prc = poll(&fds[0], fds.size(), -1); }
    while (prc == -1 && errno == EINTR);

    if (prc == -1) {
      MTrace(t_cmd, trace::Warn, "Poll failed (errno = " << errno << ") - will quit");
      m_shouldexit = true;
      return;
    }

    // Fine, treat results.
    for (std::vector<struct pollfd>::const_iterator i = fds.begin();
         i != fds.end(); ++i) {
      // If no events are ready for fd, don't bother
      if (!i->revents)
        continue;

      // Was this a new connection?
      if (i->fd == m_sock && (i->revents & POLLIN)) {
        // Accept connection and start up processor
        struct sockaddr_un sadr;
        socklen_t adrlen(sizeof sadr);
        int arc;
        do { arc = accept(m_sock, reinterpret_cast<struct sockaddr*>(&sadr), &adrlen); }
        while (arc == -1 && errno == EINTR);
        if (arc == -1) {
          MTrace(t_cmd, trace::Info, "Accepting connection failed: errno = "
                 << errno);
          continue;
        }
        // Fine, we got the connection
        m_clients.insert(std::make_pair(arc, Processor(*this)));
        MTrace(t_cmd, trace::Info, "Accepted connection");
        continue;
      }

      // Was this the wake pipe
      if (i->fd == m_wakepipe[0] && (i->revents & POLLIN)) {
        // Empty pipe
        char buf[8];
        while (-1 == read(m_wakepipe[0], buf, sizeof buf)
               && errno == EINTR);
        continue;
      }

      // See if it is a processor we recognise
      clients_t::iterator cli = m_clients.find(i->fd);
      if (cli != m_clients.end()) {
        try {
          if (i->revents & POLLIN)
            cli->second.read(i->fd);
          if (i->revents & POLLOUT)
            cli->second.write(i->fd);
          continue;
        } catch (error &e) {
          MTrace(t_cmd, trace::Info, "Error when processing client: "
                 << e.toString());
          // Remove from clients
          close(i->fd);
          m_clients.erase(cli);
          continue;
        }
      }

      // No - we don't recognise this fd
      MAssert(false, "Unrecognised fd in poll loop");
    }

    // Go again.
  }
  MTrace(t_cmd, trace::Info, "Command processor exiting.");
}

Command::Processor::Processor(Command &p)
  : m_parent(p)
{
  respond("Type \"?\" or \"help\" for help");
}

bool Command::Processor::shouldRead() const
{
  return true;
}

bool Command::Processor::shouldWrite() const
{
  return !m_outbuf.empty();
}

void Command::Processor::read(int fd)
{
  char buf[1024];
  int rc = recv(fd, buf, sizeof buf, 0);
  if (rc == 0)
    throw error("Client closed connection");

  if (rc == -1 && errno == EINTR)
    return;

  if (rc == -1)
    throw syserror("recv", "reading from client");

  { MutexLock l(m_cmdbuf_mutex);
    m_cmdbuf.append(buf, rc);
  }

  try { processInput(); }
  catch (error &e) { respond(e.toString()); }
}

void Command::Processor::write(int fd)
{
  MutexLock l(m_outbuf_mutex);

  int wrc = send(fd, m_outbuf.data(), m_outbuf.size(), 0);
  if (wrc == -1 && errno == EINTR)
    return;

  if (wrc == -1)
    throw syserror("send", "writing to client");

  m_outbuf.erase(0, wrc);
}

void Command::Processor::respond(const std::string &str)
{
  MutexLock l(m_outbuf_mutex);
  m_outbuf += str + "\n";
}

void Command::Processor::processInput()
{
  std::vector<std::string> tokens;

  { MutexLock l(m_cmdbuf_mutex);

    size_t end = m_cmdbuf.find('\n');
    if (end == m_cmdbuf.npos)
      return;

    // Got command!
    std::string command = m_cmdbuf.substr(0, end);
    m_cmdbuf.erase(0, end + 1);

    // split up into tokens
    while (!command.empty()) {
      // Skip space
      while (!command.empty() && std::isspace(command[0]))
        command.erase(0, 1);

      // Now copy until space or eol
      std::string tmp;
      while (!command.empty() && !std::isspace(command[0])) {
        tmp.push_back(command[0]);
        command.erase(0, 1);
      }

      tokens.push_back(tmp);
    }
  }

  if (tokens.empty())
    return;

  MTrace(t_cmd, trace::Info, "Got command \"" << *tokens.begin() << "\"");

  //
  // Treat commands
  //

  if (tokens[0] == "quit")
    m_parent.shutdown();

  if (tokens[0] == "help" || tokens[0] == "?")
    cmdHelp(tokens);

  if (tokens[0] == "login")
    cmdLogin(tokens);

  if (tokens[0] == "status")
    cmdStatus(tokens);

  if (tokens[0] == "newdev")
    cmdNewdev(tokens);

  if (tokens[0] == "backup") {
    m_parent.m_eng.submit(Engine::CBACKUP);
    respond("Submitted backup command to engine");
  }

}


void Command::Processor::cmdLogin(std::vector<std::string> &tokens)
{
  // We must have two more arguments; login and password
  if (tokens.size() != 3) {
    respond("Usage: login {username} {password}");
    return;
  }

  // We access config data
  MutexLock cfglock(m_parent.m_cfg.m_lock);
  if (m_parent.m_cfg.m_apihost.find("ws.keepit.com") != std::string::npos) { //real old server
  //
  // Fine, send to server
  //
  ServerConnection conn(m_parent.m_cfg.m_apihost, 443, true);
  ServerConnection::Request req(ServerConnection::mPOST, "/tokens/");
  req.setBasicAuth(tokens[1], tokens[2]);

  { using namespace xml;
    std::string descr("serverbackup");
    { char hname[1024];
      if (gethostname(hname, sizeof hname))
        throw syserror("gethostname", "retrieving local host name");
      descr +=  "on " + std::string(hname);
    }

    m_parent.m_cfg.m_token = randStr(16);
    m_parent.m_cfg.m_password = randStr(16);
    std::string ttype("Device");

    const IDocument &doc = mkDoc
      (Element("token")
       (Element("descr")(CharData<std::string>(descr))
        & Element("type")(CharData<std::string>(ttype))
        & Element("aname")(CharData<Optional<std::string> >(m_parent.m_cfg.m_token))
        & Element("apass")(CharData<Optional<std::string> >(m_parent.m_cfg.m_password))));

    req.setBody(doc);
  }

  ServerConnection::Reply rep = conn.execute(req);
  if (rep.getCode() == 201) {
	  m_parent.m_cfg.m_device_id = Optional<std::string>();
    m_parent.m_cfg.m_user_id = Optional<std::string>();
    m_parent.m_cfg.write();
    respond("Access token acquired and stored");
  } else if (rep.getCode() == 401) {
    respond("Authentication failed.");
    m_parent.m_cfg.m_token = Optional<std::string>();
    m_parent.m_cfg.m_password = Optional<std::string>();
    m_parent.m_cfg.m_device_id = Optional<std::string>("");
    m_parent.m_cfg.m_user_id = Optional<std::string>("");
  } else {
    respond(rep.toString());
    m_parent.m_cfg.m_token = Optional<std::string>();
    m_parent.m_cfg.m_password = Optional<std::string>();
    m_parent.m_cfg.m_device_id = Optional<std::string>("");
    m_parent.m_cfg.m_user_id = Optional<std::string>("");
  }

} else { //new server
//
  // Fine, send to server
  //
  
  ServerConnection conn(m_parent.m_cfg.m_apihost, 443, true);
  ServerConnection::Request req(ServerConnection::mPOST, "/tokens/");
  req.setBasicAuth(tokens[1], tokens[2]);

  { using namespace xml;
    std::string descr("serverbackup");
    { char hname[1024];
      if (gethostname(hname, sizeof hname))
        throw syserror("gethostname", "retrieving local host name");
      descr +=  "on " + std::string(hname);
    }

    m_parent.m_cfg.m_token = randStr(16);
    m_parent.m_cfg.m_password = randStr(16);
    std::string ttype("Device");

    const IDocument &doc = mkDoc
      (Element("token")
       (Element("descr")(CharData<std::string>(descr))
        & Element("type")(CharData<std::string>(ttype))
        & Element("aname")(CharData<Optional<std::string> >(m_parent.m_cfg.m_token))
        & Element("apass")(CharData<Optional<std::string> >(m_parent.m_cfg.m_password))));

    req.setBody(doc);
  }

  ServerConnection::Reply rep = conn.execute(req);
  if (rep.getCode() == 201) {
//////////////////////////
                    std::string p_id, id;
                    ServerConnection::Request req(ServerConnection::mGET, "/users/");

                    req.setBasicAuth(tokens[1], tokens[2]);
                    
                    using namespace xml;
                    const IDocument &doc = mkDoc
                                           (Element("user")
                                            (
                                                Element("id")(CharData<std::string>(p_id))

                                            )
                                           );

                    req.setBody(doc);
                    
                    try { //Execute request with catching connection errors
                        rep = conn.execute(req);
                    }

                    catch (error &e) {
                        
                         MTrace(t_cmd, trace::Info, "Cannot connect to internet (errno = " << errno << ") - will quit");
                        
                        return;
                    }

                    
                    using namespace xml;
                    const IDocument &ddoc = mkDoc(Element("user")
                                                  (Element("id")(CharData<std::string>(id))
                                                  ));

                    try {


                        std::istringstream body(std::string(rep.refBody().begin(), rep.refBody().end()));
                        XMLexer lexer(body);
                        ddoc.process(lexer);
                        
                        m_parent.m_cfg.m_user_id = id;

                       // m_cfg.m_device_id = GetDeviceId();
                    } catch (error &e) {
                        std::string ddd(std::string(rep.refBody().begin(), rep.refBody().end()));
                        

                        return;
                    }
/////////////////////////////////
    m_parent.m_cfg.write();
    respond("Access token acquired and stored");
  } else if (rep.getCode() == 401) {
    respond("Authentication failed.");
    m_parent.m_cfg.m_token = Optional<std::string>();
    m_parent.m_cfg.m_password = Optional<std::string>();
  } else {
    respond(rep.toString());
    m_parent.m_cfg.m_token = Optional<std::string>();
    m_parent.m_cfg.m_password = Optional<std::string>();
  }

} // end else
}

void Command::Processor::cmdHelp(std::vector<std::string> &tokens)
{
  if (tokens.size() <= 1) {
    respond("Commands available:");
    respond("status:       print current status");
    respond("login:        log on to Keepit account");
    respond("newdev:       create new device under account");
    respond("backup:       initiate a backup");
    respond("quit:         shut down server backup service");
    return;
  }

  if (tokens[1] == "login") {
    respond("Usage:   login  {username} {password}");
    respond("");
    respond("Use the login command to authenticate against your");
    respond("on-line Keepit account.");
    respond("The login process will create a new set of credentials");
    respond("that will be used exclusively by this device, so your");
    respond("username and password is not stored locally on this system.");
    return;
  }

  if (tokens[1] == "newdev") {
    respond("Usage:  newdev [devname]");
    respond("");
    respond("Use the newdev command to create a new device under the");
    respond("currently authenticated account. If no device name is supplied");
    respond("the host name of this local system will be used.");
    return;
  }

  respond("No help for that");
}



void Command::Processor::cmdStatus(std::vector<std::string> &tokens)
{
  respond("Status:");

  // We access config data
  MutexLock cfglock(m_parent.m_cfg.m_lock);

  // Do we have credentials?
  if (m_parent.m_cfg.m_token.isSet()) {
    respond(" credentials set");
  } else {
    respond(" credentials missing - please use \"login\"");
  }

  // Do we have a device?
  if (m_parent.m_cfg.m_device.isSet()) {
    respond(" device name is \"" + m_parent.m_cfg.m_device.get() + "\"");
  } else {
    respond(" device not set - please use \"newdev\" or \"setdev\"");
  }

  // What is the engine doing?
  respond(" engine status: " + m_parent.m_eng.status_en());
}


void Command::Processor::cmdNewdev(std::vector<std::string> &tokens)
{
  // Figure out new devicename
  std::string devname;
  if (tokens.size() == 2) {
    devname = tokens[1];
    respond("Using supplied device name: \"" + devname + "\"");
  } else {
    char hname[1024];
    if (gethostname(hname, sizeof hname))
      throw syserror("gethostname", "retrieving local host name");
    devname = hname;
    respond("Using local host name: \"" + devname + "\"");
  }

  // We access config data
  MutexLock cfglock(m_parent.m_cfg.m_lock);
if (m_parent.m_cfg.m_apihost.find("ws.keepit.com") != std::string::npos) { //real old server
  // Now ask server to create this
  ServerConnection conn(m_parent.m_cfg.m_apihost, 443, true);
  ServerConnection::Request req(ServerConnection::mPOST, "/devices/");
  req.setBasicAuth(m_parent.m_cfg.m_token.get(),
                   m_parent.m_cfg.m_password.get());

  using namespace xml;
  const IDocument &doc = mkDoc
    (Element("pc")
     (Element("name")(CharData<std::string>(devname))));

  req.setBody(doc);

  ServerConnection::Reply rep = conn.execute(req);
  switch (rep.getCode()) {
  case 201:
    respond("Successfully created and set new device");
    break;
  case 409:
    respond("Device name already exists - re-using");
    break;
  default:
    respond("Got error: " + rep.toString());
    return;
  }

  // Success!
  m_parent.m_cfg.m_device = devname;
  m_parent.m_cfg.m_device_id = Optional<std::string>("");
    m_parent.m_cfg.m_user_id = Optional<std::string>("");
  m_parent.m_cfg.write();

}
else {
////////////////////////////////New server
 {   MTrace(t_cmd, trace::Info, "Start creating device.\n");
	    
        // Attempt creation
	std::string s1 ("/users/");
	std::string s2 = ("/devices/");
	std::string s3(m_parent.m_cfg.m_user_id.get());
        std::string path = s1+s3+s2;

         ServerConnection conn(m_parent.m_cfg.m_apihost, 443, true);
        ServerConnection::Request req(ServerConnection::mPOST, path);
       req.setBasicAuth(m_parent.m_cfg.m_token.get(),
                   m_parent.m_cfg.m_password.get());
        using namespace xml;
        
        const IDocument &doc =
            mkDoc(Element("pc")
                  (Element("name")(CharData<Optional<std::string> >(m_parent.m_cfg.m_device))));
    
        req.setBody(doc);



	ServerConnection::Reply rep;
        //Use connection again
        try {
             rep = conn.execute(req);
        }
        catch (error &e) {
           
            MTrace(t_cmd, trace::Debug, "Network error"  << "\n" );
            return;
        }

        switch (rep.getCode()) {
            case 409:
                
                
                MTrace(t_cmd, trace::Info, "Device already exists" << rep.readHeader() <<  "hhe");
                MTrace(t_cmd, trace::Info, "Device id....." << m_parent.m_cfg.m_device_id.get() << "\n"  );
				m_parent.m_cfg.m_device_id = checkForExistingDevice(m_parent.m_cfg.m_device.get());
				MTrace(t_cmd, trace::Info, "Device id after....." << m_parent.m_cfg.m_device_id.get() << "\n"  );
				if(m_parent.m_cfg.m_device_id.get() == "") { MTrace(t_cmd, trace::Debug,"Cannot get divice id!"); return; }
				
                m_parent.m_cfg.write();
                break;
            case 201:
                MTrace(t_cmd, trace::Info, "Device created" << rep.readHeader() <<"hh");
				
                m_parent.m_cfg.m_device_id = rep.readHeader();
				 
                m_parent.m_cfg.write();
                break;
            default:
                MTrace(t_cmd, trace::Info, "Error creating device: " << rep.toString());
                m_parent.m_cfg.m_device = std::string();
                m_parent.m_cfg.m_user_id = std::string();
                m_parent.m_cfg.m_device_id = std::string();
                return;
        }
    }
// Success!
  m_parent.m_cfg.m_device = devname;
  m_parent.m_cfg.write();		
}
}
////////
std::string Command::Processor::checkForExistingDevice(const std::string& devn)
{
    
    ServerConnection conn(m_parent.m_cfg.m_apihost, 443, true);
    ServerConnection::Request req(ServerConnection::mGET, "/users/" + m_parent.m_cfg.m_user_id.get() + "/devices/");
    req.setBasicAuth(m_parent.m_cfg.m_token.get(),
                   m_parent.m_cfg.m_password.get());
    
    ServerConnection::Reply rep = conn.execute(req);
    
    
    if (rep.getCode() != 200) {
        MTrace(t_cmd, trace::Warn, "Failed to get list of devices: " + rep.toString());
        
        return "";
    }
    
    std::string responce(rep.refBody().begin(), rep.refBody().end());
    MTrace(t_cmd, trace::Info, "Start looking for devices response" <<  responce <<"\n" );
    std::size_t found = responce.rfind(devn);
    
    if (found!=std::string::npos) {
        
        std::string substring = responce.substr (0,found);
        std::string replacing ("<guid>");
        
        found = substring.rfind(replacing, found-1);
        std::string thirdIteration = substring.substr(found);
        
        std::string deviceUID;
        
        std::string strArr[] = {"<guid>", "</guid>", "<name>"};
        
        std::vector<std::string> strVec(strArr, strArr + 3);   
        
        for (int i = 0; i < (int)strVec.size(); i++) {
            std::string str = strVec[i];
            thirdIteration = replaceStringWithString(thirdIteration, str, "");
        }
        
        deviceUID = thirdIteration;
        MTrace(t_cmd, trace::Info, "Start looking for devices return....   -" <<  deviceUID <<"\n" );
        return deviceUID;
    } 
    
    
    return "";
}

std::string Command::Processor::replaceStringWithString(const std::string& replaceableStr, const std::string& replacingWord, const std::string& replacingStr)
{
    std::string strToReplace = replaceableStr;
    std::string strReplacing = replacingStr;
    std::string wordReplacing =replacingWord;
    
    std::size_t found;
    found = replaceableStr.rfind(wordReplacing);
    if (found != std::string::npos) strToReplace.replace (found, wordReplacing.length(), strReplacing);
    
    return strToReplace;
} 
