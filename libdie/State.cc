#include "State.h"

#include <string.h>
#include <stdio.h>

State::State(const char *info_path, const char *cmap_path, const char *pins_path, bool _cmos) :
  info(info_path),
  cmap(cmap_path, info.nl, info.sx, info.sy, false),
  ninfo(pins_path, cmap, info)
{
  cmos = _cmos;
  vcc = ninfo.nets["vcc"];
  gnd = ninfo.nets["gnd"];

  ttype.resize(info.trans.size());
  ignored.resize(info.trans.size());

  quasi_vcc.resize(info.nets.size());
  pullup.resize(info.nets.size());
  pulldown.resize(info.nets.size());
  display.resize(info.nets.size());
  oscillator.resize(info.nets.size());

  for(unsigned int i=0; i != info.nets.size(); i++) {
    quasi_vcc[i] = false;
    pullup[i] = false;
    pulldown[i] = false;
    display[i] = false;
    oscillator[i] = false;
  }

  for(unsigned int i=0; i != info.trans.size(); i++) {
    ttype[i] = T_NMOS;
    ignored[i] = false;
  }

  for(unsigned int i=0; i != info.nets.size(); i++)
    if(ninfo.names[i].substr(0, 3) == "osc")
      oscillator[i] = true;

  quasi_vcc[vcc] = true;

  if(cmos) {
    for(unsigned int i=0; i != info.trans.size(); i++) {
      const tinfo &ti = info.trans[i];
      if(ti.t1 == gnd || ti.t2 == gnd)
	ttype[i] = T_NMOS;
      else
	ttype[i] = T_PMOS;
    }
  } else {
    for(unsigned int i=0; i != info.trans.size(); i++) {
      const tinfo &ti = info.trans[i];
      int q = -1;
      if((ti.gate == vcc || ti.gate == gnd) && ti.t1 == vcc)
	q = ti.t2;
      if((ti.gate == vcc || ti.gate == gnd) && ti.t2 == vcc)
	q = ti.t1;
      if(q != -1) {
	ignored[i] = true;
	ttype[i] = ti.gate == gnd ? T_NDEPL : T_NMOS;
	if(ti.f < 2) {
	  // Pullups on vertical lines through a resistor
	  pullup[q] = true;
	  //	cinfo &ci = info.circs[ti.circ];
	  //	fprintf(stderr, "qvcc-depl %s trans %d (%d, %d)-(%d, %d) f=%g\n", ninfo.net_name(q).c_str(), ti.circ, ci.x0, ci.y0, ci.x1, ci.y1, ti.f);
	} else {
	  quasi_vcc[q] = true;
	  //	cinfo &ci = info.circs[ti.circ];
	  //	fprintf(stderr, "qvcc %s trans %d (%d, %d)-(%d, %d) f=%g\n", ninfo.net_name(q).c_str(), ti.circ, ci.x0, ci.y0, ci.x1, ci.y1, ti.f);
	}
      }
    }

    for(unsigned int i=0; i != info.trans.size(); i++) {
      const tinfo &ti = info.trans[i];
      int q = -1;
      if(ti.gate == gnd && ti.t1 == gnd)
	q = ti.t2;
      if(ti.gate == gnd && ti.t2 == gnd)
	q = ti.t1;
      if(q != -1 && ti.f < 2) {
	ignored[i] = true;
	ttype[i] = T_NDEPL;
	pulldown[q] = true;
      }
    }

    for(unsigned int i=0; i != info.trans.size(); i++) {
      const tinfo &ti = info.trans[i];
      if(!quasi_vcc[ti.gate] && ((quasi_vcc[ti.t1] && ti.gate == ti.t2) || (quasi_vcc[ti.t2] && ti.gate == ti.t1))) {
	ttype[i] = T_NDEPL;
	ignored[i] = true;
	pullup[ti.gate] = true;
      }
    }
  }

  forced_power.resize(info.nets.size());
  power.resize(info.nets.size());
  power_dist.resize(info.nets.size());
  for(unsigned int i=0; i != info.nets.size(); i++)
    forced_power[i] = quasi_vcc[i] || oscillator[i] ? S_1 : S_FLOAT;
  forced_power[gnd] = S_0;
  reset_to_floating();
}

void State::reset_to_floating()
{
  power = forced_power;
  set<int> changed;
  memset(&power_dist[0], 0, sizeof(int)*info.nets.size());
  for(unsigned int i=0; i != forced_power.size(); i++)
    if(forced_power[i] != S_FLOAT)
      changed.insert(i);
  //  apply_changed(changed);
}

void State::reset_to_zero()
{
  set<int> changed;
  for(unsigned int i=0; i != forced_power.size(); i++) {
    changed.insert(i);
    if(forced_power[i] != S_FLOAT) {
      power[i] = forced_power[i];
      power_dist[i] = 0;
    } else {
      power[i] = S_0;
      power_dist[i] = 100000;
    }
  }
  apply_changed(changed);
}

void State::apply_changed(set<int> changed)
{
  int count = 0;
  while(!changed.empty() && count < 1100) {
    bool verbose = count > 1000;
    set<int> influenced;
    for(set<int>::const_iterator i = changed.begin(); i != changed.end(); i++) {
      int net = *i;
      map<int, list<int> >::const_iterator j;
      j = info.gate_to_trans.find(net);
      if(j != info.gate_to_trans.end()) {
	for(list<int>::const_iterator k = j->second.begin(); k != j->second.end(); k++) {
	  const tinfo &ti = info.trans[*k];
	  influenced.insert(ti.t1);
	  influenced.insert(ti.t2);
	}
      }
      j = info.term_to_trans.find(net);
      if(j != info.term_to_trans.end()) {
	for(list<int>::const_iterator k = j->second.begin(); k != j->second.end(); k++) {
	  const tinfo &ti = info.trans[*k];
	  int other = ti.t1 == net ? ti.t2 : ti.t1;
	  if(other != net)
	    influenced.insert(other);
	}
      }
    }

    if(0)
      fprintf(stderr, "%d changed, %d influenced\n", int(changed.size()), int(influenced.size()));
    changed.clear();
    for(set<int>::const_iterator i = influenced.begin(); i != influenced.end(); i++) {
      int net = *i;
      if(forced_power[*i] != S_FLOAT)
	continue;
      double drive_0 = 0, drive_1 = 0;
      int dist_0 = -1, dist_1 = -1;
      map<int, list<int> >::const_iterator j = info.term_to_trans.find(*i);
      if(j != info.term_to_trans.end()) {
	for(list<int>::const_iterator k = j->second.begin(); k != j->second.end(); k++) {
	  const tinfo &ti = info.trans[*k];
	  if(ignored[*k])
	    continue;
	  if(power[ti.gate] == S_0 || power[ti.gate] == S_FLOAT)
	    continue;
	  int other = ti.t1 == net ? ti.t2 : ti.t1;
	  if(other == net)
	    continue;
	  if(power[other] == S_FLOAT)
	    continue;
	  int pd = power_dist[other];
	  if(verbose)
	    fprintf(stderr, "net %d drive from %s through %s type=%d dist=%d force=%g\n", net, ninfo.net_name(other).c_str(), ninfo.net_name(ti.gate).c_str(), power[other], pd+1, ti.f);
	  if(power[other] == S_0) {
	    if(dist_0 == -1 || dist_0 > pd+1 || (dist_0 == pd+1 && drive_0 < ti.f)) {
	      dist_0 = pd+1;
	      drive_0 = ti.f;
	    }
	  } else {
	    if(dist_1 == -1 || dist_1 > pd+1 || (dist_1 == pd+1 && drive_1 < ti.f)) {
	      dist_1 = pd+1;
	      drive_1 = ti.f;
	    }
	  }
	}
      }
      if(pullup[net]) {
	if(dist_1 == -1 || dist_1 >= 2 || (dist_1 == 1 && drive_1 < 0.1)) {
	  drive_1 = 1;
	  dist_1 = 100;
	}
      }
      int new_state;
      int new_dist;
      if(dist_0 == -1 && dist_1 == -1) {
	new_state = power[net];
	new_dist = 100000;
      } else if(dist_0 == -1 && dist_1 != -1) {
	new_state = S_1;
	new_dist = dist_1;
      } else if(dist_0 != -1 && dist_1 == -1) {
	new_state = S_0;
	new_dist = dist_0;
      } else {
	if(dist_0 < dist_1 || (dist_0 == dist_1 && drive_0 >= drive_1)) {
	  new_state = S_0;
	  new_dist = dist_0;
	} else {
	  new_state = S_1;
	  new_dist = dist_1;
	}
      }
      if(new_dist > 500)
	new_dist = 100000;
      if(new_state != power[net] || new_dist != power_dist[net]) {
	if(verbose)
	  fprintf(stderr, "net %d: %d.%d -> %d.%d (%d %g  %d %g)\n", net, power[net], power_dist[net], new_state, new_dist, dist_0, drive_0, dist_1, drive_1);
	power[net] = new_state;
	power_dist[net] = new_dist;
	changed.insert(net);
      }
    }
    count++;
  }  
  if(count == 1100)
    fprintf(stderr, "Convergence failure\n");
}
