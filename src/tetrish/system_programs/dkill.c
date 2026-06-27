#include "tetrish/system_program.h"
#include <ctype.h>
#include <libgen.h>

// change max number of daemons here
#define MAX_DAEMONS 64

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

typedef struct {
        char name[64];
        pid_t pid;
        char timestamp[128];
} DaemonInfo;

void add_to_graveyard(DaemonInfo rip) {
        char reg_path[PATH_MAX];
        strncpy(reg_path, project_root, sizeof(reg_path) - 1);
        // just like in pet cematary, try not to respawn it lol
        strncat(reg_path, "/tmp/cematary.reg",
                sizeof(reg_path) - strlen(reg_path) - 1);

        FILE *fp = fopen(reg_path, "r");
        int count = 0;

        if (fp) {
                char line[1024];
                while (fgets(line, sizeof(line), fp)) {
                        if (strncmp(line, rip.name, strlen(rip.name)) == 0 &&
                            (line[strlen(rip.name)] == ' ' ||
                             line[strlen(rip.name)] == '.'))
                                count++;
                }
                fclose(fp);
        }

        printf("adding to graveyard\n");

        char modified_name[128];
        if (count == 0)
                snprintf(modified_name, sizeof(modified_name), "%s", rip.name);
        else
                snprintf(modified_name, sizeof(modified_name), "%s.%d",
                         rip.name, count);

        // add entry
        int fd = open(reg_path, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fd == -1) {
                perror("daemon cematary register logs open");
                return;
        }

        time_t now = time(NULL);
        char *ts = ctime(&now);
        if (ts)
                ts[strcspn(ts, "\n")] = 0;

        dprintf(fd, "%s %d %s\n", modified_name, getpid(), ts);
        close(fd);
}

void dkill(void) {
        char reg_path[PATH_MAX];
        strncpy(reg_path, project_root, sizeof(reg_path) - 1);
        strncat(reg_path, "/tmp/daemons.reg",
                sizeof(reg_path) - strlen(reg_path) - 1);

        // check for daemons created with the same name and add a number to
        // differentiate it
        FILE *fd = fopen(reg_path, "r");

        if (!fd) {
                perror("dkill: failed to open daemon reg");
                return;
        }

        int count = 0;
        char line[1024];
        DaemonInfo daemons[MAX_DAEMONS];

        while (fgets(line, sizeof(line), fd) && count < MAX_DAEMONS) {
                char name[64], timestamp[128];
                int pid;

                if (sscanf(line, "%63s %d %127[^\n]", name, &pid, timestamp) !=
                    3)
                        continue;

                char proc_path[128];
                snprintf(proc_path, sizeof(proc_path), "/proc/%d", pid);
                // check all the pids just in case
                if (access(proc_path, F_OK) == 0) {
                        strncpy(daemons[count].name, name,
                                sizeof(daemons[count].name));
                        daemons[count].pid = pid;
                        strncpy(daemons[count].timestamp, timestamp,
                                sizeof(daemons[count].timestamp));
                        ++count;
                }
        }
        fclose(fd);

        if (count == 0) {
                printf("No active daemons found\n");
                return;
        }

        printf("Active daemons: \n");
        for (int i = 0; i < count; ++i) {
                printf("    %2d) %-12s PID: %-6d Started %s\n", i + 1,
                       daemons[i].name, daemons[i].pid, daemons[i].timestamp);
        }

        printf("Enter number to kill [1-%d], 0 to cancel, or 'a' to kill all "
               "daemons:",
               count);
        char input[8];
        fgets(input, sizeof(input), stdin);

        if (tolower(input[0]) == 'a') {
                for (int i = 0; i < count; ++i) {
                        if (kill(daemons[i].pid, SIGTERM) == 0) {
                                printf("Killed %-12s (PID %d)\n",
                                       daemons[i].name, daemons[i].pid);
                                DaemonInfo toinsert = daemons[i];
                                add_to_graveyard(toinsert);
                        } else {
                                perror("kill");
                        }
                }
                // empty the register after killing all of them
                fclose(fopen(reg_path, "w"));
        } else {
                int idx = atoi(input) - 1;
                if (idx >= 0 && idx < count) {
                        if (kill(daemons[idx].pid, SIGTERM) == 0) {
                                printf("Killed %-12s (PID %d)\n",
                                       daemons[idx].name, daemons[idx].pid);
                                DaemonInfo toinsert = daemons[idx];
                                add_to_graveyard(toinsert);
                        } else {
                                perror("kill");
                        }
                } else {
                        printf("Invalid input \n");
                }
        }
}

int main(int argc, char **argv) {
        (void)argc;
        (void)argv;

        resolve_project_root();
        dkill();

        return 0;
}
