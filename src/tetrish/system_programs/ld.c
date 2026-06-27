#include "tetrish/system_program.h"

// Function to convert permissions to a string
void perms_to_string(mode_t mode, char str[11]) {
        strcpy(str, "----------"); // Default to all permissions off

        if (S_ISDIR(mode))
                str[0] = 'd'; // Directory
        if (S_ISCHR(mode))
                str[0] = 'c'; // Character device
        if (S_ISBLK(mode))
                str[0] = 'b'; // Block device

        if (mode & S_IRUSR)
                str[1] = 'r'; // Owner has read permission
        if (mode & S_IWUSR)
                str[2] = 'w'; // Owner has write permission
        if (mode & S_IXUSR)
                str[3] = 'x'; // Owner has execute permission
        if (mode & S_IRGRP)
                str[4] = 'r'; // Group has read permission
        if (mode & S_IWGRP)
                str[5] = 'w'; // Group has write permission
        if (mode & S_IXGRP)
                str[6] = 'x'; // Group has execute permission
        if (mode & S_IROTH)
                str[7] = 'r'; // Others have read permission
        if (mode & S_IWOTH)
                str[8] = 'w'; // Others have write permission
        if (mode & S_IXOTH)
                str[9] = 'x'; // Others have execute permission
}

/*
    List the items in the directory
*/
int execute(char **args) {
        if (args[1] != NULL) {
                char *token = strtok(args[1], SHELL_OPT_DELIM);
                // printf("Token is %s\n", token);

                if (token != NULL) {
                        if (strcmp(token, "r") == 0) {
                                // call listdirall,
                                // execvp still need the ./bin because this was
                                // called by a process that was at the ..
                                // directory
                                if (execvp("./bin/ldr", args) == -1) {
                                        perror("Failed to execute, command is "
                                               "invalid.");
                                }
                                return 1;
                        } else {
                                printf("Invalid option. Use -r to display all "
                                       "files within the current directory and "
                                       "its subdirectories.\n");
                                return EXIT_SUCCESS;
                        }
                }
        }

        // print out all the contents of the directory using opendir() function
        DIR *d;
        struct dirent *dir;
        struct stat st;
        char permissions[11];
        d = opendir(".");
        if (d) {
                while ((dir = readdir(d)) != NULL) {
                        // Skip dotfiles
                        if (dir->d_name[0] != '.') {
                                if (stat(dir->d_name, &st) == 0) {
                                        perms_to_string(st.st_mode,
                                                        permissions);
                                        printf(COLOR_RED "%s " COLOR_GREEN
                                                         "%s\n" COLOR_RESET,
                                               permissions, dir->d_name);
                                } else {
                                        perror("stat failed");
                                }
                        }
                }
                closedir(d);
        } else {
                printf("Directory doesn't exist. \n");
        }

        return EXIT_SUCCESS;
}

int main(int argc, char **args) {
        (void)argc;
        return execute(args);
}
