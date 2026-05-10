#!/usr/bin/env python3

'''
@author Ismail Alwahsh
@since May 10, 2026
@description: Terminal UI for tinymole. Renders the tinymole ASCII banner
directly to the raw terminal on startup, then launches a Textual two-panel
interface. Left panel is a configuration form (hash, algorithm, thread count,
wordlist, candidates). Right panel tails logs/cracker.log live as the cracker
runs. Result is shown in the status bar at the bottom when the run finishes.
'''

import subprocess
import sys
import threading
import time
from pathlib import Path

import pyfiglet
from rich.console import Console
from rich.text import Text
from textual import on
from textual.app import App, ComposeResult
from textual.containers import ScrollableContainer, Vertical
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
                yield Input(placeholder="hex-encoded hash", id="hash-input")
                yield Label("Algorithm", classes="field-label")
                yield Select(
                    [("MD5", "md5"), ("SHA-1", "sha1"), ("SHA-256", "sha256")],
                    id="algo-select",
                    value="md5",
                )
                yield Label("Threads", classes="field-label")
                yield Input(value="4", id="threads-input")
                yield Label("Wordlist", classes="field-label")
                yield Input(value="data/rockyou.txt", id="wordlist-input")
                yield Label("Candidates", classes="field-label")
                yield Label("leave blank to use wordlist", classes="field-note")
                yield Input(placeholder="data/candidates_ranked.txt", id="candidates-input")
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

        cmd = [
            "./build/cracker",
            "--hash",     hash_val,
            "--algo",     algo,
            "--wordlist", wordlist,
            "--threads",  threads,
        ]
        if candidates:
            cmd += ["--candidates", candidates]

        self.query_one("#log-view", Log).clear()
        self.query_one("#status", Static).update(" running...")
        self.query_one("#crack-btn", Button).disabled = True

        threading.Thread(target=self._run, args=(cmd,), daemon=True).start()

    def _run(self, cmd: list[str]) -> None:
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
