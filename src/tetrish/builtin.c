#include "tetrish/builtin.h"

/*------------------------- function prototypes ------------------------------*/

static int bi_exit(int argc, char **argv);
static int bi_cd(int argc, char **argv);
static int bi_help(int argc, char **argv);
static int bi_usage(int argc, char **argv);
static int bi_env(int argc, char **argv);
static int bi_setenv(int argc, char **argv);
static int bi_unsetenv(int argc, char **argv);
static int bi_alias(int argc, char **argv);
static int bi_unalias(int argc, char **argv);
static int bi_bins(int argc, char **argv);

/*----------------- helper functions to make posix shell calls ---------------*/

// struct containing the fn call in shell and the actuall fn to call
struct builtin {
	const char *fnname;
	const char *desc;
	bi_func fn;
};

static struct builtin lookup_bifn[] = {
	{"exit", "Exit the shell", bi_exit},
	{"cd", "Change the current directory", bi_cd},
	{"help", "Show the help for built-in functions", bi_help},
	{"usage", "Short usage guide", bi_usage},
	{"env", "List environment variables", bi_env},
	{"setenv", "Set an environment variable", bi_setenv},
	{"unsetenv", "Remove an environment variable", bi_unsetenv},
	{"alias", "Create an alias", bi_alias},
	{"unalias", "Remove an alias", bi_unalias},
	{"bins", "List binaries native to this shell", bi_bins},

	{NULL, NULL, NULL}};

// to export for autofill in raw mode
const char *builtins[] = {"exit",    "cd",     "help",     "usage",
	"env",     "setenv", "unsetenv", "alias",
	"unalias", "bins",   NULL};

int bi_call(int argc, char **argv) {
	if (argc == 0)
		return -1;

	// iterate through lookup struct
	// check if argv is in the lookup
	// make the function call
	for (struct builtin *b = lookup_bifn; b->fnname; ++b) {
		if (strcmp(argv[0], b->fnname) == 0)
			return b->fn(argc, argv);
	}

	return -1; // not built in function, check system programs
}
/*--------------- posix shell function call definitions --------------------*/

// this guy dosent really return anyth, exit just exits
__attribute__((noreturn)) // for compiler to know this dosent return
static int
bi_exit(int argc, char **argv) {
	int status = (argc > 1) ? atoi(argv[1]) : 0;
	exit(status);
}

static int bi_cd(int argc, char **argv) {
	const char *dest;
	if (argc == 1) { // empty cd call should return to home
		dest = getenv("HOME");
		if (!dest) // $HOME is not set for some reason
			return fprintf(stderr, "cd: HOME environment not set\n");
	} else if (strcmp(argv[1], "-") == 0) { // cd - should be the same as
		// cd "$OLDPWD" && pwd
		dest = getenv("OLDPWD");
		if (!dest)
			return fprintf(stderr, "cd: OLDPWD environment not set\n");
		puts(dest); // echos the dir also
	} else {
		dest = argv[1]; // if not argv[1] should be the path
	}

	char cwd_old[PATH_MAX];
	getcwd(cwd_old, sizeof cwd_old);

	if (chdir(dest) == -1) { // cd, if not perror
		perror("cd");
		return 1;
	}

	setenv("OLDPWD", cwd_old, 1);
	return 0;
}

static int bi_help(int argc, char **argv) {
	(void)argc; // both not needed here
	(void)argv; // just declaring to get rid of compile errors

	puts("\nBuilt-in commands in the shell:");
	for (const struct builtin *b = lookup_bifn; b->fnname; ++b) {
		printf("\t %-8s  %s\n", b->fnname,
	 b->desc ? b->desc : "(No description yet)");
	}
	puts("Type: usage <command> for details\n");
	return 0;
}

enum bicmd_enum {
	CMD_EXIT,
	CMD_CD,
	CMD_HELP,
	CMD_USAGE,
	CMD_ENV,
	CMD_SETENV,
	CMD_UNSETENV,
	CMD_ALIAS,
	CMD_UNALIAS,
	CMD_BINS,
	UNKNOWN
};

static enum bicmd_enum cmd_lookup(const char *cmd) {
	if (strcmp(cmd, "exit") == 0)
		return CMD_EXIT;
	if (strcmp(cmd, "cd") == 0)
		return CMD_CD;
	if (strcmp(cmd, "help") == 0)
		return CMD_HELP;
	if (strcmp(cmd, "usage") == 0)
		return CMD_USAGE;
	if (strcmp(cmd, "env") == 0)
		return CMD_ENV;
	if (strcmp(cmd, "setenv") == 0)
		return CMD_SETENV;
	if (strcmp(cmd, "unsetenv") == 0)
		return CMD_UNSETENV;
	if (strcmp(cmd, "alias") == 0)
		return CMD_ALIAS;
	if (strcmp(cmd, "unalias") == 0)
		return CMD_UNALIAS;
	if (strcmp(cmd, "bins") == 0)
		return CMD_BINS;

	return UNKNOWN;
}

static int bi_usage(int argc, char **argv) {

	if (argc == 1) { // usage only
		puts("\nSpeedrunShell usage:");
		puts("\t  help                        list of built-in "
       "commands");
		puts("\t  usage <builtin>             detailed help for "
       "command");
		puts("\t  <builtin>    [args...]      run a command from help");
		puts("\t  <extern-cmd> [args...]      run a program from "
       "$PATH");
		puts("\t  sys                         neofetch lightweight implement");
		puts("\t  backup                      tarball path in BIN path");
		puts("\t  dspawn                      spawn regular daemon");
		puts("\t  dcheck                      check on logged daemons");
		puts("\t  dkill        [args...]      kill active daemons");
		puts("\t  dplant <plant name>         spawn a plant daemon");
		puts("\t  dplant <plant name> water   water plant daemon");
		puts("\t  dplant <plant name> stats   statcheck plant daemon\n");
		return 0;
	}

	switch (cmd_lookup(argv[1])) {
		case CMD_EXIT:
			puts("\n\texit:\n"
	"      \tExit the shell");
			break;

		case CMD_CD:
			puts("\n\tcd [path/to/directory]\n"
	"\tcd -\n"
	"      \t\tChange the working directory.\n"
	"      \t\tDefaults to $HOME with no path.\n"
	"      \n\t\tcd - switch to $OLDPWD and prints working "
	"directory.\n");
			break;

		case CMD_HELP:
			puts("\n\thelp\n"
	"      \t\tLists all built-in commands with a short "
	"description.\n");
			break;

		case CMD_USAGE:
			puts("\n\tusage [built-in cmd]\n"
	"\tusage\n"
	"      \t\tShow usage of built-in command listed in "
	"help.\n");
			break;

		case CMD_ENV:
			puts("\n\tenv\n"
	"\tenv [$PATH]..."
	"      \t\tPrints all environment variables of this "
	"shell.\n"
	"      \t\tPrints specified environment variable(s) "
	"of this shell if they exist\n");
			break;

		case CMD_SETENV:
			puts("\n\tsetenv [ENV] = [value]\n"
	"       \t\tAdds to list of process' environment "
	"variables\n");
			break;

		case CMD_UNSETENV:
			puts("\n\tunsetenv [ENV]...\n"
	"      \t\tDeletes any environment variable whose KEY "
	"matches any environment variables"
	"      \n\tDoes nothing if KEY does not exist\n");
			break;

		case CMD_ALIAS:
			puts("\n\talias [name]=\"value\"\n"
	"       \t\tAdds alias of value to name variable\n");
			break;

		case CMD_UNALIAS:
			puts("\n\tunsetenv [name]\n"
	"      \t\tDeletes any alias variable whose KEY "
	"matches the name field"
	"      \n\tDoes nothing if alias does not exist\n");
			break;

		case CMD_BINS:
			puts("\n\tbins\n"
	"       \t\tLists down the compiled binaries that can be "
	"used in the shell\n");
			break;

		default:
			fprintf(stderr, "usage: no such built-in: %s\n", argv[1]);
			return 1;
	}
	return 0;
}

/*---------------- env helper functions ------------------------*/
// man environ
extern char **environ;

static int bad_name(const char *s) {
	if (!*s || strchr(s, '='))
		return 1;

	for (const unsigned char *p = (const unsigned char *)s; *p; ++p)
		if (!isalnum(*p) && *p != '_')
			return 1;

	return 0;
}
/*---------------- env helper functions ------------------------*/

static int bi_env(int argc, char **argv) {

	if (argc == 1) { // print all env $PATH
		for (char **s = environ; *s; s++)
			puts(*s);
		return 0;
	}

	for (int i = 1; i < argc; ++i) { // check through the provided arguments
		const char *name = argv[i];
		size_t nlen = strlen(name);

		int found = 0;
		for (char **s = environ; *s; ++s) { // check if match
			if (strncmp(*s, name, nlen) == 0 && (*s)[nlen] == '=') {
				puts(*s);
				found = 1;
				break;
			}
		}
		if (!found) // provide warning if name of path not found
			fprintf(stderr, "env: %s not found\n", name);
	}
	return 0;
}

static int bi_setenv(int argc, char **argv) {

	if (argc < 2) { // non valid number of arguments
		fprintf(stderr, "usage: setenv NAME [VALUE]\n");
		return 1;
	}

	// parse for invalid names
	// split the name and value
	// setenv
	for (int i = 1; i < argc; ++i) {
		char *eq = strchr(argv[i], '=');
		if (!eq) {
			fprintf(stderr, "setenv: invalid format, expected NAME=VALUE\n");
			return 1;
		}

		*eq = '\0';
		char *name = argv[i];
		char *value = eq + 1;

		if (bad_name(name)) {
			fprintf(stderr, "setenv: invalid name format %s\n", argv[i]);
			return 1;
		}

		// strip quotations
		size_t len = strlen(value);
		if ((value[0] == '"' || value[0] == '\'') && value[0] == value[len - 1] &&
			len > 1) {
			value[len - 1] = '\0';
			value++;
			len -= 2;
		}
		// adjust relative path to aboslute to call it wherever
		// default expects absolute path
		char adjusted[PATH_MAX] = {0};
		if (value[0] != '/') {
			char cwd[PATH_MAX];
			if (!getcwd(cwd, sizeof(cwd))) {
				perror("getcwd");
				return 1;
			}
			size_t len_adj = snprintf(NULL, 0, "%s/%s", cwd, value) + 1;
			snprintf(adjusted, len_adj, "%s/%s", cwd, value);
			value = adjusted;
		}

		printf("Setting name for %s %s\n", name, value);
		if (setenv(name, value, 1) == -1) {
			perror("setenv");
			return 1;
		}
	}
	return 0;
}

static int bi_unsetenv(int argc, char **argv) {
	// check for invalid arguments (<1 arg)
	if (argc < 2) {
		fprintf(stderr, "usage: unsetenv NAME [NAME...]\n");
		return 1;
	}

	// parse for invalid names
	for (int i = 1; i < argc; ++i) {
		if (bad_name(argv[i])) {
			fprintf(stderr, "unsetenv: invalid name %s\n", argv[i]);
			return 1;
		}
	}

	// unset env
	for (int i = 1; i < argc; ++i) {
		if (unsetenv(argv[i]) == -1) {
			perror("unsetenv");
			return 1;
		}
	}
	return 0;
}

/*---------------- alias helper functions ------------------------*/
struct alias {
	char *name;
	char *value;
	struct alias *next;
};

static struct alias *alist = NULL;

static struct alias *alias_find(const char *n) {
	for (struct alias *p = alist; p; p = p->next) // p!=NULL
		if (strcmp(p->name, n) == 0)                // check key
			return p;
	return NULL;
}

const char *alias_lookup(const char *n) {
	struct alias *a = alias_find(n);
	return a ? a->value : NULL;
}

void alias_free(void) {
	for (struct alias *p = alist, *n; p; p = n) {
		n = p->next;
		free(p->name);
		free(p->value);
		free(p);
	}
}

/*---------------- alias helper functions ------------------------*/

static int bi_alias(int argc, char **argv) {
	if (argc == 1) { // list currnet aliases
		for (struct alias *p = alist; p; p = p->next)
			printf("alias %s='%s'\n", p->name, p->value);
		return 0;
	}

	for (int i = 1; i < argc; ++i) {
		char *eq = strchr(argv[i], '=');

		if (!eq) { // check alias
			struct alias *a = alias_find(argv[i]);
			if (a)
				printf("alias %s='%s'\n", a->name, a->value);
			else
				fprintf(stderr, "alias: %s not found\n", argv[i]);
			continue;
		}

		*eq = '\0'; // split name and rhs
		char *name = argv[i];
		char rhs[IN_BUFFER] = {0};

		// Copy the rest of the current argument after '='
		strncpy(rhs, eq + 1, sizeof(rhs) - 1);
		rhs[sizeof(rhs) - 1] = '\0';
		size_t rhs_len = strlen(rhs);

		if (rhs_len > 0 && (rhs[0] == '\'' || rhs[0] == '"')) {
			// strip end quote + skip initial quote
			char quote = rhs[0];
			memmove(rhs, rhs + 1, rhs_len);
			rhs_len--;

			if (rhs_len > 0 && rhs[rhs_len - 1] == quote) {
				rhs[rhs_len - 1] = '\0';
			} else {
				int closed = 0;
				for (int j = i + 1; j < argc; ++j) {
					size_t arg_len = strlen(argv[j]);
					if (rhs_len + arg_len + 2 >= IN_BUFFER) {
						fprintf(stderr, "alias: alias too long\n");
						fflush(stderr);
						return 1;
					}
					rhs[rhs_len++] = ' ';
					strcpy(&rhs[rhs_len], argv[j]);
					rhs_len += arg_len;

					if (argv[j][arg_len - 1] == quote) {
						rhs[rhs_len - 1] = '\0'; // remove closing
						// quote
						closed = 1;
						i = j; // skip
						break;
					}
				}
				if (!closed) {
					fprintf(stderr, "alias: unmatched quote\n");
					fflush(stderr);
					return 1;
				}
			}
		}
		// no rhs
		// Add or update alias
		struct alias *a = alias_find(name);
		if (!a) {
			a = calloc(1, sizeof(*a));
			if (!a) {
				perror("calloc");
				return 1;
			}
			a->name = strdup(name);
			a->next = alist;
			alist = a;
		} else {
			free(a->value);
		}
		a->value = strdup(rhs);
	}
	return 0;
}

static int bi_unalias(int argc, char **argv) {
	if (argc != 2) {
		fprintf(stderr, "unalias: usage: unalias name\n");
		return 1;
	}

	struct alias **pp = &alist;
	while (*pp && strcmp((*pp)->name, argv[1]) != 0)
		pp = &(*pp)->next;

	if (!*pp) {
		fprintf(stderr, "unalias: %s: error not found\n", argv[1]);
		return 1;
	}

	struct alias *rip = *pp;
	*pp = rip->next;
	free(rip->name);
	free(rip->value);
	free(rip);
	return 0;
}

static int bi_bins(int argc, char **argv) {
	(void)argc;
	(void)argv;

	DIR *d = opendir("./bin");
	if (!d) {
		perror("bins: failed to open ./bin");
		return 1;
	}

	struct dirent *entry;
	while ((entry = readdir(d)) != NULL) {
		if (entry->d_name[0] == '.')
			continue;

		char path[PATH_MAX];
		snprintf(path, sizeof path, "./bin/%s", entry->d_name);

		struct stat st;
		if (stat(path, &st) == 0 &&   // check file exists
			S_ISREG(st.st_mode) &&    // check if regular file
			(st.st_mode & S_IXUSR)) { // check if executable by owner
			printf("%s\n", entry->d_name);
		}
	}
	closedir(d);
	return 0;
}
