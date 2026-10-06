"""/api/v1/experiments — built-in (9 canonical) + custom experiments."""

from __future__ import annotations

import copy

from fastapi.testclient import TestClient

BUILTIN_IDS = [
    "deadlock-circular",
    "edf-deadlines",
    "memory-fragmentation",
    "priority-inheritance",
    "priority-inversion",
    "priority-starvation",
    "producer-consumer",
    "rate-monotonic",
    "rr-quantum-comparison",
]


def test_list_experiments_contains_all_builtins(client: TestClient) -> None:
    body = client.get("/api/v1/experiments").json()
    ids = [item["id"] for item in body["items"]]
    for exp_id in BUILTIN_IDS:
        assert exp_id in ids
    assert body["total"] >= 9
    builtin = next(item for item in body["items"] if item["id"] == "rate-monotonic")
    assert builtin["source"] == "builtin"
    assert builtin["scheduler"] == "rm"
    assert builtin["taskCount"] == 3


def test_get_experiment_returns_full_config(client: TestClient) -> None:
    body = client.get("/api/v1/experiments/edf-deadlines").json()
    assert body["config"]["scheduler"]["type"] == "edf"
    assert len(body["config"]["tasks"]) == 3
    assert body["source"] == "builtin"


def test_get_unknown_experiment_404(client: TestClient) -> None:
    resp = client.get("/api/v1/experiments/no-such-experiment")
    assert resp.status_code == 404
    assert "not found" in resp.json()["error"]


def test_run_experiment_creates_and_runs_simulation(client: TestClient) -> None:
    resp = client.post("/api/v1/experiments/rate-monotonic/run")
    assert resp.status_code == 200, resp.text
    rec = resp.json()
    assert rec["experimentId"] == "rate-monotonic"
    assert rec["status"] == "completed"
    assert rec["result"]["metrics"]["deadlineMisses"] == 0
    # the simulation is a first-class record
    fetched = client.get(f"/api/v1/simulations/{rec['id']}").json()
    assert fetched["name"].startswith("rate-monotonic")


def test_run_unknown_experiment_404(client: TestClient) -> None:
    resp = client.post("/api/v1/experiments/nope/run")
    assert resp.status_code == 404


def test_create_custom_experiment(client: TestClient, edf_config) -> None:
    payload = {
        "name": "my edf variant",
        "description": "custom copy",
        "config": copy.deepcopy(edf_config),
    }
    resp = client.post("/api/v1/experiments", json=payload)
    assert resp.status_code == 201, resp.text
    rec = resp.json()
    assert rec["source"] == "custom"
    assert len(rec["id"]) == 12
    assert rec["name"] == "my edf variant"

    listing = client.get("/api/v1/experiments").json()
    mine = next(item for item in listing["items"] if item["id"] == rec["id"])
    assert mine["source"] == "custom"

    fetched = client.get(f"/api/v1/experiments/{rec['id']}").json()
    assert fetched["config"]["scheduler"]["type"] == "edf"


def test_create_custom_experiment_invalid_422(client: TestClient) -> None:
    bad = {
        "name": "bad",
        "config": {
            "name": "bad",
            "duration": 10,
            "scheduler": {"type": "bogus"},
            "tasks": [{"id": "A", "steps": [{"op": "cpu", "d": 1}]}],
        },
    }
    resp = client.post("/api/v1/experiments", json=bad)
    assert resp.status_code == 422


def test_create_custom_experiment_engine_rejected_422(
    client: TestClient, memory_config
) -> None:
    # pydantic-clean but engine-rejected: `free` tag with no matching `alloc`.
    config = copy.deepcopy(memory_config)
    config["tasks"][0]["steps"][3]["tag"] = "ghost"
    resp = client.post("/api/v1/experiments", json={"config": config})
    assert resp.status_code == 422
    body = resp.json()
    assert body["error"] == "engine validation failed"
    assert any("free tag" in d for d in body["details"])


def test_validate_experiment_ok(client: TestClient) -> None:
    resp = client.post("/api/v1/experiments/producer-consumer/validate")
    assert resp.status_code == 200
    assert resp.json() == {"id": "producer-consumer", "valid": True, "details": []}


def test_validate_unknown_experiment_404(client: TestClient) -> None:
    resp = client.post("/api/v1/experiments/missing/validate")
    assert resp.status_code == 404


def test_run_custom_experiment(client: TestClient, edf_config) -> None:
    created = client.post(
        "/api/v1/experiments",
        json={"config": copy.deepcopy(edf_config)},
    ).json()
    resp = client.post(f"/api/v1/experiments/{created['id']}/run")
    assert resp.status_code == 200
    assert resp.json()["status"] == "completed"
    assert resp.json()["result"]["metrics"]["completedJobs"] == 24
