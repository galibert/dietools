#include "NetState.h"
#include "state.h"

#include <stdio.h>

NetState::NetState(int _net, QWidget *parent) : QWidget(parent)
{
  net = _net;
  setupUi(this);
  net_name->setText(nets[net]->name.c_str());
  bool is_fixed = state.is_fixed[net];
  char fixed_level = state.fixed_level[net];
  r_0->setChecked(is_fixed && fixed_level == 0);
  r_1->setChecked(is_fixed && fixed_level == 50);
  r_2->setChecked(is_fixed && fixed_level == 85);
  r_float->setChecked(!is_fixed);
  state_changed();
}

void NetState::state_changed()
{
  char volt[4];
  int pp = state.power[net];
  sprintf(volt, "%d.%d", pp/10, pp%10);
  power->setText(volt);
}

void NetState::changed_r()
{
  bool is_fixed = !r_float->isChecked();
  char fixed_level = r_1->isChecked() ? 50 : r_2->isChecked() ? 85 : 0;
  state.is_fixed[net] = is_fixed;
  state.fixed_level[net] = fixed_level;
  if(is_fixed)
    state.power[net] = fixed_level;
  set<int> changed;
  changed.insert(net);
  state.apply_changed(changed);
  state_change();
}

void NetState::set_display(bool disp)
{
  state.highlight[net] = disp;
  state_change();
}

void NetState::close()
{
  closed(net);
  QWidget::close();
}
