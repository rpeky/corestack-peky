#include "tetrish/cmdline.h"
#include "tetrish/builtin.h"
#include "tetrish/shell.h"
#include <ctype.h>
#include <dirent.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

struct termios orig;
#define BUF_MAX 512
#define MAX_MATCHES 64

void line_raw_init(void) {
	fflush(stdout);
	fflush(stdin);
	if (tcgetattr(STDIN_FILENO, &orig) == -1)
		perror("tcsetattr init");
	struct termios raw = orig;
	raw.c_iflag &= ~(IXON);
	raw.c_lflag &= ~(ECHO | ICANON);
	raw.c_cc[VMIN] = 1;
	raw.c_cc[VTIME] = 0;
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
		perror("tcsetattr");
}

void line_raw_cleanup(void) {
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig) == -1)
		perror("tcsetattr cleanup");
}

void catch_and_restore(int signo) {
	line_raw_cleanup();
	_exit(128 + signo);
}

static char *hist[128];
static int hlen = 0, hhead = 0, hcur = 0;

void history(const char *line) {
	if (!*line)
		return;
	free(hist[hhead]);
	hist[hhead] = strdup(line);
	hhead = (hhead + 1) % 128;
	if (hlen < 128)
		++hlen;
	hcur = hhead;
}

static const char *hist_up(void) {
	if (!hlen)
		return NULL;
	int next = (hcur + 127) % 128;
	if (next == hhead)
		return NULL;
	hcur = next;
	return hist[hcur];
}

static const char *hist_down(void) {
	if (hcur == hhead)
		return NULL;
	hcur = (hcur + 1) % 128;
	return (hcur == hhead) ? "" : hist[hcur];
}

void hist_free(void) {
	for (int i = 0; i < 128; ++i) {
		free(hist[i]);
	}
	hlen = hhead = hcur = 0;
}

void restore_cooked(void) {
	fflush(stdout);
	fflush(stdin);
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig) == -1)
		perror("tcsetattr restore_cooked");
}

static void redraw(const char *prompt, const char *buf, size_t len,
		   size_t pos) {
	printf("\r\033[K%s%.*s", prompt, (int)len, buf);
	if (pos < len)
		printf("\033[%zuD", len - pos);
	fflush(stdout);
}

static int is_dir(const char *path) {
	struct stat st;
	return (stat(path, &st) == 0) && S_ISDIR(st.st_mode);
}

static int cmp_str(const void *a, const void *b) {
	return strcmp((const char *)a, (const char *)b);
}

static int add_dir_matches(char matches[MAX_MATCHES][BUF_MAX],
			   int match_count, const char *token) {
	char dir_part[BUF_MAX] = ".";
	char base_part[BUF_MAX] = "";
	const char *slash = strrchr(token, '/');

	if (slash) {
		size_t dir_len = (size_t)(slash - token) + 1;
		if (dir_len >= sizeof(dir_part))
			return match_count;

		memcpy(dir_part, token, dir_len);
		dir_part[dir_len] = '\0';

		strncpy(base_part, slash + 1, sizeof(base_part) - 1);
	} else {
		strncpy(base_part, token, sizeof(base_part) - 1);
	}

	DIR *dp = opendir(dir_part);
	if (!dp)
		return match_count;

	struct dirent *e;
	while (match_count < MAX_MATCHES && (e = readdir(dp)) != NULL) {
		if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
			continue;

		if (!strncmp(e->d_name, base_part, strlen(base_part))) {
			int written;

			if (slash) {
				written = snprintf(matches[match_count], BUF_MAX, "%s%s",
		       dir_part, e->d_name);
			} else {
				written = snprintf(matches[match_count], BUF_MAX, "%s", e->d_name);
			}

			if (written < 0 || written >= BUF_MAX)
				continue;

			++match_count;
		}
	}

	closedir(dp);
	return match_count;
}

// kms the nested if else w 80 term width
// legit personal skill issue
// sorry classic C devs
static void tab_complete(char *buf, size_t *len, size_t *pos, size_t cap,
			 const char *prompt) {
	size_t start = *pos;
	while (start > 0 && !isspace((unsigned char)buf[start - 1]))
		--start;

	// defensive check if token bigger than buffer
	if (start > *pos || *pos - start >= BUF_MAX)
		return;
	char token[BUF_MAX] = {0};

	strncpy(token, buf + start, *pos - start);
	token[*pos - start] = '\0';

	int is_first_word = 1;
	for (size_t i = 0; i < start; ++i) {
		if (isspace((unsigned char)buf[i])) {
			is_first_word = 0;
			break;
		}
	}

	// find matches
	char matches[MAX_MATCHES][BUF_MAX];
	int match_count = 0;

	// builtin commands
	if (is_first_word) {
		// builtin matching exported from builtin
		for (int i = 0; builtins[i]; ++i) {
			if (strncmp(builtins[i], token, strlen(token)) == 0 &&
				match_count < MAX_MATCHES) {
				strncpy(matches[match_count++], builtins[i], BUF_MAX);
				matches[match_count - 1][BUF_MAX - 1] = '\0';
			}
		}
	}

	// check path executables
	if (is_first_word) {
		char *path_env = getenv("PATH");
		if (path_env) {
			char *path_dup = strdup(path_env);
			for (char *dir = strtok(path_dup, ":"); dir && match_count < MAX_MATCHES;
			dir = strtok(NULL, ":")) {
				DIR *dp = opendir(dir);
				if (!dp) {
					continue;
				}
				struct dirent *ent;
				while ((ent = readdir(dp)) && match_count < MAX_MATCHES) {
					if (strncmp(ent->d_name, token, strlen(token)) == 0) {
						int dup = 0;
						for (int k = 0; k < match_count; ++k) {
							if (!strcmp(matches[k], ent->d_name)) {
								dup = 1;
								break;
							}
						}
						if (!dup) {
							strncpy(matches[match_count], ent->d_name, BUF_MAX);
							matches[match_count][BUF_MAX - 1] = '\0';
							++match_count;
						}
					}
				}
				closedir(dp);
			}
			free(path_dup);
		}
	}

	// check proj bin
	if (is_first_word && match_count < MAX_MATCHES && *project_bin) {
		DIR *dp = opendir(project_bin);
		if (dp) {
			struct dirent *e;
			while (match_count < MAX_MATCHES && (e = readdir(dp)) != NULL) {
				if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
					continue;
				if (!strncmp(e->d_name, token, strlen(token))) {
					strncpy(matches[match_count++], e->d_name, BUF_MAX);
					matches[match_count - 1][BUF_MAX - 1] = '\0';
				}
			}
			closedir(dp);
		}
	}

	// match in cwd
	if (!is_first_word && match_count < MAX_MATCHES) {
		match_count = add_dir_matches(matches, match_count, token);
	}

	// matching logic
	if (match_count == 0) {
		// L
		putchar('\a');
		return;
	}

	// sort and remove dups
	qsort(matches, match_count, sizeof matches[0], cmp_str);
	int unique = 0;
	for (int i = 1; i < match_count; ++i) {
		if (strcmp(matches[i], matches[unique]) != 0) {
			++unique;
			if (unique != i)
				strncpy(matches[unique], matches[i], BUF_MAX);
			matches[unique][BUF_MAX - 1] = '\0';
		}
	}
	match_count = unique + 1;

	if (match_count == 1) {
		const char *completion = matches[0] + strlen(token);
		size_t comp_len = strlen(completion);
		int add_slash = is_dir(matches[0]);
		size_t extra = comp_len + (add_slash ? 1 : 0);

		if (*len + extra >= cap) {
			putchar('\a');
			return;
		}

		memmove(buf + *pos + extra, buf + *pos, *len - *pos);
		memcpy(buf + *pos, completion, comp_len);

		if (add_slash)
			buf[*pos + comp_len] = '/';

		*pos += extra;
		*len += extra;
		buf[*len] = '\0';

		if (prompt)
			redraw(prompt, buf, *len, *pos);
	}

	// multi match
	printf("\n");
	for (int i = 0; i < match_count; ++i) {
		printf("%s%s ", matches[i], is_dir(matches[i]) ? "/" : "");
	}
	printf("\n");
	fflush(stdout);
	if (prompt)
		redraw(prompt, buf, *len, *pos);
}
ssize_t line_read(char *p, char *out, size_t cap) {
	tcflush(STDIN_FILENO, TCIFLUSH);
	if (cap == 0)
		return -1;

	char *buf = out;
	size_t len = 0;
	size_t max = cap - 1;
	size_t pos = len;

	if (p)
		redraw(p, buf, len, pos);

	for (;;) {
		if (!buf) {
			fputs("INTERNAL ERROR: buf is NULL\n", stderr);
			abort(); /* triggers immediately if the impossible
		  happens */
		}
		int c = getchar();
		if (c == EOF)
			return -1;
		if (c == '\n') {
			putchar('\n');
			out[len] = '\0';
			history(out);
			return (size_t)len;
		}
		if ((c == 127 || c == '\b') && pos > 0) { // backspace
			memmove(&buf[pos - 1], &buf[pos], len - pos);
			--len;
			--pos;
			if (p)
				redraw(p, buf, len, pos);
			continue;
		}
		if (c == 4) { // CTRL-D
			if (len == 0)
				return -1;
			continue;
		}
		if (c == 27) { // 'esc'
			int a = getchar(), b = getchar();
			if (a == '[' && b == 'A') { // up arrow
				const char *h = hist_up();
				if (h) {
					len = snprintf(buf, cap, "%s", h);
					pos = len;
					if (p)
						redraw(p, buf, len, pos);
				}
			} else if (a == '[' && b == 'B') { // down arrow
				const char *h = hist_down();
				if (h) {
					len = snprintf(buf, cap, "%s", h);
					pos = len;
					if (p)
						redraw(p, buf, len, pos);
				}
			} else if (a == '[' && b == 'C') { // right arrow
				if (pos < len) {
					++pos;
					if (p)
						redraw(p, buf, len, pos);
				}
			} else if (a == '[' && b == 'D') { // left arrow
				if (pos > 0) {
					--pos;
					if (p)
						redraw(p, buf, len, pos);
				}
			} else if (a == '[' && b == '3') { // delete key
				int cr_check = getchar();
				if (cr_check == '~' && pos < len) {
					memmove(&buf[pos], &buf[pos + 1], len - pos - 1);
					--len;
					buf[len] = '\0';
					if (p)
						redraw(p, buf, len, pos);
				}
			} else if (a == '[' && b == '1') { // Ctrl + arrow for
				// fast skip
				int c3 = getchar();              // ';'
				int c4 = getchar();              // '5'
				int c5 = getchar();              // check if C/D ->
				// right or left
				if (c3 == ';' && c4 == '5') {
					if (c5 == 'C') { // jump right
						while (pos < len && isalnum(buf[pos]))
							++pos;
						while (pos < len && !isalnum(buf[pos]))
							++pos;
						if (p)
							redraw(p, buf, len, pos);
					} else if (c5 == 'D') {
						if (pos > 0)
							--pos;
						while (pos > 0 && isalnum(buf[pos - 1]))
							--pos;
						while (pos > 0 && !isalnum(buf[pos - 1]))
							--pos;
						if (p)
							redraw(p, buf, len, pos);
					}
				}
			} else {
				while ((b = getchar()) != -1 && isprint(b))
					continue;
			}
			continue;
		}
		if (c == '\t') {
			// to decide if its worth making a parser
			// for auto completion
			// kms, was fun tho
			tab_complete(buf, &len, &pos, cap, p);
			continue;
		}

		// cursor position
		if (isprint(c) && len < max) {
			memmove(&buf[pos + 1], &buf[pos], len - pos);
			buf[pos++] = c;
			++len;
			buf[len] = '\0';
			if (p)
				redraw(p, buf, len, pos);
		} else if (!isprint(c)) {
			putchar('\a');
			continue;
		}
	}
}
