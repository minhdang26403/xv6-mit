#include "kernel/types.h"
#include "user/user.h"

int main() {
  int p[2];
  if (pipe(p) < 0) {
    fprintf(2, "error: pipe\n");
    exit(1);
  }
  char buf[1];
  if (fork() == 0) {
    read(p[0], buf, 1);
    printf("%d: received ping\n", getpid());
    write(p[1], "o", 1);
    close(p[0]);
    close(p[1]);
    exit(0);
  }

  write(p[1], "o", 1);
  wait(0);
  read(p[0], buf, 1);
  printf("%d: received pong\n", getpid());
  close(p[0]);
  close(p[1]);
  exit(0);
}