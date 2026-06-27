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

void dcheck(void) {
        char reg_path[PATH_MAX];
        strncpy(reg_path, project_root, sizeof(reg_path) - 1);
        strncat(reg_path, "/tmp/daemons.reg",
                sizeof(reg_path) - strlen(reg_path) - 1);

        // check for daemons created with the same name and add a number to
        // differentiate it
        FILE *fd = fopen(reg_path, "r");
        int count = 0;

        if (!fd) {
                perror("dcheck open reg");
                return;
        }
        char line[1024];
        printf("------------------------------------------\n");
        printf("Registered Daemons (%s)\n", "tmp/daemons.reg");
        printf("------------------------------------------\n");

        while (fgets(line, sizeof(line), fd)) {
                char name[128], ts[128];
                int pid;

                if (sscanf(line, "%s %d %[^\n]", name, &pid, ts) != 3)
                        continue;

                char proc_path[128];
                snprintf(proc_path, sizeof(proc_path), "/proc/%d", pid);
                int check = (access(proc_path, F_OK) == 0);

                if (check)
                        ++count;

                printf("[%c] %-12s \tPID: %-6d %s%s\n", check ? '1' : '-', name,
                       pid, check ? "Started: " : "(inactive)",
                       check ? ts : "");
        }
        fclose(fd);
        printf("------------------------------------------\n");
        printf("Active daemons: %d\n", count);
}

void dcheck_graveyard(void) {
        char reg_path[PATH_MAX];
        strncpy(reg_path, project_root, sizeof(reg_path) - 1);
        strncat(reg_path, "/tmp/cematary.reg",
                sizeof(reg_path) - strlen(reg_path) - 1);

        // check for daemons created with the same name and add a number to
        // differentiate it
        FILE *fd = fopen(reg_path, "r");
        int count = 0;

        if (!fd) {
                perror("dcheck cematary open reg");
                return;
        }
        char line[1024];
        printf("\n------------------------------------------\n");
        printf("Daemon Cematary (%s)\n", "tmp/cematary.reg");
        printf("------------------------------------------\n");

        while (fgets(line, sizeof(line), fd)) {
                char name[128], ts[128];
                int pid;

                if (sscanf(line, "%s %d %[^\n]", name, &pid, ts) != 3)
                        continue;

                printf("[%c] %-12s \tPID: %-6d %s%s\n", '-', name, pid,
                       "(inactive)", "");

                ++count;
        }
        fclose(fd);
        printf("------------------------------------------\n");
        printf("Daemons burried: %d\n\n", count);
}

int main(int argc, char **argv) {
        (void)argc;
        (void)argv;

        resolve_project_root();
        dcheck();
        dcheck_graveyard();

        return 0;
}
