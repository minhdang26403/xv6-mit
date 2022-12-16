#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/param.h"

#define MAXLEN 100
#define STDIN 0

int readline(char *new_argv[MAXARG], int curr_argc) {
  char buf[MAXLEN];
  int n = 0;
  while (read(STDIN, buf + n, 1)) {
    if (n == MAXLEN - 1) {
      fprintf(2, "argument is too long\n");
      exit(1);
    }
    if (buf[n] == '\n') {
      break;
    }
    n++;
  }
  if (n == 0) {
    return 0;
  }
  buf[n] = 0;
  int offset = 0;
  while (offset < n) {
    new_argv[curr_argc++] = buf + offset;
    while (buf[offset] != ' ' && offset < n) {
      offset++;
    }
    while (buf[offset] == ' ' && offset < n) {
      buf[offset++] = 0;
    }
  }
  return curr_argc;
}

int main(int argc, char *argv[]) {
  if (argc <= 1) {
    fprintf(2, "usage: xargs command [args...]\n");
    exit(1);
  }

  char command[strlen(argv[1]) + 1];
  char *new_argv[MAXARG];
  strcpy(command, argv[1]);
  for (int i = 1; i < argc; ++i) {
    new_argv[i - 1] = malloc(strlen(argv[i]) + 1);
    strcpy(new_argv[i - 1], argv[i]);
  }

  int curr_argc;
  while ((curr_argc = readline(new_argv, argc - 1)) != 0) {
    new_argv[curr_argc] = 0;
    if (fork() == 0) {
      exec(command, new_argv);
      fprintf(2, "exec failed\n");
      exit(1);
    }
    wait(0);
  }
  for (int i = 0; i < curr_argc; ++i) {
    free(new_argv[i]);
  }
  exit(0);
}