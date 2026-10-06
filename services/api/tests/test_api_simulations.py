"""/api/v1/simulations full lifecycle through the real engine."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any

import pytest
from fastapi.testclient import TestClient

from app.config import Settings
from app.main import create_app
from tests.conftest import EXPERIMENTS_DIR

BASE = "/api/v1/simulations"

FREE_TAG_BAD: dict[str, Any] = {
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

DEADLOCK_CONFIG = json.loads(
    (EXPERIMENTS_DIR / "deadlock-circular.json").read_text(encoding="utf-8")
)


def _create(client: TestClient, config: dict, name: str | None = None) -> dict:
    resp = client.post(BASE, json={"name": name, "config": config})
    assert resp.status_code == 201, resp.text
    return resp.json()


def _run(client: TestClient, sim_id: str) -> dict:
    resp = client.post(f"{BASE}/{sim_id}/run")
    assert resp.status_code == 200, resp.text
    return resp.json()


# -- create / read ------------------------------------------------------------


def test_create_simulation(client: TestClient, rm_config) -> None:
    rec = _create(client, rm_config, name="my rm run")
    assert rec["status"] == "created"
    assert len(rec["id"]) == 12
    assert rec["createdAt"].endswith("Z")
    assert rec["name"] == "my rm run"
    assert rec["scheduler"] == "rm"
    assert rec["taskCount"] == 3
    assert rec["result"] is None


def test_create_defaults_name_from_config(client: TestClient, rm_config) -> None:
    rec = _create(client, rm_config)
    assert rec["name"] == "rate-monotonic"


def test_create_invalid_config_returns_422_with_field_path(client: TestClient) -> None:
    bad = {
        "name": "bad",
        "duration": 10,
        "scheduler": {"type": "fifo"},
        "tasks": [{"id": "A", "steps": [{"op": "warp", "d": 1}]}],
    }
    resp = client.post(BASE, json={"config": bad})
    assert resp.status_code == 422
    body = resp.json()
    locs = [list(err["loc"]) for err in body["detail"]]
    # field path points into the config: body -> config -> tasks[0] -> steps[0]
    assert any(loc[-1] == 0 and loc[-2] == "steps" for loc in locs), locs


def test_get_unknown_simulation_404(client: TestClient) -> None:
    resp = client.get(f"{BASE}/nope0000000")
    assert resp.status_code == 404
    assert "not found" in resp.json()["error"]


def test_list_simulations_pagination(client: TestClient, rm_config) -> None:
    ids = [_create(client, rm_config)["id"] for _ in range(3)]
    body = client.get(f"{BASE}?limit=2&offset=0").json()
    assert body["total"] == 3
    assert len(body["items"]) == 2
    assert body["limit"] == 2 and body["offset"] == 0
    assert all(item["id"] in ids for item in body["items"])
    tail = client.get(f"{BASE}?limit=2&offset=2").json()
    assert len(tail["items"]) == 1


# -- run -------------------------------------------------------------------------


def test_run_lifecycle_completed(client: TestClient, rm_config) -> None:
    sim = _create(client, rm_config)
    rec = _run(client, sim["id"])
    assert rec["status"] == "completed"
    assert rec["startedAt"] and rec["finishedAt"]
    result = rec["result"]
    assert result["schema"] == "micrort-result/1"
    assert result["status"] == "completed"
    assert len(result["configHash"]) == 16
    assert result["metrics"]["deadlineMisses"] == 0
    assert rec["scheduler"] == "rm"

    fetched = client.get(f"{BASE}/{sim['id']}").json()
    assert fetched["status"] == "completed"
    assert fetched["simulatedUntil"] == result["simulatedUntil"]
    assert fetched["configHash"] == result["configHash"]


def test_run_engine_stopped_status_maps_to_completed(
    client: TestClient, pc_config
) -> None:
    sim = _create(client, pc_config)
    rec = _run(client, sim["id"])
    assert rec["result"]["status"] == "stopped"  # horizon reached, work pending
    assert rec["status"] == "completed"  # API-level run finished successfully


def test_run_unknown_id_404(client: TestClient) -> None:
    resp = client.post(f"{BASE}/missing00000/run")
    assert resp.status_code == 404


def test_run_engine_validation_error_422(client: TestClient) -> None:
    sim = _create(client, FREE_TAG_BAD)
    resp = client.post(f"{BASE}/{sim['id']}/run")
    assert resp.status_code == 422
    body = resp.json()
    assert body["error"] == "engine validation failed"
    assert any("free tag" in d for d in body["details"])
    rec = client.get(f"{BASE}/{sim['id']}").json()
    assert rec["status"] == "failed"
    assert rec["finishedAt"]
    assert rec["error"]["statusCode"] == 422


def test_run_without_engine_binary_503(tmp_path: Path, rm_config) -> None:
    settings = Settings(
        engine_bin=tmp_path / "missing-engine",
        data_dir=tmp_path / "data",
        experiments_dir=EXPERIMENTS_DIR,
    )
    with TestClient(create_app(settings)) as local_client:
        sim = _create(local_client, rm_config)
        resp = local_client.post(f"{BASE}/{sim['id']}/run")
        assert resp.status_code == 503
        assert "engine binary not built" in resp.json()["error"]
        assert "scripts/build-native.sh" in resp.json()["error"]
        health = local_client.get("/api/v1/health").json()
        assert health["status"] == "degraded"
        assert health["engine"]["available"] is False


def test_stop_not_running_409(client: TestClient, rm_config) -> None:
    sim = _create(client, rm_config)
    resp = client.post(f"{BASE}/{sim['id']}/stop")
    assert resp.status_code == 409
    assert "not running" in resp.json()["error"]


# -- data endpoints ----------------------------------------------------------------


def test_trace_filters_and_pagination(client: TestClient, pc_config) -> None:
    sim = _create(client, pc_config)
    _run(client, sim["id"])
    base = f"{BASE}/{sim['id']}/trace"

    all_events = client.get(f"{base}?limit=5000").json()
    assert all_events["total"] == all_events["unfilteredTotal"] > 100

    sends = client.get(f"{base}?type=dispatch").json()  # case-insensitive
    assert sends["total"] == 72
    assert all(e["type"] == "DISPATCH" for e in sends["items"])

    msg_sends = client.get(f"{base}?type=MSG_SEND&limit=10&offset=5").json()
    assert msg_sends["total"] == 25

    res_events = client.get(f"{base}?res=BUFFER").json()
    assert res_events["total"] == 61
    assert all(e["res"] == "BUFFER" for e in res_events["items"])

    task_events = client.get(f"{base}?task=Consumer1").json()
    assert task_events["total"] > 0
    assert all(e["task"] == "Consumer1" for e in task_events["items"])

    page = msg_sends
    assert len(page["items"]) == 10
    assert page["offset"] == 5
    ts = [(e["t"], e["seq"]) for e in page["items"]]
    assert ts == sorted(ts)


def test_trace_before_run_409(client: TestClient, rm_config) -> None:
    sim = _create(client, rm_config)
    resp = client.get(f"{BASE}/{sim['id']}/trace")
    assert resp.status_code == 409
    assert "no result" in resp.json()["error"]


def test_metrics_endpoint(client: TestClient, rm_config) -> None:
    sim = _create(client, rm_config)
    _run(client, sim["id"])
    body = client.get(f"{BASE}/{sim['id']}/metrics").json()
    assert body["id"] == sim["id"]
    assert body["engine"]["avgWaiting"] == pytest.approx(2.143, abs=1e-3)
    assert body["recomputed"]["avgWaiting"] is not None
    assert body["match"] is True
    assert set(body["deltas"]) >= {"avgWaiting", "avgTurnaround", "avgResponse"}


def test_tasks_gantt_memory(client: TestClient, rm_config) -> None:
    sim = _create(client, rm_config)
    _run(client, sim["id"])
    tasks = client.get(f"{BASE}/{sim['id']}/tasks").json()["items"]
    assert [t["id"] for t in tasks] == ["FAST", "MEDIUM", "SLOW"]
    gantt = client.get(f"{BASE}/{sim['id']}/gantt").json()["items"]
    assert gantt
    assert all({"t0", "t1", "task", "state"} <= set(g) for g in gantt)
    memory = client.get(f"{BASE}/{sim['id']}/memory").json()["memory"]
    assert memory["model"] == "none"


def test_deadlocks_endpoint_reports_cycle(client: TestClient) -> None:
    sim = _create(client, DEADLOCK_CONFIG)
    _run(client, sim["id"])
    deadlocks = client.get(f"{BASE}/{sim['id']}/deadlocks").json()["items"]
    assert len(deadlocks) == 1
    assert set(deadlocks[0]["tasks"]) == {"A", "B"}
    assert set(deadlocks[0]["resources"]) == {"M1", "M2"}


def test_analysis_endpoint(client: TestClient, rm_config) -> None:
    sim = _create(client, rm_config)
    _run(client, sim["id"])
    body = client.get(f"{BASE}/{sim['id']}/analysis").json()
    assert body["rt"]["theoretical"]["totalUtilization"] == pytest.approx(0.5625)
    assert body["rt"]["theoretical"]["liuLaylandBound"] == pytest.approx(0.7798)
    assert body["rt"]["measured"]["deadlineMisses"] == 0
    assert body["starvation"]["starvedCount"] == 0
    assert body["fragmentation"]["enabled"] is False


# -- reports ------------------------------------------------------------------------


def test_report_markdown(client: TestClient, rm_config) -> None:
    sim = _create(client, rm_config)
    _run(client, sim["id"])
    resp = client.get(f"{BASE}/{sim['id']}/report?format=markdown")
    assert resp.status_code == 200
    assert resp.headers["content-type"].startswith("text/markdown")
    text = resp.text
    assert text.startswith("# Simulation Report — rate-monotonic")
    assert "## Per-task metrics" in text
    assert "## Real-time analysis" in text
    assert "Liu & Layland" in text
    assert "0.5625" in text


def test_report_csv(client: TestClient, rm_config) -> None:
    sim = _create(client, rm_config)
    _run(client, sim["id"])
    resp = client.get(f"{BASE}/{sim['id']}/report?format=csv")
    assert resp.status_code == 200
    assert resp.headers["content-type"].startswith("text/csv")
    assert resp.text.splitlines()[0].startswith("taskId,")
    assert "FAST" in resp.text


def test_report_json_export(client: TestClient, rm_config) -> None:
    sim = _create(client, rm_config)
    _run(client, sim["id"])
    resp = client.get(f"{BASE}/{sim['id']}/report?format=json")
    assert resp.status_code == 200
    assert resp.headers["content-type"].startswith("application/json")
    body = resp.json()
    stored = client.get(f"{BASE}/{sim['id']}").json()
    assert body["result"]["configHash"] == stored["result"]["configHash"]
    assert "theoretical" in body["analysis"]["rt"]


def test_report_unknown_format_422(client: TestClient, rm_config) -> None:
    sim = _create(client, rm_config)
    _run(client, sim["id"])
    resp = client.get(f"{BASE}/{sim['id']}/report?format=xml")
    assert resp.status_code == 422


def test_reports_endpoint_lists_stored_reports(client: TestClient, rm_config) -> None:
    sim = _create(client, rm_config)
    _run(client, sim["id"])
    client.get(f"{BASE}/{sim['id']}/report?format=markdown")
    client.get(f"{BASE}/{sim['id']}/report?format=csv")
    body = client.get(f"/api/v1/reports?simulationId={sim['id']}").json()
    assert body["total"] == 2
    assert {item["format"] for item in body["items"]} == {"markdown", "csv"}
    one = body["items"][0]
    fetched = client.get(f"/api/v1/reports/{one['id']}").json()
    assert fetched["content"]
    assert fetched["refId"] == sim["id"]


def test_report_format_mismatch_400(client: TestClient, rm_config) -> None:
    sim = _create(client, rm_config)
    _run(client, sim["id"])
    client.get(f"{BASE}/{sim['id']}/report?format=markdown")
    listing = client.get(f"/api/v1/reports?simulationId={sim['id']}").json()
    report_id = listing["items"][0]["id"]
    resp = client.get(f"/api/v1/reports/{report_id}?format=csv")
    assert resp.status_code == 400


# -- delete ---------------------------------------------------------------------------


def test_delete_simulation(client: TestClient, rm_config) -> None:
    sim = _create(client, rm_config)
    assert client.delete(f"{BASE}/{sim['id']}").status_code == 200
    assert client.get(f"{BASE}/{sim['id']}").status_code == 404
    assert client.delete(f"{BASE}/{sim['id']}").status_code == 404
