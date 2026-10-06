"""Pydantic validation mirrors the engine's config validation (schema §Validation rules)."""

from __future__ import annotations

import copy

import pytest
from pydantic import ValidationError

from app.models import ExperimentConfig


def _mutate(base: dict, path: list, value) -> dict:
    cfg = copy.deepcopy(base)
    node = cfg
    for key in path[:-1]:
        node = node[key]
    node[path[-1]] = value
    return cfg


def _err_msg(exc: ValidationError) -> str:
    return "; ".join(f"{err['loc']}: {err['msg']}" for err in exc.errors())


def test_valid_full_config(rm_config) -> None:
    cfg = ExperimentConfig.model_validate(rm_config)
    assert cfg.scheduler.type == "rm"
    assert cfg.name == "rate-monotonic"
    assert len(cfg.tasks) == 3
    assert cfg.cpus == 1


def test_valid_config_dumps_with_schema_alias(smoke_config) -> None:
    cfg = ExperimentConfig.model_validate(smoke_config)
    dumped = cfg.model_dump(by_alias=True)
    assert dumped["schema"] == "micrort-config/1"
    assert dumped["scheduler"] == {"type": "rr", "quantum": 4}


def test_unknown_op_rejected(rm_config) -> None:
    cfg = _mutate(rm_config, ["tasks", 0, "steps", 0, "op"], "warp")
    with pytest.raises(ValidationError, match="unknown op"):
        ExperimentConfig.model_validate(cfg)


def test_cpu_step_requires_duration(rm_config) -> None:
    cfg = _mutate(rm_config, ["tasks", 0, "steps", 0, "d"], None)
    with pytest.raises(ValidationError):
        ExperimentConfig.model_validate(cfg)


def test_duplicate_task_ids_rejected(rm_config) -> None:
    cfg = _mutate(rm_config, ["tasks", 1, "id"], "FAST")
    with pytest.raises(ValidationError, match="duplicate task ids"):
        ExperimentConfig.model_validate(cfg)


def test_bad_rr_quantum_rejected(rm_config) -> None:
    cfg = dict(rm_config)
    cfg["scheduler"] = {"type": "rr", "quantum": 0}
    with pytest.raises(ValidationError):
        ExperimentConfig.model_validate(cfg)


def test_quantum_only_for_rr(rm_config) -> None:
    cfg = dict(rm_config)
    cfg["scheduler"] = {"type": "fifo", "quantum": 4}
    with pytest.raises(ValidationError, match="only valid for scheduler type 'rr'"):
        ExperimentConfig.model_validate(cfg)


def test_cpus_must_be_one(rm_config) -> None:
    cfg = _mutate(rm_config, ["cpus"], 2)
    with pytest.raises(ValidationError):
        ExperimentConfig.model_validate(cfg)


def test_duration_bounds(rm_config) -> None:
    for bad in (0, 100_001, -5):
        cfg = _mutate(rm_config, ["duration"], bad)
        with pytest.raises(ValidationError):
            ExperimentConfig.model_validate(cfg)


def test_periodic_task_requires_period(rm_config) -> None:
    cfg = _mutate(rm_config, ["tasks", 0, "period"], None)
    with pytest.raises(ValidationError, match="periodic task requires 'period'"):
        ExperimentConfig.model_validate(cfg)


def test_sporadic_requires_sorted_releases() -> None:
    base = {
        "name": "spor",
        "duration": 20,
        "scheduler": {"type": "fifo"},
        "tasks": [
            {
                "id": "S",
                "kind": "sporadic",
                "releases": [10, 3],
                "steps": [{"op": "cpu", "d": 1}],
            }
        ],
    }
    with pytest.raises(ValidationError, match="sorted"):
        ExperimentConfig.model_validate(base)
    base["tasks"][0]["releases"] = []
    with pytest.raises(ValidationError, match="non-empty 'releases'"):
        ExperimentConfig.model_validate(base)


def test_unknown_resource_reference_rejected(rm_config) -> None:
    cfg = _mutate(rm_config, ["tasks", 0, "steps", 0, "op"], "lock")
    cfg["tasks"][0]["steps"][0]["res"] = "NOPE"
    with pytest.raises(ValidationError, match="unknown resource 'NOPE'"):
        ExperimentConfig.model_validate(cfg)


def test_lock_requires_mutex_kind() -> None:
    cfg = {
        "name": "locksem",
        "duration": 20,
        "scheduler": {"type": "fifo"},
        "resources": [{"id": "S", "type": "sem", "initial": 0, "max": 2}],
        "tasks": [
            {"id": "A", "steps": [{"op": "lock", "res": "S"}, {"op": "cpu", "d": 1}]}
        ],
    }
    with pytest.raises(ValidationError, match="resource of kind 'mutex'"):
        ExperimentConfig.model_validate(cfg)


def test_alloc_requires_memory_block(rm_config) -> None:
    cfg = _mutate(
        rm_config, ["tasks", 0, "steps", 0], {"op": "alloc", "size": 32, "tag": "a0"}
    )
    with pytest.raises(ValidationError, match="'memory' block"):
        ExperimentConfig.model_validate(cfg)


def test_task_id_pattern(rm_config) -> None:
    cfg = _mutate(rm_config, ["tasks", 0, "id"], "bad id with spaces!")
    with pytest.raises(ValidationError):
        ExperimentConfig.model_validate(cfg)


def test_task_count_limit(rm_config) -> None:
    tasks = [
        {"id": f"T{i:03d}", "steps": [{"op": "cpu", "d": 1}]} for i in range(65)
    ]
    cfg = dict(rm_config)
    cfg["tasks"] = tasks
    with pytest.raises(ValidationError):
        ExperimentConfig.model_validate(cfg)


def test_sem_initial_cannot_exceed_max() -> None:
    cfg = {
        "name": "sem",
        "duration": 10,
        "scheduler": {"type": "fifo"},
        "resources": [{"id": "S", "type": "sem", "initial": 5, "max": 2}],
        "tasks": [{"id": "A", "steps": [{"op": "cpu", "d": 1}]}],
    }
    with pytest.raises(ValidationError, match="exceeds max"):
        ExperimentConfig.model_validate(cfg)


def test_memory_model_fields(memory_config) -> None:
    cfg = ExperimentConfig.model_validate(memory_config)
    assert cfg.memory is not None
    assert cfg.memory.model == "region"
    broken = _mutate(memory_config, ["memory", "total"], None)
    with pytest.raises(ValidationError):
        ExperimentConfig.model_validate(broken)


def test_unknown_fields_rejected(rm_config) -> None:
    cfg = dict(rm_config)
    cfg["taskschduler"] = "typo"
    with pytest.raises(ValidationError):
        ExperimentConfig.model_validate(cfg)


def test_context_switch_cost_bounds(rm_config) -> None:
    cfg = _mutate(rm_config, ["contextSwitchCost"], 11)
    with pytest.raises(ValidationError):
        ExperimentConfig.model_validate(cfg)


def test_jitter_only_valid_for_periodic(rm_config) -> None:
    cfg = _mutate(rm_config, ["tasks", 0, "kind"], "aperiodic")
    cfg["tasks"][0].pop("period")
    cfg["tasks"][0]["jitter"] = 2
    with pytest.raises(ValidationError, match="'jitter' is only valid"):
        ExperimentConfig.model_validate(cfg)


def test_period_only_valid_for_periodic(rm_config) -> None:
    cfg = _mutate(rm_config, ["tasks", 0, "kind"], "aperiodic")
    with pytest.raises(ValidationError, match="'period' is only valid"):
        ExperimentConfig.model_validate(cfg)


def test_releases_only_valid_for_sporadic(rm_config) -> None:
    cfg = _mutate(rm_config, ["tasks", 0, "releases"], [1, 2])
    with pytest.raises(ValidationError, match="'releases' is only valid"):
        ExperimentConfig.model_validate(cfg)


def test_memory_policy_only_for_region(memory_config) -> None:
    cfg = _mutate(
        memory_config,
        ["memory"],
        {"model": "pool", "blockSize": 64, "blockCount": 8, "policy": "first_fit"},
    )
    with pytest.raises(ValidationError, match="'policy' only applies"):
        ExperimentConfig.model_validate(cfg)


def test_serialization_drops_engine_invalid_defaults(rm_config) -> None:
    """exclude_none keeps pydantic dumps inside the engine's accepted grammar."""
    cfg = {
        "name": "ser",
        "duration": 30,
        "scheduler": {"type": "fifo"},
        "tasks": [{"id": "A", "steps": [{"op": "cpu", "d": 1}]}],
    }
    validated = ExperimentConfig.model_validate(cfg)
    dumped = validated.model_dump(by_alias=True, exclude_none=True)
    assert "jitter" not in dumped["tasks"][0]
    assert "period" not in dumped["tasks"][0]
    assert "releases" not in dumped["tasks"][0]
    assert "quantum" not in dumped["scheduler"]
