"""/api/v1/experiments — canonical (files) + custom (DB) experiment management."""

from __future__ import annotations

import json
import logging
from pathlib import Path
from typing import Any

from fastapi import APIRouter, Request

from app.api import EngineDep, StoreDep
from app.api.routes_simulations import execute_simulation
from app.config import Settings
from app.engine import ApiError
from app.models import ExperimentCreate
from app.store import Store

logger = logging.getLogger("micrort.api.experiments")

router = APIRouter(prefix="/api/v1/experiments", tags=["experiments"])


def _get_settings(request: Request) -> Settings:
    return request.app.state.settings


def _builtin_dir(request: Request) -> Path:
    return Path(_get_settings(request).experiments_dir)


def _load_builtin(experiments_dir: Path) -> list[dict[str, Any]]:
    """Load experiments/*.json (the 9 canonical experiments) as records."""
    records: list[dict[str, Any]] = []
    if not experiments_dir.is_dir():
        logger.warning("experiments dir %s does not exist", experiments_dir)
        return records
    for path in sorted(experiments_dir.glob("*.json")):
        try:
            config = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as exc:
            logger.warning("skipping unreadable experiment file %s: %s", path, exc)
            continue
        if not isinstance(config, dict):
            continue
        scheduler = (config.get("scheduler") or {}).get("type")
        records.append(
            {
                "id": path.stem,
                "name": config.get("name", path.stem),
                "description": config.get("description"),
                "source": "builtin",
                "config": config,
                "scheduler": scheduler,
                "taskCount": len(config.get("tasks", [])),
                "duration": config.get("duration"),
            }
        )
    return records


def resolve_experiment(
    request: Request, store: Store, exp_id: str
) -> dict[str, Any] | None:
    """Look up an experiment by id: built-in file first, then custom DB entry."""
    for rec in _load_builtin(_builtin_dir(request)):
        if rec["id"] == exp_id:
            return rec
    return store.get_experiment(exp_id)


@router.get("")
def list_experiments(
    request: Request, store: StoreDep
) -> dict[str, Any]:
    items = _load_builtin(_builtin_dir(request)) + store.list_experiments()
    return {"items": items, "total": len(items)}


@router.get("/{exp_id}")
def get_experiment(
    exp_id: str, request: Request, store: StoreDep
) -> dict[str, Any]:
    rec = resolve_experiment(request, store, exp_id)
    if rec is None:
        raise ApiError(404, f"experiment '{exp_id}' not found")
    return rec


@router.post("", status_code=201)
def create_experiment(
    payload: ExperimentCreate,
    request: Request,
    store: StoreDep,
    engine: EngineDep,
) -> dict[str, Any]:
    """Store a custom experiment (pydantic- and engine-validated)."""
    config = payload.config.model_dump(by_alias=True, exclude_none=True)
    verdict = engine.validate_config(config)
    if not verdict["valid"]:
        raise ApiError(422, "engine validation failed", verdict["details"])
    name = payload.name or payload.config.name
    rec = store.create_experiment(name, payload.description, config)
    logger.info("stored custom experiment %s (%s)", rec["id"], name)
    return rec


@router.post("/{exp_id}/run")
def run_experiment(
    exp_id: str,
    request: Request,
    store: StoreDep,
    engine: EngineDep,
) -> dict[str, Any]:
    """Create a simulation from the experiment and run it immediately."""
    exp = resolve_experiment(request, store, exp_id)
    if exp is None:
        raise ApiError(404, f"experiment '{exp_id}' not found")
    sim = store.create_simulation(f"{exp['name']} (from {exp_id})", exp["config"])
    rec = execute_simulation(store, engine, sim["id"])
    rec = dict(rec)
    rec["experimentId"] = exp_id
    return rec


@router.post("/{exp_id}/validate")
def validate_experiment(
    exp_id: str,
    request: Request,
    store: StoreDep,
    engine: EngineDep,
) -> dict[str, Any]:
    exp = resolve_experiment(request, store, exp_id)
    if exp is None:
        raise ApiError(404, f"experiment '{exp_id}' not found")
    verdict = engine.validate_config(exp["config"])
    if not verdict["valid"]:
        raise ApiError(422, "engine validation failed", verdict["details"])
    return {"id": exp_id, "valid": True, "details": []}
