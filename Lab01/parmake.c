// /*
//  * parmake: compile multiple C files, then link them into one binary.
//  *
//  *
//  * Usage:
//  *     ./parmake -o <output_binary> <file1.c> <file2.c> ... <fileN.c>
//  *
//  * Build:
//  *     gcc -Wall -Wextra -o parmake parmake.c
//  *
//  * Fill in main() below. Argument parsing and one string helper are already
//  * done for you. The full spec is in README.md.
//  *
//  * Useful man pages:
//  *     man 2 fork        man 3 exec (execvp)        man 2 wait
//  *
//  */
// #include <stdio.h>  /* printf, fprintf */
// #include <stdlib.h> /* malloc */
// #include <string.h> /* strcmp, strlen, strdup */

// // TODO: include the headers you need for fork, exec, and wait

// /*
//  * GIVEN: c_to_o("foo.c") returns a new string "foo.o" allocated on the heap.
//  *
//  * You will need this for the final step in which you create the executable by linking all the created .o files.
//  *
//  */
// char *c_to_o(const char *cfile) {
//     char *ofile = strdup(cfile);
//     ofile[strlen(ofile) - 1] = 'o';
//     return ofile;
// }

// int main(int argc, char *argv[]) {
//     if (argc <= 3 || strcmp(argv[1], "-o") != 0) {
//         fprintf(stderr, "usage: %s -o <output_binary> <file1.c> [file2.c ...]\n", argv[0]);
//         return 0;
//     }

//     const char *output_binary = argv[2];
//     int nfiles = argc - 3;   /* how many source files were given as input */
//     char **files = &argv[3]; /* files[0] .. files[nfiles - 1]       */

//     // TODO: find a way to store the pid and filename for each child you
//     // fork below, so you can match them back up in the next step.
//     // Hint: arrays work fine here.

//     // TODO: fork a child process to compile each file, running
//     // `gcc -c <file>`. This has to be parallel; every child should be
//     // running at once, not one at a time.

//     // TODO: wait for every child you forked and check how it exited.
//     // Print "FAILED: <filename.c>" for any file whose compile failed.
//     // Check the man pages above for how to do this.

//     // TODO: if every file compiled successfully, link the .o files into
//     // <output_binary>, and return 0. Otherwise, return 1 without linking.

//     return 0;
// }

/*
 * REFERENCE SOLUTION for the parmake lab.
 */

#include <stdio.h>     /* printf, fprintf */
#include <stdlib.h>    /* malloc */
#include <string.h>    /* strcmp, strlen, strdup */
#include <sys/types.h> /* pid_t */
#include <sys/wait.h>  /* wait, WIFEXITED, WEXITSTATUS */
#include <unistd.h>    /* fork, execvp */

/*
 * GIVEN: c_to_o("foo.c") returns a new string "foo.o" allocated on the heap.
 *
 * You will need this for the final step in which you create the executable
 * by linking all the created .o files.
 *
 */
char *c_to_o(const char *cfile) {
    char *ofile = strdup(cfile);
    ofile[strlen(ofile) - 1] = 'o';
    return ofile;
}

int main(int argc, char *argv[]) {
    if (argc <= 3 || strcmp(argv[1], "-o") != 0) {
        fprintf(stderr, "usage: %s -o <output_binary> <file1.c> [file2.c ...]\n", argv[0]);
        return 0;
    }

    const char *output_binary = argv[2];
    int nfiles = argc - 3;
    char **files = &argv[3];

    for(int i = 0; i< nfiles; i++){
        printf("%s \n", files[i]);
    }
    // so what i can say is that files is an array of strings 
    // suppose this was a list of strings that i had to execute then i would have used to form an array by processing it first 


    pid_t *pids = malloc(sizeof(pid_t) * nfiles);
    int nchildren = 0;
    int failures = 0;

    /* Fork everything first, with no waiting inside this loop. */
    for (int i = 0; i < nfiles; i++) {
        pid_t pid = fork();
        if (pid == -1) {
            /* No child exists for this file. Report it and keep going. */
            printf("FAILED: %s\n", files[i]);
            failures++;
            pids[i] = -1;
            continue;
        }
        if (pid == 0) {
            char *args[] = {"gcc", "-c", files[i], NULL};
            execvp(args[0], args);
            /* Only reached if exec failed. */
            exit(1);
        }
        pids[i] = pid;
        nchildren++;
    }

    /* Reap every child we actually created, in whatever order they finish. */
    for (int reaped = 0; reaped < nchildren; reaped++) {
        int status;
        pid_t pid = wait(&status);
        if (pid == -1) break;
        const char *filename = "?";
        for (int i = 0; i < nfiles; i++) {
            if (pids[i] == pid) {
                filename = files[i];
                break;
            }
        }

        /*
        Basically we have 4 cases - 
        1. exit(0) - compiled and ran success
        2. exit(1 or anything) - custom error whose meaning we have chosen
        3. killed by sig - w_if_exited = false
        */

        if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
            printf("FAILED: %s\n", filename);
            failures++;
        }
        
    }

    if (failures > 0) return 1;

    /* gcc file1.o ... fileN.o -o <output_binary> */
    char **args = malloc(sizeof(char *) * (nfiles + 4));
    int k = 0;
    args[k++] = "gcc";
    for (int i = 0; i < nfiles; i++)
        args[k++] = c_to_o(files[i]);
    args[k++] = "-o";
    args[k++] = (char *)output_binary;
    args[k] = NULL;

    pid_t pid = fork();
    if (pid == -1) return 1;
    if (pid == 0) {
        execvp(args[0], args);
        exit(1);
    }
    wait(NULL);
    return 0;
}

/*
Lab1
so basically, we create a child and change the exec like this we form n childs each doing something different i can track there progress using wait and reap as well
total time is max of exec time for a child
and finally we build command string array and then exec the parent process 
*/