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

/*
void backup(void) {
	char *target = getenv("BACKUP_DIR");
	if (!target) {
		fprintf(stderr, "BACKUP_DIR env not set\n");
		exit(EXIT_FAILURE);
	}

	struct stat st;
	if (stat(target, &st) == -1) {
		perror("stat");
		exit(EXIT_FAILURE);
	}

	int is_dir = S_ISDIR(st.st_mode);
	int is_file = S_ISREG(st.st_mode);
}
*/

int main(int argc, char **argv) {
	(void)argc;
	(void)argv;

	resolve_project_root();

	const char *backup_path = getenv("BACKUP_DIR");
	if (!backup_path) {
		fprintf(stderr, "BACKUP_DIR not set");
		return 1;
	}

	struct stat st;
	if (stat(backup_path, &st) == -1) {
		perror("stat");
		return 1;
	}

	time_t now = time(NULL);
	struct tm *tm_info = localtime(&now);
	char timebuff[64];
	strftime(timebuff, sizeof(timebuff), "%Y%m%d_%H%M%S", tm_info);

	const char *base = basename((char *)backup_path);

	// char archive_path[PATH_MAX];
	size_t len = snprintf(NULL, 0, "%s/archive/%s_%s.tar.gz", project_root,
		       base, timebuff) +
		1;

	char *archive_path = malloc(len);
	if (!archive_path) {
		perror("malloc");
		exit(EXIT_FAILURE);
	}
	// snprintf(archive_path, sizeof(archive_path),
	// "%s/archive/%s_%s.tar.gz",
	//          project_root, base, timebuff);
	snprintf(archive_path, len, "%s/archive/%s_%s.tar.gz", project_root,
	  base, timebuff);

	// create a child to make the tarball

	pid_t pid = fork();
	if (pid == 0) {
		execlp("tar", "tar", "-czf", archive_path, "-C",
	 dirname((char *)backup_path), base, (char *)NULL);
		perror("execlp tar");
		exit(1);
	} else if (pid > 0) {
		int status;
		waitpid(pid, &status, 0);
		if (WIFEXITED(status) && (WEXITSTATUS(status) == 0)) {
			printf("Archived %s into %s\n", backup_path,
	  archive_path);
		} else {
			fprintf(stderr, "Failed to archive\n");
		}
	} else {
		perror("fork tar");
		return 1;
	}

	free(archive_path);
	return 0;
}
