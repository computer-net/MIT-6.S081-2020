#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char *argv[]) {
  int p1[2], p2[2];
  char buffer[] = {'l'};
  int len = sizeof(buffer);
  pipe(p1);
  pipe(p2);
  if (fork() == 0) {
	close(p1[1]);
	close(p2[0]);
	if (read(p1[0], buffer, len) != len) {
	  printf("child read error!\n");
	  exit(1);
	}
	printf("%d: received ping\n", getpid());
	if (write(p2[1], buffer, len) != len) {
	  printf("child write error\n");
	  exit(1);
	}
	exit(0);
  } else {
	close(p1[0]);
	close(p2[1]);
	if (write(p1[1], buffer, len) != len) {
	  printf("parent write error!\n");
	  exit(1);
	}
	if (read(p2[0], buffer, len) != len) {
	  printf("parent read error!\n");
	  exit(1);
	}
	printf("%d: received pong\n");
	exit(0);
  }
  exit(0);
}
