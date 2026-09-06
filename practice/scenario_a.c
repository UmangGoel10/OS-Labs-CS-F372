/*
 * Scenario A: fork/exec/wait a batch of shell commands in parallel.
 *
 * Usage:
 *     ./scenario_a "ls" "sleep 2" "false" "echo hi"
 *
 * No sh -c, no string.h: each child tokenizes its own command by hand
 * (splitting on spaces) and calls execvp directly. Since fork() gives the
 * child copy-on-write memory, writing '\0' into the command string inside
 * the child never affects the parent's copy.
 *
 * Build:
 *     gcc -Wall -Wextra -o scenario_a scenario_a.c
 */
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>

#define MAX_TOKENS 16

/* Splits cmd in place on spaces and execs it. Never returns on success. */
void child_run(char *cmd) {
    char *args[MAX_TOKENS];
    int nargs = 0;
    int i = 0;

    while (cmd[i] != '\0' && nargs < MAX_TOKENS - 1) {
        while (cmd[i] == ' ')
            i++;
        if (cmd[i] == '\0')
            break;

        args[nargs++] = &cmd[i]; /* start of this token */

        while (cmd[i] != ' ' && cmd[i] != '\0')
            i++;
        if (cmd[i] == ' ') {
            cmd[i] = '\0'; /* terminate the token */
            i++;
        }
    }
    args[nargs] = NULL;

    if (nargs == 0)
        _exit(127); /* empty command */

    execvp(args[0], args);
    perror("execvp"); /* only reached if exec failed */
    _exit(127);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s \"<cmd1>\" [\"<cmd2>\" ...]\n", argv[0]);
        return 1;
    }

    int ncmds = argc - 1;
    char **cmds = &argv[1]; /* cmds[0] .. cmds[ncmds - 1] */

    pid_t pids[ncmds];
    int failed[ncmds];

    for (int i = 0; i < ncmds; i++) {
        failed[i] = 0;
        pid_t pid = fork();

        if (pid < 0) {
            perror("fork");
            pids[i] = -1;
            failed[i] = 1; /* no child was created; report FAILED directly */
        } else if (pid == 0) {
            child_run(cmds[i]);
            /* not reached */
        } else {
            pids[i] = pid;
        }
    }

    int remaining = 0;
    for (int i = 0; i < ncmds; i++)
        if (pids[i] > 0)
            remaining++;

    while (remaining > 0) {
        int status;
        pid_t done = wait(&status);
        if (done < 0)
            break; /* no children left */

        for (int i = 0; i < ncmds; i++) {
            if (pids[i] == done) {
                failed[i] = !(WIFEXITED(status) && WEXITSTATUS(status) == 0);
                break;
            }
        }
        remaining--;
    }

    for (int i = 0; i < ncmds; i++)
        printf("%s: %s\n", cmds[i], failed[i] ? "FAILED" : "OK");

    return 0;
}
