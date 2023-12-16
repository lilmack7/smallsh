# smallsh
Building of small shell, a shell in C that implements a CLI, in the same vein as bash.

Features:
This program has both an interactive and uninteractive mode.
It will parse input into tokens, and expands parameters '$$', '$!', '$?', and '${parameter}'.
It has built-in functionality relating to the commands exit and cd, while other commands are executed using the EXEC function.
Handles redirection operators '<', '>', and '>>'.
Can run commands in the background via '&'.
Has unique behavior when interacting with SIGINT and SIGSTP signals. 
