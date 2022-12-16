#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

#define O_RDONLY 0

void find(char *path, char *filename) {
  int fd;
  struct dirent de;
  struct stat st;
  char buf[512], *p;

  if ((fd = open(path, O_RDONLY)) < 0) {
    fprintf(2, "find: cannot find %s\n", path);
    exit(1);
  }
  if (fstat(fd, &st) < 0) {
    fprintf(2, "find: cannot stat %s\n", path);
    exit(1);
  }

  // Store the current path into buffer to add full path
  strcpy(buf, path);
  p = buf + strlen(buf);
  *p++ = '/';
  while (read(fd, &de, sizeof(de)) == sizeof(de)) {
    if (de.inum == 0) {
      continue;
    }
    memmove(p, de.name, DIRSIZ);
    p[DIRSIZ] = 0;
    if (stat(buf, &st) < 0) {
      fprintf(2, "find: cannot stat %s\n", buf);
      continue;
    }
    switch (st.type) {
      case T_DEVICE:
      case T_FILE:
        if (strcmp(de.name, filename) == 0) {
          printf("%s\n", buf);
        }
        break;
      case T_DIR:
        if (strcmp(de.name, ".") != 0 && strcmp(de.name, "..") != 0) {
          find(buf, filename);
        }
    }
  }
  close(fd);
}

int main(int argc, char *argv[]) {
  if (argc != 3) {
    fprintf(2, "usage: find [path] [filename]\n");
    exit(1);
  }

  find(argv[1], argv[2]);
  exit(0);
}