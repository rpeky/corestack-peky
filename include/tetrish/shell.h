#ifndef SHELL_H
#define SHELL_H

#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <limits.h> // For PATH_MAX
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

// consider for win32
#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#endif

#define IN_BUFFER 1024
#define MAX_ARGS 128
#define BIN_PATH "./bin/"
#define ever (;;)

void clear_screen(void);
void startup(void);
void cleanup_handlers(void);
int tokenise(char *line, char **argv, size_t cap);
int expand_alias(char **argv, int argc, size_t cap);
void fork_child(char **argv);

extern char project_root[PATH_MAX];
extern char project_bin[PATH_MAX];
void resolve_project_root(void);

#endif
