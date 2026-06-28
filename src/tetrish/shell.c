#define _XOPEN_SOURCE 700
#include "tetrish/shell.h"
#include "tetrish/builtin.h"
#include "tetrish/cmdline.h"
#include <termios.h>
#include <time.h>
extern void hist_free(void);
extern struct termios orig;
int argv_allocated = 0;

/*-----------------helper prototypes -------------------------*/

static void type_prompt(char *dst, size_t cap);
static void child_sancheck(pid_t pid, int status, char run);
static void source_rc(void);
static void run_rc(FILE *f);
static void create_empty_rc(const char *path);
static void tty_disable_ctrl_c(void);
static void tty_restore_ctrl_c(void);
static void tty_flags_sancheck(char run);
static void add_bin_topath(void);
static void intentional_terminate_log(void);
static void check_tempdir(void);
static void check_archivedir(void);
static void startup_log(void);

/*--------------- global declarations ----------------------*/
char project_root[PATH_MAX];
char project_bin[PATH_MAX];

/*--------------- function declarations ----------------------*/
const char *project_bin_fallback = "/home/peky/git_repos/CSE50005/bin";

void resolve_project_root(void) {
	char path[PATH_MAX];
	ssize_t n = readlink("/proc/self/exe", path, sizeof(path) - 1);
	if (n == -1) {
		perror("readlink");
		strncpy(project_root, ".", sizeof(project_root) - 1);
	} else {
		path[n] = '\0';

		char *bin_dir = dirname(path);
		strncpy(project_bin, bin_dir, sizeof(project_bin) - 1);

		char tmp[PATH_MAX];
		strncpy(tmp, bin_dir, sizeof(tmp) - 1);
		strncpy(project_root, dirname(tmp), sizeof(project_root) - 1);
		return;
	}
	strncpy(project_bin, "./bin", sizeof(project_bin) - 1);
	project_bin[sizeof(project_bin) - 1] = '\0';
}

void clear_screen(void) {
	// man clear -> \033[2J or /033[3J
	// man 4 console_codes
	// ESC [2J ESC [H
	fprintf(stdout, "\033[2J\033[H"); // see man clear
}

// sh style cwd>, fallback to speedrunshell>
static void type_prompt(char *dst, size_t cap) {
	if (getcwd(dst, cap) == NULL)
		snprintf(dst, cap, "speedrunshell");
	strncat(dst, "> ", cap - strlen(dst) - 1);
}

static void child_sancheck(pid_t pid, int status, char run) {
	if (!run)
		return;
	// this part required a little chatgpt to sancheck
	if (WIFEXITED(status)) {
		int ecode = WEXITSTATUS(status);
		// 0 - success
		// 1-125 program defaults
		// 126 - command does not exist
		// 127 - exist but failed to execute
		printf("[child %d exited with status %d]\n", pid, ecode);

	} else if (WIFSIGNALED(status)) {
		int sig = WTERMSIG(status);
		printf("[child %d killed by signal %d%s]\n", pid, sig,
	 WCOREDUMP(status) ? " (core dumped)" : "");

	} else if (WIFSTOPPED(status)) {
		printf("[child %d stopped by signal %d]\n", pid,
	 WSTOPSIG(status));
	}
	fflush(stdout);
}

static void source_rc(void) {
	// https://unix.stackexchange.com/questions/3809/how-can-i-make-a-program-executable-from-everywhere
	// also i asked chatgpt how to get the path for this my bad lol inferior
	// c programmer kms
	char rc_path[PATH_MAX] = {0};
	char home_path[PATH_MAX] = {0};

	// check repo root
	int n = snprintf(rc_path, sizeof(rc_path), "%s/.tetrishrc", project_root);
	if (n < 0 || n >= (int)sizeof(rc_path)) {
		fprintf(stderr, "rc path too long\n");
		return;
	}

	// check home dir in /home/usr
	const char *home = getenv("HOME");
	if (home) {
		int n = snprintf(home_path, sizeof(home_path), "%s/.tetrishrc", home);
		if (n < 0 || n >= (int)sizeof(home_path)) {
			home_path[0] = '\0';
		}
	}

	FILE *fp = fopen(rc_path, "r");
	// if found in repo root
	if (fp) {
		run_rc(fp);
		fclose(fp);
		return;
	}

	// if found in home dir
	fp = home_path[0] ? fopen(home_path, "r") : NULL;
	if (fp) {
		run_rc(fp);
		fclose(fp);
		return;
	}

	// otherwise make an empty file
	create_empty_rc(rc_path);
}

static void run_rc(FILE *f) {
	char *line = NULL;
	size_t cap = 0;

	while (getline(&line, &cap, f) != -1) {
		// remove \n from parse
		size_t len = strlen(line);
		if (len > 0 && line[len - 1] == '\n')
			line[len - 1] = '\0';

		// skip spaces, tabs, blank lines and comments
		char *s = line;
		while (*s == ' ' || *s == '\t')
			++s;
		if (*s == '\0' || *s == '#')
			continue;

		// tokenise the rc inputs
		char *argv[MAX_ARGS];
		int argc = 0;
		char *tok = strtok(s, " \t");

		while (tok && argc + 1 < MAX_ARGS) {
			argv[argc++] = tok;
			tok = strtok(NULL, " \t");
		}

		argv[argc] = NULL;

		// try builtins

		if (bi_call(argc, argv) >= 0)
			continue; // skip fork since using built in function

		pid_t pid = fork();
		if (pid == 0) { // child
			tty_restore_ctrl_c();
			execvp(argv[0], argv); // try executing argv[0]
			perror(argv[0]);
			_exit(errno == ENOENT ? 127 : 126);
		}
		int status;               // parent
		waitpid(pid, &status, 0); // check for status of process
	}
	// free buffer
	free(line);
}

static void create_empty_rc(const char *path) {
	int fd = open(path, O_CREAT | O_EXCL | O_WRONLY, 0644); // man 2 open
	// rw-r--r--
	// 2+4/4/4
	if (fd == -1)
		return; // file exists
	const char *header = "# tetrish start-up file\n"
		"# Add one command per line; blank lines and # "
		"comments are ignored.\n\n";
	write(fd, header, strlen(header));
	close(fd);
}

static void tty_disable_ctrl_c(void) {
	// Ignore Ctrl_C signal (ctrl-z should still kill process)
	// when this breaks:
	// open another terminal
	// run ps and find the PID of cseshell
	// use the final boss of killing processes: kill -9 PID
	struct sigaction sa = {.sa_handler = SIG_IGN, .sa_flags = SA_RESTART};
	sigaction(SIGINT, &sa, NULL);  // Ctrl-C
	sigaction(SIGTSTP, &sa, NULL); // Ctrl-Z
	sigaction(SIGQUIT, &sa, NULL); /* Ctrl-\ */
	// wtf ctrl- '\' comments the line below without the ' '
}

static void tty_restore_ctrl_c(void) {
	// Restore Ctrl-C behaviour to exit cat and xxd like
	// envs
	struct sigaction def = {.sa_handler = SIG_DFL};
	sigaction(SIGINT, &def, NULL);
	sigaction(SIGQUIT, &def, NULL); /* Ctrl-\ */
	sigaction(SIGTSTP, &def, NULL); // Ctrl-Z
}

static void tty_flags_sancheck(char run) {
	// check raw io flags
	if (!run)
		return;
	struct termios t;
	tcgetattr(STDIN_FILENO, &t);
	fprintf(stderr, "DBG: ICANON=%s  ECHO=%s\n",
	 (t.c_lflag & ICANON) ? "ON" : "OFF",
	 (t.c_lflag & ECHO) ? "ON" : "OFF");
}

static void add_bin_topath(void) {
	// add ./bin to path for execvp to find system_program binaries
	char new_path[PATH_MAX * 2];
	const char *old = getenv("PATH");
	snprintf(new_path, sizeof new_path, "%s:%s", project_bin, old ? old : "");
	setenv("PATH", new_path, 1);
}

int tokenise(char *line, char **argv, size_t cap) {
	memset(argv, 0, sizeof(char *) * MAX_ARGS);
	size_t argc = 0;
	char *tok = strtok(line, " \t");
	while (tok && argc + 1 < cap) {
		argv[argc++] = tok;
		tok = strtok(NULL, " \t");
	}
	argv[argc] = NULL;
	return (int)argc;
}

int expand_alias(char **argv, int argc, size_t cap) {
	const char *exp = alias_lookup(argv[0]);
	argv_allocated = 0;
	if (!exp)
		return argc;

	char tmp[IN_BUFFER];
	strncpy(tmp, exp, sizeof tmp - 1);
	tmp[sizeof tmp - 1] = '\0';

	char *v[MAX_ARGS];
	int n = tokenise(tmp, v, MAX_ARGS);

	if (n + argc - 1 >= (int)cap || n >= MAX_ARGS - 1) {
		fprintf(stderr, "alias expansion too long\n");
		fflush(stderr);
		return 0;
	}

	char *new_argv[MAX_ARGS];
	int idx = 0;
	for (int i = 0; i < n; ++i) {
		new_argv[idx++] = strdup(v[i]);
	}

	for (int i = 1; i < argc; ++i) {
		new_argv[idx++] = strdup(argv[i]);
	}
	new_argv[idx] = NULL;

	for (int i = 0; i <= idx; ++i) {
		argv[i] = new_argv[i];
	}
	argv_allocated = 1;
	return idx;
}

//------------ execute failed --------------------//
// https://www.gnu.org/software/bash/manual/html_node/Exit-Status.html
// execvp failed
// check errno if command not found or
// cannot execute

void fork_child(char **argv) {
	pid_t pid = fork();
	if (pid == 0) { // child
		restore_cooked();
		tty_restore_ctrl_c();
		execvp(argv[0], argv); // try executing argv[0]
		perror(argv[0]);
		_exit(errno == ENOENT ? 127 : 126);
	}
	int status;                       // parent
	waitpid(pid, &status, WUNTRACED); // check for status of process
	child_sancheck(pid, status, 0);
	line_raw_init();
}

// for system_programs
static void check_tempdir(void) {
	printf("creating ./tmp\n");
	char tmp_path[PATH_MAX];
	strncpy(tmp_path, project_root, sizeof(tmp_path) - 1);
	strncat(tmp_path, "/tmp", sizeof(tmp_path) - strlen(tmp_path) - 1);

	struct stat st = {0};
	if (stat(tmp_path, &st) == -1) {
		if (mkdir(tmp_path, 0755) == 0)
			printf("Created %s in project root\n", tmp_path);
		else
			perror("mkdir tmp");
	}
}

// for system_programs
static void check_archivedir(void) {
	printf("creating ./archive\n");
	char tmp_path[PATH_MAX];
	strncpy(tmp_path, project_root, sizeof(tmp_path) - 1);
	strncat(tmp_path, "/archive", sizeof(tmp_path) - strlen(tmp_path) - 1);

	struct stat st = {0};
	if (stat(tmp_path, &st) == -1) {
		if (mkdir(tmp_path, 0755) == 0)
			printf("Created %s in project root\n", tmp_path);
		else
			perror("mkdir archive");
	}
}

static void startup_log(void) {
	char log_path[PATH_MAX];
	strncpy(log_path, project_root, sizeof(log_path) - 1);
	strncat(log_path, "/tmp/tetrish.log",
	 sizeof(log_path) - strlen(log_path) - 1);

	int fd = open(log_path, O_WRONLY | O_CREAT | O_APPEND, 0644);
	if (fd == -1) {
		perror("log open");
		return;
	}

	time_t now = time(NULL);
	dprintf(fd, "%sStarted speedrun cseshell (tetrish).\n", ctime(&now));
	close(fd);
}

static void intentional_terminate_log(void) {
	char log_path[PATH_MAX];
	strncpy(log_path, project_root, sizeof(log_path) - 1);
	strncat(log_path, "/tmp/tetrish.log",
	 sizeof(log_path) - strlen(log_path) - 1);

	int fd = open(log_path, O_WRONLY | O_CREAT | O_APPEND, 0644);
	if (fd == -1) {
		perror("log open");
		return;
	}

	time_t now = time(NULL);
	dprintf(fd, "%sSuccessfully terminated speedrun cseshell.\n",
	 ctime(&now));
	close(fd);
}

void startup(void) {
	resolve_project_root();
	check_tempdir();
	check_archivedir();
	startup_log();
	tty_disable_ctrl_c();
	add_bin_topath();
	clear_screen();
	source_rc();
	line_raw_init(); 
	tty_flags_sancheck(1);
}

void cleanup_handlers(void) {
	atexit(alias_free);
	atexit(hist_free);
	atexit(line_raw_cleanup);
	atexit(intentional_terminate_log);
	signal(SIGTERM, catch_and_restore);
	signal(SIGQUIT, catch_and_restore);
}

int main(void) {
	setvbuf(stderr, NULL, _IONBF, 0);
	cleanup_handlers();
	startup();
	// Print initial shell prompt
	puts("Welcome to the speedran shell - peky"); 

	char *argv[MAX_ARGS];
	char prompt[IN_BUFFER];
	char linebuff[IN_BUFFER];

	while (1) {
		type_prompt(prompt, sizeof(prompt));
		if (line_read(prompt, linebuff, sizeof linebuff) == -1)
			break;

		int argc = tokenise(linebuff, argv, MAX_ARGS);
		if (argc == 0)
			continue; // overflowed

		argc = expand_alias(argv, argc, MAX_ARGS);
		if (argc == 0)
			continue; // overflowed

		if (bi_call(argc, argv) >= 0)
			continue; // skip fork since using built in
			// function
		else
			fork_child(argv);

		if (argv_allocated) {
			for (int i = 0; argv[i] != NULL; ++i) {
				free(argv[i]);
			}
		}
	}

	puts("Ending tetrish");
	return 0;
}
