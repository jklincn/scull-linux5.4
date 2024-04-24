#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    DIR *d;
    struct dirent *entry;
    char path[1024];
    int status, max_name_len = 0, name_len, result = 0;

    d = opendir(".");
    if (d == NULL) {
        perror("Failed to open directory");
        return 1;
    }

    // 第一次遍历：找出最长的文件名长度
    while ((entry = readdir(d)) != NULL) {
        if (strcmp(entry->d_name, ".") != 0 &&
            strcmp(entry->d_name, "..") != 0) {
            name_len = strlen(entry->d_name);
            if (name_len > max_name_len) {
                max_name_len = name_len;
            }
        }
    }
    // 重置目录流的位置
    rewinddir(d);

    printf("\nRunning Tests...\n\n");

    setbuf(stdout, NULL);

    while ((entry = readdir(d)) != NULL) {
        if (strcmp(entry->d_name, ".") != 0 &&
            strcmp(entry->d_name, "..") != 0) {
            snprintf(path, sizeof(path), "./%s", entry->d_name);
            if (strcmp(path, argv[0]) != 0) {
                printf("[%s]: ", entry->d_name);
                pid_t pid = fork();
                if (pid == 0) {
                    execl(path, entry->d_name, (char *)NULL);
                    perror("execl failed");
                    exit(1);
                } else if (pid > 0) {
                    waitpid(pid, &status, 0);
                    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
                        printf("%-*sTEST PASSED!\n",
                               max_name_len - (int)strlen(entry->d_name), "");
                    } else {
                        result = 1;
                        printf("%-*sTEST FAILED!\n",
                               max_name_len - (int)strlen(entry->d_name), "");
                    }
                } else {
                    perror("Failed to fork");
                }
            }
        }
    }
    closedir(d);
    return result;
}
