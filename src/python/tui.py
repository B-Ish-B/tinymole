#!/usr/bin/env python3

'''
@author Ismail Alwahsh
@since May 9, 2026
@description: Terminal UI for tinymole. Renders the tinymole ASCII banner
directly to the raw terminal on startup, then launches a Textual two-panel
interface. Left panel is a configuration form (hash, algorithm, thread count,
wordlist, candidates). Right panel tails logs/cracker.log live as the cracker
runs. If the wordlist file is missing the UI falls back to the Weakpass API
and logs the lookup progress in the right panel. Result is shown in the status
bar at the bottom when the run finishes.
'''

import subprocess
import sys
import threading
import time
from pathlib import Path

import pyfiglet
from rich.console import Console
from textual import on
from textual.app import App, ComposeResult
from textual.containers import ScrollableContainer, Vertical
from textual.events import Key
from textual.screen import Screen
from textual.widgets import Button, Input, Label, Log, Select, Static

LOG_PATH = Path("logs/cracker.log")


def _make_banner() -> str:
    lines = pyfiglet.figlet_format("tinymole", font="standard").splitlines()
    lines[1] = lines[1].replace("(_)", "| |", 1)  # i dot (_) looks like a face
    lines = [l for l in lines if l.strip() != "|___/"]  # remove isolated y descender
    while lines and not lines[-1].strip():
        lines.pop()
    return "\n".join(lines)


def _show_splash() -> None:
    console = Console()
    banner = _make_banner()
    console.print()
    for line in banner.splitlines():
        console.print(line, style="bold color(214)", justify="center", highlight=False)
    console.print()
    console.print("press [bold]enter[/bold] to start", justify="center")
    try:
        input()
    except (EOFError, KeyboardInterrupt):
        sys.exit(0)


def _tab_complete(current: str) -> str:
    '''Return the next tab-completion for a partial path, or the input unchanged.'''
    if not current:
        return current
    p = Path(current)
    parent = p.parent if not current.endswith('/') else p
    prefix = p.name if not current.endswith('/') else ''
    try:
        matches = sorted(
            c for c in parent.iterdir()
            if c.name.startswith(prefix)
        )
    except (OSError, PermissionError):
        return current
    if not matches:
        return current
    # Cycle: if current already matches the last entry, wrap to the first
    match = matches[0]
    for i, m in enumerate(matches):
        if str(m) == current:
            match = matches[(i + 1) % len(matches)]
            break
    return str(match) + ('/' if match.is_dir() else '')


class PathInput(Input):
    '''Input subclass with tab path completion and word-deletion shortcuts.'''

    def _on_key(self, event: Key) -> None:
        if event.key == 'tab':
            self.value = _tab_complete(self.value)
            self.cursor_position = len(self.value)
            event.prevent_default()
            event.stop()
            return

        # ctrl+w and ctrl+backspace: delete word to the left
        if event.key in ('ctrl+w', 'ctrl+backspace'):
            pos = self.cursor_position
            val = self.value
            while pos > 0 and val[pos - 1] == ' ':
                pos -= 1
            while pos > 0 and val[pos - 1] != ' ':
                pos -= 1
            self.value = val[:pos] + val[self.cursor_position:]
            self.cursor_position = pos
            event.prevent_default()
            event.stop()
            return

        # ctrl+delete: delete word to the right
        if event.key == 'ctrl+delete':
            pos = self.cursor_position
            val = self.value
            end = pos
            while end < len(val) and val[end] == ' ':
                end += 1
            while end < len(val) and val[end] != ' ':
                end += 1
            self.value = val[:pos] + val[end:]
            event.prevent_default()
            event.stop()
            return

        # ctrl+u: clear the whole line
        if event.key == 'ctrl+u':
            self.value = ''
            event.prevent_default()
            event.stop()
            return

        # ctrl+k: delete from cursor to end of line
        if event.key == 'ctrl+k':
            self.value = self.value[:self.cursor_position]
            event.prevent_default()
            event.stop()
            return


class CrackerScreen(Screen):
    CSS = """
    CrackerScreen {
        layout: horizontal;
    }

    #sidebar {
        width: 42;
        border-right: solid $accent;
    }

    #sidebar-scroll {
        padding: 1 2;
        height: 1fr;
    }

    .field-label {
        color: $text-muted;
        height: 1;
        margin-top: 1;
    }

    .field-note {
        color: $text-disabled;
        height: 1;
    }

    #crack-btn {
        margin-top: 2;
        width: 100%;
    }

    #log-panel {
        width: 1fr;
        padding: 1 2;
    }

    #log-header {
        height: 1;
        color: $text-muted;
        margin-bottom: 1;
    }

    #log-view {
        height: 1fr;
        border: none;
        background: $background;
    }

    #status {
        dock: bottom;
        height: 1;
        background: $accent;
        color: $background;
        padding: 0 1;
    }
    """

    def compose(self) -> ComposeResult:
        with Vertical(id="sidebar"):
            with ScrollableContainer(id="sidebar-scroll"):
                yield Label("Hash", classes="field-label")
                yield PathInput(placeholder="hex-encoded hash", id="hash-input")
                yield Label("Algorithm", classes="field-label")
                yield Select(
                    [("MD5", "md5"), ("SHA-1", "sha1"), ("SHA-256", "sha256")],
                    id="algo-select",
                    value="md5",
                )
                yield Label("Threads", classes="field-label")
                yield PathInput(value="4", id="threads-input")
                yield Label("Wordlist", classes="field-label")
                yield Label("leave blank to use weakpass API", classes="field-note")
                yield PathInput(placeholder="data/rockyou.txt", id="wordlist-input")
                yield Label("Candidates", classes="field-label")
                yield Label("leave blank to use wordlist", classes="field-note")
                yield PathInput(placeholder="data/candidates_ranked.txt", id="candidates-input")
                yield Button("Crack", id="crack-btn", variant="primary")

        with Vertical(id="log-panel"):
            yield Static("log", id="log-header")
            yield Log(id="log-view", auto_scroll=True)

        yield Static(" ready", id="status")

    @on(Button.Pressed, "#crack-btn")
    def start_crack(self) -> None:
        hash_val   = self.query_one("#hash-input",        Input).value.strip()
        algo       = self.query_one("#algo-select",       Select).value
        threads    = self.query_one("#threads-input",     Input).value.strip() or "4"
        wordlist   = self.query_one("#wordlist-input",    Input).value.strip()
        candidates = self.query_one("#candidates-input",  Input).value.strip()

        if not hash_val:
            self.query_one("#status", Static).update(" error: hash is required")
            return

        self.query_one("#log-view", Log).clear()
        self.query_one("#status", Static).update(" running...")
        self.query_one("#crack-btn", Button).disabled = True

        wordlist_exists = bool(wordlist) and Path(wordlist).exists()

        if not wordlist_exists:
            threading.Thread(
                target=self._run_api,
                args=(hash_val, algo),
                daemon=True,
            ).start()
        else:
            cmd = [
                "./build/cracker",
                "--hash",     hash_val,
                "--algo",     algo,
                "--wordlist", wordlist,
                "--threads",  threads,
            ]
            if candidates:
                cmd += ["--candidates", candidates]
            threading.Thread(target=self._run_cracker, args=(cmd,), daemon=True).start()

    def _run_api(self, hash_val: str, algo: str) -> None:
        log_widget = self.query_one("#log-view",  Log)
        status     = self.query_one("#status",    Static)
        crack_btn  = self.query_one("#crack-btn", Button)

        def log(msg: str) -> None:
            self.app.call_from_thread(log_widget.write_line, msg)

        log("no local wordlist -- querying weakpass API")
        log(f"hash: {hash_val}  algo: {algo}")
        log("")

        proc = subprocess.Popen(
            ["uv", "run", "src/python/weakpass_lookup.py", "--hash", hash_val, "--algo", algo],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        stdout, stderr = proc.communicate()

        for line in (stdout + stderr).strip().splitlines():
            log(line)

        result = stdout.strip()
        label = f" {result}" if result else " not found"
        self.app.call_from_thread(status.update, label)
        self.app.call_from_thread(setattr, crack_btn, "disabled", False)

    def _run_cracker(self, cmd: list[str]) -> None:
        log_widget = self.query_one("#log-view",  Log)
        status     = self.query_one("#status",    Static)
        crack_btn  = self.query_one("#crack-btn", Button)

        proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, text=True)

        def tail_log() -> None:
            for _ in range(30):
                if LOG_PATH.exists():
                    break
                time.sleep(0.1)

            seen = 0
            while proc.poll() is None:
                try:
                    lines = LOG_PATH.read_text().splitlines()
                    for line in lines[seen:]:
                        self.app.call_from_thread(log_widget.write_line, line)
                    seen = len(lines)
                except OSError:
                    pass
                time.sleep(0.15)

            try:
                lines = LOG_PATH.read_text().splitlines()
                for line in lines[seen:]:
                    self.app.call_from_thread(log_widget.write_line, line)
            except OSError:
                pass

        tail = threading.Thread(target=tail_log, daemon=True)
        tail.start()

        proc.wait()
        result = (proc.stdout.read() or "").strip()
        tail.join(timeout=1)

        label = f" {result}" if result else " not found"
        self.app.call_from_thread(status.update, label)
        self.app.call_from_thread(setattr, crack_btn, "disabled", False)


class TinyMole(App):
    TITLE = "tinymole"
    BINDINGS = [("q", "quit", "Quit")]

    def on_mount(self) -> None:
        self.push_screen(CrackerScreen())


if __name__ == "__main__":
    _show_splash()
    TinyMole().run()
