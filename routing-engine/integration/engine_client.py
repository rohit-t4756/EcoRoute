"""Standard-library bridge to the persistent local C++ worker; no shell commands."""
from __future__ import annotations
import json
import math
import queue
import subprocess
import threading
from pathlib import Path

class EngineError(RuntimeError):
    def __init__(self, code, message):
        super().__init__(message)
        self.code = code

def _number(value):
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        raise ValueError("Expected a finite number")
    return repr(value)

def _id(value):
    text = str(value)
    if not text.isascii() or not text.isdecimal() or not 0 <= int(text) <= 2**64 - 1:
        raise ValueError("Expected an unsigned 64-bit node ID")
    return text

def _text(value):
    if not isinstance(value, str) or any(ord(c) < 32 for c in value):
        raise ValueError("Expected text without control characters")
    return json.dumps(value, ensure_ascii=False)

class EngineProcess:
    """Serializes pipe requests; concurrent callers safely share one worker.

    On timeout the worker is terminated so a late response cannot be misassigned.
    A subsequent call starts a new worker. Use multiple instances for parallel
    worker processes; each has its own engine and bounded caches.
    """
    def __init__(self, executable, configuration, timeout_seconds=10):
        if not math.isfinite(timeout_seconds) or timeout_seconds <= 0:
            raise ValueError("Positive timeout required")
        self.executable = str(Path(executable).resolve())
        self.configuration = str(Path(configuration).resolve())
        self.timeout = timeout_seconds
        self._lock = threading.Lock()
        self._process = None
        self._responses = None

    def _start(self):
        if self._process and self._process.poll() is None:
            return
        self._stop()
        self._responses = queue.Queue()
        self._process = subprocess.Popen(
            [self.executable, "--config", self.configuration, "--serve"],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
            text=True, encoding="utf-8", bufsize=1)
        process, responses = self._process, self._responses
        def read():
            try:
                for line in process.stdout:
                    responses.put(line)
            finally:
                responses.put(None)
        threading.Thread(target=read, daemon=True).start()

    def _stop(self):
        if self._process:
            process = self._process
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=2)
            for stream in (process.stdin, process.stdout):
                if stream:
                    stream.close()
            self._process = None

    def close(self):
        with self._lock:
            self._stop()

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.close()

    def command(self, command):
        if "\n" in command or "\r" in command or len(command.encode("utf-8")) > 65536:
            raise ValueError("Invalid or oversized command")
        with self._lock:
            self._start()
            try:
                self._process.stdin.write(command + "\n")
                self._process.stdin.flush()
                line = self._responses.get(timeout=self.timeout)
                if line is None:
                    raise EngineError("Unavailable", "C++ worker exited")
                result = json.loads(line)
                if "error" in result:
                    raise EngineError(result["error"]["code"], result["error"]["message"])
                return result
            except queue.Empty as error:
                self._stop()
                raise EngineError("DeadlineExceeded", "C++ worker request timed out") from error
            except (OSError, json.JSONDecodeError) as error:
                self._stop()
                raise EngineError("Unavailable", "C++ worker protocol failure") from error

    def health(self):
        return self.command("health")

    def update_traffic(self, directory):
        return self.command("update " + _text(str(Path(directory).resolve())))

    def route(self, *, vehicle_profile_id, preference, departure_time_unix_seconds,
              source_node_id=None, target_node_id=None, origin=None, destination=None,
              max_routes=3, max_extra_time_ratio=0.25, max_extra_time_seconds=600):
        if type(departure_time_unix_seconds) is not int or not 0 <= departure_time_unix_seconds < 2**63:
            raise ValueError("UTC Unix seconds must be a nonnegative int64")
        if type(max_routes) is not int or not 1 <= max_routes <= 3:
            raise ValueError("max_routes must be 1..3")
        if source_node_id is not None or target_node_id is not None:
            if origin is not None or destination is not None:
                raise ValueError("Choose node IDs or coordinate endpoints")
            fields = ["nodes", _id(source_node_id), _id(target_node_id)]
        else:
            if origin is None or destination is None or len(origin) != 2 or len(destination) != 2:
                raise ValueError("origin/destination must be [latitude,longitude]")
            fields = ["coordinates", *map(_number, origin), *map(_number, destination)]
        fields += [_text(vehicle_profile_id), _number(preference), str(departure_time_unix_seconds),
                   str(max_routes), _number(max_extra_time_ratio), _number(max_extra_time_seconds)]
        return self.command(" ".join(fields))