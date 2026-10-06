"""/api/v1/comparisons — variant sweeps over one base config."""

from __future__ import annotations

import copy

from fastapi.testclient import TestClient

BASE = "/api/v1/comparisons"


def test_edf_vs_priority_p_on_same_workload(client: TestClient) -> None:
    payload = {
        "experimentId": "edf-deadlines",
        "variants": [
            {"label": "edf"},
            {"label": "priority_p", "scheduler": {"type": "priority_p"}},
        ],
    }
    resp = client.post(BASE, json=payload)
    assert resp.status_code == 201, resp.text
    rec = resp.json()
    assert len(rec["id"]) == 12
    assert rec["variantCount"] == 2
    assert rec["baseConfig"]["name"] == "edf-deadlines"

    variants = rec["results"]["variants"]
    assert [v["label"] for v in variants] == ["edf", "priority_p"]
    edf, prio = variants
    assert edf["scheduler"] == "edf" and prio["scheduler"] == "priority_p"
    assert edf["metrics"]["deadlineMisses"] == 0
    assert prio["metrics"]["deadlineMisses"] == 4  # SENSING misses under fixed prio
    assert edf["metrics"]["completedJobs"] == 24
    assert edf["metrics"]["cpuUtilization"] == 0.923

    best = rec["results"]["bestPerMetric"]
    assert best["deadlineMisses"]["label"] == "edf"
    assert best["avgTurnaround"]["label"] == "priority_p"  # 8.8 beats 9.333
    assert best["throughput"]["direction"] == "higher"

    deltas = rec["results"]["deltasVsFirst"]
    assert [d["label"] for d in deltas] == ["edf", "priority_p"]
    assert deltas[0]["metrics"]["deadlineMisses"] == 0
    assert deltas[1]["metrics"]["deadlineMisses"] == 4
    assert deltas[1]["metrics"]["completedJobs"] == -4

    fetched = client.get(f"{BASE}/{rec['id']}").json()
    assert fetched["results"]["variants"][0]["label"] == "edf"


def test_aging_rescues_starved_background_task(client: TestClient) -> None:
    payload = {
        "experimentId": "priority-starvation",
        "variants": [
            {"label": "no-aging"},
            {"label": "aging-rescue", "aging": {"interval": 4, "cap": 22}},
        ],
    }
    resp = client.post(BASE, json=payload)
    assert resp.status_code == 201, resp.text
    variants = resp.json()["results"]["variants"]
    baseline, rescued = variants
    assert baseline["starvedTaskIds"] == ["BG"]
    assert baseline["aging"] is False
    assert rescued["aging"] is True
    assert rescued["starvedTaskIds"] == []  # BG no longer starved
    assert rescued["metrics"]["avgWaiting"] < baseline["metrics"]["avgWaiting"]
    assert rescued["metrics"]["deadlineMisses"] == 0


def test_rr_quantum_partial_override_deep_merge(client: TestClient) -> None:
    payload = {
        "experimentId": "rr-quantum-comparison",
        "variants": [
            {"label": "q2", "scheduler": {"quantum": 2}},
            {"label": "q16", "scheduler": {"quantum": 16}},
        ],
    }
    resp = client.post(BASE, json=payload)
    assert resp.status_code == 201, resp.text
    rec = resp.json()
    variants = rec["results"]["variants"]
    assert [v["scheduler"] for v in variants] == ["rr", "rr"]  # type inherited
    metrics = [v["metrics"] for v in variants]
    assert metrics[0]["ctxSwitches"] != metrics[1]["ctxSwitches"]
    assert rec["results"]["bestPerMetric"]["avgWaiting"]["value"] in {
        metrics[0]["avgWaiting"],
        metrics[1]["avgWaiting"],
    }


def test_comparison_report_markdown(client: TestClient) -> None:
    create = {
        "experimentId": "edf-deadlines",
        "variants": [
            {"label": "edf"},
            {"label": "priority_p", "scheduler": {"type": "priority_p"}},
        ],
    }
    rec = client.post(BASE, json=create).json()
    resp = client.get(f"{BASE}/{rec['id']}/report")
    assert resp.status_code == 200
    assert resp.headers["content-type"].startswith("text/markdown")
    text = resp.text
    assert text.startswith("# Comparison Report")
    assert "## Per-variant metrics" in text
    assert "## Best per metric" in text
    assert "## Delta vs first variant" in text
    assert "edf" in text and "priority_p" in text


def test_list_comparisons(client: TestClient) -> None:
    listing = client.get(BASE).json()
    assert listing["total"] == 0
    client.post(
        BASE,
        json={
            "experimentId": "rate-monotonic",
            "variants": [{"label": "rm"}],
        },
    )
    listing = client.get(BASE).json()
    assert listing["total"] == 1
    assert listing["items"][0]["labels"] == ["rm"]


def test_invalid_variant_override_422(client: TestClient) -> None:
    payload = {
        "experimentId": "rr-quantum-comparison",
        "variants": [{"label": "bad", "scheduler": {"quantum": 0}}],
    }
    resp = client.post(BASE, json=payload)
    assert resp.status_code == 422
    body = resp.json()
    assert body["error"] == "merged variant config is invalid"
    assert any("quantum" in d for d in body["details"])


def test_base_config_and_experiment_id_mutually_exclusive(client: TestClient, rm_config) -> None:
    payload = {
        "config": copy.deepcopy(rm_config),
        "experimentId": "rate-monotonic",
        "variants": [{"label": "x"}],
    }
    resp = client.post(BASE, json=payload)
    assert resp.status_code == 422
    payload = {"variants": [{"label": "x"}]}
    assert client.post(BASE, json=payload).status_code == 422


def test_unknown_base_experiment_404(client: TestClient) -> None:
    payload = {"experimentId": "ghost", "variants": [{"label": "x"}]}
    resp = client.post(BASE, json=payload)
    assert resp.status_code == 404


def test_inline_base_config_comparison(client: TestClient, rm_config) -> None:
    payload = {
        "name": "inline rm vs fifo",
        "config": copy.deepcopy(rm_config),
        "variants": [
            {"label": "rm"},
            {"label": "fifo", "scheduler": {"type": "fifo"}},
        ],
    }
    resp = client.post(BASE, json=payload)
    assert resp.status_code == 201, resp.text
    rec = resp.json()
    assert rec["baseSource"] == "inline"
    assert rec["name"] == "inline rm vs fifo"
    schedulers = [v["scheduler"] for v in rec["results"]["variants"]]
    assert schedulers == ["rm", "fifo"]


def test_get_unknown_comparison_404(client: TestClient) -> None:
    resp = client.get(f"{BASE}/missing00000")
    assert resp.status_code == 404
