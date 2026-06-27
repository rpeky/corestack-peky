#include "tetrish/system_program.h"

void perms_to_string(mode_t mode, char str[11]) {
        strcpy(str, "----------");
        if (S_ISDIR(mode))
                str[0] = 'd';
        if (S_ISCHR(mode))
                str[0] = 'c';
        if (S_ISBLK(mode))
                str[0] = 'b';

        if (mode & S_IRUSR)
                str[1] = 'r';
        if (mode & S_IWUSR)
                str[2] = 'w';
        if (mode & S_IXUSR)
                str[3] = 'x';
        if (mode & S_IRGRP)
                str[4] = 'r';
        if (mode & S_IWGRP)
                str[5] = 'w';
        if (mode & S_IXGRP)
                str[6] = 'x';
        if (mode & S_IROTH)
                str[7] = 'r';
        if (mode & S_IWOTH)
                str[8] = 'w';
        if (mode & S_IXOTH)
                str[9] = 'x';
}

void print_path_with_colored_slash(const char *path) {
        const char *p = path;
        while (*p) {
                if (*p == '/') {
                        printf(COLOR_YELLOW "/%s" COLOR_RESET, COLOR_GREEN);
                } else {
                        putchar(*p);
                }
                p++;
        }
}

void list_directory(const char *basePath) {
        char path[1000];
        struct dirent *dp;
        DIR *dir = opendir(basePath);
        if (!dir)
                return; // Unable to open directory

        while ((dp = readdir(dir)) != NULL) {
                if (strcmp(dp->d_name, ".") != 0 &&
                    strcmp(dp->d_name, "..") != 0) {
                        // Construct new path
                        snprintf(path, sizeof(path), "%s/%s", basePath,
                                 dp->d_name);

                        // Skip dotfiles
                        if (dp->d_name[0] != '.') {
                                struct stat st;
                                char permissions[11] = {0};
                                if (stat(path, &st) == 0) {
                                        perms_to_string(st.st_mode,
                                                        permissions);
                                        printf(COLOR_RED "%s " COLOR_RESET,
                                               permissions);
                                        print_path_with_colored_slash(path);
                                        printf("\n");
                                }

                                // Recursively list directories
                                if (S_ISDIR(st.st_mode)) {
                                        list_directory(path);
                                }
                        }
                }
        }
        closedir(dir);
}

int main(void) {
        // printf("Recursively listing all visible files under the current
        // directory with permissions:\n");
        list_directory(".");

        return EXIT_SUCCESS;
}
