"""/api/v1/simulations — lifecycle, execution and analysis of single runs."""

from __future__ import annotations

import logging
from typing import Any, Literal

from fastapi import APIRouter, Query
from fastapi.responses import Response

from app.analysis import (
    fragmentation_summary,
    recompute_metrics,
    rt_analysis,
    starvation_report,
)
from app.api import EngineDep, StoreDep
from app.engine import ApiError, EngineRunner, SimulationStopped
from app.models import SimulationCreate
from app.reports import render_json_export, render_report, render_task_csv
from app.store import Store, utc_now_iso

logger = logging.getLogger("micrort.api.simulations")

router = APIRouter(prefix="/api/v1/simulations", tags=["simulations"])


def _require_sim(store: Store, sim_id: str) -> dict[str, Any]:
    rec = store.get_simulation(sim_id)
    if rec is None:
        raise ApiError(404, f"simulation '{sim_id}' not found")
    return rec


def _require_result(rec: dict[str, Any]) -> dict[str, Any]:
    result = rec.get("result")
    if not isinstance(result, dict):
        raise ApiError(
            409,
            f"simulation '{rec['id']}' has no result yet (status={rec['status']})",
        )
    return result


@router.post("", status_code=201)
def create_simulation(
    payload: SimulationCreate, store: StoreDep
) -> dict[str, Any]:
    """Create a simulation record (status=created). Runs nothing yet."""
    name = payload.name or payload.config.name
    config = payload.config.model_dump(by_alias=True, exclude_none=True)
    rec = store.create_simulation(name, config)
    logger.info("created simulation %s (%s)", rec["id"], name)
    return rec


@router.get("")
def list_simulations(
    *,
    limit: int = Query(default=50, ge=1, le=500),
    offset: int = Query(default=0, ge=0),
    store: StoreDep,
) -> dict[str, Any]:
    items, total = store.list_simulations(limit=limit, offset=offset)
    return {"items": items, "total": total, "limit": limit, "offset": offset}


@router.get("/{sim_id}")
def get_simulation(sim_id: str, store: StoreDep) -> dict[str, Any]:
    return _require_sim(store, sim_id)


@router.post("/{sim_id}/run")
def run_simulation(
    sim_id: str,
    store: StoreDep,
    engine: EngineDep,
) -> dict[str, Any]:
    """Run the engine synchronously (it is fast) and store the result."""
    return execute_simulation(store, engine, sim_id)


def execute_simulation(store: Store, engine: EngineRunner, sim_id: str) -> dict[str, Any]:
    """Shared execution path (also used by /experiments/{id}/run)."""
    rec = _require_sim(store, sim_id)
    if rec["status"] == "running":
        raise ApiError(409, f"simulation '{sim_id}' is already running")
    store.update_simulation(sim_id, status="running", started_at=utc_now_iso())
    config: dict[str, Any] = rec["config"]
    try:
        result = engine.run_config(config, sim_id=sim_id)
    except SimulationStopped:
        rec = store.update_simulation(
            sim_id,
            status="stopped",
            finished_at=utc_now_iso(),
            error={"message": "stopped by user"},
        )
        return rec  # type: ignore[return-value]
    except ApiError as exc:
        store.update_simulation(
            sim_id,
            status="failed",
            finished_at=utc_now_iso(),
            error={"statusCode": exc.status_code, "message": exc.message,
                   "details": exc.details},
        )
        raise
    rec = store.update_simulation(
        sim_id,
        status="completed",
        result=result,
        finished_at=utc_now_iso(),
        clear_error=True,
    )
    # Engine 'stopped' (horizon reached with pending work) is still a completed
    # API-level run; the engine's own status stays inside the result document.
    logger.info(
        "ran simulation %s -> %s (%s events)",
        sim_id,
        result.get("status"),
        (result.get("metrics") or {}).get("totalEvents"),
    )
    return rec


@router.post("/{sim_id}/stop")
def stop_simulation(
    sim_id: str,
    store: StoreDep,
    engine: EngineDep,
) -> dict[str, Any]:
    """Terminate the active engine subprocess for this simulation."""
    _require_sim(store, sim_id)
    if not engine.stop(sim_id):
        raise ApiError(409, f"simulation '{sim_id}' is not running")
    return {"id": sim_id, "status": "stopping", "stopped": True}


@router.delete("/{sim_id}")
def delete_simulation(sim_id: str, store: StoreDep) -> dict[str, Any]:
    if not store.delete_simulation(sim_id):
        raise ApiError(404, f"simulation '{sim_id}' not found")
    return {"id": sim_id, "deleted": True}


@router.get("/{sim_id}/trace")
def get_trace(
    sim_id: str,
    *,
    type: str | None = Query(default=None),
    task: str | None = Query(default=None),
    res: str | None = Query(default=None),
    limit: int = Query(default=100, ge=1, le=5000),
    offset: int = Query(default=0, ge=0),
    store: StoreDep,
) -> dict[str, Any]:
    rec = _require_sim(store, sim_id)
    result = _require_result(rec)
    events = sorted(
        result.get("trace") or [],
        key=lambda e: (e.get("t", 0), e.get("seq", 0)),
    )
    if type:
        wanted = type.upper()
        events = [e for e in events if (e.get("type") or "").upper() == wanted]
    if task:
        events = [e for e in events if e.get("task") == task]
    if res:
        events = [e for e in events if e.get("res") == res]
    total = len(events)
    return {
        "id": sim_id,
        "items": events[offset : offset + limit],
        "total": total,
        "limit": limit,
        "offset": offset,
        "unfilteredTotal": len(result.get("trace") or []),
    }


@router.get("/{sim_id}/metrics")
def get_metrics(sim_id: str, store: StoreDep) -> dict[str, Any]:
    rec = _require_sim(store, sim_id)
    result = _require_result(rec)
    cross = recompute_metrics(result)
    return {
        "id": sim_id,
        "status": rec["status"],
        "engine": result.get("metrics"),
        "recomputed": cross["recomputed"],
        "deltas": cross["deltas"],
        "match": cross["match"],
        "tolerance": cross["tolerance"],
    }


@router.get("/{sim_id}/tasks")
def get_tasks(sim_id: str, store: StoreDep) -> dict[str, Any]:
    rec = _require_sim(store, sim_id)
    result = _require_result(rec)
    return {"id": sim_id, "items": result.get("tasks") or []}


@router.get("/{sim_id}/gantt")
def get_gantt(sim_id: str, store: StoreDep) -> dict[str, Any]:
    rec = _require_sim(store, sim_id)
    result = _require_result(rec)
    return {"id": sim_id, "items": result.get("gantt") or []}


@router.get("/{sim_id}/memory")
def get_memory(sim_id: str, store: StoreDep) -> dict[str, Any]:
    rec = _require_sim(store, sim_id)
    result = _require_result(rec)
    return {"id": sim_id, "memory": result.get("memory") or {}}


@router.get("/{sim_id}/deadlocks")
def get_deadlocks(sim_id: str, store: StoreDep) -> dict[str, Any]:
    rec = _require_sim(store, sim_id)
    result = _require_result(rec)
    return {"id": sim_id, "items": result.get("deadlocks") or []}


@router.get("/{sim_id}/analysis")
def get_analysis(sim_id: str, store: StoreDep) -> dict[str, Any]:
    rec = _require_sim(store, sim_id)
    result = _require_result(rec)
    return {
        "id": sim_id,
        "rt": rt_analysis(rec["config"], result),
        "starvation": starvation_report(result),
        "fragmentation": fragmentation_summary(result),
    }


@router.get("/{sim_id}/report")
def get_report(
    sim_id: str,
    *,
    format: Literal["markdown", "json", "csv"] = Query(default="markdown"),
    store: StoreDep,
) -> Response:
    rec = _require_sim(store, sim_id)
    result = _require_result(rec)
    analysis = {
        "rt": rt_analysis(rec["config"], result),
        "starvation": starvation_report(result),
        "fragmentation": fragmentation_summary(result),
        "recompute": recompute_metrics(result),
    }
    if format == "markdown":
        content = render_report(result, rec["config"], analysis)
        media = "text/markdown; charset=utf-8"
    elif format == "csv":
        content = render_task_csv(result)
        media = "text/csv; charset=utf-8"
    else:
        content = render_json_export(result, rec["config"], analysis)
        media = "application/json"
    store.create_report("simulation", sim_id, format, content)
    return Response(content=content, media_type=media)
