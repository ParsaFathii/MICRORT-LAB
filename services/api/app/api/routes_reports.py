"""/api/v1/reports — stored report access."""

from __future__ import annotations

import logging
from typing import Any, Literal

from fastapi import APIRouter, Query

from app.api import StoreDep
from app.engine import ApiError

logger = logging.getLogger("micrort.api.reports")

router = APIRouter(prefix="/api/v1/reports", tags=["reports"])


@router.get("")
def list_reports(
    *,
    simulationId: str | None = Query(default=None),
    comparisonId: str | None = Query(default=None),
    limit: int = Query(default=50, ge=1, le=500),
    offset: int = Query(default=0, ge=0),
    store: StoreDep,
) -> dict[str, Any]:
    if simulationId and comparisonId:
        raise ApiError(422, "pass only one of 'simulationId' or 'comparisonId'")
    ref_id = simulationId or comparisonId
    items, total = store.list_reports(ref_id=ref_id, limit=limit, offset=offset)
    return {"items": items, "total": total, "limit": limit, "offset": offset}


@router.get("/{report_id}")
def get_report(
    report_id: str,
    *,
    format: Literal["markdown", "json", "csv"] | None = Query(default=None),
    store: StoreDep,
) -> dict[str, Any]:
    rec = store.get_report(report_id)
    if rec is None:
        raise ApiError(404, f"report '{report_id}' not found")
    if format is not None and format != rec["format"]:
        raise ApiError(
            400,
            f"report '{report_id}' was generated as '{rec['format']}' "
            f"(regenerate from the source endpoint for '{format}')",
        )
    return rec
