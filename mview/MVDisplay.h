#ifndef MVDISPLAY_H
#define MVDISPLAY_H

#include <QtGui/QWidget>
#include <vector>
#include <string>

using namespace std;
class circuit_map;

class MVDisplay : public QWidget {
  Q_OBJECT

public:
  MVDisplay(QWidget *parent = 0);
  ~MVDisplay();

public slots:
  void fit_to_window();
  void zoom_in();
  void zoom_out();
  void hscroll(int);
  void vscroll(int);
  void set_active(bool);
  void set_poly(bool);
  void set_metal(bool);
  void set_levels(bool);
  void state_changed();

signals:
  void track(int);

protected:
  void paintEvent(QPaintEvent *e);
  void resizeEvent(QResizeEvent *e);

  void mousePressEvent(QMouseEvent *e);
  void mouseMoveEvent(QMouseEvent *e);
  void mouseReleaseEvent(QMouseEvent *e);

private:
  unsigned int *generated_image;
  QImage *qimg;
  int *posx, *posy;
  double zoom;
  int xc, yc;
  bool active_on, poly_on, metal_on, levels_on;

  void get(int l, int x, int y, int &circ, bool &inside, int &net, char &type, int &power);
  unsigned int alpha(unsigned int cur, unsigned int val, double level);
  void set_zoom(double zoom);
  void regen_scale();
  void regen_pos();
  void generate_image();
};

#endif

