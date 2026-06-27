#ifndef BUILTIN_H
#define BUILTIN_H

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define IN_BUFFER 1024

typedef int (*bi_func)(int argc, char **argv);

int bi_call(int argc, char **argv); // return -1 if not built in
                                    // return >=0 index of bi fn
extern const char *builtins[];
const char *alias_lookup(const char *name);
void alias_free(void);

#endif
