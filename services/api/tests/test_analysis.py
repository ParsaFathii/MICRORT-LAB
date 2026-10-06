"""Analysis layer: metric recomputation (cross-check), RT theory, starvation, fragmentation."""

from __future__ import annotations

import pytest

from app.analysis import (
    fragmentation_summary,
    recompute_metrics,
    rm_utilization_bound,
    rt_analysis,
    starvation_report,
    variant_metrics,
)
from app.engine import EngineRunner
from tests.conftest import ENGINE_BIN


@pytest.fixture(scope="module")
def engine() -> EngineRunner:
    return EngineRunner(ENGINE_BIN)


@pytest.fixture(scope="module")
def rm_result(engine: EngineRunner, rm_config):
    return engine.run_config(rm_config)


@pytest.fixture(scope="module")
def edf_result(engine: EngineRunner, edf_config):
    return engine.run_config(edf_config)


@pytest.fixture(scope="module")
def ps_result(engine: EngineRunner, starvation_config):
    return engine.run_config(starvation_config)


@pytest.fixture(scope="module")
def mem_result(engine: EngineRunner, memory_config):
    return engine.run_config(memory_config)


@pytest.fixture(scope="module")
def pc_result(engine: EngineRunner, pc_config):
    return engine.run_config(pc_config)


# -- recompute_metrics cross-check -----------------------------------------


def test_recompute_matches_engine_rm(rm_result) -> None:
    cross = recompute_metrics(rm_result)
    assert cross["match"] is True
    assert cross["engine"]["avgWaiting"] == pytest.approx(2.143, abs=1e-3)
    for key in ("avgWaiting", "avgTurnaround", "avgResponse", "cpuUtilization", "throughput"):
        assert cross["deltas"][key] is not None
        assert abs(cross["deltas"][key]) <= 1e-3
    assert cross["recomputed"]["completedJobs"] == cross["engine"]["completedJobs"]


def test_recompute_matches_engine_edf(edf_result) -> None:
    cross = recompute_metrics(edf_result)
    assert cross["match"] is True


def test_recompute_matches_engine_starvation(ps_result) -> None:
    # Exercises the unfinished-job semantics (HOT3's last job never completes).
    cross = recompute_metrics(ps_result)
    assert cross["match"] is True
    assert cross["recomputed"]["releasedJobs"] == 64
    assert cross["recomputed"]["completedJobs"] == 63


def test_recompute_matches_engine_producer_consumer(pc_result) -> None:
    # Run reaches the horizon with work pending (engine status "stopped").
    cross = recompute_metrics(pc_result)
    assert cross["match"] is True


# -- Liu & Layland bounds ----------------------------------------------------


def test_rm_utilization_bound_n3() -> None:
    assert rm_utilization_bound(3) == pytest.approx(0.7798, abs=1e-4)


def test_rm_utilization_bound_n1() -> None:
    assert rm_utilization_bound(1) == pytest.approx(1.0, abs=1e-9)


def test_rm_utilization_bound_monotone() -> None:
    assert rm_utilization_bound(2) > rm_utilization_bound(3) > rm_utilization_bound(10)


def test_rm_utilization_bound_rejects_bad_n() -> None:
    with pytest.raises(ValueError):
        rm_utilization_bound(0)


# -- rt_analysis --------------------------------------------------------------


def test_rt_analysis_rate_monotonic(rm_result, rm_config) -> None:
    rt = rt_analysis(rm_config, rm_result)
    theory = rt["theoretical"]
    assert theory["totalUtilization"] == pytest.approx(0.5625, abs=1e-4)
    assert theory["periodicTaskCount"] == 3
    assert theory["liuLaylandBound"] == pytest.approx(0.7798, abs=1e-4)
    assert theory["liuLaylandSchedulable"] is True
    assert theory["hyperbolicBound"]["product"] == pytest.approx(1.6562, abs=1e-3)
    assert theory["hyperbolicBound"]["schedulable"] is True
    # measured block: what actually happened
    assert rt["measured"]["deadlineMisses"] == 0
    assert rt["measured"]["completionRate"] == 1.0
    assert rt["measured"]["starvedTasks"] == []
    assert "schedulable" in rt["verdict"]


def test_rt_analysis_separates_theory_and_measurement(rm_result, rm_config) -> None:
    rt = rt_analysis(rm_config, rm_result)
    assert "deadlineMisses" not in rt["theoretical"]
    assert "liuLaylandBound" not in rt["measured"]


def test_rt_analysis_eds_deadlines_all_met(edf_result, edf_config) -> None:
    rt = rt_analysis(edf_config, edf_result)
    assert rt["measured"]["deadlineMisses"] == 0
    assert rt["theoretical"]["totalUtilization"] > 0.8  # dense but schedulable


# -- starvation ----------------------------------------------------------------


def test_starvation_report_detects_background_task(ps_result) -> None:
    report = starvation_report(ps_result)
    starved_ids = [t["id"] for t in report["starvedTasks"]]
    assert starved_ids == ["BG"]
    bg = report["starvedTasks"][0]
    assert bg["readyWaitTotal"] == 145
    assert bg["engineStarved"] is True and bg["recomputedStarved"] is True
    # The largest READY residence overall belongs to HOT3 (168) — starvation is
    # about wait-vs-CPU progress, not raw wait (see heuristic).
    assert report["maxReadyWait"] == {"task": "HOT3", "readyWaitTotal": 168}


def test_starvation_threshold_is_configurable(ps_result) -> None:
    report = starvation_report(ps_result, threshold=200)
    assert report["starvedCount"] == 0
    assert report["threshold"] == 200


def test_starvation_report_no_starvation(rm_result) -> None:
    report = starvation_report(rm_result)
    assert report["starvedCount"] == 0


# -- fragmentation ---------------------------------------------------------------


def test_fragmentation_summary_memory_experiment(mem_result) -> None:
    frag = fragmentation_summary(mem_result)
    assert frag["enabled"] is True
    assert frag["model"] == "region"
    assert frag["policy"] == "first_fit"
    assert frag["allocFailures"] >= 1
    assert frag["failureCount"] >= 1
    assert frag["fragmentationMax"] > 0
    assert frag["eventCount"] > 0
    assert frag["finalFree"]["fragmentation"] >= 0


def test_fragmentation_summary_disabled(rm_result) -> None:
    assert fragmentation_summary(rm_result) == {"enabled": False}


# -- comparison metric extraction ---------------------------------------------------


def test_variant_metrics_keys(edf_result) -> None:
    vm = variant_metrics(edf_result)
    assert vm["deadlineMisses"] == 0
    assert vm["completedJobs"] == 24
    assert vm["cpuUtilization"] == pytest.approx(0.923, abs=1e-3)
