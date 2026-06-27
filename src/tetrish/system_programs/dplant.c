#include "tetrish/system_program.h"
#include <libgen.h>
#include <pthread.h>
#include <sys/file.h>
#define MAX_DAEMONS 64

char project_root[PATH_MAX];
int plant_lock_fd = -1;

typedef struct PlantStats {
	int waterlevel;
	int health;
	int level;
	time_t last_watered;
	time_t last_updated;
} PlantStats;

typedef struct PlantSpace {
	PlantStats stats;
	pthread_mutex_t mu;
	int running;
	char name[64];
} PlantSpace;

void resolve_project_root(void) {
	// printf("resolving project root \n");
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
	// printf("project root path: %s\n", project_root);
}

int plant_lock(const char *name) {
	size_t len =
		snprintf(NULL, 0, "%s/tmp/plant_%s.lock", project_root, name) + 1;
	char *lock = malloc(len);
	if (!lock) {
		perror("malloc");
		free(lock);
		exit(EXIT_FAILURE);
	}
	snprintf(lock, len, "%s/tmp/plant_%s.lock", project_root, name);

	plant_lock_fd = open(lock, O_CREAT | O_RDWR, 0644);
	if (plant_lock_fd == -1) {
		perror("lock open");
		return 0;
	}

	// check if already locked
	if (flock(plant_lock_fd, LOCK_EX | LOCK_NB) == -1) {
		close(plant_lock_fd);
		return 0;
	}
	free(lock);
	return 1;
}

void unlock_plant(void) {
	if (plant_lock_fd != -1) {
		flock(plant_lock_fd, LOCK_UN);
		close(plant_lock_fd);
	}
}

void spawn_plant(void) {
	printf("Spawning a plant\n");
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

void daemon_spawn_log(const char *name) {
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
	flock(fd, LOCK_EX);
	dprintf(fd, "[%s] Spawned dplant daemon %s [PID=%d].\n", name, ctime(&now),
		getpid());
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
	flock(fd, LOCK_EX);
	dprintf(fd, "%sLogging dspawn daemon [%d] message: %s.\n", ctime(&now),
		getpid(), msg);
	close(fd);
}

int is_daemon_registered(const char *name) {
	char reg_path[PATH_MAX];
	strncpy(reg_path, project_root, sizeof(reg_path) - 1);
	strncat(reg_path, "/tmp/daemons.reg",
		sizeof(reg_path) - strlen(reg_path) - 1);

	FILE *fp = fopen(reg_path, "r");
	if (!fp)
		return 0;

	char line[256];
	while (fgets(line, sizeof(line), fp)) {
		char regname[64], ts[128];
		int pid;

		if (sscanf(line, "%63s %d %127[^\n]", regname, &pid, ts) != 3)
			continue;

		if (strcmp(regname, name) == 0) {
			fclose(fp);
			return 1;
		}
	}

	fclose(fp);
	return 0;
}

void daemon_register(const char *name) {
	if (is_daemon_registered(name)) {
		fprintf(stderr, "daemon name: %s already registered\n", name);
		return;
	}

	char reg_path[PATH_MAX];
	strncpy(reg_path, project_root, sizeof(reg_path) - 1);
	strncat(reg_path, "/tmp/daemons.reg",
		sizeof(reg_path) - strlen(reg_path) - 1);

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

	// flock(fd, LOCK_EX);
	dprintf(fd, "%s %d %s\n", name, getpid(), ts);
	close(fd);
}

int plant_init_state(const char *name, PlantStats *plant) {
	plant->waterlevel = 100;
	plant->health = 100;
	plant->level = 1;
	plant->last_watered = time(NULL);
	plant->last_updated = time(NULL);

	char state_path[PATH_MAX];
	strncpy(state_path, project_root, sizeof(state_path) - 1);
	strncat(state_path, "/tmp/",
		sizeof(state_path) - strlen(state_path) - 1);
	strncat(state_path, name, sizeof(state_path) - strlen(state_path) - 1);
	strncat(state_path, ".state",
		sizeof(state_path) - strlen(state_path) - 1);

	int fd = open(state_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd == -1) {
		perror("plant init state");
		return -1;
	}
	// no need to format w dpirntf, just write the struct directly
	// can view w xxd i guess if you want to
	flock(fd, LOCK_EX);
	write(fd, plant, sizeof(PlantStats));
	flock(fd, LOCK_UN);
	close(fd);
	return 0;
}

void plant_save(const char *name, const PlantStats *plant) {
	size_t len =
		snprintf(NULL, 0, "%s/tmp/%s.state", project_root, name) + 1;
	char *state_path = malloc(len);
	if (!state_path) {
		perror("malloc");
		free(state_path);
		exit(EXIT_FAILURE);
	}
	snprintf(state_path, len, "%s/tmp/%s.state", project_root, name);

	int fd = open(state_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd == -1) {
		perror("plant save state");
		return;
	}
	flock(fd, LOCK_EX);
	write(fd, plant, sizeof(PlantStats));
	flock(fd, LOCK_UN);
	close(fd);
	free(state_path);
}

int plant_load(const char *name, PlantStats *plant) {
	size_t len =
		snprintf(NULL, 0, "%s/tmp/%s.state", project_root, name) + 1;
	char *state_path = malloc(len);
	if (!state_path) {
		perror("malloc");
		free(state_path);
		exit(EXIT_FAILURE);
	}
	snprintf(state_path, len, "%s/tmp/%s.state", project_root, name);

	int fd = open(state_path, O_RDONLY);
	if (fd != -1) {
		if (read(fd, plant, sizeof(PlantStats)) == sizeof(PlantStats)) {
			close(fd);
			// previously forgot to free the path
			perror("malloc");
			free(state_path);
			return 0;
		}
		close(fd);
	}
	free(state_path);
	return -1;
}

void plant_water(const char *name) {
	size_t len =
		snprintf(NULL, 0, "%s/tmp/%s.state", project_root, name) + 1;
	char *state_path = malloc(len);
	if (!state_path) {
		perror("malloc");
		free(state_path);
		exit(EXIT_FAILURE);
	}
	snprintf(state_path, len, "%s/tmp/%s.state", project_root, name);

	int fd = open(state_path, O_RDONLY);
	if (fd == -1) {
		perror("open plant state");
		return;
	}

	PlantStats plant;
	if (read(fd, &plant, sizeof(PlantStats)) != sizeof(PlantStats)) {
		perror("read plant state");
		close(fd);
		return;
	}
	close(fd);

	time_t now = time(NULL);
	plant.waterlevel =
		(plant.waterlevel + 20 > 99) ? 100 : plant.waterlevel + 20;
	plant.last_watered = now;
	plant.last_updated = now;

	// save the new state
	plant_save(name, &plant);

	printf("Successfully watered %s successfully!\nWater state at %d!\n",
	       name, plant.waterlevel);
	free(state_path);
}

int is_plant_running(const char *plantname) {
	char reg_path[PATH_MAX];
	strncpy(reg_path, project_root, sizeof(reg_path) - 1);
	strncat(reg_path, "/tmp/daemons.reg",
		sizeof(reg_path) - strlen(reg_path) - 1);

	FILE *fp = fopen(reg_path, "r");
	if (!fp) {
		perror("plant running reg open");
		return 0;
	}

	// check if there is a match
	char line[256];
	while (fgets(line, sizeof(line), fp)) {
		char name[64], ts[128];
		int pid;

		if (sscanf(line, "%63s %d %127[^\n]", name, &pid, ts) != 3)
			continue;

		if (strcmp(name, plantname) == 0) {
			char proc_path[128];
			snprintf(proc_path, sizeof(proc_path), "/proc/%d", pid);
			if (access(proc_path, F_OK) == 0) {
				fclose(fp);
				return 1;
			}
		}
	}

	// default there is no active daemon with this name
	fclose(fp);
	return 0;
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

	// dprintf(fd, "%s %d %s\n", modified_name, getpid(), ts);
	dprintf(fd, "%s %d %s\n", modified_name, rip.pid, ts);
	close(fd);
}

void dkill_auto(const char *name) {
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

	for (int i = 0; i < count; ++i) {
		// compare string instead of address
		if (strcmp(daemons[i].name, name) != 0)
			continue;

		if (kill(daemons[i].pid, SIGTERM) == 0) {
			printf("Killed %-12s (PID %d)\n", daemons[i].name,
			       daemons[i].pid);
			DaemonInfo toinsert = daemons[i];
			add_to_graveyard(toinsert);
		} else {
			perror("kill");
		}
	}
}

void plant_died(const char *name) {
	daemon_log("HP has hit 0, plant has died");
	daemon_log("Natural plant death, died from hitting HP 0");

	if (plant_lock_fd != -1) {
		flock(plant_lock_fd, LOCK_UN);
		close(plant_lock_fd);

		size_t len = snprintf(NULL, 0, "%s/tmp/plant_%s.lock",
				      project_root, name);
		char *lock_path = malloc(len);
		if (!lock_path) {
			perror("malloc");
			free(lock_path);
			return;
		}
		snprintf(lock_path, len, "%s/tmp/plant_%s.lock", project_root,
			 name);
		unlink(lock_path);
	}

	printf("running dkill_auto");
	dkill_auto(name);

	exit(EXIT_SUCCESS);
}

void daemon_work(const char *name) {
	PlantStats plant;
	if (plant_load(name, &plant) != 0) {
		daemon_log("Initializing new plant state");
		if (plant_init_state(name, &plant) != 0) {
			daemon_log("Initializing new plant state");
			exit(EXIT_FAILURE);
		}
	}

	while (1) {
		time_t now = time(NULL);
		int seconds_passed = (int)difftime(now, plant.last_updated);

		// reduce every minute
		if (seconds_passed >= 60) {
			// plant.waterlevel -= seconds_passed / 60;
			plant.waterlevel -= 101;
			if (plant.waterlevel < 0)
				plant.waterlevel = 0;

			// if (plant.waterlevel == 0)
			//         plant.health -= 5;
			if (plant.waterlevel == 0)
				plant.health -= 100;
			else if (plant.waterlevel > 80 && plant.health < 100)
				plant.health += 1;

			if (plant.health > 95 && plant.waterlevel > 95)
				plant.level++;

			plant.last_updated = now;
			plant_save(name, &plant);
			daemon_log("one plant cycle, state updated");
		}

		if (plant.health <= 0) {
			plant_died(name);
		}

		sleep(10);
	}
}

int plant_stats(const char *plant_name, PlantStats *statplant) {
	if (plant_load(plant_name, statplant) == 0) {
		printf("Plant:                     %s\n", plant_name);
		printf("Water Level:               %d\n",
		       statplant->waterlevel);
		printf("Health:                    %d\n", statplant->health);
		printf("Level:                     %d\n", statplant->level);
		printf("Last Watered:              %s",
		       ctime(&(statplant->last_watered)));
		printf("Last Updated:              %s",
		       ctime(&(statplant->last_updated)));
		return 0;
	}
	return 1;
}

void respawn_plant(const char *name) {
	if (is_plant_running(name)) {
		fprintf(stderr, "plant daemon is still alive\n");
		return;
	}

	if (!is_daemon_registered(name)) {
		fprintf(stderr, "plant daemon not registered\n");
		return;
	}

	printf("respawning dplant daemon: %s\n", name);
	spawn_plant();
	daemon_register(name);
	daemon_spawn_log(name);
	daemon_log("respawned dplant");
	daemon_work(name);

	unlock_plant();
}

int main(int argc, char **argv) {

	if (argc < 2 || argc > 3) {
		fprintf(stderr, "usage: dplant <plant_name>\n");
		return 1;
	}

	resolve_project_root();
	const char *plant_name = argv[1];

	// wtf if i dkill and run dplant plant water it creates a new daemon lol
	// if(argc == 3 && is_plant_running(plant_name)){
	// stop spawning D:
	if (argc == 3 && !is_plant_running(plant_name) &&
	    strcmp("respawn", argv[2]) != 0) {
		fprintf(stderr, "No running daemon for '%s']n", plant_name);
		fflush(stderr);
		return 1;
	} else if (argc == 3 && strcmp("respawn", argv[2]) == 0) {
		respawn_plant(plant_name);
		return 0;
	}

	if (argc == 3 && is_plant_running(plant_name)) {
		if (strcmp(argv[2], "water") == 0) {
			plant_water(plant_name);
			return 0;
		} else if (strcmp(argv[2], "stats") == 0) {
			PlantStats statplant;
			if (plant_stats(plant_name, &statplant) != 0) {
				fprintf(stderr,
					"unable to load state for: %s\n",
					plant_name);
			}
			return 0;
		} else {
			fprintf(stderr,
				"unknown command for running daemon: %s\n",
				argv[2]);
			return 1;
		}
	}

	if (is_plant_running(plant_name)) {
		fprintf(stderr, "dplant daemon <%s> is already active\n",
			plant_name);
		return 1;
	}

	if (!plant_lock(plant_name)) {
		fprintf(stderr, "could not acquire lock for daemon: <%s>\n",
			plant_name);
		return 1;
	}

	if (argc == 2 && !is_daemon_registered(plant_name)) {
		spawn_plant();
		daemon_register(plant_name);
		daemon_spawn_log(plant_name);
		daemon_log("dplant daemon started");
		daemon_work(plant_name);

		unlock_plant();
		return 0;
	}

	return 1;
}
