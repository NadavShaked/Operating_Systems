#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/myParam.h"

int main(int argc, char **argv)
{
  int i;

  if (argc < 2)
  {
    fprintf(2, "usage: kill pid...\n");
    exit(1);
  }

  // ------------------------ Task 2.1.1 ------------------------
  for (i = 1; i < argc - 1; i += 2)
    kill(atoi(argv[i]), atoi(argv[i + 1]));
  // ------------------------------------------------------------

  exit(0);
}
