/*
 Part of this code is originally from previous term attempt (Summer 2023 session of CS344).
 Checked that resubmission was allowed.
*/

#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <err.h>
#include <errno.h>
#include <unistd.h>
#include <ctype.h>
#include <string.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <stdint.h>
#include <signal.h>

#ifndef MAX_WORDS
#define MAX_WORDS 512
#endif

char *words[MAX_WORDS];
size_t wordsplit(char const *line);
char * expand(char const *word);

char main_pid[10];
char background_pid[10] = "";
char foreground_status[10] = "0";
char env_text[1000];

struct tokenHolder {
  char *parsedWords[MAX_WORDS];
  int  wordCounter;
  char *parsedRedirect[10];
  char *parsedFile[10];
  int  redirectCounter;
  int  fileCounter;
  int  parsedBackground;
};



void parse(size_t length, struct tokenHolder *results, int parseCounter);
void exiter(struct tokenHolder data);
void changer (struct tokenHolder data);
void redirect (struct tokenHolder data);
void handle_SIGINT(int signum);


int main(int argc, char *argv[])
{
  FILE *input = stdin;
  char *input_fn = "(stdin)";
  if (argc == 2) {
    input_fn = argv[1];
    input = fopen(input_fn, "re");
    if (!input) err(1, "%s", input_fn);
  } else if (argc > 2) {
    errx(1, "too many arguments");
  }

  struct sigaction SIGSTP_default = {0};
  struct sigaction SIGSTP_action = {0};
  SIGSTP_action.sa_handler = SIG_IGN;

  struct sigaction SIGINT_default = {0};

  struct sigaction SIGINT_actionIgnore = {0};
  SIGINT_actionIgnore.sa_handler = SIG_IGN;

  struct sigaction SIGINT_actionNothing = {0};
  SIGINT_actionNothing.sa_handler = handle_SIGINT;

  sigaction(20, &SIGSTP_action, &SIGSTP_default);
  
  sigaction(2, &SIGINT_actionIgnore, &SIGINT_default);


  char *line = NULL;
  size_t n = 0;
  int childStatus;
  pid_t childPID;
  int trackedStatus;
  pid_t trackedPID;

  for (;;) {

    while ((trackedPID = waitpid(0, &trackedStatus, WNOHANG | WUNTRACED)) && trackedPID > 0){
        if (WIFEXITED(trackedStatus) != 0) {
          fprintf(stderr, "Child process %jd done. Exit status %d.\n", (intmax_t) trackedPID, WEXITSTATUS(trackedStatus));
        }
        else if (WIFSIGNALED(trackedStatus) != 0) {
          fprintf(stderr, "Child process %jd done. Signaled %d.\n", (intmax_t) trackedPID, WTERMSIG(trackedStatus));
        }
        else if (WIFSTOPPED(trackedStatus) != 0) {
          kill(trackedPID, SIGCONT);
          fprintf(stderr, "Child process %jd stopped. Continuing.\n", (intmax_t) trackedPID);
        }
    }
    prompt:
    if (input == stdin) {
      char *PS1 = getenv("PS1");
      if (PS1){
        fprintf(stderr, "%s", PS1);
      }
      else {
        fprintf(stderr, "");
      }
    }
    
    struct tokenHolder *parseResults = malloc(sizeof(struct tokenHolder));
    sigaction(2, &SIGINT_actionNothing, NULL);
    ssize_t line_len = getline(&line, &n, input);
    if (errno == EINTR) {
      clearerr(input);
      errno = 0;
      sigaction(2, &SIGINT_actionIgnore, NULL);
      fprintf(stderr, "\n");
      goto prompt;
    }
    sigaction(2, &SIGINT_actionIgnore, NULL);
    if (line_len < 0) exit(0);
    size_t nwords = wordsplit(line);

    for (size_t i = 0; i < nwords; ++i) {
      char *exp_word = expand(words[i]);
      free(words[i]);
      words[i] = exp_word;
    }
   
    if (nwords == 0) continue;
    parse(nwords, parseResults, 0);
    if (parseResults->wordCounter == 0) continue;

    if (strcmp(parseResults->parsedWords[0], "exit") == 0) exiter(*parseResults);

    else if (strcmp(parseResults->parsedWords[0], "cd") == 0) changer(*parseResults);

    else {
      int execResults = 0;
      pid_t newChild = fork();
      switch (newChild) {
        case -1:
          fprintf(stderr, "Catastrophic child error!\n");

        case 0:
          sigaction(20, &SIGSTP_default, NULL);
          sigaction(2, &SIGINT_default, NULL);
          redirect(*parseResults);          
          execResults = execvp(parseResults->parsedWords[0], parseResults->parsedWords);
          if (execResults == -1){
            fprintf(stderr, "command failure\n");
            sprintf(foreground_status, "%i", WEXITSTATUS(childStatus));
            exit(1);
          }
         else{
            exit(0);
         }

        default:

          if (parseResults->parsedBackground == 1){
            sprintf(background_pid, "%i", newChild);
            childPID = waitpid(newChild, &childStatus, WNOHANG);
          }
          
          else {

            childPID = waitpid(newChild, &childStatus, WUNTRACED);
                      
            if (WIFEXITED(childStatus)){
            sprintf(foreground_status, "%i", WEXITSTATUS(childStatus));
            }

            else if (WIFSIGNALED(childStatus)){
              sprintf(foreground_status, "%i", WTERMSIG(childStatus) + 128);
            }
            else if (WIFSTOPPED(childStatus)){
              sprintf(background_pid, "%i", childPID);
              kill(childPID, SIGCONT);
              fprintf(stderr, "Child process %jd stopped. Continuing.\n", (intmax_t) childPID);
            }
          }
      }

    }
    free(parseResults);
    parseResults = NULL;
  }
}

char *words[MAX_WORDS] = {0};

/* Splits a string into words delimited by whitespace. Recognizes
 * comments as '#' at the beginning of a word, and backslash escapes.
 *
 * Returns number of words parsed, and updates the words[] array
 * with pointers to the words, each as an allocated string.
 */
size_t wordsplit(char const *line) {
  size_t wlen = 0;
  size_t wind = 0;

  char const *c = line;
  for (;*c && isspace(*c); ++c); /* discard leading space */

  for (; *c;) {
    if (wind == MAX_WORDS) break;
    /* read a word */
    if (*c == '#') break;
    for (;*c && !isspace(*c); ++c) {
      if (*c == '\\') ++c;
      void *tmp = realloc(words[wind], sizeof **words * (wlen + 2));
      if (!tmp) err(1, "realloc");
      words[wind] = tmp;
      words[wind][wlen++] = *c; 
      words[wind][wlen] = '\0';
    }
    ++wind;
    wlen = 0;
    for (;*c && isspace(*c); ++c);
  }
  return wind;
}


/* Find next instance of a parameter within a word. Sets
 * start and end pointers to the start and end of the parameter
 * token.
 */
char
param_scan(char const *word, char const **start, char const **end)
{
  static char *prev;
  if (!word) word = prev;
  
  char ret = 0;
  *start = 0;
  *end = 0;
  for (char const *s = word; *s && !ret; ++s) {
    s = strchr(s, '$');
    if (!s) break;
    switch (s[1]) {
    case '$':
    case '!':
    case '?':
      ret = s[1];
      *start = s;
      *end = s + 2;
      break;
    case '{':;
      char *e = strchr(s + 2, '}');
      if (e) {
        ret = s[1];
        *start = s;
        *end = e + 1;
      }
      break;
    }
  }
  prev = *end;
  return ret;
}

/* Simple string-builder function. Builds up a base
 * string by appending supplied strings/character ranges
 * to it.
 */
char *
build_str(char const *start, char const *end)
{
  static size_t base_len = 0;
  static char *base = 0;

  if (!start) {
    /* Reset; new base string, return old one */
    char *ret = base;
    base = NULL;
    base_len = 0;
    return ret;
  }
  /* Append [start, end) to base string 
   * If end is NULL, append whole start string to base string.
   * Returns a newly allocated string that the caller must free.
   */
  size_t n = end ? end - start : strlen(start);
  size_t newsize = sizeof *base *(base_len + n + 1);
  void *tmp = realloc(base, newsize);
  if (!tmp) err(1, "realloc");
  base = tmp;
  memcpy(base + base_len, start, n);
  base_len += n;
  base[base_len] = '\0';

  return base;
}

/* Expands all instances of $! $$ $? and ${param} in a string 
 * Returns a newly allocated string that the caller must free
 */
char *
expand(char const *word)
{
  char const *pos = word;
  char *start, *end;
  char c = param_scan(pos, &start, &end);
  build_str(NULL, NULL);
  build_str(pos, start);
  while (c) {
    if (c == '!') build_str(background_pid, NULL);
    else if (c == '$'){
      sprintf(main_pid, "%d", getpid()); 
      build_str(main_pid, NULL);
    }
    else if (c == '?') build_str(foreground_status, NULL);
    else if (c == '{') {
      char *s = start + 2;
      char *f = strchr(s, '}');
      char hold = *f;
      *f = '\0';
      char const *val = getenv(s);
      if (val) {
      build_str(val, NULL);
      }
      else { build_str("", NULL);
      }
      *f = hold;
      
    }
    pos = end;
    c = param_scan(pos, &start, &end);
    build_str(pos, start);
  }
  return build_str(start, NULL);
}

void 
parse(size_t length, struct tokenHolder *results, int parseCounter) {
  results->wordCounter = 0;
  results->parsedBackground = 0;
  results->redirectCounter = 0;
  results->fileCounter = 0;
  results->parsedFile[0] = NULL;
  results->parsedRedirect[0] = NULL;

  for (int i = 0; i < MAX_WORDS; i++) {
    results->parsedWords[i] = NULL;
  }

  for (int i = 0; i < length; i++) {
   if (strcmp(words[i], "<") == 0 || strcmp(words[i], ">") == 0 || strcmp(words[i], ">>") == 0) {
     results->parsedRedirect[results->redirectCounter] = words[i];
     i++;
     results->redirectCounter++;
     results->parsedFile[results->fileCounter] = words[i];
     results->fileCounter++;
   }
   else if( strcmp(words[i], "&") == 0 && i == length - 1) {
     results->parsedBackground = 1;
   }
   else {
    results->parsedWords[parseCounter] = words[i];
    results->wordCounter++;
    parseCounter++;
   }
  }
}

void
exiter (struct tokenHolder data) {
   if (data.wordCounter == 1) exit(atoi(foreground_status));

   else if (data.wordCounter == 2) {
     for (int i = 0; i < strlen(words[1]); i++) {
      if (data.parsedWords[1][i] < '0' || data.parsedWords[1][i] > '9'){ 
        fprintf(stderr, "%s: not a number\n", data.parsedWords[1]);
        return;
      }     
     }
     exit(atoi(data.parsedWords[1]));
   }
   else {
     fprintf(stderr, "too many arguments\n");
   }
}


void
changer (struct tokenHolder data) {
  if (data.wordCounter == 1) chdir(getenv("HOME"));

  else if (data.wordCounter == 2) {
    int change = chdir(data.parsedWords[1]);

    if (change == -1){
      int currentError = errno;
      fprintf(stderr, "%s\n", strerror(currentError));
      return;
    }
    return;
  }

  else {
    fprintf(stderr, "too many arguments\n");
    return;
  }
}

void
redirect (struct tokenHolder data) {
  
  int redirection;

  for (int i = 0; i < data.redirectCounter; i++) {
   
    if (strcmp(data.parsedRedirect[i], "<") == 0) {
      redirection = open(data.parsedFile[i], O_RDONLY);
      if (redirection == -1) {
        strerror(errno);
      }
      else{
      dup2(redirection, STDIN_FILENO);
      close(redirection);
      }
    }
    
    else if (strcmp(data.parsedRedirect[i], ">") == 0) {
      redirection = open(data.parsedFile[i], O_CREAT | O_TRUNC | O_WRONLY, 0666);
      dup2(redirection, STDOUT_FILENO);
      close(redirection);
    }

    else {
      redirection = open(data.parsedFile[i], O_CREAT | O_APPEND | O_WRONLY, 0666);
      dup2(redirection, STDOUT_FILENO);
      close(redirection);
    }
  }
}

void
handle_SIGINT (int signum) {}
