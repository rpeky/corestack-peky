#include "tetrish/system_program.h"
#include <libgen.h>

char project_root[PATH_MAX];

void resolve_project_root(void) {
	printf("resolving project root \n");
	char path[PATH_MAX];
	// should return in the /bin dir, need to back one out
	ssize_t n = readlink("/proc/self/exe", path, sizeof(path) - 1);
	if (n == -1) {
		// fallback if unable to get path
		perror("readlink");
		const char *home = getenv("HOME");
		if (home)
			strncpy(project_root, home, sizeof(project_root));
		else
			strcpy(project_root, ".");
		return;
	}

	path[n] = '\0';
	char *dir = dirname(path);
	if (strcmp(basename(dir), "bin") == 0)
		strncpy(project_root, dirname(dir), sizeof(project_root) - 1);
	else
		strncpy(project_root, dir, sizeof(project_root) - 1);

	project_root[sizeof(project_root) - 1] = '\0';
	printf("project root path: %s\n", project_root);
}

void spawn_daemon(void) {
	printf("some kind of daemon spawning program\n");
	printf(
		"spawning a daemon, remember to run ./daemonslayer to kill them\n");
	// first fork
	pid_t pid = fork();
	if (pid < 0)
		exit(EXIT_FAILURE);
	if (pid > 0)
		exit(EXIT_SUCCESS);

	if (setsid() < 0)
		exit(EXIT_FAILURE);

	// disable signals
	signal(SIGCHLD, SIG_IGN);
	signal(SIGHUP, SIG_IGN);

	// second child fork
	pid = fork();
	if (pid < 0)
		exit(EXIT_FAILURE);
	if (pid > 0)
		exit(EXIT_SUCCESS);

	umask(0); // allow daemon perms
	// chdir("/"); //change wd to / (always exists)
	// staying in projects root to write to tmp

	// close all fd
	for (int fd = sysconf(_SC_OPEN_MAX); fd >= 0; fd--)
		close(fd);

	// dump stdin to /dev/null
	int null_fd = open("/dev/null", O_RDWR);
	if (null_fd != -1) {
		dup2(null_fd, STDIN_FILENO);
		dup2(null_fd, STDOUT_FILENO);
		dup2(null_fd, STDERR_FILENO);
		if (null_fd > 2)
			close(null_fd);
	}
}

void daemon_spawn_log(void) {
	char log_path[PATH_MAX];
	strncpy(log_path, project_root, sizeof(log_path) - 1);
	strncat(log_path, "/tmp/dspawn.log",
	 sizeof(log_path) - strlen(log_path) - 1);

	int fd = open(log_path, O_WRONLY | O_CREAT | O_APPEND, 0644);
	if (fd == -1) {
		perror("log open");
		return;
	}

	time_t now = time(NULL);
	dprintf(fd, "%sStarted dspawn daemon [%d].\n", ctime(&now), getpid());
	close(fd);
}

void daemon_log(const char *msg) {
	char log_path[PATH_MAX];
	strncpy(log_path, project_root, sizeof(log_path) - 1);
	strncat(log_path, "/tmp/dspawn.log",
	 sizeof(log_path) - strlen(log_path) - 1);

	int fd = open(log_path, O_WRONLY | O_CREAT | O_APPEND, 0644);
	if (fd == -1) {
		perror("log open");
		return;
	}

	time_t now = time(NULL);
	dprintf(fd, "%sLogging dspawn daemon [%d] message: %s.\n", ctime(&now),
	 getpid(), msg);
	close(fd);
}

void daemon_register(const char *name) {
	char reg_path[PATH_MAX];
	strncpy(reg_path, project_root, sizeof(reg_path) - 1);
	strncat(reg_path, "/tmp/daemons.reg",
	 sizeof(reg_path) - strlen(reg_path) - 1);

	// check for daemons created with the same name and add a number to
	// differentiate it
	FILE *fp = fopen(reg_path, "r");
	int count = 0;

	if (fp) {
		char line[1024];
		while (fgets(line, sizeof(line), fp)) {
			if (strncmp(line, name, strlen(name)) == 0 &&
				(line[strlen(name)] == ' ' ||
				line[strlen(name)] == '.'))
				count++;
		}
		fclose(fp);
	}

	char modified_name[64];
	if (count == 0)
		snprintf(modified_name, sizeof(modified_name), "%s", name);
	else
		snprintf(modified_name, sizeof(modified_name), "%s.%d", name,
	   count);

	// add entry
	int fd = open(reg_path, O_WRONLY | O_CREAT | O_APPEND, 0644);
	if (fd == -1) {
		perror("daemon register logs open");
		return;
	}

	time_t now = time(NULL);
	char *ts = ctime(&now);
	if (ts)
		ts[strcspn(ts, "\n")] = 0;

	dprintf(fd, "%s %d %s\n", modified_name, getpid(), ts);
	close(fd);
}

void daemon_work(void) {
	while (1) {
		daemon_log("one work cycle");
		sleep(10);
	}
}

int main(int argc, char **argv) {
	(void)argc;
	(void)argv;

	resolve_project_root();
	spawn_daemon();
	daemon_register("dspawnowo");
	daemon_spawn_log();
	daemon_log("test");
	daemon_work();
	return 0;
}
