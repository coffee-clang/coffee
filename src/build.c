#include "build.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

static int run_command(char **argv) {
    pid_t pid = fork();
    
    if (pid == 0) {
        execvp(argv[0], argv);
        perror("execvp");
        exit(1);
    } else if (pid > 0) {
        int status;
        waitpid(pid, &status, 0);
        return WEXITSTATUS(status);
    }
    
    perror("fork");
    return 1;
}

int build_project(manifest_t *manifest, build_opts_t *opts) {
    if (!manifest || !manifest->package.name) {
        fprintf(stderr, "Error: No valid manifest found\n");
        return 1;
    }
    
    const char *cc = getenv("CC") ? getenv("CC") : "clang";
    const char *output_dir = opts && opts->target_dir ? opts->target_dir : "target/debug";
    
    char cmd[4096];
    int ret;
    
    ret = snprintf(cmd, sizeof(cmd), "mkdir -p %s", output_dir);
    if (ret < 0 || (size_t)ret >= sizeof(cmd)) return 1;
    ret = system(cmd);
    if (ret != 0) return 1;
    
    const char *name = manifest->package.name;
    
    char *flags = strdup("");
    if (opts && opts->release) {
        flags = strdup("-O2");
    } else if (opts && opts->debug) {
        flags = strdup("-g");
    } else {
        flags = strdup("-O0 -g");
    }
    
    ret = snprintf(cmd, sizeof(cmd),
        "%s %s -o %s/%s src/*.c 2>&1",
        cc, flags, output_dir, name);
    free(flags);
    
    if (ret < 0 || (size_t)ret >= sizeof(cmd)) return 1;
    
    if (opts && opts->verbose) {
        printf("Building: %s\n", cmd);
    }
    
    return system(cmd);
}

int build_run(manifest_t *manifest, build_opts_t *opts, char **args, int argc) {
    int ret = build_project(manifest, opts);
    if (ret != 0) {
        return ret;
    }
    
    const char *output_dir = opts && opts->target_dir ? opts->target_dir : "target/debug";
    const char *name = manifest->package.name;
    
    char exe_path[4096];
    ret = snprintf(exe_path, sizeof(exe_path), "%s/%s", output_dir, name);
    if (ret < 0 || (size_t)ret >= sizeof(exe_path)) return 1;
    
    if (access(exe_path, X_OK) != 0) {
        fprintf(stderr, "Error: Executable not found: %s\n", exe_path);
        return 1;
    }
    
    char cmd[4096];
    ret = snprintf(cmd, sizeof(cmd), "%s", exe_path);
    for (int i = 0; i < argc && args && (size_t)ret < sizeof(cmd) - 1; i++) {
        size_t len = strlen(cmd);
        ret = snprintf(cmd + len, sizeof(cmd) - len, " %s", args[i]);
    }
    
    return system(cmd);
}
