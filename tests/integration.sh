#!/usr/bin/env bash
#
# Integration tests: drive the built shell the way a user would and compare what
# comes back. Each case runs in a throwaway sandbox that is also MINISHELL_HOME,
# so `~`, the history file and every relative path stay inside it.

set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SHELL_BIN="$ROOT/bin/minishell"

if [ ! -x "$SHELL_BIN" ]; then
    echo "integration: $SHELL_BIN is not built; run make first" >&2
    exit 1
fi

passed=0
failed=0
SANDBOX=""

setup_sandbox() {
    SANDBOX="$(mktemp -d)"
    mkdir -p "$SANDBOX/docs" "$SANDBOX/docs/deep" "$SANDBOX/empty"
    printf 'alpha\nbravo\ncharlie\n' > "$SANDBOX/words.txt"
    printf 'hidden\n'               > "$SANDBOX/.secret"
    printf 'note\n'                 > "$SANDBOX/docs/notes.md"
    printf 'buried\n'               > "$SANDBOX/docs/deep/notes.md"
    mkdir -p "$SANDBOX/docs/deep/notes"
}

teardown_sandbox() {
    [ -n "$SANDBOX" ] && rm -rf "$SANDBOX"
    SANDBOX=""
}

# run <input> -> the shell's output, stderr merged in, sandbox as home.
# The history file is cleared first so each case starts from a known state;
# run_keep leaves it, for the cases that test persistence.
run() {
    rm -f "$SANDBOX/.minishell_history"
    run_keep "$1"
}

run_keep() {
    ( cd "$SANDBOX" && MINISHELL_HOME="$SANDBOX" "$SHELL_BIN" 2>&1 ) <<< "$1"
}

run_status() {
    rm -f "$SANDBOX/.minishell_history"
    ( cd "$SANDBOX" && MINISHELL_HOME="$SANDBOX" "$SHELL_BIN" >/dev/null 2>&1 ) <<< "$1"
    echo $?
}

check() {
    local name="$1" expected="$2" actual="$3"
    if [ "$expected" = "$actual" ]; then
        passed=$((passed + 1))
    else
        failed=$((failed + 1))
        echo "  FAIL $name"
        echo "    expected: $(printf '%q' "$expected")"
        echo "    actual:   $(printf '%q' "$actual")"
    fi
}

# A fresh sandbox per section, so files one case writes cannot reach the next.
section() {
    [ -n "$SANDBOX" ] && teardown_sandbox
    setup_sandbox
    echo "$1"
}

# --------------------------------------------------------------------------

section "words and quoting"
check "plain arguments"   "hello world"      "$(run 'echo hello world')"
check "extra whitespace"  "a b"              "$(run 'echo   a    b')"
check "single quotes"     "one   two"        "$(run "echo 'one   two'")"
check "double quotes"     "a | b ; c"        "$(run 'echo "a | b ; c"')"
check "escaped space"     "a b"              "$(run 'echo a\ b')"
check "adjacent quoting"  "premidpost"       "$(run "echo pre'mid'post")"
check "empty argument"    ""                 "$(run 'echo ""')"
check "exit status var"   "0"                "$(run 'true ; echo $?')"
check "failed status var" "1"                "$(run 'false ; echo $?')"

section "pipelines"
check "two stages"   "ALPHA"        "$(run 'echo alpha | tr a-z A-Z')"
check "three stages" "BRAVO"        "$(run 'printf "bravo\ncharlie\n" | head -1 | tr a-z A-Z')"
check "four stages"  "3"            "$(run 'printf "a\nb\nc\n" | sort | uniq | wc -l')"
check "builtin into pipe" "docs"    "$(run 'peek | head -1')"
check "pipe status"  "0"            "$(run_status 'echo x | cat')"

section "redirection"
check "write"        "captured"     "$(run 'echo captured > out.txt ; cat out.txt')"
check "append"       "one
two"                                "$(run 'echo one > out.txt ; echo two >> out.txt ; cat out.txt')"
check "read"         "alpha"        "$(run 'head -1 < words.txt')"
check "both ways"    "ALPHA"        "$(run 'tr a-z A-Z < words.txt > up.txt ; head -1 up.txt')"
check "in a pipe"    "CHARLIE"      "$(run 'tr a-z A-Z < words.txt | tail -1')"
check "missing input" "minishell: nope.txt: No such file or directory" \
                                    "$(run 'cat < nope.txt')"

section "warp"
check "home by default" "$SANDBOX"          "$(run 'warp docs ; warp' | tail -1)"
check "tilde"           "$SANDBOX"          "$(run 'warp docs ; warp ~' | tail -1)"
check "relative"        "$SANDBOX/docs"     "$(run 'warp docs')"
check "parent"          "$SANDBOX"          "$(run 'warp docs ; warp ..' | tail -1)"
check "previous"        "$SANDBOX/docs
$SANDBOX
$SANDBOX/docs"                              "$(run 'warp docs ; warp .. ; warp -')"
check "persists"        "deep
notes.md"                                   "$(run 'warp docs ; peek' | tail -2)"
check "missing"         "warp: nope: No such file or directory" "$(run 'warp nope')"
check "missing status"  "1"                 "$(run_status 'warp nope')"

section "peek"
check "sorted"      "docs
empty
words.txt"                                  "$(run 'peek')"
check "hidden"      "1"                     "$(run 'peek -a' | grep -c '^\.secret$')"
check "path"        "deep
notes.md"                                   "$(run 'peek docs')"
check "empty dir"   ""                      "$(run 'peek empty')"
check "long form"   "total 0"               "$(run 'peek -l empty')"
check "clustered"   "3"                     "$(run 'peek -la empty | wc -l')"
check "bad flag"    "peek: unknown flag \`-z\`" "$(run 'peek -z')"
check "missing dir" "peek: nope: No such file or directory" "$(run 'peek nope')"

section "seek"
check "by name"       "./docs/deep/notes
./docs/deep/notes.md
./docs/notes.md"                              "$(run 'seek notes')"
check "files only"    "./docs/deep/notes.md
./docs/notes.md"                              "$(run 'seek -f notes')"
check "dirs only"     "./docs/deep/notes"     "$(run 'seek -d notes')"
check "from a root"   "docs/deep/notes.md
docs/notes.md"                                "$(run 'seek notes.md docs')"
check "no match"      "No match found"        "$(run 'seek nothinghere')"
check "no match code" "1"                     "$(run_status 'seek nothinghere')"
check "both filters"  "seek: -d and -f cannot be used together" "$(run 'seek -d -f notes')"
check "act on file"   "docs/deep/notes.md
buried"                                       "$(run 'seek -e -f notes.md docs/deep')"
check "act needs one" "1"                     "$(run_status 'seek -e notes')"

section "glob"
check "match"      "words.txt"                "$(run 'peek *.txt')"
check "two matches" "more.txt
words.txt"                                    "$(run 'echo x > more.txt ; peek *.txt')"
check "no match"   "peek: *.nothing: No such file or directory" "$(run 'peek *.nothing')"

section "history"
check "records"    " 1  echo one
 2  echo two"                                 "$(run 'echo one
echo two
pastevents' | tail -2)"
check "no repeats" "1"                        "$(run 'echo same
echo same
pastevents' | grep -c 'echo same')"
check "execute"    "echo target
target"                                       "$(run 'echo target
pastevents execute 1' | tail -2)"
check "purge"      "gone"                     "$(run 'echo gone
pastevents purge
pastevents')"
check "survives a restart" " 1  echo remembered" "$(run 'echo remembered' >/dev/null; run_keep 'pastevents')"
check "bad index"  "pastevents: no event numbered 99" "$(run 'echo x
pastevents execute 99' | tail -1)"

section "jobs"
check "background prints a number" "1" \
    "$(run 'sleep 0.1 &' | grep -c '^\[1\] [0-9]*$')"
check "activities lists it" "1" \
    "$(run 'sleep 0.4 &
activities' | grep -c 'sleep 0.4 - Running')"
check "activities when idle" "No activities spawned by this shell." \
    "$(run 'activities')"

section "process introspection"
check "proclore self"  "1"   "$(run 'proclore' | grep -c '^pid : [0-9]*$')"
check "proclore fields" "5"  "$(run 'proclore' | wc -l)"
check "proclore bad pid" "proclore: 999999: no such process" "$(run 'proclore 999999')"
check "ping bad args"  "ping: usage: ping <pid> <signal>" "$(run 'ping 1')"
check "ping bad pid"   "ping: \`abc\` is not a pid"       "$(run 'ping abc 9')"

section "iMan"
check "needs an argument" "iMan: usage: iMan <command>" "$(run 'iMan')"
check "rejects odd names" "iMan: \`../etc\` is not a valid command name" "$(run 'iMan ../etc')"

section "errors and exit"
check "unknown command" "minishell: nosuch: command not found" "$(run 'nosuch')"
check "not found code"  "127"  "$(run_status 'nosuch')"
check "pipe syntax"     "minishell: syntax error: missing command around \`|\`" "$(run '| cat')"
check "quote syntax"    "minishell: unterminated single quote" "$(run "echo 'x")"
check "exit code"       "7"    "$(run_status 'exit 7')"
check "exit stops"      "before" "$(run 'echo before
exit
echo after')"
check "exit bad arg"    "exit: \`abc\` is not a number" "$(run 'exit abc')"

section "command line"
check "-c runs one command" "from -c" \
    "$(cd "$SANDBOX" && MINISHELL_HOME="$SANDBOX" "$SHELL_BIN" -c 'echo from -c' 2>&1)"
check "-v prints a version" "1" \
    "$("$SHELL_BIN" -v | grep -c '^minishell [0-9]')"
check "-h prints usage" "1" \
    "$("$SHELL_BIN" -h | grep -c 'Usage:')"
check "unknown option" "2" \
    "$("$SHELL_BIN" --nope >/dev/null 2>&1; echo $?)"

section "script files"
cat > "$SANDBOX/script.msh" <<'EOSCRIPT'
echo from a script
echo second line
EOSCRIPT
check "runs a file" "from a script
second line" \
    "$(cd "$SANDBOX" && MINISHELL_HOME="$SANDBOX" "$SHELL_BIN" script.msh 2>&1)"
check "missing file" "1" \
    "$(cd "$SANDBOX" && MINISHELL_HOME="$SANDBOX" "$SHELL_BIN" nope.msh >/dev/null 2>&1; echo $?)"

teardown_sandbox

echo
echo "$((passed + failed)) checks, $failed failed"
[ "$failed" -eq 0 ]
