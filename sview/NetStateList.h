#ifndef NETSTATELIST_H
#define NETSTATELIST_H

#include "NetStateListUI.h"
#include "NetState.h"

#include <map>

using namespace std;

class NetStateList : public QScrollArea, private Ui::NetStateListUI {
  Q_OBJECT

public:
  NetStateList(QWidget *parent = 0);
  void add_net(int net);

signals:
  void state_change_up();
  void state_change_down();
  void closed();

public slots:
  void state_changed_up();
  void state_changed_down();
  void net_closed(int);
  void close();

private:
  map<int, NetState *> nets;
};

#endif
