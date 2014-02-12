#include "SVMain.h"
#include "state.h"

#include <stdio.h>

SVMain::SVMain(QWidget *parent) : QMainWindow(parent)
{
  state_load(schem_file);
  svmain = this;
  setupUi(this);
}

void SVMain::set_scroll_pos_range(int xc, int yc, int xm, int ym)
{
  hscroll->setValue(xc);
  vscroll->setValue(yc);
  hscroll->setMaximum(xm);
  vscroll->setMaximum(ym);
}

void SVMain::set_steps(int x1, int y1, int xp, int yp)
{
  hscroll->setSingleStep(x1);
  hscroll->setPageStep(xp);
  vscroll->setSingleStep(y1);
  vscroll->setPageStep(yp);
}

void SVMain::set_status(const char *status)
{
  statusbar->showMessage(status);
}

void SVMain::track(int net)
{
  if(!nlist) {
    nlist = new NetStateList;
    QObject::connect(this, SIGNAL(state_change()), nlist, SLOT(state_changed_down()));
    QObject::connect(nlist, SIGNAL(state_change_up()), this, SLOT(state_changed()));
    QObject::connect(nlist, SIGNAL(closed()), this, SLOT(net_list_closed()));
  }
  nlist->show();  
  nlist->add_net(net);
}

void SVMain::state_changed()
{
  state_change();
}

void SVMain::close()
{
  exit(0);
}

void SVMain::net_list_closed()
{
  fprintf(stderr, "net list closed slot\n");
  nlist = NULL;
}
