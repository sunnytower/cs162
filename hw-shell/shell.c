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
#include <pwd.h>
#include <sys/stat.h>
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
  if (tokens_get_length(tokens) > 2 || tokens_get_length(tokens) < 2) {
    printf("invalid argument\n");
    return -1;
  }
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
}


void child_task(int start, int end, struct tokens* tokens) {
  char* argv[end - start + 1];
  int k = 0;
  for (int i = start; i < end; ++i) {
    char* token = tokens_get_token(tokens, i);
    if (token[0] == '>') {
      token = tokens_get_token(tokens, ++i);
      int fd = open(token, O_WRONLY | O_CREAT | O_TRUNC, 0664);
      dup2(fd, STDOUT_FILENO);
    } else if (token[0] == '<') {
      token = tokens_get_token(tokens, ++i);
      int fd = open(token, O_RDONLY);
      if (fd == -1) {
        perror("file don't exist");
        exit(-1);
      }
      dup2(fd, STDIN_FILENO);
    } else {
      argv[k++] = token;
    }
  }
  argv[k] = NULL;

  const char* cmd = tokens_get_token(tokens, start);
  if (strchr(cmd, '/')) {
    if (execv(cmd, argv) == -1) {
        perror(cmd);
    }
  } else {
    /* program in PATH */
    char* env_path = getenv("PATH");
    char* temp = strtok(env_path, ":");
    while (temp != NULL) {
      /* size: path  / cmd NULL */
      char path[strlen(temp) + 1 + strlen(cmd) + 1];
      strcpy(path, temp);
      strcat(path, "/");
      strcat(path, cmd);
      if (access(path, X_OK) == 0) {
        execv(path, argv);
      }
      temp = strtok(NULL, ":");
    }
    printf("%s: command not found\n", cmd);
  }
  /* execv failed */
  exit(-1);
}

int execute(int read_fd, int write_fd, int start, int end, struct tokens* tokens) {
  pid_t pid = fork();
  if (pid == -1) {
    perror("fork error\n");
    return -1;
  } else if (pid == 0) {
    /* child */
    dup2(read_fd, STDIN_FILENO);
    dup2(write_fd, STDOUT_FILENO);
    child_task(start, end, tokens);
  } else {
    /* parent */
  }
  return 0;
}

/* find i | to seperate tasks before | is what child i - 1 need to execute, after | is what child i need to execute */
int run_tasks(struct tokens* tokens) {
  size_t tokens_length = tokens_get_length(tokens);
  int read_fd = STDIN_FILENO;
  int pipe_fd[2];
  int start = 0;
  int process_num = 0;
  for (int i = 0; i < tokens_length; ++i) {
    char* token = tokens_get_token(tokens, i);
    if (token[0] == '|') {
      if (pipe(pipe_fd) == -1) {
        perror("can't create pipe\n");
        if (read_fd != STDIN_FILENO) {
          close(read_fd);
        }
        return -1;
      }
      /* pipe_fd[1] is write_fd */
      if (execute(read_fd, pipe_fd[1], start, i, tokens) == -1) {
        close(pipe_fd[0]);
        close(pipe_fd[1]);
        return -1;
      }
      ++process_num;
      if (read_fd != STDIN_FILENO) {
        close(read_fd);
      }
      close(pipe_fd[1]);
      read_fd = pipe_fd[0];
      start = i + 1;
    }
  }
  /* tail case and no pipe case */
  if (execute(read_fd, STDOUT_FILENO, start, tokens_length, tokens) == -1) {
    return -1;
  }
  if (read_fd != STDIN_FILENO) {
    close(read_fd);
  }
  ++process_num;
  for (int i = 0; i < process_num; ++i) {
    wait(NULL);
  }
  return 0;
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
      run_tasks(tokens);
    }

    if (shell_is_interactive)
      /* Please only print shell prompts when standard input is not a tty */
      fprintf(stdout, "%d: ", ++line_num);

    /* Clean up memory */
    tokens_destroy(tokens);
  }

  return 0;
}
