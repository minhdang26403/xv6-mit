#include "kernel/types.h"
#include "user/user.h"

#define READEND 0
#define WRITEEND 1

void generate_prime(int *pl) {
  close(pl[WRITEEND]);
  int prime;
  if (read(pl[READEND], &prime, sizeof(int)) == 0) {
    close(pl[READEND]);
    return;
  }

  printf("prime %d\n", prime);
  int pr[2];
  pipe(pr);

  if (fork() == 0) {
    generate_prime(pr);
    exit(0);
  }

  close(pr[READEND]);
  int num;
  while (read(pl[READEND], &num, sizeof(int)) != 0) {
    if (num % prime != 0) {
      write(pr[WRITEEND], &num, sizeof(int));
    }
  }
  close(pl[READEND]);
  close(pr[WRITEEND]);
  wait((int *)0);
}

int main() {

  int p[2];
  pipe(p);

  if (fork() == 0) {
    generate_prime(p);
    exit(0);
  }

  close(p[READEND]);
  for (int i = 2; i <= 35; ++i) {
    write(p[WRITEEND], &i, sizeof(int));
  }
  close(p[WRITEEND]);
  wait((int *)0);
  exit(0);
}