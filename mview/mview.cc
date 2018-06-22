#undef _FORTIFY_SOURCE

#include <tiffio.h>
#include <expat.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <zlib.h>
#include <errno.h>

#include <string>
#include <vector>

#include "MVMain.h"
#include "globals.h"
#include <reader.h>

int main(int argc, char **argv)
{
  reader rd(argv[1]);
  map_file = rd.gw();
  layers_file = rd.gw();
  pins_file = rd.gw();
  bool cmos;
  const char *mode = rd.gw();
  if(!strcmp(mode, "nmos"))
    cmos = false;
  else if(!strcmp(mode, "cmos"))
    cmos = true;
  else {
    fprintf(stderr, "Mode [%s] unknown\n", mode);
    exit(1);
  }    

  state = new State(layers_file, map_file, pins_file, cmos);

  QApplication app(argc, argv);
  MVMain mv;
  mv.show();

  return app.exec();
}
