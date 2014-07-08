#undef _FORTIFY_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#include "SVMain.h"
#include "state.h"

using namespace std;

int main(int argc, char **argv)
{
  schem_file = argv[1];

  if(argv[2]) {
    char msg[4096];
    sprintf(msg, "Error opening trace file %s", argv[2]);
    int fd = open(argv[2], O_RDONLY);
    if(fd < 0) {
      perror(msg);
      exit(1);
    }
    trace_size = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);

    unsigned char *data = (unsigned char *)malloc(trace_size);
    read(fd, data, trace_size);
    close(fd);
    trace_data = data;

  } else {
    trace_data = NULL;
    trace_size = 0;
  }

  freetype_init();

  QApplication app(argc, argv);
  SVMain sv;
  sv.show();

  return app.exec();
}
