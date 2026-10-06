"""/api/v1/health — service + engine availability (cached, never hot-probes)."""

from __future__ import annotations

import logging
import time
from typing import Any

from fastapi import APIRouter, Request

from app.api import StoreDep

logger = logging.getLogger("micrort.api.health")

router = APIRouter(prefix="/api/v1/health", tags=["health"])

CACHE_TTL_S = 30.0


@router.get("")
def health_status(request: Request, store: StoreDep) -> dict[str, Any]:
    """Report service status, engine availability (30 s cache) and row counts.

    The engine binary is *not* probed on every request: the availability result
    (and the `schema` subprocess output) are cached for 30 seconds.
    """
    engine = request.app.state.engine
    cache: dict[str, Any] = request.app.state.health_cache
    now = time.monotonic()
    entry = cache.get("engine")
    if entry is None or now - entry["checkedAtMonotonic"] > CACHE_TTL_S:
        entry = {
            "checkedAtMonotonic": now,
            "available": engine.available(),
            "info": engine.schema_info() if engine.available() else None,
        }
        cache["engine"] = entry
    counts = store.counts()
    return {
        "status": "ok" if entry["available"] else "degraded",
        "engine": {
            "available": entry["available"],
            "binary": str(engine.binary),
            "info": entry["info"],
            "cachedForSeconds": CACHE_TTL_S,
        },
        "counts": counts,
    }
