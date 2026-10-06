"""SQLite persistence for simulations, custom experiments, comparisons, reports.

Thread-safety: one shared connection opened with check_same_thread=False; every
operation is serialized by a re-entrant lock. The schema is bootstrapped on
first use and the data directory is created automatically.
"""

from __future__ import annotations

import json
import sqlite3
import threading
from datetime import UTC, datetime
from pathlib import Path
from typing import Any
from uuid import uuid4

_SCHEMA = """
CREATE TABLE IF NOT EXISTS simulations (
    id          TEXT PRIMARY KEY,
    name        TEXT,
    status      TEXT NOT NULL,
    config      TEXT NOT NULL,
    result      TEXT,
    error       TEXT,
    created_at  TEXT NOT NULL,
    started_at  TEXT,
    finished_at TEXT
);
CREATE TABLE IF NOT EXISTS experiments (
    id          TEXT PRIMARY KEY,
    name        TEXT NOT NULL,
    description TEXT,
    source      TEXT NOT NULL,
    config      TEXT NOT NULL,
    created_at  TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS comparisons (
    id          TEXT PRIMARY KEY,
    name        TEXT,
    base_config TEXT NOT NULL,
    variants    TEXT NOT NULL,
    results     TEXT,
    created_at  TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS reports (
    id          TEXT PRIMARY KEY,
    kind        TEXT NOT NULL,
    ref_id      TEXT NOT NULL,
    format      TEXT NOT NULL,
    content     TEXT NOT NULL,
    created_at  TEXT NOT NULL
);
"""


def new_id() -> str:
    """Short uuid4-hex identifier (12 chars) used across all entities."""
    return uuid4().hex[:12]


def utc_now_iso() -> str:
    """ISO 8601 UTC timestamp with millisecond precision and Z suffix."""
    return (
        datetime.now(UTC).isoformat(timespec="milliseconds").replace(
            "+00:00", "Z"
        )
    )


def _loads(raw: str | None) -> Any:
    if raw is None:
        return None
    try:
        return json.loads(raw)
    except json.JSONDecodeError:
        return raw


def _dumps(value: Any) -> str:
    return json.dumps(value, separators=(",", ":"))


class Store:
    """SQLite-backed store; every method is thread-safe."""

    def __init__(self, data_dir: Path) -> None:
        self._dir = Path(data_dir)
        self._dir.mkdir(parents=True, exist_ok=True)
        self._db_path = self._dir / "micrort.sqlite3"
        self._lock = threading.RLock()
        self._conn = sqlite3.connect(str(self._db_path), check_same_thread=False)
        self._conn.row_factory = sqlite3.Row
        self._conn.execute("PRAGMA journal_mode=WAL")
        with self._lock:
            self._conn.executescript(_SCHEMA)
            self._conn.commit()

    # ------------------------------------------------------------------
    # simulations
    # ------------------------------------------------------------------

    def create_simulation(self, name: str | None, config: dict[str, Any]) -> dict[str, Any]:
        rec = {
            "id": new_id(),
            "name": name,
            "status": "created",
            "config": config,
            "result": None,
            "error": None,
            "createdAt": utc_now_iso(),
            "startedAt": None,
            "finishedAt": None,
        }
        with self._lock:
            self._conn.execute(
                "INSERT INTO simulations (id, name, status, config, result, error,"
                " created_at, started_at, finished_at) VALUES (?,?,?,?,?,?,?,?,?)",
                (
                    rec["id"],
                    rec["name"],
                    rec["status"],
                    _dumps(config),
                    None,
                    None,
                    rec["createdAt"],
                    None,
                    None,
                ),
            )
            self._conn.commit()
        return self._with_summary_fields(rec)

    def get_simulation(self, sim_id: str) -> dict[str, Any] | None:
        with self._lock:
            row = self._conn.execute(
                "SELECT * FROM simulations WHERE id = ?", (sim_id,)
            ).fetchone()
        return self._sim_record(row) if row else None

    def list_simulations(self, limit: int, offset: int) -> tuple[list[dict[str, Any]], int]:
        with self._lock:
            total = self._conn.execute("SELECT COUNT(*) FROM simulations").fetchone()[0]
            rows = self._conn.execute(
                "SELECT * FROM simulations ORDER BY created_at DESC, id LIMIT ? OFFSET ?",
                (limit, offset),
            ).fetchall()
        return [self._sim_summary(r) for r in rows], total

    def update_simulation(
        self,
        sim_id: str,
        *,
        status: str | None = None,
        result: dict[str, Any] | None = None,
        error: Any | None = None,
        started_at: str | None = None,
        finished_at: str | None = None,
        clear_error: bool = False,
    ) -> dict[str, Any] | None:
        with self._lock:
            row = self._conn.execute(
                "SELECT * FROM simulations WHERE id = ?", (sim_id,)
            ).fetchone()
            if not row:
                return None
            rec = self._sim_record(row)
            if status is not None:
                rec["status"] = status
            if result is not None:
                rec["result"] = result
            if error is not None:
                rec["error"] = error
            elif clear_error:
                rec["error"] = None
            if started_at is not None:
                rec["startedAt"] = started_at
            if finished_at is not None:
                rec["finishedAt"] = finished_at
            self._conn.execute(
                "UPDATE simulations SET status=?, result=?, error=?, started_at=?,"
                " finished_at=? WHERE id=?",
                (
                    rec["status"],
                    _dumps(rec["result"]) if rec["result"] is not None else None,
                    _dumps(rec["error"]) if rec["error"] is not None else None,
                    rec["startedAt"],
                    rec["finishedAt"],
                    sim_id,
                ),
            )
            self._conn.commit()
        return Store._with_summary_fields(rec)

    def delete_simulation(self, sim_id: str) -> bool:
        with self._lock:
            cur = self._conn.execute("DELETE FROM simulations WHERE id = ?", (sim_id,))
            self._conn.commit()
            deleted = cur.rowcount > 0
            if deleted:
                self._conn.execute("DELETE FROM reports WHERE kind='simulation' AND ref_id=?",
                                   (sim_id,))
                self._conn.commit()
        return deleted

    @staticmethod
    def _sim_record(row: sqlite3.Row) -> dict[str, Any]:
        result = _loads(row["result"])
        rec: dict[str, Any] = {
            "id": row["id"],
            "name": row["name"],
            "status": row["status"],
            "config": _loads(row["config"]),
            "result": result,
            "error": _loads(row["error"]),
            "createdAt": row["created_at"],
            "startedAt": row["started_at"],
            "finishedAt": row["finished_at"],
        }
        return Store._with_summary_fields(rec)

    @staticmethod
    def _sim_summary(row: sqlite3.Row) -> dict[str, Any]:
        rec = {
            "id": row["id"],
            "name": row["name"],
            "status": row["status"],
            "createdAt": row["created_at"],
            "startedAt": row["started_at"],
            "finishedAt": row["finished_at"],
        }
        config = _loads(row["config"]) or {}
        rec["scheduler"] = (config.get("scheduler") or {}).get("type")
        rec["taskCount"] = len(config.get("tasks", []))
        rec["duration"] = config.get("duration")
        result = _loads(row["result"])
        if isinstance(result, dict):
            metrics = result.get("metrics", {}) or {}
            rec["resultStatus"] = result.get("status")
            rec["completedJobs"] = metrics.get("completedJobs")
            rec["deadlineMisses"] = metrics.get("deadlineMisses")
            rec["totalEvents"] = metrics.get("totalEvents")
        return rec

    @staticmethod
    def _with_summary_fields(rec: dict[str, Any]) -> dict[str, Any]:
        config = rec.get("config") or {}
        rec["scheduler"] = (config.get("scheduler") or {}).get("type")
        rec["taskCount"] = len(config.get("tasks", []))
        rec["duration"] = config.get("duration")
        result = rec.get("result")
        if isinstance(result, dict):
            rec["configHash"] = result.get("configHash")
            rec["simulatedUntil"] = result.get("simulatedUntil")
        return rec

    # ------------------------------------------------------------------
    # experiments (custom, user-authored; built-ins live on disk)
    # ------------------------------------------------------------------

    def create_experiment(
        self, name: str, description: str | None, config: dict[str, Any]
    ) -> dict[str, Any]:
        rec = {
            "id": new_id(),
            "name": name,
            "description": description,
            "source": "custom",
            "config": config,
            "createdAt": utc_now_iso(),
        }
        with self._lock:
            self._conn.execute(
                "INSERT INTO experiments (id, name, description, source, config,"
                " created_at) VALUES (?,?,?,?,?,?)",
                (
                    rec["id"],
                    name,
                    description,
                    "custom",
                    _dumps(config),
                    rec["createdAt"],
                ),
            )
            self._conn.commit()
        return rec

    def get_experiment(self, exp_id: str) -> dict[str, Any] | None:
        with self._lock:
            row = self._conn.execute(
                "SELECT * FROM experiments WHERE id = ?", (exp_id,)
            ).fetchone()
        if not row:
            return None
        return {
            "id": row["id"],
            "name": row["name"],
            "description": row["description"],
            "source": "custom",
            "config": _loads(row["config"]),
            "createdAt": row["created_at"],
        }

    def list_experiments(self) -> list[dict[str, Any]]:
        with self._lock:
            rows = self._conn.execute(
                "SELECT * FROM experiments ORDER BY created_at DESC"
            ).fetchall()
        return [
            {
                "id": r["id"],
                "name": r["name"],
                "description": r["description"],
                "source": "custom",
                "config": _loads(r["config"]),
                "createdAt": r["created_at"],
            }
            for r in rows
        ]

    # ------------------------------------------------------------------
    # comparisons
    # ------------------------------------------------------------------

    def create_comparison(
        self,
        name: str | None,
        base_config: dict[str, Any],
        variants: list[dict[str, Any]],
        results: dict[str, Any],
    ) -> dict[str, Any]:
        rec = {
            "id": new_id(),
            "name": name,
            "baseConfig": base_config,
            "variants": variants,
            "results": results,
            "createdAt": utc_now_iso(),
        }
        with self._lock:
            self._conn.execute(
                "INSERT INTO comparisons (id, name, base_config, variants, results,"
                " created_at) VALUES (?,?,?,?,?,?)",
                (
                    rec["id"],
                    rec["name"],
                    _dumps(base_config),
                    _dumps(variants),
                    _dumps(results),
                    rec["createdAt"],
                ),
            )
            self._conn.commit()
        return rec

    def get_comparison(self, cmp_id: str) -> dict[str, Any] | None:
        with self._lock:
            row = self._conn.execute(
                "SELECT * FROM comparisons WHERE id = ?", (cmp_id,)
            ).fetchone()
        if not row:
            return None
        return {
            "id": row["id"],
            "name": row["name"],
            "baseConfig": _loads(row["base_config"]),
            "variants": _loads(row["variants"]),
            "results": _loads(row["results"]),
            "createdAt": row["created_at"],
        }

    def list_comparisons(self, limit: int, offset: int) -> tuple[list[dict[str, Any]], int]:
        with self._lock:
            total = self._conn.execute("SELECT COUNT(*) FROM comparisons").fetchone()[0]
            rows = self._conn.execute(
                "SELECT * FROM comparisons ORDER BY created_at DESC, id LIMIT ? OFFSET ?",
                (limit, offset),
            ).fetchall()
        return [self._cmp_summary(r) for r in rows], total

    @staticmethod
    def _cmp_summary(row: sqlite3.Row) -> dict[str, Any]:
        results = _loads(row["results"]) or {}
        variants = results.get("variants", []) or []
        return {
            "id": row["id"],
            "name": row["name"],
            "createdAt": row["created_at"],
            "variantCount": len(_loads(row["variants"]) or []),
            "labels": [v.get("label") for v in variants],
            "bestPerMetric": results.get("bestPerMetric", {}),
        }

    # ------------------------------------------------------------------
    # reports
    # ------------------------------------------------------------------

    def create_report(
        self, kind: str, ref_id: str, fmt: str, content: str
    ) -> dict[str, Any]:
        rec = {
            "id": new_id(),
            "kind": kind,
            "refId": ref_id,
            "format": fmt,
            "content": content,
            "createdAt": utc_now_iso(),
        }
        with self._lock:
            self._conn.execute(
                "INSERT INTO reports (id, kind, ref_id, format, content, created_at)"
                " VALUES (?,?,?,?,?,?)",
                (rec["id"], kind, ref_id, fmt, content, rec["createdAt"]),
            )
            self._conn.commit()
        return rec

    def get_report(self, report_id: str) -> dict[str, Any] | None:
        with self._lock:
            row = self._conn.execute(
                "SELECT * FROM reports WHERE id = ?", (report_id,)
            ).fetchone()
        if not row:
            return None
        return {
            "id": row["id"],
            "kind": row["kind"],
            "refId": row["ref_id"],
            "format": row["format"],
            "content": row["content"],
            "createdAt": row["created_at"],
        }

    def list_reports(
        self, ref_id: str | None, limit: int, offset: int
    ) -> tuple[list[dict[str, Any]], int]:
        where, params = "", []
        if ref_id:
            where = "WHERE ref_id = ?"
            params = [ref_id]
        with self._lock:
            total = self._conn.execute(
                f"SELECT COUNT(*) FROM reports {where}", params
            ).fetchone()[0]
            rows = self._conn.execute(
                f"SELECT * FROM reports {where} ORDER BY created_at DESC, id"
                " LIMIT ? OFFSET ?",
                [*params, limit, offset],
            ).fetchall()
        return (
            [
                {
                    "id": r["id"],
                    "kind": r["kind"],
                    "refId": r["ref_id"],
                    "format": r["format"],
                    "size": len(r["content"]),
                    "createdAt": r["created_at"],
                }
                for r in rows
            ],
            total,
        )

    # ------------------------------------------------------------------

    def counts(self) -> dict[str, int]:
        with self._lock:
            return {
                "simulations": self._conn.execute(
                    "SELECT COUNT(*) FROM simulations"
                ).fetchone()[0],
                "experiments": self._conn.execute(
                    "SELECT COUNT(*) FROM experiments"
                ).fetchone()[0],
                "comparisons": self._conn.execute(
                    "SELECT COUNT(*) FROM comparisons"
                ).fetchone()[0],
                "reports": self._conn.execute(
                    "SELECT COUNT(*) FROM reports"
                ).fetchone()[0],
            }

    def close(self) -> None:
        with self._lock:
            self._conn.close()


__all__ = ["Store", "new_id", "utc_now_iso"]
