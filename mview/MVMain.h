#ifndef MVMAIN_H
#define MVMAIN_H

#include "MVMainUI.h"
#include "NetStateList.h"

class MVMain : public QMainWindow, private Ui::MVMainUI {
  Q_OBJECT

public:
  MVMain(QWidget *parent = 0);
  void set_steps(int x1, int xp, int y1, int yp);
  void set_status(const char *status);

signals:
  void state_change();

public slots:
  void track(int);
  void state_changed();
  void close();
  void net_list_closed();
  void reset_to_floating();
  void reset_to_zero();

private:
  NetStateList *nlist;
};

#endif
