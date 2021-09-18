///
/// Our status window implementation
///
// $Id: status.cc,v 1.11 2013/07/29 10:50:51 sf Exp $

#include "status.hh"
#include "common/trace.hh"
#include <QApplication>
#include <QVBoxLayout>

#include <sstream>
#include <iomanip>

namespace {
  trace::Path t_stat("/gui/status");
}

Status::Status()
  : m_topbar(new QLabel)
  , m_progress(new QLabel)
  , m_activities(new QLabel)
{
  // +---------------------+
  // |  T O P B A R        |
  // |  p r o g r e s s    |
  // +---------------------+
  // | file0.png  (upload) |
  // | /dir/      (scan)   |
  // +---------------------+
  //
  setContentsMargins(0, 0, 0, 0);
  setStyleSheet("QDialog { background-image: url(:/background.png); padding: 0px; }");

  QVBoxLayout *vb(new QVBoxLayout);
  vb->setContentsMargins(0, 0, 0, 0);
  vb->setSpacing(0);
  vb->addWidget(m_topbar);
  vb->addWidget(m_progress);
  vb->addSpacing(25);
  vb->addWidget(m_activities);
  setLayout(vb);

  m_logo = new QLabel(this);
  m_logo->resize(1535 / 4, 298 / 4); // image dimensions; 500x298
  m_logo->move(0, 0);
  m_logo->setPixmap(QPixmap(":/biglogo1.png").scaled(m_logo->width(), m_logo->height(),
						    Qt::KeepAspectRatio,
						    Qt::SmoothTransformation));

  m_topbar->setTextFormat(Qt::RichText);
  m_topbar->setStyleSheet("QLabel { border: none; margin: 0px; padding: 6px; background-color: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 rgba(87,102,90,255), stop:1 rgba(58,68,60,255));}");
  m_topbar->setFixedHeight(50);

  m_progress->setTextFormat(Qt::RichText);
  m_progress->setStyleSheet("QLabel { background-color: qlineargradient(x1:0, y1:0, x2:.5, y2:0, x3:1, y3:0, stop:0 rgba(234,171,60,255), stop:1 rgba(227,150,37,255), stop:0 rgba(234,171,60,255)); };");
  m_progress->setFixedHeight(5);

  m_activities->setFont(QFont("Tahoe", 12));
  m_activities->setTextFormat(Qt::RichText);
  m_activities->setStyleSheet("QLabel { border: 2px solid #e39625; "
			      " background-color: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 rgba(87,102,90,255), stop:1 rgba(58,68,60,255));"
			      " padding: 6px;"
			      " margin: 12px;"
			      "}");

  m_activities->setText("012345678901234567890123456789"
			"012345678901234567890123456789");
  m_activities->setFixedWidth(m_activities->sizeHint().width());
  m_activities->setText("");

  setFixedWidth(sizeHint().width());
 

}

// This method is called from the worker threads directly - it will
// therefore queue a call to the GUI thread to run renderStatus()
void Status::newStatus(const status_t &s)
{
  MutexLock l(m_latest_lock);
  m_latest = s;
  QMetaObject::invokeMethod(this, "renderStatus");
 
}

void Status::getStatus(status_t &s)
{
  MutexLock l(m_latest_lock);
  s = m_latest;
}

namespace {
  // Chop the file name short if too long
  std::string chopstring(const std::string &s, size_t maxlen) {
    MAssert(maxlen > 3, "chopstring maxlen too short");
    if (s.size() <= maxlen)
      return s;
    // Fine, we need three dots and a maxlen-3 substring of s
    return "..." + s.substr(s.size() - (maxlen - 3), std::string::npos);
  }
}


void Status::renderStatus()
{
  MutexLock l(m_latest_lock);

  bool all_idle = true;
  QString sub;
  for (std::vector<Upload::threadstatus_t>::const_iterator i = m_latest.m_threads.begin();
       i != m_latest.m_threads.end(); ++i) {
    // Idle detection
    if (i->state != Upload::threadstatus_t::OSIdle)
      all_idle = false;
    // Format percentage - if any
    std::ostringstream pct;
    if (i->object_progress.isSet())
      pct << std::setprecision(3) << i->object_progress.get() * 100 << "% ";
    // Build up rich text status string
    sub += QString::fromUtf8((std::string(i == m_latest.m_threads.begin() ? "" : "<br/>")
			      + (i->state == Upload::threadstatus_t::OSScanning
				 ? "<img src=':/keepitwork02.png'/> <span style='color: white; font-style: italic'>Scanning</span> " : "")
			      + (i->state == Upload::threadstatus_t::OSUploading
				 ? "<img src=':/keepitwork00.png'/> " : "")
                              + (i->state == Upload::threadstatus_t::OSFinishing
                                 ? "<img src=':/keepitwork01.png'/> <span style='color: white; font-style: italic'>Finishing...</span> " : "")
			      + "<span style='color: white;'>"
			      + pct.str()
			      + chopstring(i->object, 60)
			      + "</span>").c_str());
  }

  QString act("<span style='color: #e39625; font-size: large; "
	      "font-weight: bold;'>"
              + (all_idle ? QString("Backup complete.") : QString("Uploading files"))
              + "</span><br/>");

  m_activities->setText(act + sub);
 
}
