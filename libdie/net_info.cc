#undef _FORTIFY_SOURCE

#include "net_info.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>

net_info::net_info(const char *fname, const circuit_map &cmap, const circuit_info &info)
{
  char msg[4096];
  sprintf(msg, "Open %s", fname);
  int fd = open(fname, O_RDONLY);
  if(fd<0) {
    perror(msg);
    exit(2);
  }

  // On Windows, lseek is upper bound for text files, thanks to
  // CLRF translation.
  int size = lseek(fd, 0, SEEK_END);
  lseek(fd, 0, SEEK_SET);

  assert(size != 0); // If file was empty, don't bother.

  char *data = (char *)malloc(size+1);
  #ifdef _WIN32
    // Full read of file will get number of characters. If not buffering
    // full file, getc in for loop might be better.
    int char_size = read(fd, data, size);
    assert(char_size <= size);
    data[char_size] = 0;
  #else
    read(fd, data, size);
    data[size] = 0;
  #endif
  close(fd);

  names.resize(info.nets.size());
  pos = data;
  has_nl = false;

  while(*pos) {
    if(*pos == '\n' || *pos == '#') {
      nl();
      continue;
    }

    char type = gw()[0];
    int x = gi();
    int y = gi();
    std::string name = gw();
    nl();

    int cm;
    switch(type) {
    case 'm': cm = cmap.nl >= 3 ? 2 : 1; break;
    case 'p': cm = 1; break;
    case 'a': cm = 0; break;
    default:
      fprintf(stderr, "Unknown layer type in pins.txt: %c\n", type);
      exit(1);
    }

    int circ = cmap.p(cm, x, y);
    if(circ == -1) {
      fprintf(stderr, "No circuit at %c %d %d\n", type, x, y);
      exit(1);
    }
    int net = info.circs[circ].net;
    if(names[net] != "") {
      fprintf(stderr, "Net name collision %s and %s\n", names[net].c_str(), name.c_str());
      exit(1);
    }
    names[net] = name;
    nets[name] = net;
  }
  free(data);
  pos = NULL;
}

const char *net_info::gw()
{
  assert(!has_nl);
  while(*pos == ' ')
    pos++;
  const char *r = pos;
  while(*pos != ' ' && *pos != '\n')
    pos++;
  assert(pos != r);
  has_nl = *pos == '\n';
  *pos++ = 0;
  return r;
}

void net_info::nl()
{
  if(!has_nl) {
    while(*pos && *pos != '\n')
      pos++;
    if(*pos)
      pos++;
  }
  has_nl = false;
}

int net_info::gi()
{
  return strtol(gw(), 0, 10);
}

double net_info::gd()
{
  return strtod(gw(), 0);
}

std::string net_info::net_name(int net) const
{
  if(net != -1 && names[net] != "")
    return names[net];
  char buf[32];
  sprintf(buf, "%d", net);
  return buf;
}

int net_info::find(std::string name) const
{
  auto i = nets.find(name);
  if(i == nets.end()) {
    fprintf(stderr, "Net %s unknown\n", name.c_str());
    exit(1);
  }
  return i->second;
}
