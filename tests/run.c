#include <sys/wait.h>
#include <stdbool.h>
#include <signal.h>
#include <dirent.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>

typedef struct {
    size_t passed;
    size_t failed;
} TestStats;

static bool has_suffix(const char *s, const char *suffix) {
    size_t len = strlen(s);
    size_t suffix_len = strlen(suffix);

    return len >= suffix_len &&
           strcmp(s + len - suffix_len, suffix) == 0;
}

static bool is_test(const char *name) {
    return has_suffix(name, ".pass.coda") ||
           has_suffix(name, ".fail.coda");
}

static bool expected_success(const char *name) {
    return has_suffix(name, ".pass.coda");
}

static int run_test(const char *compiler, const char *path, const char *output_path) {
    pid_t pid = fork();

    if (pid < 0)
        return -1;

    if (pid == 0) {
        int fd = open(output_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);

        if (fd < 0)
            _exit(127);

        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        close(fd);

        execl(compiler, compiler, path, (char *)NULL);
        _exit(127);
    }

    int status;

    if (waitpid(pid, &status, 0) < 0)
        return -1;

    return status;
}

static void test_file(const char *compiler, const char *path, TestStats *stats) {
    char output_path[4096];

    snprintf(output_path, sizeof(output_path), "/tmp/coda-test-%ld.txt", (long)getpid());

    int status = run_test(compiler, path, output_path);

    if (status == -1) {
        fprintf(stderr, "FAIL %s: could not execute compiler\n", path);
        stats->failed++;
        return;
    }

    if (!WIFEXITED(status)) {
        if (WIFSIGNALED(status)) {
            fprintf(stdout, "FAIL %s: compiler terminated by signal %d (%s)\n", path, WTERMSIG(status), strsignal(WTERMSIG(status)));
        } else {
            fprintf(stdout, "FAIL %s: compiler did not exit normally\n", path);
        }

        FILE *output = fopen(output_path, "r");

        if (output != NULL) {
            char buffer[4096];

            while (fgets(buffer, sizeof(buffer), output) != NULL)
                fputs(buffer, stdout);

            fclose(output);
        }

        unlink(output_path);
        stats->failed++;
        return;
    }

    int code = WEXITSTATUS(status);
    bool actual_success = code == 0;
    bool expected = expected_success(path);

    if (actual_success == expected) {
        printf("PASS %s\n", path);
        stats->passed++;
        unlink(output_path);
        return;
    }

    printf("FAIL %s\n", path);
    printf("  expected: %s\n", expected ? "success" : "failure");
    printf("  actual:   %s\n", actual_success ? "success" : "failure");
    printf("\n");

    FILE *output = fopen(output_path, "r");

    if (output != NULL) {
        char buffer[4096];

        while (fgets(buffer, sizeof(buffer), output) != NULL)
            fputs(buffer, stdout);

        fclose(output);
    }

    printf("\n");

    stats->failed++;
    unlink(output_path);
}

static void test_directory(const char *compiler, const char *path, TestStats *stats) {
    DIR *dir = opendir(path);

    if (dir == NULL) {
        perror(path);
        stats->failed++;
        return;
    }

    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0)
            continue;

        char child[4096];
        snprintf(child, sizeof(child), "%s/%s", path, entry->d_name);

        if (entry->d_type == DT_DIR) {
            test_directory(compiler, child, stats);
        } else if (entry->d_type == DT_REG && is_test(entry->d_name)) {
            test_file(compiler, child, stats);
        }
    }

    closedir(dir);
}

int main(int argc, char **argv) {
    const char *compiler = argc > 1 ? argv[1] : "./codac";

    TestStats stats = {0};

    test_directory(compiler, "tests", &stats);

    printf("\n%zu passed, %zu failed\n", stats.passed, stats.failed);

    return stats.failed != 0;
}