#ifndef STATE_H
#define STATE_H

#include <vector>
#include <string>
#include <set>
#include <map>

class net {
public:
  struct point {
    int x, y;
  };
  struct line {
    int p1, p2;
  };

  std::string name;
  std::vector<point> pt;
  std::vector<line> lines;
  std::vector<int> dots;
  unsigned int id;

  void draw(unsigned int *ids, int ox, int oy, int w, int h, int z) const;
};

class node {
public:
  enum { NODE_MARK = 0x01000000, NET_MARK = 0x02000000, TYPE_MASK = 0xff000000, ID_MASK = 0xffffff };
  enum { T, D, I, V, G, P, C };
  enum { T1, T2, GATE };
  enum {
    W_S = 0,
    E_S = 1,
    N_S = 2,
    S_S = 3,
    W_D = 4,
    E_D = 5,
    N_D = 6,
    S_D = 7
  };

  int type;
  int x, y, orientation;
  int x0, y0, x1, y1;
  std::string name;
  unsigned int id;

  double f;
  int netids[3];
  net *nets[3];

  int name_width, name_height;
  unsigned char *name_image;

  void bbox();
  void draw(unsigned int *ids, int ox, int oy, int w, int h, int z) const;
  void draw_mosfet(unsigned int *ids, int ox, int oy, int w, int h, int z) const;
  void draw_gnd(unsigned int *ids, int ox, int oy, int w, int h, int z) const;
  void draw_vcc(unsigned int *ids, int ox, int oy, int w, int h, int z) const;
  void draw_pad(unsigned int *ids, int ox, int oy, int w, int h, int z) const;
  void draw_capacitor(unsigned int *ids, int ox, int oy, int w, int h, int z) const;
};

class state_t {
public:
  int sx, sy;
  double ratio;

  std::vector<bool> selectable_net;
  std::vector<bool> highlight;
  std::vector<bool> is_fixed;
  std::vector<char> fixed_level;
  std::vector<char> power;
  std::vector<int> delay;    

  std::vector<std::vector<int> > gate_to_trans;
  std::vector<std::vector<int> > term_to_trans;


  std::set<std::string> save_selected;
  std::map<std::string, char> save_fixed_level;

  int trace_pos;

  void save();
  void build();
  void reload();
  void apply_changed(std::set<int> changed);

  void load_trace();
  void trace_next();
  void trace_prev();
  void trace_info(int &tp, int &ts);

  state_t();

private:
  enum {
    ET = 7,
    ED = -30
  };

  void add_transistor(node *tr, std::vector<int> &nids, std::set<int> &nid_set, std::set<int> &changed, std::set<node *> &accepted_trans, std::map<int, std::vector<node *> > &rejected_trans_per_gate);
  void add_net(int nid, std::vector<int> &nids, std::set<int> &nid_set, std::set<int> &changed, std::set<node *> &accepted_trans, std::map<int, std::vector<node *> > &rejected_trans_per_gate);
  void dump_equation_system(std::string equation, const std::vector<int> &constants, const std::vector<int> &nids_to_solve, const std::set<node *> &accepted_trans);
  std::string c2s(int vr, const std::vector<int> &constants, int pos);
  void build_equation(std::string &equation, std::vector<int> &constants, const std::vector<int> &nids_to_solve, const std::vector<int> &levels, const std::set<node *> &accepted_trans, const std::map<int, int> &nid_to_index) const;

  static void pull(int &term, int gate, int oterm);
  static void minmax(int &minv, int &maxv, int value);
  static std::string i2v(int id);
  static int get_id(std::string equation, unsigned int &pos);

  static void Ta__(const std::vector<int> &constants, std::vector<int> &level);
  static void Ta_b(const std::vector<int> &constants, std::vector<int> &level);
  static void Daa_(const std::vector<int> &constants, std::vector<int> &level);
  static void Da__(const std::vector<int> &constants, std::vector<int> &level);
  static void Daa__Ta_b(const std::vector<int> &constants, std::vector<int> &level);
  static void Dbb__Ta_b(const std::vector<int> &constants, std::vector<int> &level);
  static void Ta___Ta_b(const std::vector<int> &constants, std::vector<int> &level);
  static void Tb___Ta_b(const std::vector<int> &constants, std::vector<int> &level);
  static void Ta_b_Ta_c(const std::vector<int> &constants, std::vector<int> &level);
  static void Ta___Ta__(const std::vector<int> &constants, std::vector<int> &level);
  static void Ta___Ta___Ta_b(const std::vector<int> &constants, std::vector<int> &level);
  static void Tbb__Taa__Ta___Taab(const std::vector<int> &constants, std::vector<int> &level);
  static void Dbb__Ta_b_Tb_c(const std::vector<int> &constants, std::vector<int> &level);
  static void Daa__Ta_b_Ta_c(const std::vector<int> &constants, std::vector<int> &level);
  static void Ta_b_Tb_c(const std::vector<int> &constants, std::vector<int> &level);
  static void Ta___Dabb(const std::vector<int> &constants, std::vector<int> &level);
  static void Tc___Dbb__Ta___Tbac_Taab(const std::vector<int> &constants, std::vector<int> &level);
  static void Dbb__Tc___Ta___Tbac_Taab(const std::vector<int> &constants, std::vector<int> &level);
  static void Ta___Dabb_Dacc_Dadd_Daee(const std::vector<int> &constants, std::vector<int> &level);
  static void Ta___Dabb_Dacc_Dadd_Daee_Daff(const std::vector<int> &constants, std::vector<int> &level);
  static void Daa__Taab(const std::vector<int> &constants, std::vector<int> &level);
  static void Tbb__Daa__Taab(const std::vector<int> &constants, std::vector<int> &level);
  static void Tb___Tc___Te___Ta_b_Ta_c_Ta_d_Ta_e_Ta_f(const std::vector<int> &constants, std::vector<int> &level);
  static void Ta___Daa__Taab(const std::vector<int> &constants, std::vector<int> &level);
  static void Tb___Dbb__Tabb(const std::vector<int> &constants, std::vector<int> &level);
  static void Tb___Dbb__Ta_b(const std::vector<int> &constants, std::vector<int> &level);
  static void Ta___Daa_(const std::vector<int> &constants, std::vector<int> &level);
  static void Ta___Ta___Daa_(const std::vector<int> &constants, std::vector<int> &level);
  static void Ta___Ta___Ta___Daa_(const std::vector<int> &constants, std::vector<int> &level);
  static void Ta___Ta___Ta___Ta___Daa_(const std::vector<int> &constants, std::vector<int> &level);
  static void Ta___Ta___Ta___Ta___Ta___Ta___Ta___Daa_(const std::vector<int> &constants, std::vector<int> &level);

  std::map<std::string, void (*)(const std::vector<int> &constants, std::vector<int> &level)> solvers;
  void register_solvers();
};

extern std::vector<node *> nodes;
extern std::vector<net *> nets;
extern state_t state;

extern const char *schem_file;
extern const unsigned char *trace_data;
extern int trace_size;

std::string id_to_name(unsigned int id);

extern class SVMain *svmain;

void state_load(const char *fname);
void freetype_init();

#endif
