///
/// Implementation of our sign in or sign up dialogue
///
// $Id: signin.cc,v 1.31 2013/10/17 14:43:53 sf Exp $
///

#include "signin.hh"
#include "common/partial.hh"
#include <vector>
#include <QImage>
#include <QLabel>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPixmap>
#include <QMessageBox>
#include <QApplication>
#include "common/error.hh"
#include "xml/xmlio.hh"
#include "common/string.hh"
#include "client/serverconnection.hh"
#include "common/trace.hh"
#include <QTabWidget>
#include <QProcess>
//signing module

namespace {
  trace::Path t_signin("/signin");
}


SignIn::SignIn(SvcCfg &cfg,std::string ad)
  : m_cfg(cfg)
  , m_adata(ad)
  , m_signin_toptext(new QLabel(this))
  , m_signin_go(0)
  , m_topbar(new QLabel(this))
  , m_progress(new QLabel(this))
  , m_err(new QLabel(this))
  , m_activities(new QGroupBox)
  , m_defaulturl("ws.keepit.com")
  , m_testurl("ws-test.keepit.com")
  
{
  
  if(m_cfg.m_uname.empty()) {
  if( remove((m_adata + "\\cache.db").c_str()) != 0) MTrace(t_signin, trace::Debug, "Cannot remove cache file.");
  }
  // We can control Dialog window in such manner
  // this->setWindowFlags(Qt::FramelessWindowHint);

  this->setWindowFlags(Qt::WindowCloseButtonHint);
  setWindowTitle(tr("Keepit Login"));
  this->setFixedSize(520,270);
    //Logo over form
    m_logo = new QLabel(this);
    m_logo->resize(1535 / 4, 298 / 4); // image dimensions; 
       m_logo->move(0, 0);
    m_logo->setPixmap(QPixmap(":/biglogo1.png").scaled(m_logo->width(), m_logo->height(),
						     Qt::KeepAspectRatio,
		   				    Qt::SmoothTransformation));
    // Dark green bend with slogan
    m_topbar->setTextFormat(Qt::RichText);
    m_topbar->setStyleSheet("QLabel { border: none; margin: 0px; padding: 6px; background-color: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 rgba(87,102,90,255), stop:1 rgba(58,68,60,255));}");
    m_topbar->setFixedSize(520,40);
    //    m_topbar->setText("<span style='color: #e39625; font-size: x-large; "
    //                          "font-weight: bold;'>                           KeepIt - Securing your data.</span>");
    QString s("<span style='color: #e39625; font-size: x-large; font-weight: bold;'></span>");
       m_topbar->setText(s);
   
    

    QHBoxLayout *horizontalLayout = new QHBoxLayout; 

    horizontalLayout->addWidget(m_topbar);
    //Dark yellow bend
    m_progress->setTextFormat(Qt::RichText);
    m_progress->setStyleSheet("QLabel { background-color: qlineargradient(x1:0, y1:0, x2:.5, y2:0, x3:1, y3:0, stop:0 rgba(234,171,60,255), stop:1 rgba(227,150,37,255), stop:0 rgba(234,171,60,255)); };");
    m_progress->move(0,40);
    m_progress->setFixedSize(520,8);

    // Main widget with tabs in center of login screen
    m_tabwidget = new QTabWidget(this);
    m_tabwidget->setFixedSize(466,164);
    m_tabwidget->move(26,67);

    //    QPalette pal = m_tabwidget->palette();
    //    pal.setColor(m_tabwidget->backgroundRole(), Qt::blue);
    //    m_tabwidget->setPalette(pal);

    //    m_tabwidget->setStyleSheet("QTabWidget::tab-bar {  left: 2px;  }");


    QWidget *m_tab1 = new QWidget(this); //first tab widget
    QWidget *m_tab2 = new QWidget(this); //second tab

    // Add them to main
    m_tabwidget->addTab(m_tab1, tr("Sign In"));
    m_tabwidget->addTab(m_tab2, tr("Register"));

    // FIRST TAB FACE
    
    QLabel *m_bar = new QLabel(m_tab1);
    m_bar->setTextFormat(Qt::RichText);
    m_bar->setStyleSheet("QLabel { border: none; margin: 0px; padding: 6px; background-color: #dae4db;}");
    m_bar->setFixedSize(450,30);
    m_bar->setText("<span style='color: #000000; font-size: x-large; "
                                "font-weight: bold;'> Please enter your credentials.</span>");
    m_bar->move(5,3);


    QLabel *email_label = new QLabel(m_tab1);
    email_label->setTextFormat(Qt::RichText);
    email_label->setFixedSize(120,15);
    email_label->setText("<span style='color: #000000; font-size: x-small; "
                                "font-weight: italic;'> Email address:</span>");
    email_label->move(45,45);

    QLabel *passw_label = new QLabel(m_tab1);
    passw_label->setTextFormat(Qt::RichText);
    passw_label->setFixedSize(120,15);
    passw_label->setText("<span style='color: #000000; font-size: x-small; "
                                "font-weight: italic;'> Password:</span>");
    passw_label->move(64,78);

    // Entry fields
    email_entry = new QLineEdit(m_tab1);
    email_entry->setPlaceholderText("E-mail address");
    email_entry->move(125,45);
    email_entry->setFixedSize(205,22);
    passw_entry = new QLineEdit(m_tab1);
    passw_entry->setPlaceholderText("Password");
    passw_entry->setEchoMode(QLineEdit::Password);
    passw_entry->setFixedSize(205,22);
    passw_entry->move(125,78);

    // Let's go! button
    m_signin_go = new QPushButton("&Proceed!",m_tab1);
    m_signin_go->setFixedSize(80,30);
    m_signin_go->move(360,95);
    connect(m_signin_go, SIGNAL(clicked()), this, SLOT(doSignin()));

    QLabel *m_barw = new QLabel(m_tab1);
    m_barw->setTextFormat(Qt::RichText);
    m_barw->setFixedSize(120,15);
    m_barw->setText("<span style='color: #e39025; font-size: x-small; "
                                "font-weight: italic;'> Forgot your password?</span>");
    m_barw->move(125,110);

   

    /////////////////////////////////////////// END OF FIRST TAB /////////////////////////////
    // SECOND TAB
    ///////////////////////////////////////////////////////////////////////////////////

    QLabel *name_label = new QLabel(m_tab2);
    name_label->setTextFormat(Qt::RichText);
    name_label->setFixedSize(120,15);
    name_label->setText("<span style='color: #000000; font-size: x-small; "
                                "font-weight: italic;'> Full name:</span>");
    name_label->move(62,9);

    QLabel *email_label2 = new QLabel(m_tab2);
    email_label2->setTextFormat(Qt::RichText);
    email_label2->setFixedSize(120,15);
    email_label2->setText("<span style='color: #000000; font-size: x-small; "
                                "font-weight: italic;'> Email address:</span>");
    email_label2->move(45,45);

    QLabel *passw_label2 = new QLabel(m_tab2);
    passw_label2->setTextFormat(Qt::RichText);
    passw_label2->setFixedSize(120,15);
    passw_label2->setText("<span style='color: #000000; font-size: x-small; "
                                "font-weight: italic;'> Password:</span>");
    passw_label2->move(65,78);

    // Entry fields
    name_entry = new QLineEdit(m_tab2);
    name_entry->setPlaceholderText("Full name");
    name_entry->move(125,9);
    name_entry->setFixedSize(205,22);
    email_entry2 = new QLineEdit(m_tab2);
    email_entry2->setPlaceholderText("E-mail address");
    email_entry2->move(125,45);
    email_entry2->setFixedSize(205,22);
    passw_entry2 = new QLineEdit(m_tab2);
    passw_entry2->setPlaceholderText("Password");
    passw_entry2->setEchoMode(QLineEdit::Password);
    passw_entry2->setFixedSize(205,22);
    passw_entry2->move(125,78);

    // Proceed button
     m_proceed_go = new QPushButton("&Proceed!",m_tab2);
     m_proceed_go->setFixedSize(80,30);
     m_proceed_go->move(360,95);
     connect(m_proceed_go, SIGNAL(clicked()), this, SLOT(doRegistration()));

     // SECOND TAB END
     
     //Create status line at bottom of all above
     // we will put here warning and error messages
    m_err->setTextFormat(Qt::RichText);
    m_err->setStyleSheet("QLabel { border: none; margin: 0px; padding: 6px; }");
    m_err->setFixedSize(460,30);

    m_err->move(56,230);
    m_err->hide();


    setContentsMargins(6, 6, 6,6);
    m_tabwidget->setCurrentIndex(0);
    email_entry->setFocus();
    //Fill background
    setStyleSheet("QDialog { background-image: url(:/background.png); padding: 0px; }");
    connect(this->m_tabwidget, SIGNAL(currentChanged(int)), this, SLOT(clearfields()));
}

    ///////////////////
////////////////////////////////////////START SIGNING IN
void SignIn::doSignin() 
{
  MTrace(t_signin, trace::Debug, "Will attempt sign-in");
  ServerConnection::Reply rep;
  // First, disable all controls and update the top text...
  email_entry->setEnabled(false);
  passw_entry->setEnabled(false);
  m_signin_go->setEnabled(false);

  // Attempt creating an access token using the given credentials.

  m_signin_toptext->setText(tr("Creating device credentials..."));
  //if email entry has word @test-keepit -> server is test, if not server is production
  std::size_t found;
  std::string st = (email_entry->text().toUtf8().data());
  email = email_entry->text().toUtf8().data();
  password = passw_entry->text().toUtf8().data();
  found  = st.find("@test-keepit");
  if (found!=std::string::npos)
    m_cfg.m_ngserver = m_testurl;
  else
    m_cfg.m_ngserver = m_defaulturl;
    
  m_cfg.write();
  //Important - block for old server
  if(found == std::string::npos) {
  ServerConnection conn(m_cfg.m_ngserver, 443, true);
  conn.setDefaultBasicAuth(email_entry->text().toUtf8().data(),
 			   passw_entry->text().toUtf8().data());

  { ServerConnection::Request req(ServerConnection::mPOST, "/tokens/");
    req.setBasicAuth(conn);
    std::string descr("serverbackup");
    { char hname[1024];
      if (gethostname(hname, sizeof hname))
        throw syserror("gethostname", "retrieving local host name");
      descr +=  "on " + std::string(hname);
    }
    
    { using namespace xml;
      m_cfg.m_uname = randStr(16);
      m_cfg.m_upass = randStr(16);
      std::string ttype("User");
      
      using namespace xml;
      const IDocument &doc = mkDoc
                             (Element("token")
                              (Element("descr")(CharData<std::string>(descr))
                               & Element("type")(CharData<std::string>(ttype))
                               & Element("aname")(CharData<std::string>(m_cfg.m_uname))
                               & Element("apass")(CharData<std::string>(m_cfg.m_upass))));
      
      req.setBody(doc);
    }
 
    MTrace(t_signin, trace::Debug, "Attempt create user token \""
           << m_cfg.m_uname << ":" << m_cfg.m_upass << "\" on server "
           << m_cfg.m_ngserver << ":443");
    
    bool failedToCreateTokens = false;
    // Execute request and catch connection errors
    try {
      rep = conn.execute(req);
	}
    catch(error &e) { //if connection is lost
      clearfields();
      showerror("Unable to connect: Make sure you are connected to the internet.");
      MTrace(t_signin, trace::Info, "Attempt to sign-in - Network error" << e.toString().c_str() << "\n" );
      return;
	}
    if (rep.getCode() == 201) {
      MTrace(t_signin, trace::Info, "Successfully created user credentials");
      { using namespace xml;
        m_cfg.m_aname = randStr(16);
        m_cfg.m_apass = randStr(16);
        std::string ttype("Device");
        
        using namespace xml;
        const IDocument &doc = mkDoc
                               (Element("token")
                                (Element("descr")(CharData<std::string>(descr))
                                 & Element("type")(CharData<std::string>(ttype))
                                 & Element("aname")(CharData<std::string>(m_cfg.m_aname))
                                 & Element("apass")(CharData<std::string>(m_cfg.m_apass))));
        
        req.setBody(doc);
      }
  
      MTrace(t_signin, trace::Debug, "Attempt create device token \""
             << m_cfg.m_aname << ":" << m_cfg.m_apass << "\" on server "
             << m_cfg.m_ngserver << ":443");
  
      try { //Execute request and catch connection problems
	rep = conn.execute(req);
      }
      catch(error &e) {
	clearwarning();
	showerror("Unable to connect: Make sure you are connected to the internet.");
	MTrace(t_signin, trace::Debug, "Will attempt sign-inNetwork error" << e.toString().c_str() << "\n" );
	return;
      }

      if (rep.getCode() == 201) {
        MTrace(t_signin, trace::Info, "Successfully created machine credentials");

        m_cfg.write();
        //
        // Success!
        //
        m_err->hide();
      } else {
        failedToCreateTokens = true;
        if (rep.getCode() == 401) {
          MTrace(t_signin, trace::Info, "Authorization failure on machine credentails creation");
          m_signin_toptext->setText("<span style='color: #ff0000; font-size: 8pt; "
                                    "font-weight: bold;'>Login failed...</span>");
        } else {
          m_signin_toptext->setText(tr("Unable to sign in - please re-try later"));
        }
      }
    } else {
      failedToCreateTokens = true;
      if (rep.getCode() == 401) {
        MTrace(t_signin, trace::Info, "Authorization failure on user credentails creation");
        m_signin_toptext->setText("<span style='color: #ff0000; font-size: 8pt; "
                                  "font-weight: bold;'>Login failed...</span>");
      } else {
        m_signin_toptext->setText(tr("Unable to sign in - please re-try later"));
      }
    }
    
    if (failedToCreateTokens) {
      //
      // Authentication failure!
      //
      showerror("The username or password you used does not exist or is incorrect!");
      email_entry->setEnabled(true);
      passw_entry->setEnabled(true);
      m_signin_go->setEnabled(true);
      m_cfg.m_aname = std::string();
      m_cfg.m_apass = std::string();
      m_cfg.m_uname = std::string();
      m_cfg.m_upass = std::string();
      return;
    }
  }

  // Fine, then create a device (using our host name and user name) -
  // if we fail with a 409 conflict it just means it was already
  // created, which is fine.
  { char hname[1024];
    if (gethostname(hname, sizeof hname))
      throw syserror("gethostname", "retrieving local host name");
    if (!hname[0])
      throw error("Local host name was empty");

    // Get logged in user name
    wchar_t uname[1024];
    DWORD uchars(sizeof uname / sizeof uname[0]);
    if (!GetUserName(uname, &uchars))
      throw syserror("GetUserName", "retrieving current user name");
    if (!uname[0])
      throw error("Current user name was empty");

    m_cfg.m_devname = utf16_to_utf8(uname) + " @ " + hname;
    MTrace(t_signin, trace::Info, "Using device name \"" << m_cfg.m_devname << "\"");

    // Attempt creation
    ServerConnection::Request req(ServerConnection::mPOST, "/devices/");
    req.setBasicAuth(conn);

    using namespace xml;
    const IDocument &doc = mkDoc
      (Element("pc")
       (Element("name")(CharData<std::string>(m_cfg.m_devname))));

    req.setBody(doc);
    //Use connection again
    try {
      rep = conn.execute(req);
    }
    catch(error &e) {
      clearfields();
      showerror("Unable to connect: Make sure you are connected to the internet.");
      MTrace(t_signin, trace::Debug, "Network error" << e.toString().c_str() << "\n" );
      return;
    }

    switch (rep.getCode()) {
    case 409:
      MTrace(t_signin, trace::Info, "Device already exists");
      m_cfg.write();
      break;
    case 201:
      MTrace(t_signin, trace::Info, "Device created");
      m_cfg.write();
      break;
    default:
      MTrace(t_signin, trace::Info, "Error creating device: " << rep.toString());
      m_cfg.m_devname = std::string();
      m_cfg.m_aname = std::string();
      m_cfg.m_apass = std::string();
      m_cfg.m_uname = std::string();
      m_cfg.m_upass = std::string();
      
      return;
    }
  }

  
 }
else {

ServerConnection conn(m_cfg.m_ngserver, 443, true);
    conn.setDefaultBasicAuth(email_entry->text().toUtf8().data(),
                             passw_entry->text().toUtf8().data());
    MTrace(t_signin, trace::Info, "Token start!");
    {   ServerConnection::Request req(ServerConnection::mPOST, "/tokens/");
	    conn.setDefaultBasicAuth(email, password);
        req.setBasicAuth(conn);
        std::string descr("serverbackup");
        {            char hname[1024];
            if (gethostname(hname, sizeof hname))
                throw syserror("gethostname", "retrieving local host name");
            descr +=  "on " + std::string(hname);
        }

    { 
	using namespace xml;
        m_cfg.m_uname = randStr(16);
        m_cfg.m_upass = randStr(16);
        std::string ttype("User");

    using namespace xml;
        const IDocument &doc = mkDoc
                                (Element("token")
                                 (Element("descr")(CharData<std::string>(descr))
                                 & Element("type")(CharData<std::string>(ttype))
                                 & Element("aname")(CharData<std::string>(m_cfg.m_uname))
                                 & Element("apass")(CharData<std::string>(m_cfg.m_upass))));

            req.setBody(doc);
        }

        MTrace(t_signin, trace::Debug, "Attempt create user token \""
               << m_cfg.m_uname << ":" << m_cfg.m_upass << "\" on server "
               << m_cfg.m_ngserver << ":443");

        bool failedToCreateTokens = false;
        // Execute request and catch connection errors
        try {
            rep = conn.execute(req);
        }
        catch (error &e) { //if connection is lost
            clearfields();
            showerror("Unable to connect: Make sure you are connected to the internet.");
            MTrace(t_signin, trace::Info, "Attempt to sign-in - Network error" << e.toString().c_str() << "\n" );
            return;
        }
        if (rep.getCode() == 201) {
            MTrace(t_signin, trace::Info, "Successfully created user credentials");
            {   using namespace xml;
                m_cfg.m_aname = randStr(16);
                m_cfg.m_apass = randStr(16);
                std::string ttype("Device");

                using namespace xml;
                const IDocument &doc = mkDoc
                                       (Element("token")
                                        (Element("descr")(CharData<std::string>(descr))
                                         & Element("type")(CharData<std::string>(ttype))
                                         & Element("aname")(CharData<std::string>(m_cfg.m_aname))
                                         & Element("apass")(CharData<std::string>(m_cfg.m_apass))));

                req.setBody(doc);
            }

            MTrace(t_signin, trace::Debug, "Attempt create device token \""
                   << m_cfg.m_aname << ":" << m_cfg.m_apass << "\" on server "
                   << m_cfg.m_ngserver << ":443");

            try { //Execute request and catch connection problems
                rep = conn.execute(req);
            }
            catch (error &e) {
                clearwarning();
                showerror("Unable to connect: Make sure you are connected to the internet.");
                MTrace(t_signin, trace::Debug, "Will attempt sign-inNetwork error" << e.toString().c_str() << "\n" );
                return;
            }

            if (rep.getCode() == 201) {
                MTrace(t_signin, trace::Info, "Successfully created machine credentials");
                ////////////////////////////////////
                MTrace(t_signin, trace::Info, "Start with id!");
               
                    std::string p_id, id;
                    ServerConnection::Request req(ServerConnection::mGET, "/users/");

                    req.setBasicAuth(email_entry->text().toUtf8().data(),
                                     passw_entry->text().toUtf8().data());
                    MTrace(t_signin, trace::Info, "Test server!");
                    using namespace xml;
                    const IDocument &doc = mkDoc
                                           (Element("user")
                                            (
                                                Element("id")(CharData<std::string>(p_id))

                                            )
                                           );

                    req.setBody(doc);
                    MTrace(t_signin, trace::Info, "Start new connect" << "\n" );
                    try { //Execute request with catching connection errors
                        rep = conn.execute(req);
                    }

                    catch (error &e) {
                        clearfields();
                        showerror("Unable to connect: Make sure you are connected to the internet.");
                        MTrace(t_signin, trace::Info, "Network error" << e.toString().c_str() << "\n" );
                        return;
                    }

                    MTrace(t_signin, trace::Info, "Finish new connect" << "\n" );
                    using namespace xml;
                    const IDocument &ddoc = mkDoc(Element("user")
                                                  (Element("id")(CharData<std::string>(id))
                                                  ));

                    try {


                        std::istringstream body(std::string(rep.refBody().begin(), rep.refBody().end()));
                        XMLexer lexer(body);
                        ddoc.process(lexer);
                        MTrace(t_signin, trace::Info, "ID parse user details: " + id);
                        m_cfg.m_user_id = id;

                       // m_cfg.m_device_id = GetDeviceId();
                    } catch (error &e) {
                        std::string ddd(std::string(rep.refBody().begin(), rep.refBody().end()));
                        MTrace(t_signin, trace::Info, "Failed to parse user details: " + e.toString() + "gggggggggg: " );

                        return;
                    }




                
                //////////////////////////////////////

                m_cfg.write();
                //
                // Success!
                //
                m_err->hide();
            } else {
                failedToCreateTokens = true;
                if (rep.getCode() == 401) {
                    MTrace(t_signin, trace::Info, "Authorization failure on machine credentails creation");
                    m_signin_toptext->setText("<span style='color: #ff0000; font-size: 8pt; "
                                              "font-weight: bold;'>Login failed...</span>");
                } else {
                    m_signin_toptext->setText(tr("Unable to sign in - please re-try later"));
                }
            }
        } else {
            failedToCreateTokens = true;
            if (rep.getCode() == 401) {
                MTrace(t_signin, trace::Info, "Authorization failure on user credentails creation");
                m_signin_toptext->setText("<span style='color: #ff0000; font-size: 8pt; "
                                          "font-weight: bold;'>Login failed...</span>");
            } else {
                m_signin_toptext->setText(tr("Unable to sign in - please re-try later"));
            }
        }

        if (failedToCreateTokens) {
            //
            // Authentication failure!
            //
            showerror("The username or password you used does not exist or is incorrect!");
            email_entry->setEnabled(true);
            passw_entry->setEnabled(true);
            m_signin_go->setEnabled(true);
            m_cfg.m_aname = std::string();
            m_cfg.m_apass = std::string();
            m_cfg.m_uname = std::string();
            m_cfg.m_upass = std::string();
            return;
        }
    }
//////////////////Start working with device!
    // Fine, then create a device (using our host name and user name) -
    // if we fail with a 409 conflict it just means it was already
    // created, which is fine.
    {        char hname[1024];
        if (gethostname(hname, sizeof hname))
            throw syserror("gethostname", "retrieving local host name");
        if (!hname[0])
            throw error("Local host name was empty");

        // Get logged in user name
        wchar_t uname[1024];
        DWORD uchars(sizeof uname / sizeof uname[0]);
        if (!GetUserName(uname, &uchars))
            throw syserror("GetUserName", "retrieving current user name");
        if (!uname[0])
            throw error("Current user name was empty");

        m_cfg.m_devname = utf16_to_utf8(uname) + " @ " + hname;
        MTrace(t_signin, trace::Info, "Using device name \"" << m_cfg.m_devname << "\"");

        // Attempt creation
        std::string path = "/users/" +m_cfg.m_user_id + "/devices/";
        
        ServerConnection::Request req(ServerConnection::mPOST, path);
        req.setBasicAuth(conn);
        using namespace xml;
        
        const IDocument &doc =
            mkDoc(Element("pc")
                  (Element("name")(CharData<std::string>(m_cfg.m_devname))));
    
        req.setBody(doc);




        //Use connection again
        try {
            rep = conn.execute(req);
        }
        catch (error &e) {
            clearfields();
            showerror("Unable to connect: Make sure you are connected to the internet.");
            MTrace(t_signin, trace::Debug, "Network error" << e.toString().c_str() << "\n" );
            return;
        }

        switch (rep.getCode()) {
            case 409:
                MTrace(t_signin, trace::Info, "Device already exists" << rep.readHeader() <<  "hhe");
				m_cfg.m_device_id = checkForExistingDevice(m_cfg.m_devname);
				if(m_cfg.m_device_id == "") { showerror("Cannot get divice id!"); return; }
                m_cfg.write();
                break;
            case 201:
                MTrace(t_signin, trace::Info, "Device created" << rep.readHeader() <<"hh");
				
                m_cfg.m_device_id = rep.readHeader();
				 
                m_cfg.write();
                break;
            default:
                MTrace(t_signin, trace::Info, "Error creating device: " << rep.toString());
                m_cfg.m_devname = std::string();
                m_cfg.m_aname = std::string();
                m_cfg.m_apass = std::string();
                m_cfg.m_uname = std::string();
                m_cfg.m_upass = std::string();
                m_cfg.m_user_id = std::string();
                m_cfg.m_device_id = std::string();
                return;
        }
    }

    
		


} 
// Emit our signed-in complete signal
    emit signedIn();
    // Make smart application restart - detach new and kill old if success
    if ( QProcess::startDetached(QString("\"") + QApplication::applicationFilePath() + "\"") )
    {        QApplication::quit();    }
    else
        qDebug("Cannot restart process! Try to make it manually!");

 return;    
}

void SignIn::doRegistration()
{
  std::string id;
  MTrace(t_signin, trace::Debug, "Will attempt sign-in");
  ServerConnection:: Reply rep;
  // First, disable all controls and update the top text...
  email_entry2->setEnabled(false);
  passw_entry2->setEnabled(false);
  m_proceed_go->setEnabled(false);
  name_entry->setEnabled(false);
  std::string p_id = "";
  // Attempt creating an access token using the given credentials.
  // let us choose server
  std::size_t found;
  std::string st = (email_entry2->text().toUtf8().data());
  found  = st.find("@test-keepit");
 
 if (found!=std::string::npos)
    { 
	m_cfg.m_ngserver = m_testurl; 
	ServerConnection conn(m_cfg.m_ngserver, 443, true);
	
	
	ServerConnection::Request req(ServerConnection::mGET, "/users/");
  
     req.setBasicAuth("public-signup",
                   "public-signup");
  
 using namespace xml;
  const IDocument &doc = mkDoc
    (Element("user")
     (
      Element("id")(CharData<std::string>(p_id))
      
      )
     );
  
  req.setBody(doc);  
	MTrace(t_signin, trace::Info, "Start new connect" << "\n" );
	try { //Execute request with catching connection errors 
    rep = conn.execute(req);
	    }
	
    catch(error &e) {
    clearfields();
    showerror("Unable to connect: Make sure you are connected to the internet.");
    MTrace(t_signin, trace::Info, "Network error" << e.toString().c_str() << "\n" );
    return;
    }
	
	MTrace(t_signin, trace::Info, "Finish new connect" << "\n" );
	using namespace xml;
    const IDocument &ddoc = mkDoc(Element("user")
            (Element("id")(CharData<std::string>(id))
            )); 

  try {
    
	
	std::istringstream body(std::string(rep.refBody().begin(), rep.refBody().end()));
    XMLexer lexer(body);
    ddoc.process(lexer);
	MTrace(t_signin, trace::Info, "ID parse user details: " + id);
  } catch (error &e) {
    std::string ddd(std::string(rep.refBody().begin(), rep.refBody().end()));
    MTrace(t_signin, trace::Info, "Failed to parse user details: " + e.toString() + "gggggggggg: " );
    
    return;
  }
	
	
	

	
  /////////////////////////////////////////////////  
  m_cfg.write();

 {

  ServerConnection conn(m_cfg.m_ngserver, 443, true);

  ServerConnection::Request req(ServerConnection::mPOST, "/users/"+id+"/users");
  
  req.setBasicAuth("public-signup",
                   "public-signup");
  
  std::string login = email_entry2->text().toUtf8().data();
  std::string password = passw_entry2->text().toUtf8().data();
 // std::string userName = name_entry->text().toUtf8().data();
  using namespace xml;
  const IDocument &doc = mkDoc
    (Element("user_create")
     (
      Element("login")(CharData<std::string>(login))
      & Element("password")(CharData<std::string>(password))
  //    & Element("fullname")(CharData<std::string>(userName))
      )
     );
  
  req.setBody(doc);
  
  try { //Execute request with catching connection errors 
    rep = conn.execute(req);
  }
  catch(error &e) {
    clearfields();
    showerror("Unable to connect: Make sure you are connected to the internet.");
    MTrace(t_signin, trace::Debug, "Network error" << e.toString().c_str() << "\n" );
    return;
  }
}


  if (rep.getCode() != 201) {
    MTrace(t_signin, trace::Warn, "Failed to create user: " + rep.toString());
    
    // Notify about the result
   
  email_entry2->setEnabled(true);
  passw_entry2->setEnabled(true);
  m_proceed_go->setEnabled(true);
  name_entry->setEnabled(true);
  if(rep.getCode() == 409) showerror ("User with these credentials already exist!");
  QMessageBox::critical(0, QObject::tr("Error"),  QObject::tr("Cannot register! Try to do it later or change login."));
  return;
  }
    //Success
    
  
  
   emit registered();
   email_entry = email_entry2;
   passw_entry = passw_entry2;
   doSignin();
}
else {
///////////////////////OlD Server
m_cfg.m_ngserver = m_defaulturl;
    
  m_cfg.write();

  ServerConnection conn(m_cfg.m_ngserver, 443, true);

  ServerConnection::Request req(ServerConnection::mPOST, "/users/");
  
  req.setBasicAuth("public-signup",
                   "public-signup");
  
  std::string email = email_entry2->text().toUtf8().data();
  std::string password = passw_entry2->text().toUtf8().data();
  std::string userName = name_entry->text().toUtf8().data();
  using namespace xml;
  const IDocument &doc = mkDoc
    (Element("user_create")
     (
      Element("email")(CharData<std::string>(email))
      & Element("password")(CharData<std::string>(password))
      & Element("fullname")(CharData<std::string>(userName))
      )
     );
  
  req.setBody(doc);
  
  try { //Execute request with catching connection errors 
    rep = conn.execute(req);
  }
  catch(error &e) {
    clearfields();
    showerror("Unable to connect: Make sure you are connected to the internet.");
    MTrace(t_signin, trace::Debug, "Network error" << e.toString().c_str() << "\n" );
    return;
  }

  if (rep.getCode() != 201) {
    MTrace(t_signin, trace::Warn, "Failed to create user: " + rep.toString());
    
    // Notify about the result
   
  email_entry2->setEnabled(true);
  passw_entry2->setEnabled(true);
  m_proceed_go->setEnabled(true);
  name_entry->setEnabled(true);
  if(rep.getCode() == 409) showerror ("User with these credentials already exist!");
  QMessageBox::critical(0, QObject::tr("Error"),  QObject::tr("Cannot register! Try to do it later or change login."));
  return;
  }
    //Success
    
  
  
   emit registered();
   email_entry = email_entry2;
   passw_entry = passw_entry2;
   doSignin();

}
}

void SignIn::clearwarning() 
{
  this->m_err->hide();
  this->m_err->setText("");

  if(m_tabwidget->currentIndex() == 0) email_entry->setFocus();
  else
  name_entry->setFocus();
  email_entry->clear();
  email_entry2->clear();
  passw_entry->clear();
  passw_entry2->clear();
  name_entry->clear();
  email_entry->setEnabled(true);
  email_entry2->setEnabled(true);
  passw_entry->setEnabled(true);
  passw_entry2->setEnabled(true);
  name_entry->setEnabled(true);
  m_signin_go->setEnabled(true);
  m_proceed_go->setEnabled(true);
  
}

void SignIn::showerror(QString s)
{
  QString sh ="<span style='color: #ff0000; font-size: 8pt; "
    "font-weight: bold;'> " +s+ "</span>";
  m_err->setText(sh);
  m_err->show();

}

void SignIn::clearfields() 
{
  this->m_err->hide();
  this->m_err->setText("");

  if(m_tabwidget->currentIndex() == 0) email_entry->setFocus();
  else
  name_entry->setFocus();

  //  email_entry->clear();
  //  email_entry2->clear();
  //  passw_entry->clear();
  //  passw_entry2->clear();
  //  name_entry->clear();
  email_entry->setEnabled(true);
  email_entry2->setEnabled(true);
  passw_entry->setEnabled(true);
  passw_entry2->setEnabled(true);
  name_entry->setEnabled(true);
  m_signin_go->setEnabled(true);
  m_proceed_go->setEnabled(true);
  
}


std::string SignIn::checkForExistingDevice(const std::string& devn)
{
    MutexLock l(m_slock);
    ServerConnection conn(m_cfg.m_ngserver, 443, true);
    ServerConnection::Request req(ServerConnection::mGET, "/users/" + m_cfg.m_user_id + "/devices/");
    req.setBasicAuth(email, password);
    
    ServerConnection::Reply rep = conn.execute(req);
    
    
    if (rep.getCode() != 200) {
        MTrace(t_signin, trace::Warn, "Failed to get list of devices: " + rep.toString());
        
        return false;
    }
    
    std::string responce(rep.refBody().begin(), rep.refBody().end());
    MTrace(t_signin, trace::Info, "Start looking for devices response" <<  responce <<"\n" );
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
        MTrace(t_signin, trace::Info, "Start looking for devices return" <<  deviceUID <<"\n" );
        return deviceUID;
    } 
    
    
    return "";
}

std::string SignIn::replaceStringWithString(const std::string& replaceableStr, const std::string& replacingWord, const std::string& replacingStr)
{
    std::string strToReplace = replaceableStr;
    std::string strReplacing = replacingStr;
    std::string wordReplacing =replacingWord;
    
    std::size_t found;
    found = replaceableStr.rfind(wordReplacing);
    if (found != std::string::npos) strToReplace.replace (found, wordReplacing.length(), strReplacing);
    
    return strToReplace;
} 
