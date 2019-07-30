#include "SVMain.h"
#include "state.h"

#include <stdio.h>

#include <QFileSystemWatcher>

SVMain::SVMain(QWidget *parent) : QMainWindow(parent)
{
  state_load(schem_file);
  svmain = this;
  setupUi(this);
  schem_watch = new QFileSystemWatcher;
  QObject::connect(schem_watch, &QFileSystemWatcher::fileChanged, display_widget, &SVDisplay::reload);
  QObject::connect(display_widget, &SVDisplay::set_hscroll, hscroll, &QScrollBar::setValue);
  QObject::connect(display_widget, &SVDisplay::set_vscroll, vscroll, &QScrollBar::setValue);
  nlist = NULL;
}

void SVMain::set_scroll_pos_range(int xc, int yc, int xm, int ym)
{
  hscroll->setMaximum(xm);
  vscroll->setMaximum(ym);
  hscroll->setValue(xc);
  vscroll->setValue(yc);
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
    QObject::connect(this, &SVMain::state_change, nlist, &NetStateList::state_changed_down);
    QObject::connect(nlist, &NetStateList::state_change_up, this, &SVMain::state_changed);
    QObject::connect(nlist, &NetStateList::closed, this, &SVMain::net_list_closed);
  }
  nlist->show();
  nlist->add_net(net);
}

void SVMain::state_changed()
{
  state_change();
}

void SVMain::auto_reload_toggle(bool enabled)
{
  if(enabled) {
      schem_watch->addPath(schem_file);
  } else {
      schem_watch->removePath(schem_file);
  }
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
