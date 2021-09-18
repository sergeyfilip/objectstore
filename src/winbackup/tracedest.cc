///
/// Implementation of the simple trace window
///
// $Id: tracedest.cc,v 1.2 2013/05/16 11:30:46 joe Exp $
///

#include "tracedest.hh"

QDialogDestination::QDialogDestination(size_t ml)
  : m_maxlines(ml)
{
  m_txt = new QLabel;
  m_txt->setTextFormat(Qt::PlainText);
  m_txt->setWordWrap(true);
  m_txt->setAlignment(Qt::AlignLeft | Qt::AlignBottom);

  m_layout = new QVBoxLayout;
  m_layout->addWidget(m_txt);
  setLayout(m_layout);

  setWindowTitle(tr("Trace"));
  resize(800, 400);
}

void QDialogDestination::output(const std::string &t)
{
  MutexLock l(m_txt_lock);

  // Remove first line if we reached max
  if (m_lines.size() == m_maxlines)
    m_lines.pop_front();

  // Add new line
  m_lines.push_back(t);

  // Render
  m_rendered.clear();
  for (std::list<std::string>::const_iterator i = m_lines.begin();
       i != m_lines.end(); ++i)
    m_rendered += QString::fromUtf8((*i + "\n").c_str());

  QMetaObject::invokeMethod(this, "render");
}

void QDialogDestination::render()
{
  MutexLock l(m_txt_lock);
  m_txt->setText(m_rendered);
}
