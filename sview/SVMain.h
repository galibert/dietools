#ifndef MVMAIN_H
#define MVMAIN_H

#include "ui_SVMainUI.h"
#include "NetStateList.h"

#include <QFileSystemWatcher>

class SVMain : public QMainWindow, private Ui::SVMainUI {
  Q_OBJECT

public:
  SVMain(QWidget *parent = 0);
  void set_scroll_pos_range(int xc, int yc, int xm, int ym);
  void set_steps(int x1, int y1, int xp, int yp);
  void set_status(const char *status);

signals:
  void state_change();

public slots:
  void track(int);
  void state_changed();
  void close();
  void net_list_closed();
  void auto_reload_toggle(bool);

private:
  NetStateList *nlist;
  QFileSystemWatcher *schem_watch;
};

#endif
