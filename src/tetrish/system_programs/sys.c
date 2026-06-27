#include "tetrish/system_program.h"
#include <stdarg.h>
#include <sys/sysinfo.h>

#define MAX_LINES 32
#define INFO_WIDTH 80

static const char *logo[] = {
	"                              ", "   ▄▄▄▄                ▄▄▄▄   ",
	"  ██▀▀██              ██▀▀██  ", " ██    ██ ██      ██ ██    ██ ",
	" ██    ██ ▀█  ██  █▀ ██    ██ ", " ██    ██  ██▄██▄██  ██    ██ ",
	"  ██▄▄██   ▀██  ██▀   ██▄▄██  ", "   ▀▀▀▀     ▀▀  ▀▀     ▀▀▀▀   ",
	"                              ", "                              ",
};

static char *info[MAX_LINES];
static int info_count = 0;

void display_line(const char *disp, ...) {
	if (info_count >= MAX_LINES)
		return;

	va_list args;
	va_start(args, disp);
	char *buff = malloc(INFO_WIDTH);
	vsnprintf(buff, INFO_WIDTH, disp, args);
	va_end(args);

	info[info_count++] = buff;
}

void userinfo(void) {
	// do not pass this pointer to free!
	struct passwd *pw = getpwuid(getuid());
	struct rusage ru;
	getrusage(RUSAGE_SELF, &ru);

	display_line("User:               %s", pw->pw_name);
	display_line("Home:               %s", pw->pw_dir);
	display_line("UID:GID:            %d:%d", pw->pw_uid, pw->pw_gid);
	// express timeval struct tv_sec seconds value
	// damn sys is too short, both are 0
	// display_line("User CPU Time:      %lds\n", ru.ru_utime.tv_sec);
	// display_line("System CPU Time:    %lds\n", ru.ru_stime.tv_sec);
}

void resourceinfo(void) {
	struct sysinfo si;
	sysinfo(&si);

	display_line("Uptime:             %ld min", si.uptime / 60);
	display_line("Free Ram:           %ld MiB", si.freeram / 1024 / 1024);
	display_line("Buffer Ram:         %ld MiB", si.bufferram / 1024 / 1024);
	display_line("Shared Ram:         %ld MiB", si.sharedram / 1024 / 1024);
	display_line("Total Ram:          %ld MiB", si.totalram / 1024 / 1024);
}

void systeminfo(void) {
	FILE *f = fopen("/etc/os-release", "r");
	if (!f) {
		perror("os-release");
		return;
	}

	char osversion[256];
	while (fgets(osversion, sizeof(osversion), f)) {
		if (strncmp(osversion, "PRETTY_NAME=", 12) == 0) {
			char *start = strchr(osversion, '=') + 1;
			if (*start == '\"')
				start++; // goto next address
			char *end = strchr(start, '\"');
			if (end)
				*end = '\0';
			display_line("OS:                 %s", start);
			break;
		}
	}
	fclose(f);

	struct utsname un;
	uname(&un);

	display_line("Kernel:             %s  %s  (%s)", un.sysname, un.release,
	      un.machine);
	// display_line("OS:                 %s\n", un.version); // not very
	// readable
}

int main(int argc, char **argv) {
	(void)argc;
	(void)argv;

	userinfo();
	resourceinfo();
	systeminfo();

	int logo_lines = sizeof(logo) / sizeof(logo[0]);
	int lines = (logo_lines > info_count) ? logo_lines : info_count;

	for (int i = 0; i < lines; ++i) {
		const char *l = (i < logo_lines) ? logo[i] : "              ";
		const char *r = (i < info_count) ? info[i] : "";
		printf("%s  %s\n", l, r);
	}

	for (int i = 0; i < lines; ++i)
		free(info[i]);
	return 0;
}
