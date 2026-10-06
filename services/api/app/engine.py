"""EngineRunner — subprocess wrapper around the micrort-engine CLI.

Contract (docs/spec/SIMULATION_SCHEMA.md §Engine CLI):
    micrort-engine run      --config <file> --out <file>
    micrort-engine validate --config <file>
    micrort-engine schema

Exit codes: 0 = success, 2 = validation error (JSON on stderr), 3 = runtime
failure. The runner never blocks forever (60 s timeout, hard kill afterwards)
and normalizes every failure to :class:`ApiError`.
"""

from __future__ import annotations

import json
import logging
import subprocess
import tempfile
import threading
from pathlib import Path
from typing import Any

logger = logging.getLogger("micrort.engine")

ENGINE_TIMEOUT_S = 60.0
NOT_BUILT_MESSAGE = "engine binary not built — run scripts/build-native.sh"


class ApiError(Exception):
    """Normalized service error carrying an HTTP status code and details."""

    def __init__(self, status_code: int, message: str, details: list[str] | None = None):
        super().__init__(message)
        self.status_code = status_code
        self.message = message
        self.details = details or []


class SimulationStopped(Exception):
    """Raised by run_config when the run was terminated via EngineRunner.stop()."""


class _RunSlot:
    """Registry entry for one active engine subprocess (keyed by simulation id)."""

    __slots__ = ("proc", "stop_requested")

    def __init__(self) -> None:
        self.proc: subprocess.Popen[str] | None = None
        self.stop_requested = False


class EngineRunner:
    """Thread-safe wrapper: validate / run / stop / schema-info."""

    def __init__(self, engine_bin: Path, timeout: float = ENGINE_TIMEOUT_S) -> None:
        self._bin = Path(engine_bin)
        self._timeout = timeout
        self._registry: dict[str, _RunSlot] = {}
        self._lock = threading.Lock()
        self._schema_cache: dict[str, Any] | None = None

    # -- availability -------------------------------------------------------

    @property
    def binary(self) -> Path:
        return self._bin

    def available(self) -> bool:
        """True when the engine binary exists and is executable."""
        try:
            return self._bin.is_file() and self._bin.stat().st_mode & 0o111 != 0
        except OSError:
            return False

    def _require_available(self) -> None:
        if not self.available():
            raise ApiError(503, NOT_BUILT_MESSAGE)

    # -- schema info ---------------------------------------------------------

    def schema_info(self) -> dict[str, Any] | None:
        """Engine self-describing schema summary (cached for the process life)."""
        if self._schema_cache is not None:
            return self._schema_cache
        if not self.available():
            return None
        try:
            proc = subprocess.run(
                [str(self._bin), "schema"],
                capture_output=True,
                text=True,
                timeout=10,
            )
            if proc.returncode == 0:
                self._schema_cache = json.loads(proc.stdout)
                return self._schema_cache
            logger.warning("engine schema exit=%s stderr=%s", proc.returncode, proc.stderr)
        except (subprocess.SubprocessError, json.JSONDecodeError, OSError) as exc:
            logger.warning("engine schema probe failed: %s", exc)
        return None

    # -- validate ----------------------------------------------------------

    def validate_config(self, config: dict[str, Any]) -> dict[str, Any]:
        """Run `engine validate`; returns {"valid": bool, "details": [...]}."""
        self._require_available()
        with tempfile.TemporaryDirectory(prefix="micrort-validate-") as td:
            cfg_path = Path(td) / "config.json"
            cfg_path.write_text(json.dumps(config), encoding="utf-8")
            try:
                proc = subprocess.run(
                    [str(self._bin), "validate", "--config", str(cfg_path)],
                    capture_output=True,
                    text=True,
                    timeout=self._timeout,
                )
            except subprocess.TimeoutExpired as exc:
                raise ApiError(502, f"engine validate timed out after {self._timeout}s") from exc
            except OSError as exc:
                raise ApiError(502, f"engine validate failed to start: {exc}") from exc
        payload = self._parse_json(proc.stdout, "validate")
        if payload is None:
            raise ApiError(
                502,
                "engine validate returned non-JSON output",
                [proc.stderr.strip()[-500:]] if proc.stderr.strip() else [],
            )
        return {
            "valid": bool(payload.get("valid")),
            "details": list(payload.get("details", [])),
        }

    # -- run ----------------------------------------------------------------

    def run_config(
        self, config: dict[str, Any], sim_id: str | None = None
    ) -> dict[str, Any]:
        """Run the engine on a config; returns the parsed result document.

        Raises ApiError(422) on engine validation errors, ApiError(502) on
        runtime/unknown failures or timeouts, ApiError(503) when the binary is
        missing, and SimulationStopped when the run was terminated via stop().
        """
        self._require_available()
        slot: _RunSlot | None = None
        if sim_id is not None:
            slot = _RunSlot()
            with self._lock:
                self._registry[sim_id] = slot
        try:
            with tempfile.TemporaryDirectory(prefix="micrort-run-") as td:
                cfg_path = Path(td) / "config.json"
                out_path = Path(td) / "result.json"
                cfg_path.write_text(json.dumps(config), encoding="utf-8")
                try:
                    proc = subprocess.Popen(
                        [
                            str(self._bin),
                            "run",
                            "--config",
                            str(cfg_path),
                            "--out",
                            str(out_path),
                        ],
                        stdout=subprocess.PIPE,
                        stderr=subprocess.PIPE,
                        text=True,
                    )
                    if slot is not None:
                        with self._lock:
                            slot.proc = proc
                    try:
                        _, stderr = proc.communicate(timeout=self._timeout)
                    except subprocess.TimeoutExpired:
                        self._kill(proc)
                        raise ApiError(
                            502, f"engine run timed out after {self._timeout}s"
                        ) from None
                except OSError as exc:
                    raise ApiError(502, f"engine run failed to start: {exc}") from exc
                if slot is not None and slot.stop_requested:
                    raise SimulationStopped(sim_id)
                return self._interpret_run(proc.returncode, stderr, out_path)
        finally:
            if slot is not None:
                with self._lock:
                    self._registry.pop(sim_id, None)

    def _interpret_run(
        self, code: int, stderr: str, out_path: Path
    ) -> dict[str, Any]:
        if code == 0:
            result = self._parse_json_file(out_path)
            if result is None:
                raise ApiError(502, "engine run produced no parseable result document")
            return result
        if code == 2:
            err = self._parse_json(stderr, "run")
            details = list(err.get("details", [])) if err else []
            if not details and stderr.strip():
                details = [stderr.strip()[-500:]]
            raise ApiError(422, "engine validation failed", details)
        if code < 0:
            raise ApiError(502, f"engine run terminated by signal {-code}")
        raise ApiError(
            502,
            f"engine run failed with exit code {code}",
            [stderr.strip()[-500:]] if stderr.strip() else [],
        )

    # -- stop ---------------------------------------------------------------

    def stop(self, sim_id: str) -> bool:
        """Terminate the active engine run for sim_id. False when not running."""
        with self._lock:
            slot = self._registry.get(sim_id)
            if slot is None or slot.proc is None:
                return False
            slot.stop_requested = True
            proc = slot.proc
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self._kill(proc)
        logger.info("stopped engine run for simulation %s", sim_id)
        return True

    def is_running(self, sim_id: str) -> bool:
        with self._lock:
            slot = self._registry.get(sim_id)
            return slot is not None and slot.proc is not None

    # -- helpers -------------------------------------------------------------

    @staticmethod
    def _kill(proc: subprocess.Popen[str]) -> None:
        proc.kill()
        try:
            proc.communicate(timeout=5)
        except subprocess.TimeoutExpired:  # pragma: no cover - pathological
            pass

    @staticmethod
    def _parse_json(text: str, what: str) -> dict[str, Any] | None:
        try:
            payload = json.loads(text)
        except json.JSONDecodeError:
            logger.debug("engine %s output was not JSON: %.200s", what, text)
            return None
        return payload if isinstance(payload, dict) else None

    @staticmethod
    def _parse_json_file(path: Path) -> dict[str, Any] | None:
        try:
            text = path.read_text(encoding="utf-8")
        except OSError:
            return None
        try:
            payload = json.loads(text)
        except json.JSONDecodeError:
            return None
        return payload if isinstance(payload, dict) else None


__all__ = [
    "ApiError",
    "EngineRunner",
    "ENGINE_TIMEOUT_S",
    "NOT_BUILT_MESSAGE",
    "SimulationStopped",
]
