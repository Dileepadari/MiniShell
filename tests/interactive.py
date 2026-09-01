#!/usr/bin/env python3
"""
Interactive tests: drive the shell through a pseudo-terminal.

The line editor, the prompt and job control only exist when stdin is a tty, so
none of it is reachable from the piped tests in integration.sh. This opens a pty,
types at it and waits for what should come back.
"""
import os
import pty
import re
import select
import shutil
import signal
import struct
import subprocess
import sys
import fcntl
import termios
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SHELL = os.path.join(ROOT, "bin", "minishell")

passed = 0
failed = 0

ANSI = re.compile(r"\x1b\[[0-9;]*[A-Za-z]|\x1b\][^\x07]*\x07")


def clean(text):
    """Strip escape sequences and carriage returns so output can be compared."""
    return ANSI.sub("", text).replace("\r", "")


class Session:
    def __init__(self, home):
        self.master, slave = pty.openpty()
        # 200 columns keeps the editor from scrolling a line out of view.
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 40, 200, 0, 0))
        env = dict(os.environ, MINISHELL_HOME=home, TERM="dumb")

        def become_session_leader():
            os.setsid()
            fcntl.ioctl(0, termios.TIOCSCTTY, 0)   # adopt the pty as our tty

        self.proc = subprocess.Popen(
            [SHELL], stdin=slave, stdout=slave, stderr=slave,
            cwd=home, env=env, preexec_fn=become_session_leader, close_fds=True)
        os.close(slave)
        self.buffer = ""
        self.wait_for("> ")

    def send(self, data):
        os.write(self.master, data.encode())

    def read_available(self, timeout=0.2):
        ready, _, _ = select.select([self.master], [], [], timeout)
        if not ready:
            return ""
        try:
            chunk = os.read(self.master, 65536).decode(errors="replace")
        except OSError:
            return ""
        self.buffer += chunk
        return chunk

    def wait_for(self, needle, timeout=5.0):
        deadline = time.time() + timeout
        while time.time() < deadline:
            if needle in clean(self.buffer):
                return True
            if not self.read_available(0.1) and self.proc.poll() is not None:
                break
        return needle in clean(self.buffer)

    def drain(self, seconds=0.3):
        deadline = time.time() + seconds
        while time.time() < deadline:
            self.read_available(0.05)
        return clean(self.buffer)

    def clear(self):
        self.drain(0.2)
        self.buffer = ""

    def close(self):
        try:
            self.send("\x04")
            self.proc.wait(timeout=2)
        except Exception:
            self.proc.kill()
        finally:
            os.close(self.master)


def check(name, condition, detail=""):
    global passed, failed
    if condition:
        passed += 1
    else:
        failed += 1
        print("  FAIL %s" % name)
        if detail:
            print("    %s" % detail.replace("\n", "\n    "))


def main():
    if not os.access(SHELL, os.X_OK):
        print("interactive: %s is not built; run make first" % SHELL, file=sys.stderr)
        return 1

    home = tempfile.mkdtemp()
    open(os.path.join(home, "words.txt"), "w").write("alpha\nbravo\n")
    os.mkdir(os.path.join(home, "workspace"))

    try:
        print("prompt and editing")
        s = Session(home)
        out = clean(s.buffer)
        check("prompt shows user@host and ~", re.search(r"<[^@]+@[^:]+:~> $", out) is not None, out)

        s.clear()
        s.send("echo interactive\n")
        s.wait_for("interactive")
        check("runs a typed command", "interactive" in clean(s.buffer), clean(s.buffer))

        # Backspace should remove the mistyped character, not the whole word.
        s.clear()
        s.send("echo ZZZX\x7f\n")
        s.wait_for("\nZZZ\n")
        check("backspace edits the line", "\nZZZ\n" in clean(s.buffer), clean(s.buffer))

        # Ctrl-A jumps home, so the inserted text lands at the front.
        s.clear()
        s.send("world\x01echo hello \n")
        s.wait_for("hello world")
        check("ctrl-a moves to the start", "hello world" in clean(s.buffer), clean(s.buffer))

        # Ctrl-U discards what has been typed so far.
        s.clear()
        s.send("echo discarded\x15echo kept\n")
        s.wait_for("kept")
        text = clean(s.buffer)
        check("ctrl-u clears the line", "kept" in text and "discarded\n" not in text, text)

        print("history recall")
        s.clear()
        s.send("\x1b[A")            # up arrow
        s.drain(0.3)
        check("up arrow recalls", "echo kept" in clean(s.buffer), clean(s.buffer))
        s.send("\x15")              # discard the recalled line
        s.drain(0.2)

        print("tab completion")
        # Only one file starts with "words", so the whole name is filled in.
        s.clear()
        s.send("peek words\t")
        s.drain(0.4)
        check("completes a unique file name", "peek words.txt " in clean(s.buffer), clean(s.buffer))
        s.send("\x15")
        s.drain(0.2)

        # "wo" matches words.txt and workspace/, so only the shared "wor" is
        # filled in; pressing tab again with nothing to add lists both.
        s.clear()
        s.send("peek wo\t")
        s.drain(0.4)
        check("completes as far as candidates agree", "peek wor" in clean(s.buffer), clean(s.buffer))
        s.send("\t")
        s.drain(0.4)
        text = clean(s.buffer)
        check("lists the candidates", "words.txt" in text and "workspace/" in text, text)
        s.send("\x15")
        s.drain(0.2)

        s.clear()
        s.send("pastev\t")
        s.drain(0.4)
        check("completes a builtin", "pastevents" in clean(s.buffer), clean(s.buffer))
        s.send("\x15")
        s.drain(0.2)

        print("interrupts")
        s.clear()
        s.send("echo abandoned\x03")
        s.wait_for("^C")
        s.drain(0.3)
        text = clean(s.buffer)
        check("ctrl-c abandons the line", "^C" in text and "\nabandoned" not in text, text)

        s.clear()
        s.send("sleep 5\n")
        time.sleep(0.4)
        s.send("\x03")              # Ctrl-C reaches the job, not the shell
        s.drain(0.5)
        s.send("echo alive\n")
        s.wait_for("alive")
        check("ctrl-c kills the job and keeps the shell", "alive" in clean(s.buffer), clean(s.buffer))

        print("job control")
        s.clear()
        s.send("sleep 30\n")
        time.sleep(0.4)
        s.send("\x1a")              # Ctrl-Z
        s.wait_for("stopped")
        check("ctrl-z stops the job", "stopped" in clean(s.buffer), clean(s.buffer))

        s.clear()
        s.send("activities\n")
        s.wait_for("sleep 30")
        check("a stopped job is listed", "sleep 30 - Stopped" in clean(s.buffer), clean(s.buffer))

        s.clear()
        s.send("bg 1\n")
        s.wait_for("[1] sleep 30 &")
        s.drain(0.3)
        s.send("activities\n")
        s.wait_for("Running")
        check("bg resumes it", "sleep 30 - Running" in clean(s.buffer), clean(s.buffer))

        s.clear()
        s.send("fg 1\n")
        time.sleep(0.4)
        s.send("\x03")              # interrupt the job now in the foreground
        s.drain(0.5)
        s.send("activities\n")
        s.wait_for("activities")
        s.drain(0.4)
        check("fg hands over the terminal, then the job ends",
              "No activities" in clean(s.buffer), clean(s.buffer))

        s.clear()
        s.send("sleep 0.2 &\n")
        s.wait_for("[")
        s.drain(1.0)
        s.send("\n")
        s.drain(0.4)
        check("a finished background job is announced", "done" in clean(s.buffer), clean(s.buffer))

        print("slow command reporting")
        # The threshold is two seconds, so this has to actually wait.
        s.clear()
        s.send("sleep 3\n")
        s.wait_for("sleep : 3s", timeout=8)
        check("a slow command is timed in the next prompt",
              "sleep : 3s>" in clean(s.buffer), clean(s.buffer))
        s.clear()
        s.send("echo quick\n")
        s.wait_for("quick")
        s.drain(0.3)
        check("the timing note is shown only once",
              "sleep : 3s>" not in clean(s.buffer).split("quick", 1)[1], clean(s.buffer))

        print("neonate")
        s.clear()
        s.send("neonate -n 1\n")
        s.drain(0.6)
        check("prints a pid", re.search(r"\n\d+\n", clean(s.buffer)) is not None, clean(s.buffer))
        s.send("x")
        s.drain(0.6)
        s.clear()
        s.send("echo after neonate\n")
        s.wait_for("after neonate")
        check("x stops it", "after neonate" in clean(s.buffer), clean(s.buffer))

        print("exit")
        s.clear()
        s.send("\x04")              # Ctrl-D on an empty line
        s.proc.wait(timeout=3)
        check("ctrl-d exits", s.proc.returncode == 0, "return code %s" % s.proc.returncode)
        os.close(s.master)
    finally:
        shutil.rmtree(home, ignore_errors=True)

    print()
    print("%d checks, %d failed" % (passed + failed, failed))
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
