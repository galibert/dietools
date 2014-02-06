#undef _FORTIFY_SOURCE

#include "SVMain.h"
#include "state.h"

using namespace std;

int main(int argc, char **argv)
{
  schem_file = argv[1];

  freetype_init();

  QApplication app(argc, argv);
  SVMain sv;
  sv.show();

  return app.exec();
}
