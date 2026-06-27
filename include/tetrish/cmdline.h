#ifndef CMDLINE_H
#define CMDLINE_H

#include <stddef.h>
#include <sys/types.h>

void line_raw_init(void);
void line_raw_cleanup(void);
ssize_t line_read(char *p, char *buf, size_t cap);
void history(const char *line);
void hist_free(void);
void restore_cooked(void);
void catch_and_restore(int signo);

#endif
