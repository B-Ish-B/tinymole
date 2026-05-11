#!/usr/bin/env python3

'''
@author Ismail Alwahsh
@since May 9, 2026
@description: Terminal UI for tinymole. Launches a Textual application with a
splash screen showing the tinymole ASCII banner. Press space to enter the main
two-panel interface. Left panel is a configuration form (hash, algorithm,
thread count, wordlist, candidates). Right panel tails logs/cracker.log live
as the cracker runs. Runs the local cracker first; falls back to the Weakpass
API if no wordlist is provided or the hash is not found locally. A spinner in
the status bar shows when a run is active; the bar changes color on finish to
indicate found or not found.
'''

import subprocess
import threading
import time
from pathlib import Path

import pyfiglet
from textual import on
from textual.app import App, ComposeResult
from textual.containers import ScrollableContainer, Vertical
from textual.events import Key
from textual.screen import Screen
from textual.widgets import Button, Input, Label, Log, Select, Static

LOG_PATH = Path("logs/cracker.log")
_SPINNER = "|/-\\"


def _make_banner() -> str:
    lines = pyfiglet.figlet_format("tinymole", font="slant").splitlines()
    lines = [l for l in lines if l.strip() not in ("/____/", "\\____/")]
    while lines and not lines[-1].strip():
        lines.pop()
    return "\n".join(lines)


def _tab_complete(current: str) -> str:
    '''Return the next tab-completion for a partial path, or the input unchanged.'''
    if not current:
        return current
    p = Path(current)
    parent = p.parent if not current.endswith('/') else p
    prefix = p.name if not current.endswith('/') else ''
    try:
        matches = sorted(c for c in parent.iterdir() if c.name.startswith(prefix))
    except (OSError, PermissionError):
        return current
    if not matches:
        return current
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

        if event.key == 'ctrl+u':
            self.value = ''
            event.prevent_default()
            event.stop()
            return

        if event.key == 'ctrl+k':
            self.value = self.value[:self.cursor_position]
            event.prevent_default()
            event.stop()
            return


class SplashScreen(Screen):
    BINDINGS = [("space", "start", "Start")]

    CSS = """
    SplashScreen {
        align: center middle;
    }

    #splash-banner {
        color: #ffaf00;
        text-style: bold;
        text-align: center;
        width: auto;
    }

    #splash-hint {
        margin-top: 2;
        text-align: center;
        color: $text-muted;
        width: auto;
    }
    """

    def compose(self) -> ComposeResult:
        yield Static(_make_banner(), id="splash-banner", markup=False)
        yield Static("press [bold]space[/bold] to start", id="splash-hint")

    def action_start(self) -> None:
        self.app.switch_screen(CrackerScreen())


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

    #status.found {
        background: $success;
        color: $background;
    }

    #status.not-found {
        background: $error;
        color: $background;
    }

    #status.running {
        background: $warning;
        color: $background;
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
                yield Label("leave blank to use weakpass API only", classes="field-note")
                yield PathInput(placeholder="data/rockyou.txt", id="wordlist-input")
                yield Label("Candidates", classes="field-label")
                yield Label("leave blank to use wordlist", classes="field-note")
                yield PathInput(placeholder="data/candidates_ranked.txt", id="candidates-input")
                yield Button("Crack", id="crack-btn", variant="primary")

        with Vertical(id="log-panel"):
            yield Static("log", id="log-header")
            yield Log(id="log-view", auto_scroll=True)

        yield Static(" ready", id="status")

    def _set_status(self, text: str, css_class: str) -> None:
        status = self.query_one("#status", Static)
        status.update(text)
        status.set_classes(css_class)

    def _spin(self, stop_event: threading.Event, phase_label: str) -> None:
        status = self.query_one("#status", Static)
        i = 0
        while not stop_event.is_set():
            frame = _SPINNER[i % len(_SPINNER)]
            self.app.call_from_thread(
                lambda f=frame, p=phase_label: (
                    status.update(f" {f}  {p}"),
                    status.set_classes("running"),
                )
            )
            i += 1
            time.sleep(0.1)

    @on(Button.Pressed, "#crack-btn")
    def start_crack(self) -> None:
        hash_val   = self.query_one("#hash-input",        Input).value.strip()
        algo       = self.query_one("#algo-select",       Select).value
        threads    = self.query_one("#threads-input",     Input).value.strip() or "4"
        wordlist   = self.query_one("#wordlist-input",    Input).value.strip()
        candidates = self.query_one("#candidates-input",  Input).value.strip()

        if not hash_val:
            self._set_status(" error: hash is required", "not-found")
            return

        self.query_one("#log-view", Log).clear()
        self._set_status(" starting...", "running")
        self.query_one("#crack-btn", Button).disabled = True

        wordlist_exists = bool(wordlist) and Path(wordlist).exists()

        threading.Thread(
            target=self._run,
            args=(hash_val, algo, threads, wordlist if wordlist_exists else "", candidates),
            daemon=True,
        ).start()

    def _run(self, hash_val: str, algo: str, threads: str, wordlist: str, candidates: str) -> None:
        import os
        log_widget = self.query_one("#log-view", Log)
        crack_btn  = self.query_one("#crack-btn", Button)
        pid = os.getpid()

        def log(msg: str) -> None:
            import datetime
            now = datetime.datetime.now()
            ts = now.strftime("%H:%M:%S.") + f"{now.microsecond * 1000:09d}"
            line = f"{ts} [{pid}] weakpass_lookup.py    LOG_INFO    weakpass    {msg}"
            self.app.call_from_thread(log_widget.write_line, line)

        stop_spin = threading.Event()
        crack_result = ""

        # step 1: local cracker (if wordlist provided)
        if wordlist:
            cmd = [
                "./build/cracker",
                "--hash",     hash_val,
                "--algo",     algo,
                "--wordlist", wordlist,
                "--threads",  threads,
            ]
            if candidates:
                cmd += ["--candidates", candidates]

            spin = threading.Thread(target=self._spin, args=(stop_spin, "cracking..."), daemon=True)
            spin.start()

            cracker_proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, text=True)

            def tail_log() -> None:
                for _ in range(30):
                    if LOG_PATH.exists():
                        break
                    time.sleep(0.1)
                seen = 0
                while cracker_proc.poll() is None:
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

            cracker_proc.wait()
            stop_spin.set()
            spin.join()
            tail.join(timeout=1)

            # parse result from log file -- more reliable than stdout pipe
            crack_result = ""
            try:
                for line in reversed(LOG_PATH.read_text().splitlines()):
                    if "CRACKED in" in line and " ms: " in line:
                        crack_result = "cracked: " + line.split(" ms: ", 1)[1].strip()
                        break
                    if "NOT FOUND" in line:
                        break
            except OSError:
                pass

            if crack_result.startswith("cracked:"):
                self.app.call_from_thread(self._set_status, f" {crack_result}", "found")
                self.app.call_from_thread(setattr, crack_btn, "disabled", False)
                return

        # step 2: online API fallback (weakpass)
        stop_spin2 = threading.Event()
        spin2 = threading.Thread(target=self._spin, args=(stop_spin2, "checking online APIs..."), daemon=True)
        spin2.start()

        log(f"querying APIs  hash: {hash_val}  algo: {algo}")

        api_proc = subprocess.Popen(
            ["uv", "run", "src/python/weakpass_lookup.py", "--hash", hash_val, "--algo", algo],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        api_stdout, _ = api_proc.communicate()

        stop_spin2.set()
        spin2.join()

        api_result = api_stdout.strip()
        if api_result.startswith("cracked:"):
            log(api_result)
            self.app.call_from_thread(self._set_status, f" {api_result}", "found")
            self.app.call_from_thread(setattr, crack_btn, "disabled", False)
            return

        log("not found via API")
        self.app.call_from_thread(self._set_status, " not found", "not-found")
        self.app.call_from_thread(setattr, crack_btn, "disabled", False)


class TinyMole(App):
    TITLE = "tinymole"
    BINDINGS = [("q", "quit", "Quit")]

    def on_mount(self) -> None:
        self.push_screen(SplashScreen())


if __name__ == "__main__":
    TinyMole().run()
