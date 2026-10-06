"""Shared fixtures: temp data dir, per-test app/client, sample configs, engine runner."""

from __future__ import annotations

import json
import os
from pathlib import Path
from typing import Any

import pytest
from fastapi.testclient import TestClient

from app.config import BASE_DIR, Settings
from app.engine import EngineRunner
from app.main import create_app


def _resolve_engine_bin() -> Path:
    """CI and developers may point MICRORT_ENGINE_BIN at any build location."""
    override = os.environ.get("MICRORT_ENGINE_BIN")
    if override:
        return Path(override).expanduser().resolve()
    return BASE_DIR.parent.parent / "engine" / "build" / "engine" / "micrort-engine"


ENGINE_BIN = _resolve_engine_bin()
EXPERIMENTS_DIR = BASE_DIR.parent.parent / "experiments"


def load_experiment(name: str) -> dict[str, Any]:
    return json.loads((EXPERIMENTS_DIR / f"{name}.json").read_text(encoding="utf-8"))


@pytest.fixture()
def settings(tmp_path: Path) -> Settings:
    return Settings(
        engine_bin=ENGINE_BIN,
        data_dir=tmp_path / "data",
        experiments_dir=EXPERIMENTS_DIR,
    )


@pytest.fixture()
def app(settings: Settings):
    return create_app(settings)


@pytest.fixture()
def client(app) -> TestClient:
    with TestClient(app) as test_client:
        yield test_client


@pytest.fixture()
def engine_runner() -> EngineRunner:
    return EngineRunner(ENGINE_BIN)


@pytest.fixture(scope="module")
def rm_config() -> dict[str, Any]:
    return load_experiment("rate-monotonic")


@pytest.fixture(scope="module")
def edf_config() -> dict[str, Any]:
    return load_experiment("edf-deadlines")


@pytest.fixture(scope="module")
def starvation_config() -> dict[str, Any]:
    return load_experiment("priority-starvation")


@pytest.fixture(scope="module")
def memory_config() -> dict[str, Any]:
    return load_experiment("memory-fragmentation")


@pytest.fixture(scope="module")
def pc_config() -> dict[str, Any]:
    return load_experiment("producer-consumer")


@pytest.fixture(scope="module")
def smoke_config() -> dict[str, Any]:
    return {
        "schema": "micrort-config/1",
        "name": "smoke",
        "description": "tiny two-task smoke run",
        "seed": 0,
        "duration": 40,
        "scheduler": {"type": "rr", "quantum": 4},
        "tasks": [
            {
                "id": "A",
                "kind": "aperiodic",
                "priority": 5,
                "arrival": 0,
                "steps": [{"op": "cpu", "d": 3}],
            },
            {
                "id": "B",
                "kind": "periodic",
                "arrival": 2,
                "period": 10,
                "relativeDeadline": 8,
                "steps": [{"op": "cpu", "d": 2}],
            },
        ],
    }
