#include "MVMain.h"
#include "globals.h"

#include <stdio.h>

MVMain::MVMain(QWidget *parent) : QMainWindow(parent)
{
  nlist = NULL;
  setupUi(this);
  mvmain = this;
  hscroll->setMaximum(state->info.sx);
  vscroll->setMaximum(state->info.sy);
}

void MVMain::set_steps(int x1, int xp, int y1, int yp)
{
  hscroll->setSingleStep(x1);
  hscroll->setPageStep(xp);
  vscroll->setSingleStep(y1);
  vscroll->setPageStep(yp);
}

void MVMain::set_status(const char *status)
{
  statusbar->showMessage(status);
}

void MVMain::track(int net)
{
  //  if(net == state->vcc || net == state->gnd)
  //    return;

  if(!nlist) {
    nlist = new NetStateList;
    QObject::connect(this, SIGNAL(state_change()), nlist, SLOT(state_changed_down()));
    QObject::connect(nlist, SIGNAL(state_change_up()), this, SLOT(state_changed()));
    QObject::connect(nlist, SIGNAL(closed()), this, SLOT(net_list_closed()));
  }
  nlist->show();  
  nlist->add_net(net);
}

void MVMain::state_changed()
{
  state_change();
}

void MVMain::close()
{
  exit(0);
}

void MVMain::net_list_closed()
{
  fprintf(stderr, "net list closed slot\n");
  nlist = NULL;
}

void MVMain::reset_to_floating()
{
  state->reset_to_floating();
  state_change();
}

void MVMain::reset_to_zero()
{
  state->reset_to_zero();
  state_change();
}
