"""EngineRunner against the REAL micrort-engine binary."""

from __future__ import annotations

import threading
import time
from pathlib import Path
from typing import Any

import pytest

from app.engine import ApiError, EngineRunner, SimulationStopped
from tests.conftest import ENGINE_BIN

# Heavy workload: ~500k trace events, ~3 s of engine wall time — big enough to
# be stopped mid-run by the registry test.
HEAVY_CONFIG: dict[str, Any] = {
    "schema": "micrort-config/1",
    "name": "heavy",
    "seed": 0,
    "duration": 100000,
    "scheduler": {"type": "rr", "quantum": 1},
    "tasks": [
        {
            "id": f"T{i:02d}",
            "kind": "periodic",
            "period": 20,
            "steps": [{"op": "cpu", "d": 1}, {"op": "io", "d": 5}],
        }
        for i in range(16)
    ],
}


def _strip_wall(doc: dict) -> dict:
    return {k: v for k, v in doc.items() if k != "wallMicros"}


def test_engine_binary_exists() -> None:
    assert ENGINE_BIN.is_file(), "engine binary must be built (scripts/build-native.sh)"


def test_validate_ok(engine_runner: EngineRunner, rm_config) -> None:
    verdict = engine_runner.validate_config(rm_config)
    assert verdict == {"valid": True, "details": []}


def test_validate_bad(engine_runner: EngineRunner, rm_config) -> None:
    bad = dict(rm_config)
    bad["tasks"] = [{"id": "A", "steps": [{"op": "warp", "d": 1}]}]
    verdict = engine_runner.validate_config(bad)
    assert verdict["valid"] is False
    assert any("unknown op" in d for d in verdict["details"])


def test_run_smoke_produces_result_document(
    engine_runner: EngineRunner, smoke_config
) -> None:
    result = engine_runner.run_config(smoke_config)
    assert result["schema"] == "micrort-result/1"
    assert result["config"]["name"] == "smoke"
    assert len(result["configHash"]) == 16
    for key in (
        "tasks",
        "metrics",
        "trace",
        "gantt",
        "resources",
        "deadlocks",
        "memory",
        "anomalies",
        "schedulerNotes",
        "simulatedUntil",
        "status",
    ):
        assert key in result
    assert result["metrics"]["totalEvents"] == len(result["trace"])
    assert result["metrics"]["completedJobs"] >= 1


def test_run_is_deterministic(engine_runner: EngineRunner, smoke_config) -> None:
    first = engine_runner.run_config(smoke_config)
    second = engine_runner.run_config(smoke_config)
    assert _strip_wall(first) == _strip_wall(second)


def test_run_validation_error_normalized(engine_runner: EngineRunner) -> None:
    bad = {
        "schema": "micrort-config/1",
        "name": "freebad",
        "duration": 20,
        "scheduler": {"type": "fifo"},
        "memory": {"model": "region", "total": 512, "policy": "first_fit"},
        "tasks": [
            {
                "id": "A",
                "steps": [
                    {"op": "alloc", "size": 32, "tag": "x"},
                    {"op": "free", "tag": "nope"},
                    {"op": "cpu", "d": 1},
                ],
            }
        ],
    }
    with pytest.raises(ApiError) as excinfo:
        engine_runner.run_config(bad)
    assert excinfo.value.status_code == 422
    assert any("free tag" in d for d in excinfo.value.details)


def test_stop_unknown_simulation(engine_runner: EngineRunner) -> None:
    assert engine_runner.stop("does-not-exist") is False


def test_stop_active_run(engine_runner: EngineRunner) -> None:
    outcome: dict[str, Any] = {}

    def target() -> None:
        try:
            outcome["result"] = engine_runner.run_config(HEAVY_CONFIG, sim_id="stopme")
        except SimulationStopped:
            outcome["stopped"] = True
        except ApiError as exc:  # pragma: no cover - unexpected failure path
            outcome["error"] = exc

    thread = threading.Thread(target=target)
    thread.start()
    deadline = time.monotonic() + 5.0
    while not engine_runner.is_running("stopme") and time.monotonic() < deadline:
        time.sleep(0.01)
    assert engine_runner.is_running("stopme"), "engine run should be observable"
    assert engine_runner.stop("stopme") is True
    thread.join(timeout=20)
    assert not thread.is_alive()
    assert outcome.get("stopped") is True, f"unexpected outcome: {outcome}"


def test_missing_engine_binary_returns_503(smoke_config) -> None:
    runner = EngineRunner(Path("/nonexistent/micrort-engine"))
    with pytest.raises(ApiError) as excinfo:
        runner.run_config(smoke_config)
    assert excinfo.value.status_code == 503
    assert "scripts/build-native.sh" in excinfo.value.message
    with pytest.raises(ApiError) as excinfo_validate:
        runner.validate_config(smoke_config)
    assert excinfo_validate.value.status_code == 503


def test_schema_info(engine_runner: EngineRunner) -> None:
    info = engine_runner.schema_info()
    assert info is not None
    assert info["config"] == "micrort-config/1"
    assert "edf" in info["schedulers"]
