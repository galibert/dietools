#ifndef NETSTATE_H
#define NETSTATE_H

#include "NetStateUI.h"

class NetState : public QWidget, private Ui::NetStateUI {
  Q_OBJECT

public:
  NetState(int net, QWidget *parent = 0);

signals:
  void state_change();
  void closed(int);

public slots:
  void changed_r();
  void set_display(bool);
  void state_changed();
  void close();

private:
  int net;
};

#endif
