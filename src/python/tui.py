#!/usr/bin/env python3

# @author Ish
# @since May 2026
# TUI for tinymole. Splash screen shows ASCII banner, then transitions to a
# two-panel interface: left = config form, right = live log tail.

import subprocess
import threading
import time
from pathlib import Path

from textual import on
from textual.app import App, ComposeResult
from textual.containers import Vertical
from textual.screen import Screen
from textual.widgets import Button, Input, Label, Log, Select, Static

LOG_PATH = Path("logs/cracker.log")

BANNER = r"""
 _   _                           _
| |_(_)_ __  _   _ _ __ ___  ___| | ___
| __| | '_ \| | | | '_ ` _ \/ _ \ |/ _ \
| |_| | | | | |_| | | | | | |  __/ |  __/
 \__|_|_| |_|\__, |_| |_| |_|\___|_|\___|
             |___/
"""


class SplashScreen(Screen):
    CSS = """
    SplashScreen {
        align: center middle;
        background: $background;
    }
    #banner {
        color: $accent;
        text-align: center;
        text-style: bold;
    }
    #hint {
        color: $text-muted;
        text-align: center;
        margin-top: 2;
    }
    """

    def compose(self) -> ComposeResult:
        yield Static(BANNER, id="banner")
        yield Static("press any key to start", id="hint")

    def on_key(self) -> None:
        self.app.switch_screen(CrackerScreen())


class CrackerScreen(Screen):
    CSS = """
    CrackerScreen {
        layout: horizontal;
    }
    #sidebar {
        width: 36;
        min-width: 36;
        padding: 1 2;
        border-right: solid $accent;
    }
    #log-panel {
        width: 1fr;
        padding: 1 2;
    }
    .field-label {
        color: $text-muted;
        margin-top: 1;
    }
    #crack-btn {
        margin-top: 2;
        width: 100%;
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
            yield Label("Hash", classes="field-label")
            yield Input(placeholder="hex-encoded hash", id="hash-input")
            yield Label("Algorithm", classes="field-label")
            yield Select(
                [("MD5", "md5"), ("SHA-1", "sha1"), ("SHA-256", "sha256")],
                id="algo-select",
                value="md5",
            )
            yield Label("Threads", classes="field-label")
            yield Input(placeholder="4", value="4", id="threads-input")
            yield Label("Wordlist", classes="field-label")
            yield Input(value="data/rockyou.txt", id="wordlist-input")
            yield Label("Candidates  (leave blank = wordlist)", classes="field-label")
            yield Input(placeholder="data/candidates_ranked.txt", id="candidates-input")
            yield Button("Crack", id="crack-btn", variant="primary")
        with Vertical(id="log-panel"):
            yield Log(id="log-view", auto_scroll=True)
        yield Static(" ready", id="status")

    @on(Button.Pressed, "#crack-btn")
    def start_crack(self) -> None:
        hash_val    = self.query_one("#hash-input",      Input).value.strip()
        algo        = self.query_one("#algo-select",     Select).value
        threads     = self.query_one("#threads-input",   Input).value.strip() or "4"
        wordlist    = self.query_one("#wordlist-input",  Input).value.strip()
        candidates  = self.query_one("#candidates-input", Input).value.strip()

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
        log_widget  = self.query_one("#log-view",  Log)
        status      = self.query_one("#status",    Static)
        crack_btn   = self.query_one("#crack-btn", Button)

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
    BINDINGS = [("q", "quit", "Quit"), ("ctrl+c", "quit", "Quit")]

    def on_mount(self) -> None:
        self.push_screen(SplashScreen())


if __name__ == "__main__":
    TinyMole().run()
