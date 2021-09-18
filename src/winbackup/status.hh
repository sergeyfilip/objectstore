///
/// Status window
///
//
// $Id: status.hh,v 1.6 2013/05/23 14:47:02 joe Exp $
//

#ifndef WINBACKUP_STATUS_HH
#define WINBACKUP_STATUS_HH

#include <QLabel>
#include <QDialog>

#include <string>
#include <vector>
#include "backup/upload.hh"

/// Status window
class Status : public QDialog {
  Q_OBJECT

public:
  Status();

  struct status_t {
    // Worker thread status
    std::vector<Upload::threadstatus_t> m_threads;
  };

  /// Call this method to set the new status object
  void newStatus(const status_t&);

  /// Call this method to inquire about the most recent status
  void getStatus(status_t&);

private:
  QLabel *m_topbar;
  QLabel *m_progress;
  QLabel *m_logo;

  QLabel *m_activities; /// rich text label

  Mutex m_latest_lock;
  status_t m_latest;  /// latest status

private slots:
  /// We queue a call to this method to render the new status display
  /// after every call to newStatus
  void renderStatus();

};

#endif
