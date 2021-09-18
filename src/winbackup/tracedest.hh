///
/// For tracing; open a QDialog with a simple console-like view of the
/// trace
///
/// $Id: tracedest.hh,v 1.2 2013/05/16 11:30:46 joe Exp $
///


#ifndef WINBACKUP_TRACEDEST_HH
#define WINBACKUP_TRACEDEST_HH

#include "common/trace.hh"
#include "common/mutex.hh"

#include <QDialog>
#include <QVBoxLayout>
#include <QLabel>

#include <string>
#include <list>

/// \class QDialogDestination
//
/// This is a simple QDialog application window that displays the output
class QDialogDestination : public QDialog, public trace::Destination {
  Q_OBJECT

public:
  QDialogDestination(size_t maxlines = 20);

  void output(const std::string &);

public slots:
  void render();

private:
  const size_t m_maxlines;

  /// We protect our qlabel with a mutex - many different threads may
  /// cause text additions at any time...
  Mutex m_txt_lock;
  std::list<std::string> m_lines;

  QVBoxLayout *m_layout;
  QLabel *m_txt;

  /// Cache of rendered output
  QString m_rendered;
};


#endif

