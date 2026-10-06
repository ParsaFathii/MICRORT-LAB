"""/api/v1/comparisons — multi-variant runs over one base config.

Merge semantics (documented in README.md): each variant's `scheduler`,
`memory` and `aging` objects are **deep-merged** over the base experiment
config — nested dicts merge recursively, lists and scalars are replaced
wholesale. The merged config is re-validated (pydantic) before each engine
run, so determinism is preserved: identical base + identical overrides ⇒
identical result document (modulo wallMicros).
"""

from __future__ import annotations

import logging
from typing import Any

from fastapi import APIRouter, Query, Request
from fastapi.responses import Response
from pydantic import ValidationError

from app.analysis import COMPARISON_METRICS, variant_metrics
from app.api import EngineDep, StoreDep
from app.api.routes_experiments import resolve_experiment
from app.engine import ApiError, EngineRunner
from app.models import ComparisonCreate, ExperimentConfig
from app.reports import render_comparison_report
from app.store import Store

logger = logging.getLogger("micrort.api.comparisons")

router = APIRouter(prefix="/api/v1/comparisons", tags=["comparisons"])


def deep_merge(base: dict[str, Any], override: dict[str, Any]) -> dict[str, Any]:
    """Recursive dict merge: dicts merge, lists and scalars are replaced."""
    out = dict(base)
    for key, value in override.items():
        if isinstance(value, dict) and isinstance(out.get(key), dict):
            out[key] = deep_merge(out[key], value)
        else:
            out[key] = value
    return out


def _require_comparison(store: Store, cmp_id: str) -> dict[str, Any]:
    rec = store.get_comparison(cmp_id)
    if rec is None:
        raise ApiError(404, f"comparison '{cmp_id}' not found")
    return rec


def _run_variant(
    engine: EngineRunner, base: dict[str, Any], overrides: dict[str, Any]
) -> dict[str, Any]:
    merged = deep_merge(base, overrides)
    try:
        ExperimentConfig.model_validate(merged)
    except ValidationError as exc:
        details = [
            f"{'.'.join(str(p) for p in err['loc'])}: {err['msg']}" for err in exc.errors()
        ]
        raise ApiError(422, "merged variant config is invalid", details) from exc
    result = engine.run_config(merged)
    starved = [t.get("id") for t in result.get("tasks", []) if t.get("starved")]
    return {
        "metrics": variant_metrics(result),
        "configHash": result.get("configHash"),
        "status": result.get("status"),
        "scheduler": (merged.get("scheduler") or {}).get("type"),
        "starvedTaskIds": starved,
        "aging": bool(merged.get("aging")),
    }


def _best_per_metric(variants: list[dict[str, Any]]) -> dict[str, dict[str, Any]]:
    best: dict[str, dict[str, Any]] = {}
    for metric, direction in COMPARISON_METRICS.items():
        candidates = [
            (v["label"], v["metrics"].get(metric))
            for v in variants
            if v["metrics"].get(metric) is not None
        ]
        if not candidates:
            continue
        pick = min(candidates, key=lambda c: c[1]) if direction == "lower" else max(
            candidates, key=lambda c: c[1]
        )
        best[metric] = {"label": pick[0], "value": pick[1], "direction": direction}
    return best


def _deltas_vs_first(variants: list[dict[str, Any]]) -> list[dict[str, Any]]:
    if not variants:
        return []
    first = variants[0]["metrics"]
    out = []
    for v in variants:
        out.append(
            {
                "label": v["label"],
                "metrics": {
                    k: (
                        None
                        if v["metrics"].get(k) is None or first.get(k) is None
                        else round(v["metrics"][k] - first[k], 4)
                    )
                    for k in COMPARISON_METRICS
                },
            }
        )
    return out


@router.post("", status_code=201)
def create_comparison(
    payload: ComparisonCreate,
    request: Request,
    store: StoreDep,
    engine: EngineDep,
) -> dict[str, Any]:
    """Run every variant over the base config and store the comparison."""
    if payload.config is not None:
        base = payload.config.model_dump(by_alias=True, exclude_none=True)
        source = "inline"
    else:
        exp = resolve_experiment(request, store, payload.experimentId or "")
        if exp is None:
            raise ApiError(404, f"experiment '{payload.experimentId}' not found")
        base = exp["config"]
        source = f"experiment:{payload.experimentId}"

    run_variants: list[dict[str, Any]] = []
    for spec in payload.variants:
        overrides: dict[str, Any] = {}
        for section in ("scheduler", "memory", "aging"):
            value = getattr(spec, section)
            if value is not None:
                overrides[section] = value
        entry = _run_variant(engine, base, overrides)
        entry["label"] = spec.label
        run_variants.append(entry)

    results = {
        "variants": run_variants,
        "bestPerMetric": _best_per_metric(run_variants),
        "deltasVsFirst": _deltas_vs_first(run_variants),
    }
    stored_variants = [
        {"label": spec.label, **{k: v for k, v in {
            "scheduler": spec.scheduler, "memory": spec.memory, "aging": spec.aging
        }.items() if v is not None}}
        for spec in payload.variants
    ]
    rec = dict(store.create_comparison(
        payload.name or f"comparison of {base.get('name', source)}",
        base,
        stored_variants,
        results,
    ))
    rec["baseSource"] = source
    rec["variantCount"] = len(run_variants)
    logger.info(
        "created comparison %s (%s variants over %s)",
        rec["id"], len(run_variants), source,
    )
    return rec


@router.get("")
def list_comparisons(
    *,
    limit: int = Query(default=50, ge=1, le=500),
    offset: int = Query(default=0, ge=0),
    store: StoreDep,
) -> dict[str, Any]:
    items, total = store.list_comparisons(limit=limit, offset=offset)
    return {"items": items, "total": total, "limit": limit, "offset": offset}


@router.get("/{cmp_id}")
def get_comparison(cmp_id: str, store: StoreDep) -> dict[str, Any]:
    return _require_comparison(store, cmp_id)


@router.get("/{cmp_id}/report")
def comparison_report(
    cmp_id: str, store: StoreDep
) -> Response:
    rec = _require_comparison(store, cmp_id)
    content = render_comparison_report(rec)
    store.create_report("comparison", cmp_id, "markdown", content)
    return Response(content=content, media_type="text/markdown; charset=utf-8")
