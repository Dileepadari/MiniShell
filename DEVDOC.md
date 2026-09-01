# MiniShell - Developer Documentation

Technical reference for the MiniShell codebase: how a line of input becomes
running processes, how job control and the line editor work, and how to build
and test it. For what the shell does from a user's point of view, see
[README.md](./README.md).

## Table of contents

- [Tech stack](#tech-stack)
- [Architecture overview](#architecture-overview)
- [Source layout](#source-layout)
- [Data model](#data-model)
- [The pipeline: lexer, parser, exec](#the-pipeline-lexer-parser-exec)
- [Expansion](#expansion)
- [Execution model](#execution-model)
- [Job control and signals](#job-control-and-signals)
- [The line editor](#the-line-editor)
- [History](#history)
- [Exit statuses](#exit-statuses)
- [Environment variables](#environment-variables)
- [Local development](#local-development)
- [Testing](#testing)
- [Known constraints and gotchas](#known-constraints-and-gotchas)

## Tech stack

C11 against glibc on Linux, built by a single Makefile with no external build
tooling. The only library dependencies are libc and, optionally, OpenSSL: the
Makefile probes for it with `pkg-config` and defines `HAVE_OPENSSL` when it is
present, which is what lets `iMan` fetch over HTTPS. Everything else - the line
editor, globbing, job control - uses POSIX interfaces directly (`termios`,
`glob(3)`, `tcsetpgrp`, `waitpid`) rather than readline or a shell library, so
the mechanism is visible in the source.

## Architecture overview

```
  input line
      |
      v
  +----------+     tokens      +----------+   pipeline_t   +--------+
  |  lexer   | --------------> |  parser  | -------------> |  exec  |
  +----------+                 +----------+                +--------+
  quotes, escapes,             grammar, redirections,       fork, pipe,
  $VAR, one segment            ~ and glob expansion         dup2, execvp
  at a time                                                     |
                                                                v
                                              +--------+   +----------+
                                              |  jobs  |<->| builtins |
                                              +--------+   +----------+
                                              process groups,  in-shell
                                              terminal,        commands
                                              fg/bg/activities
```

`shell.c` owns the loop that drives this and the single `shell_t` that every
stage is handed. There are no other globals except the job table it points at
and the history array in `history.c`.

The one structural decision worth knowing up front: **the shell lexes one
`;`-separated segment at a time**, rather than the whole line at once. Expansion
happens in the lexer, so scanning the whole line up front would expand `$?` in
`false ; echo $?` before `false` had run. `lex_segment()` stops after the first
top-level `;` or `&` and reports where it stopped; `shell_run_line()` loops.

## Source layout

```
include/        one header per module, each documenting its own contract
  minishell.h   shared includes, limits, and the shell_t state object
  lexer.h       token types and the scanner entry points
  parser.h      command_t / pipeline_t and the grammar
  exec.h        running a parsed line
  jobs.h        the job table and fg/bg/activities
  builtins.h    the builtin dispatch table
  terminal.h    the line reader
  history.h     pastevents storage
  prompt.h      prompt rendering
  signals.h     signal dispositions
  shell.h       lifecycle and the read-eval-print loop
  util.h        allocation, growable buffers, path helpers

src/
  main.c        argument handling: -c, -v, -h, a script file, or interactive
  shell.c       init, terminal handshake, the loop, per-segment evaluation
  lexer.c       quoting, escapes and variable substitution
  parser.c      grammar, plus tilde and glob expansion
  exec.c        pipes, redirection, fork/exec, builtin dispatch
  jobs.c        job table, waiting, fg/bg/activities
  signals.c     what the shell ignores and what children get back
  terminal.c    raw mode, editing keys, history recall, tab completion
  prompt.c      the prompt string and its visible width
  history.c     the 15-entry list and its file
  util.c        xmalloc, sbuf_t, svec_t, path_join, path_abbreviate
  builtins/     one file per builtin, plus table.c for the dispatch table

tests/
  unit/         lexer, parser, util and history, with a small assert harness
  integration.sh  drives the built binary through a pipe
  interactive.py  drives it through a pseudo-terminal
```

## Data model

```c
typedef struct command {
    char **argv;      /* NULL-terminated, fully expanded */
    int    argc;
    char  *infile;    /* `< file`, or NULL */
    char  *outfile;   /* `> file` or `>> file`, or NULL */
    int    append;
} command_t;

typedef struct pipeline {
    command_t       *cmds;
    int              ncmds;
    int              background;
    char            *text;    /* reconstructed, shown by `activities` */
    struct pipeline *next;
} pipeline_t;
```

A pipeline is one job. `text` is rebuilt from the expanded argv rather than
sliced out of the input, which keeps the parser from having to track source
spans and means `activities` shows what actually ran.

`shell_t` (in `minishell.h`) carries the home directory, the previous directory
for `warp -`, the history path, the last exit status, the shell's process group
and terminal fd, the job table, and the pending slow-command note for the next
prompt. It is passed to every stage; the one global, `g_shell`, exists because
`main()` needs somewhere to put it.

## The pipeline: lexer, parser, exec

**Lexer** (`lexer.c`). Produces `TOK_WORD`, `TOK_PIPE`, `TOK_SEMI`, `TOK_AMP`,
`TOK_REDIR_IN`, `TOK_REDIR_OUT`, `TOK_REDIR_APPEND` and a terminating `TOK_EOF`.
It owns every quoting rule, because only the scanner knows which quoting context
a character is in - `$HOME` expands inside double quotes and not inside single
ones, and that distinction is gone by the time quotes have been removed. Words
carry a `quoted` flag so later stages know not to tilde-expand or glob them.

Variable lookup is a callback (`var_lookup_fn`), so the shell can add `$?` and
`$$` while the unit tests supply a fixture and never touch the real environment.

**Parser** (`parser.c`). A single pass over the tokens with the grammar in
`parser.h`. Redirections are pulled out of the word stream and attached to the
command being built; the last redirection of each kind wins. Tilde expansion and
globbing happen here, after quoting is resolved, because one unquoted word can
become several arguments.

**Exec** (`exec.c`). Walks the `pipeline_t` list. See below.

## Expansion

In order, and each step only on what the previous one produced:

1. **Variables**, in the lexer, at scan time. `$NAME`, `${NAME}`, `$?`, `$$`.
   An unset name becomes empty. A `$` that starts nothing recognisable stays a
   literal `$`.
2. **Tilde**, in the parser, only on unquoted words. `~` and `~/x` use the shell
   home; `~user` uses `getpwnam`; an unknown user is left alone.
3. **Globbing**, in the parser, only on unquoted words containing `*`, `?` or
   `[`. Uses `glob(3)`. A pattern that matches nothing is kept literally, so the
   command reports the pattern back rather than silently losing an argument.

There is no field splitting: an expansion that produces spaces stays one
argument. That is a deliberate simplification, not an oversight - splitting
would need a fourth pass and a quoting-aware rejoin.

## Execution model

For each pipeline:

- **A lone foreground builtin runs in the shell process.** Redirections are
  applied by saving `STDIN`/`STDOUT` with `dup()`, and restored afterwards. This
  is what allows `warp`, `exit`, `pastevents purge` and `seek -e` on a directory
  to change the shell's own state.
- **Everything else forks.** `run_forked_pipeline()` creates `ncmds - 1` pipes,
  one at a time, and hands each child its read end from the previous iteration
  and the write end of the current one. Redirections are applied after the pipe
  wiring, so an explicit `>` overrides the pipe, as in any shell.
- A builtin inside a pipeline runs in the child and its effect on shell state is
  lost. `warp x | cat` changes nothing, which is what bash does too.

Both the parent and the child call `setpgid()` on the child, because neither can
assume it was scheduled first. `fflush(NULL)` runs before the fork loop: without
it, buffered shell output would reach a redirected stdout after the child's.

## Job control and signals

An interactive shell ignores `SIGINT`, `SIGQUIT`, `SIGTSTP`, `SIGTTIN`, `SIGTTOU`
and `SIGPIPE`. Nothing is lost by that, because every job runs in its own process
group and the terminal driver delivers Ctrl-C and Ctrl-Z to the *foreground*
group, which is never the shell's. `signals_reset_child()` puts every disposition
back to the default between fork and exec, so children behave normally.

At startup `claim_terminal()` waits until the shell is in the foreground process
group, then makes itself a group leader and takes the terminal. If `tcgetpgrp()`
fails - the tty is not our controlling terminal, which happens under some test
harnesses - job control is switched off (`term_fd = -1`) and the shell carries on
with line editing intact, rather than looping on a handshake that can never
complete.

The job table is a fixed array of `MAX_JOBS` slots. **Job numbers are assigned to
background jobs only**, and to a foreground job at the moment it is stopped, so
the numbers count what a user would call jobs and do not jump. `jobs_poll()` runs
before every prompt with `WNOHANG | WUNTRACED | WCONTINUED` and reports finished
background jobs there, rather than from a `SIGCHLD` handler where `printf` would
not be async-signal-safe.

`jobs_wait_foreground()` copies the pgid, the last pid and the table slot before
it starts waiting, because the entry is freed the moment the last process of the
pipeline exits.

## The line editor

`terminal.c` puts the tty into raw mode (`ICANON`, `ECHO`, `ISIG` and `IEXTEN`
off) for the duration of one `line_read()` and restores it before anything else
runs. `ISIG` off is why Ctrl-C and Ctrl-Z arrive as ordinary bytes while you are
typing: the editor acts on them directly and no handler is involved.

Both `tcsetattr` calls use `TCSADRAIN` rather than `TCSAFLUSH`. `TCSAFLUSH`
discards unread input, which would throw away anything typed while the previous
command was running.

Redrawing is single-row: long lines scroll horizontally inside the terminal
width rather than wrapping, so a repaint never has to work out where the
terminal chose to break. The prompt's visible width comes from `prompt_build()`,
which counts columns separately from the string because the string contains ANSI
colour escapes.

Completion looks at what precedes the word: in command position it offers
builtins and every executable on `PATH`, elsewhere it offers filesystem paths.
It inserts the longest prefix all candidates share, appends a space for a unique
match and a `/` for a directory, and lists the candidates when there is nothing
left to agree on.

When stdin is not a terminal, `line_read()` falls back to `getline()` and none of
the above happens.

## History

Up to `HISTORY_MAX` (15) lines in memory, oldest first, mirrored to
`.minishell_history` in the shell home. Blank lines, an exact repeat of the
previous line and anything whose first word is `pastevents` are not recorded.

Saving writes a temporary file and renames it, so an interrupted save cannot
truncate the history already on disk.

History is recorded for interactive and piped input, but not for `-c` or a
script file argument - a script should not push a user's own history out of the
window.

## Exit statuses

| Status | Means |
|---|---|
| 0 | Success |
| 1 | The command itself failed |
| 2 | A syntax or quoting error in the input |
| 126 | Found but not executable |
| 127 | Command not found |
| 128 + n | Killed by signal n |

`$?` reads the status of the last pipeline. The shell's own exit status is that
of the last command it ran, or the argument to `exit`.

## Environment variables

| Variable | Read by | Purpose |
|---|---|---|
| `MINISHELL_HOME` | `shell.c` | Overrides the shell home, the directory shown as `~`. Without it the home is the working directory at startup. The test suites set this so every case is hermetic. |
| `PATH` | `exec.c`, `terminal.c` | Command lookup, and the candidate list for tab completion. |
| `USER` | `prompt.c` | Last fallback for the prompt's user name, after `getlogin()` and `getpwuid()`. |
| `TERM` | not read | The editor emits plain ANSI sequences and does not consult terminfo. |

The shell reads the environment but never writes to it: there is no `export` and
no assignment syntax.

## Local development

```sh
git clone <repo> && cd MiniShell
make                 # bin/minishell
make run             # build and start it
make test            # unit, integration and interactive suites
make debug           # rebuild with ASan and UBSan
make clean
```

`make debug` does a clean rebuild, because sanitizer objects cannot be linked
against ordinary ones. Run `make clean` before switching back.

Objects land in `build/`, mirroring the source tree; binaries in `bin/`. Header
dependencies are tracked with `-MMD -MP`, so touching a header rebuilds what
depends on it.

## Testing

Three suites, all run by `make test`:

- **`tests/unit/`** - the pure logic: lexer, parser, util and history, about 150
  assertions through a small harness in `tests/unit/test.h`. Each suite is a
  function listed in `test_main.c`. A failing check reports its file and line and
  lets the rest of the run continue.
- **`tests/integration.sh`** - drives the built binary through a pipe and
  compares output: quoting, pipelines, redirection, every builtin, glob, history,
  exit statuses, the command-line options. Each section gets a fresh sandbox
  directory which is also `MINISHELL_HOME`, and the history file is cleared
  before each case, so no case can see another's leftovers.
- **`tests/interactive.py`** - opens a pseudo-terminal, makes it the shell's
  controlling terminal, and types at it. This is the only place the line editor,
  the prompt, Ctrl-C, Ctrl-Z, `fg`/`bg` and `neonate` are reachable at all.

Not covered: `iMan`'s network path (the tests check argument validation only, so
the suite does not depend on a network), and the `MAX_JOBS` overflow path.

Everything passes under `make debug`, which is worth running after any change to
`exec.c` or `jobs.c`.

## Known constraints and gotchas

- **The shell home is not `$HOME`.** It is the directory the shell started in,
  which is what the specification asks for. `~` in the prompt, in `warp` and in
  completion all mean that directory. Set `MINISHELL_HOME` to pin it.
- **`TCSAFLUSH` loses type-ahead.** Anything typed while a command runs is
  discarded when the editor re-enters raw mode with `TCSAFLUSH`. Both calls use
  `TCSADRAIN` for that reason; changing them back reintroduces the bug silently,
  because nothing crashes - a line simply never runs.
- **A job entry can be freed while you hold a pointer to it.** `absorb_status()`
  releases the slot when the last process of a pipeline exits. Copy what you need
  out of a `job_t *` before any wait call, as `jobs_wait_foreground()` does.
- **Both parent and child must `setpgid()`.** Setting it in only one of them is a
  race: whichever runs second finds the group already correct, but whichever runs
  first is what makes `tcsetpgrp()` safe.
- **`man.he.net` is HTTPS-only.** It answers port 80 with a 301 and nothing else,
  so a plain-socket fetch cannot work. `iMan` needs OpenSSL, and falls back to
  the local `man` without it. Its `<PRE>` tag carries attributes, so the
  extractor matches the tag name and skips to the `>`.
- **Builtins in a pipeline are children.** `warp x | cat` and `exit | cat` do
  nothing to the shell. That is correct, and matches bash, but it surprises
  people who expect `exit` to work anywhere.
- **`neonate` without a terminal prints once and returns.** There is no way to
  press `x`, and looping forever in a script would be worse.
