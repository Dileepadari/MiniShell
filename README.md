<p align="center">
  <img src="./assets/logo-mark.png" width="96" alt="ADK DEV">
</p>

# MiniShell

A Unix shell for Linux, written in C. It runs external programs, pipelines and
redirections the way any shell does, and adds a set of builtins with names of
their own: `warp` for cd, `peek` for ls, `seek` for find, `proclore` for process
details, `pastevents` for history.

It is a working daily-driver-shaped shell rather than a demo: line editing with
history and tab completion, real job control with process groups and terminal
handover, and a test suite that drives it through a pseudo-terminal.

For architecture, module layout and setup, see **[DEVDOC.md](./DEVDOC.md)**.

## Getting started

```sh
make          # builds bin/minishell
./bin/minishell
```

The prompt shows your user, host and working directory:

```
<you@machine:~/projects>
```

`~` is the directory the shell was started in, not your account's home. Type
`help` for the builtin list, `help <name>` for one of them, and `exit` or Ctrl-D
to leave.

## Features

### Command language

- Pipelines of any length: `peek -la | grep .md | wc -l`
- Redirection with `<`, `>` and `>>`, on any command in a pipeline
- Several commands per line with `;`, and background execution with `&`
- Single quotes, double quotes and backslash escapes, with the usual rules:
  `echo 'a $b'` prints `a $b`, `echo "a $HOME"` expands
- Variables: `$HOME`, `${HOME}`, `$?` for the last exit status, `$$` for the
  shell's pid
- Filename patterns: `peek *.md`, `warp src/*/`, `cat log?.txt`. A pattern that
  matches nothing is passed through untouched rather than vanishing
- `~` and `~user` expand to the shell home and to that account's home

Each `;`-separated segment is expanded only once the segment before it has
finished, so `false ; echo $?` prints `1` and `touch new.txt ; peek *.txt`
includes the file that was just created.

### Builtins

| Command | Does |
|---|---|
| `warp [dir]...` | Change directory and print where you landed. No argument goes to the shell home, `-` to the previous directory, and several arguments are visited in turn. |
| `peek [-a] [-l] [path]...` | List a directory. `-a` includes hidden entries, `-l` gives the long form. Flags cluster (`-la`). Output is columnised on a terminal and one-per-line into a pipe. |
| `seek [-d\|-f] [-e] <name> [dir]` | Search a tree by name, matching `notes` against both `notes` and `notes.md`. `-d` and `-f` restrict to directories or files; `-e` acts on a single match, printing a file or moving into a directory. |
| `proclore [pid]` | Report a process: state, group, virtual memory and executable path. Defaults to the shell. |
| `pastevents [purge \| execute <n>]` | Show the last 15 commands, clear them, or re-run one by number. |
| `activities` | List the jobs this shell has spawned, in command order, with their state. |
| `fg [job]`, `bg [job]` | Resume a job in the foreground or the background. With no argument, the most recent one. |
| `ping <pid> <signal>` | Send a signal to a process. |
| `neonate -n <seconds>` | Print the most recently created pid at that interval until you press `x`. |
| `iMan <command>` | Show a manual page, fetched from man.he.net and falling back to the local `man`. |
| `exit [status]` | Leave the shell. |
| `help [command]` | List the builtins, or explain one. |

Anything that is not a builtin is looked up on `PATH`.

### Job control

- `sleep 30 &` runs in the background and prints a job number
- Ctrl-C interrupts the foreground job and leaves the shell running
- Ctrl-Z stops it and hands you back the prompt; `activities` then shows it as
  Stopped, and `bg 1` or `fg 1` picks it up again
- Finished background jobs are reported before the next prompt, with how they
  ended: `[1] done`, `[1] exited with 2`, `[1] terminated by Terminated`
- A command that takes more than two seconds has its name and duration shown in
  the next prompt: `<you@machine:~ sleep : 5s>`

### Line editing

The prompt is a real editor, not a raw read:

| Key | Does |
|---|---|
| Left, Right, Home, End | Move within the line |
| Up, Down | Walk through history; the line you were typing comes back |
| Tab | Complete a builtin or a `PATH` command in command position, a file path elsewhere. Fills in as far as the candidates agree, and lists them when they disagree |
| Backspace, Delete | Remove a character |
| Ctrl-A, Ctrl-E | Start and end of line |
| Ctrl-U, Ctrl-K | Delete to the start or to the end |
| Ctrl-W | Delete the word before the cursor |
| Ctrl-L | Clear the screen |
| Ctrl-C | Abandon the line |
| Ctrl-D | Exit on an empty line, delete a character otherwise |

Anything typed while a command is running is kept and used for the next prompt.

### Running non-interactively

```sh
./bin/minishell -c 'peek -la | wc -l'   # one command
./bin/minishell script.msh              # a file of commands
echo 'peek' | ./bin/minishell           # a pipe
```

Piped and `-c` input skip the editor and read plain lines, which is what makes
the shell scriptable and testable.

## What it deliberately does not do

The grammar stops where the exercise stops. There is no `&&` or `||`, no `2>`
or other numbered redirections, no here-documents, no subshells or command
substitution, no shell variables of its own (it reads the environment but never
assigns to it), no functions, and no aliases. A word that expands to something
with spaces stays one argument, because there is no field splitting.

## Requirements

Linux, a C11 compiler and make. `proclore`, `activities` and `neonate` read
`/proc`. OpenSSL is optional and only affects `iMan`; without it that command
uses the local `man`.
