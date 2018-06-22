#undef _FORTIFY_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>

#include "SVMain.h"
#include "state.h"

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

    assert(trace_size != 0); // If file was empty, don't bother.

    unsigned char *data = (unsigned char *)malloc(trace_size+1);
    #ifdef _WIN32
      // Full read of file will get number of characters. If not buffering
      // full file, getc in for loop might be better.
      int char_size = read(fd, data, trace_size);
      assert(char_size <= trace_size);
      data[char_size] = 0;
    #else
      read(fd, data, trace_size);
      data[trace_size] = 0;
    #endif
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
