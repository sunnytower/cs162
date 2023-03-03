#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <signal.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#include <linux/limits.h>
#include "tokenizer.h"

/* Convenience macro to silence compiler warnings about unused function parameters. */
#define unused __attribute__((unused))

/* Whether the shell is connected to an actual terminal or not. */
bool shell_is_interactive;

/* File descriptor for the shell input */
int shell_terminal;

/* Terminal mode settings for the shell */
struct termios shell_tmodes;

/* Process group id for the shell */
pid_t shell_pgid;

int cmd_exit(struct tokens* tokens);
int cmd_help(struct tokens* tokens);

int cmd_pwd(struct tokens* tokens);
int cmd_cd(struct tokens* tokens);
/* Built-in command functions take token array (see parse.h) and return int */
typedef int cmd_fun_t(struct tokens* tokens);

/* Built-in command struct and lookup table */
typedef struct fun_desc {
  cmd_fun_t* fun;
  char* cmd;
  char* doc;
} fun_desc_t;

fun_desc_t cmd_table[] = {
    {cmd_help, "?", "show this help menu"},
    {cmd_exit, "exit", "exit the command shell"},
    {cmd_pwd, "pwd", "show the current direcotry"},
    {cmd_cd, "cd", "take one argument as path, change the current direcotry to that directory"},
};

/* Prints a helpful description for the given command */
int cmd_help(unused struct tokens* tokens) {
  for (unsigned int i = 0; i < sizeof(cmd_table) / sizeof(fun_desc_t); i++)
    printf("%s - %s\n", cmd_table[i].cmd, cmd_table[i].doc);
  return 1;
}

/* Exits this shell */
int cmd_exit(unused struct tokens* tokens) { exit(0); }
/* show the current directory */
int cmd_pwd(unused struct tokens* tokens) {
  char buf[PATH_MAX];
  getcwd(buf, PATH_MAX);
  printf("%s\n", buf);
  return 0;
}
/* take the first argument as path, change current directory to that directory*/
int cmd_cd(unused struct tokens* tokens) {
  const char *path = tokens_get_token(tokens, 1); 
  return chdir(path);
}
/* Looks up the built-in command, if it exists. */
int lookup(char cmd[]) {
  for (unsigned int i = 0; i < sizeof(cmd_table) / sizeof(fun_desc_t); i++)
    if (cmd && (strcmp(cmd_table[i].cmd, cmd) == 0))
      return i;
  return -1;
}

/* Intialization procedures for this shell */
void init_shell() {
  /* Our shell is connected to standard input. */
  shell_terminal = STDIN_FILENO;

  /* Check if we are running interactively */
  shell_is_interactive = isatty(shell_terminal);

  if (shell_is_interactive) {
    /* If the shell is not currently in the foreground, we must pause the shell until it becomes a
     * foreground process. We use SIGTTIN to pause the shell. When the shell gets moved to the
     * foreground, we'll receive a SIGCONT. */
    while (tcgetpgrp(shell_terminal) != (shell_pgid = getpgrp()))
      kill(-shell_pgid, SIGTTIN);

    /* Saves the shell's process id */
    shell_pgid = getpid();

    /* Take control of the terminal */
    tcsetpgrp(shell_terminal, shell_pgid);

    /* Save the current termios to a variable, so it can be restored later. */
    tcgetattr(shell_terminal, &shell_tmodes);
  }
}


int main(unused int argc, unused char* argv[]) {
  init_shell();

  static char line[4096];
  int line_num = 0;

  /* Please only print shell prompts when standard input is not a tty */
  if (shell_is_interactive)
    fprintf(stdout, "%d: ", line_num);

  while (fgets(line, 4096, stdin)) {
    /* Split our line into words. */
    struct tokens* tokens = tokenize(line);

    /* Find which built-in function to run. */
    int fundex = lookup(tokens_get_token(tokens, 0));

    if (fundex >= 0) {
      cmd_table[fundex].fun(tokens);
    } else {
      /* deal with pipe, redirection, exectuable file from env */
      const int length = tokens_get_length(tokens);
      pid_t pid = fork();

      if (pid == 0) {
      /* child process */

        setpgrp();

      /* if the tokens have > or <, set the right side of > or < to infd or outfd */
        bool redirect_out = false;
        bool redirect_in = false;
        char *out_filename;
        char *in_filename;
        int outfd = -1;
        int infd = -1;
        int out_index = -1;
        int in_index = -1;
        for (int i = 0; i < length; ++i) {
          if (strcmp(">", tokens_get_token(tokens, i)) == 0) {
            redirect_out = true;
            out_index = i;
            if (i < length - 1) {
              out_filename = tokens_get_token(tokens, i + 1);
              outfd = open(out_filename, O_CREAT | O_WRONLY | O_TRUNC, 0644);
            }
            break;
          } else if (strcmp("<", tokens_get_token(tokens, i)) == 0) {
            redirect_in = true;
            in_index = i;
            if (i < length - 1) {
              in_filename = tokens_get_token(tokens, i + 1);
              infd = open(in_filename, O_RDONLY);
            }
            break;
          }
        }

        int argc;
        if (redirect_out) {
          argc = out_index;
        } else if (redirect_in) {
          argc = in_index;
        } else {
          argc = length;
        }

        char *argv[argc + 1];
        argv[argc] = NULL;
        for (int i = 0; i < argc; ++i) {
          argv[i] = tokens_get_token(tokens, i);
          if (i == 0) {
            /*if first argument has /, use path to find the full filename */
            char *path = getenv("PATH");
            char *path_copy = malloc(strlen(path) + 1); 
            strcpy(path_copy, path);
            char *contains_slash = strchr(argv[i], '/');
            if (contains_slash == NULL) { 
              /* only deal with no slash in filename */
              char *path_split = strtok(path_copy, ":");
              while (path_split != NULL) {
                char *full_path = malloc(strlen(path_split) + strlen(argv[i]) + 2); 
                if (full_path != NULL) {
                  strcpy(full_path, path_split);
                  strcat(full_path, "/");
                  strcat(full_path, argv[i]);
                }
                if (access(full_path, F_OK) != -1) {
                  /* File exists, break from loop and execute */
                  argv[i] = full_path;
                  break;
                }
                path_split = strtok(NULL, ":");
              }
            }
            free(path_copy);
            // if (full_file_path == NULL) {
              // full_file_path = argv[i];
            // }
          }
          printf(" argv[%d] is %s\n",i, argv[i]);
        }

        if (redirect_in) {
          if (!infd) {
            printf("can't open file :%s\n", in_filename);
          } else {
            dup2(infd, STDIN_FILENO);
            close(infd);
          }
        } else if (redirect_out) {
          if (!outfd) {
            printf("can't open file:%s\n",out_filename);
          } else {
            dup2(outfd, STDOUT_FILENO);
            close(outfd);
          }
        }
        /* deal with path name if exists */
        execv(argv[0], argv);

      } else {
        /* parent process */
        waitpid(pid, 0, 0);
      }
      
    }

    if (shell_is_interactive)
      /* Please only print shell prompts when standard input is not a tty */
      fprintf(stdout, "%d: ", ++line_num);

    /* Clean up memory */
    tokens_destroy(tokens);
  }

  return 0;
}
