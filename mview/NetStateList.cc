#include "NetStateList.h"
#include "globals.h"

#include <stdio.h>

NetStateList::NetStateList(QWidget *parent) : QScrollArea(parent)
{
  setupUi(this);
}

void NetStateList::add_net(int net)
{
  if(nets.find(net) == nets.end()) {
    NetState *ns = new NetState(net, contents);
    QObject::connect(this, SIGNAL(state_change_down()), ns, SLOT(state_changed()));
    QObject::connect(ns, SIGNAL(state_change()), this, SLOT(state_changed_up()));
    QObject::connect(ns, SIGNAL(closed(int)), this, SLOT(net_closed(int)));

    nets[net] = ns;
    contents_layout->addWidget(ns);
    ns->show();
  }
}

void NetStateList::state_changed_up()
{
  state_change_up();
}

void NetStateList::state_changed_down()
{
  state_change_down();
}

void NetStateList::net_closed(int net)
{
  state->display[net] = false;
  nets.erase(nets.find(net));
}

void NetStateList::close()
{
  for(auto i = nets.begin(); i != nets.end(); i++)
    state->display[i->first] = false;
  fprintf(stderr, "net list closed\n");
  closed();
  QScrollArea::close();
}
