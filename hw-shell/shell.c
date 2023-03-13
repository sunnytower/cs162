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

void signal_ingore() {
  signal(SIGINT, SIG_DFL);
  signal(SIGQUIT, SIG_DFL);
  signal(SIGKILL, SIG_DFL);
  signal(SIGTERM, SIG_DFL);
  signal(SIGTSTP, SIG_DFL);
  signal(SIGCONT, SIG_DFL);
  signal(SIGTTIN, SIG_DFL);
  signal(SIGTTOU, SIG_DFL);
}
void signal_default() {
  signal(SIGINT, SIG_DFL);
  signal(SIGQUIT, SIG_DFL);
  signal(SIGKILL, SIG_DFL);
  signal(SIGTERM, SIG_DFL);
  signal(SIGTSTP, SIG_DFL);
  signal(SIGCONT, SIG_DFL);
  signal(SIGTTIN, SIG_DFL);
  signal(SIGTTOU, SIG_DFL);
}
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
  const char* path = tokens_get_token(tokens, 1);
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

  /* the normal wait to exit shell is exit command */
  signal_ingore();
}

bool have_redirection(char* argv[], int argc) {
  for (int i = 0; i < argc; ++i) {
    if (strcmp("<", argv[i]) == 0) {
      return true;
    } else if (strcmp(">", argv[i]) == 0) {
      return true;
    }
  }
  return false;
}
/* -1 if no pipes */
int have_pipes(char* argv[], int argc) {
  int n = 0;
  for (int i = 0; i < argc; ++i) {
    if (strcmp("|", argv[i]) == 0) {
      n += 1;
    }
  }
  if (n == 0) {
    return -1;
  }
  return n;
}

/* command should be on the heap */
void get_full_path_command(char** command) {
  char* path = getenv("PATH");
  char* path_copy = malloc(strlen(path) + 1);
  strcpy(path_copy, path);
  char* contains_slash = strchr(*command, '/');
  if (contains_slash == NULL) {
    /* only deal with no slash in filename */
    char* path_split = strtok(path_copy, ":");
    while (path_split != NULL) {
      char* full_path = malloc(strlen(path_split) + strlen(*command) + 2);
      if (full_path != NULL) {
        strcpy(full_path, path_split);
        strcat(full_path, "/");
        strcat(full_path, *command);
      }
      if (access(full_path, F_OK) != -1) {
        /* File exists, break from loop and execute */
        char* tmp = *command;
        *command = full_path;
        free(tmp);
        break;
      }
      path_split = strtok(NULL, ":");
    }
  }
  free(path_copy);
}

/* deal with one redirection at a time */
void redirection_handler(char* argv[], int argc) {
  /* if the tokens have > or <, set the right side of > or < to infd or outfd */
  bool redirect_out = false;
  bool redirect_in = false;
  char* out_filename;
  char* in_filename;
  int outfd = -1;
  int infd = -1;
  int out_index = -1;
  int in_index = -1;
  for (int i = 0; i < argc; ++i) {
    if (strcmp(">", argv[i]) == 0) {
      redirect_out = true;
      out_index = i;
      if (i < argc - 1) {
        out_filename = argv[i + 1];
        outfd = open(out_filename, O_CREAT | O_WRONLY | O_TRUNC, 0644);
      }
      break;
    } else if (strcmp("<", argv[i]) == 0) {
      redirect_in = true;
      in_index = i;
      if (i < argc - 1) {
        in_filename = argv[i + 1];
        infd = open(in_filename, O_RDONLY);
      }
      break;
    }
  }

  int argc_after;
  if (redirect_out) {
    argc_after = out_index;
  } else if (redirect_in) {
    argc_after = in_index;
  } else {
    argc_after = argc;
  }

  char* argv_after[argc_after + 1];
  argv_after[argc_after] = NULL;
  for (int i = 0; i < argc_after; ++i) {
    argv_after[i] = argv[i];
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
      printf("can't open file:%s\n", out_filename);
    } else {
      dup2(outfd, STDOUT_FILENO);
      close(outfd);
    }
  }
  /* deal with path name if exists */
  execv(argv_after[0], argv_after);
}

void execute(char* argv[], int argc) {
  if (have_redirection(argv, argc)) {
    redirection_handler(argv, argc);
  } else {
    execv(argv[0], argv);
  }
}

/* deal with n process with n - 1 pipes */
void pipe_handler(char* argv[], int argc, int n) {
  const int READ_END = 0;
  const int WRITE_END = 1;
  pid_t pid;
  int pipe_arr[n - 1][2];
  for (int i = 0; i < n - 1; ++i) {
    if (pipe(pipe_arr[i]) == -1) {
      printf("pipe create error!\n");
    }
  }

  int start_index = 0;
  int end_index = -1;
  int i = 0;
  for (; i < n; ++i) {
    pid = fork();
    if (pid == 0) {
      /* child process */
      if (i == 0) {
        dup2(pipe_arr[0][WRITE_END], STDOUT_FILENO);
        close(pipe_arr[0][WRITE_END]);
      } else if (i == n - 1) {
        dup2(pipe_arr[n - 2][READ_END], STDIN_FILENO);
        close(pipe_arr[n - 2][READ_END]);
      } else {
        dup2(pipe_arr[i - 1][READ_END], STDIN_FILENO);
        dup2(pipe_arr[i][WRITE_END], STDOUT_FILENO);
        close(pipe_arr[i - 1][READ_END]);
        close(pipe_arr[i][WRITE_END]);
      }
      /* execute command */
      if (i != n - 1) {
        for (int k = start_index; k < argc; ++k) {
          if (strcmp("|", argv[k]) == 0) {
            end_index = k - 1;
            int sub_argc = end_index - start_index + 1;
            /* from start_index count sub_argc times is sub_argv */
            char* sub_argv[sub_argc + 1];
            sub_argv[sub_argc] = NULL;
            for (int j = start_index; j < sub_argc; ++j) {
              sub_argv[j - start_index] = malloc(strlen(argv[j]) + 1);
              strcpy(sub_argv[j - start_index], argv[j]);
            }
            get_full_path_command(&sub_argv[0]);
            execute(sub_argv, sub_argc);
            /* change start_index */
            printf("child should never go through this!\n");
          }
        }
      } else {
        /* tail process */
        end_index = argc - 1;
        int sub_argc = end_index - start_index + 1;
        char* sub_argv[sub_argc + 1];
        sub_argv[sub_argc] = NULL;
        for (int j = start_index; j < argc; ++j) {
          sub_argv[j - start_index] = malloc(strlen(argv[j]) + 1);
          strcpy(sub_argv[j - start_index], argv[j]);
        }
        get_full_path_command(&sub_argv[0]);
        execute(sub_argv, sub_argc);
        printf("child should never go through this!\n");
      }
    } else {
      /* parent process */
      if (i == 0) {
        close(pipe_arr[0][WRITE_END]);
      } else if (i == n - 1) {
        close(pipe_arr[n - 2][READ_END]);
      } else {
        close(pipe_arr[i - 1][READ_END]);
        close(pipe_arr[i][WRITE_END]);
      }

      waitpid(pid, NULL, 0);
      for (int k = start_index; k < argc; ++k) {
        if (strcmp("|", argv[k]) == 0) {
          end_index = k - 1;
          start_index = k + 1;
          break;
        }
      }
    }
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
      int argc = tokens_get_length(tokens);
      // bool run_bg = false;
      // /* Check if the process will run in background */
      // if (strcmp("&", tokens_get_token(tokens, argc - 1)) == 0) {
      //   run_bg = true;
      //   argc -= 1;
      // }

      /* convert tokens to argv_shell array */
      char* argv[argc + 1];
      argv[argc] = NULL;
      for (int i = 0; i < argc; ++i) {
        char* token = tokens_get_token(tokens, i);
        argv[i] = malloc(strlen(token) + 1);
        strcpy(argv[i], token);
        if (i == 0) {
          get_full_path_command(&argv[0]);
        }
      }

      int n = have_pipes(argv, argc);
      if (n == -1) {
        pid_t pid = fork();
        if (pid == 0) {
          /* child process */
          // tcsetpgrp(STDIN_FILENO, getpid());
          signal_default();
          execute(argv, argc);
        } else {
          /* parent process */
          // setpgid(pid, pid);
          waitpid(pid, NULL, 0);
        }
      } else {
        /* have pipes */
        pipe_handler(argv, argc, n + 1);
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
