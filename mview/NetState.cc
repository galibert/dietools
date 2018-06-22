#include "NetState.h"
#include "globals.h"

#include <stdio.h>

NetState::NetState(int _net, QWidget *parent) : QWidget(parent)
{
  net = _net;
  setupUi(this);
  net_name->setText(state->ninfo.net_name(net).c_str());
  int forced_power = state->forced_power[net];
  r_0->setChecked(forced_power == State::S_0);
  r_1->setChecked(forced_power == State::S_1);
  r_float->setChecked(forced_power == State::S_FLOAT);
  state_changed();
}

void NetState::state_changed()
{
  static const char *text[] = { "0", "1", "-" };
  int p = state->power[net];
  power->setText(text[p]);
}

void NetState::changed_r()
{
  int p = -1;
  if(r_0->isChecked())
    p = State::S_0;
  if(r_1->isChecked())
    p = State::S_1;
  if(r_float->isChecked())
    p = State::S_FLOAT;
  if(p != -1) {
    state->forced_power[net] = p;
    if(p != State::S_FLOAT)
      state->power[net] = p;
    std::set<int> changed;
    changed.insert(net);
    state->apply_changed(changed);
    state_change();
  }
}

void NetState::set_display(bool disp)
{
  state->display[net] = disp;
  state_change();
}

void NetState::close()
{
  closed(net);
  QWidget::close();
}
