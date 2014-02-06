#ifndef MVMAIN_H
#define MVMAIN_H

#include "SVMainUI.h"
#include "NetStateList.h"

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

private:
  NetStateList *nlist;
};

#endif
