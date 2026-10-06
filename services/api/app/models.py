"""Pydantic models mirroring docs/spec/SIMULATION_SCHEMA.md §Experiment configuration.

Validation rules enforced here mirror the engine's `validate` command (both layers
must agree; the engine stays authoritative for run-time-only rules such as
`free`-tag matching, which is checked at run start).

Field-name note: the config key ``schema`` is exposed via the ``schema_`` attribute
with alias ``schema`` (``schema`` shadows a deprecated BaseModel method).
"""

from __future__ import annotations

import re
from enum import StrEnum
from typing import Any, Literal

from pydantic import BaseModel, ConfigDict, Field, model_validator

STEP_OPS = (
    "cpu",
    "io",
    "sleep",
    "lock",
    "unlock",
    "wait",
    "signal",
    "send",
    "recv",
    "evwait",
    "evset",
    "alloc",
    "free",
)
DUR_LIMIT = 100_000

# op -> required resource kind (empty = no resource semantics)
OP_RESOURCE_KIND: dict[str, str] = {
    "lock": "mutex",
    "unlock": "mutex",
    "wait": "sem",
    "signal": "sem",
    "send": "msgq",
    "recv": "msgq",
    "evwait": "evflags",
    "evset": "evflags",
}

_ID_RE = re.compile(r"^[A-Za-z0-9_-]{1,31}$")


class StrictModel(BaseModel):
    """Base model: unknown fields are rejected (mirrors strict engine parsing)."""

    model_config = ConfigDict(extra="forbid", populate_by_name=True)


class StepCfg(StrictModel):
    """One program step of a task job (schema §Program steps)."""

    op: str
    d: int | None = Field(default=None, ge=1, le=DUR_LIMIT)
    res: str | None = None
    dev: str | None = None
    msg: int | None = None
    mask: int | None = Field(default=None, ge=0)
    mode: Literal["any", "all"] | None = None
    size: int | None = Field(default=None, ge=1)
    tag: str | None = None

    @model_validator(mode="after")
    def _check_op_fields(self) -> StepCfg:
        if self.op not in STEP_OPS:
            raise ValueError(
                f"unknown op '{self.op}' (valid ops: {', '.join(STEP_OPS)})"
            )
        if self.op in ("cpu", "io", "sleep"):
            if self.d is None:
                raise ValueError(f"op '{self.op}' requires field 'd'")
        elif self.op in OP_RESOURCE_KIND:
            if not self.res:
                raise ValueError(f"op '{self.op}' requires field 'res'")
        elif self.op == "send":
            if not self.res:
                raise ValueError("op 'send' requires field 'res'")
            if self.msg is None:
                raise ValueError("op 'send' requires field 'msg'")
        elif self.op == "alloc":
            if self.size is None:
                raise ValueError("op 'alloc' requires field 'size'")
        elif self.op == "free":
            if not self.tag:
                raise ValueError("op 'free' requires field 'tag'")
        return self


class TaskCfg(StrictModel):
    """A task definition (schema §Task object)."""

    id: str = Field(pattern=r"^[A-Za-z0-9_-]{1,31}$")
    name: str | None = None
    kind: Literal["aperiodic", "periodic", "sporadic"] = "aperiodic"
    priority: int = Field(default=0, ge=-100, le=100)
    arrival: int = Field(default=0, ge=0)
    period: int | None = Field(default=None, ge=1)
    relativeDeadline: int | None = Field(default=None, ge=0)
    jitter: int | None = Field(default=None, ge=0)
    releases: list[int] | None = None
    steps: list[StepCfg] = Field(min_length=1, max_length=256)

    @model_validator(mode="after")
    def _check_kind_fields(self) -> TaskCfg:
        if self.kind != "periodic":
            if self.period is not None:
                raise ValueError("field 'period' is only valid for periodic tasks")
            if self.jitter is not None:
                raise ValueError("field 'jitter' is only valid for periodic tasks")
        if self.kind != "sporadic" and self.releases is not None:
            raise ValueError("field 'releases' is only valid for sporadic tasks")
        if self.kind == "periodic":
            if self.period is None:
                raise ValueError("periodic task requires 'period' >= 1")
            if self.jitter is not None and self.jitter >= self.period:
                raise ValueError(
                    f"jitter {self.jitter} must be < period {self.period}"
                )
        elif self.kind == "sporadic":
            if not self.releases:
                raise ValueError("sporadic task requires non-empty 'releases'")
            if any(r < 0 for r in self.releases):
                raise ValueError("sporadic release ticks must be >= 0")
            if self.releases != sorted(self.releases):
                raise ValueError("sporadic 'releases' must be sorted ascending")
        return self


class ResourceCfg(StrictModel):
    """A synchronization resource (schema §Resource object)."""

    id: str = Field(pattern=r"^[A-Za-z0-9_-]{1,31}$")
    type: Literal["mutex", "sem", "msgq", "evflags"]
    protocol: Literal["none", "inherit"] | None = None
    initial: int | None = Field(default=None, ge=0)
    max: int | None = Field(default=None, ge=1)
    capacity: int | None = Field(default=None, ge=1)

    @model_validator(mode="after")
    def _check_type_fields(self) -> ResourceCfg:
        if self.type == "mutex":
            if self.initial is not None or self.max is not None or self.capacity is not None:
                raise ValueError("mutex takes no 'initial'/'max'/'capacity' fields")
        elif self.type == "sem":
            if self.initial is None or self.max is None:
                raise ValueError("sem requires 'initial' and 'max'")
            if self.initial > self.max:
                raise ValueError(f"sem initial {self.initial} exceeds max {self.max}")
            if self.capacity is not None:
                raise ValueError("sem takes no 'capacity' field")
        elif self.type == "msgq":
            if self.capacity is None:
                raise ValueError("msgq requires 'capacity' >= 1")
            if self.initial is not None or self.max is not None:
                raise ValueError("msgq takes no 'initial'/'max' fields")
        return self


class MemoryCfg(StrictModel):
    """Memory model configuration (schema §Experiment configuration)."""

    model: Literal["region", "pool"]
    total: int | None = Field(default=None, ge=1)
    policy: Literal["first_fit", "best_fit", "worst_fit"] | None = None
    blockSize: int | None = Field(default=None, ge=1)
    blockCount: int | None = Field(default=None, ge=1)

    @model_validator(mode="after")
    def _check_model_fields(self) -> MemoryCfg:
        if self.model == "region":
            if self.total is None:
                raise ValueError("region memory requires 'total' >= 1")
            if self.blockSize is not None or self.blockCount is not None:
                raise ValueError("region memory takes no 'blockSize'/'blockCount'")
        else:
            if self.blockSize is None or self.blockCount is None:
                raise ValueError("pool memory requires 'blockSize' and 'blockCount'")
            if self.total is not None:
                raise ValueError(
                    "pool memory takes no 'total' (it is blockSize*blockCount)"
                )
            if self.policy is not None:
                raise ValueError(
                    "memory: field 'policy' only applies to the region model"
                )
        return self


class AgingCfg(StrictModel):
    """Priority aging configuration (effective for the priority* family)."""

    interval: int = Field(ge=1)
    cap: int = Field(ge=0)


class SchedulerCfg(StrictModel):
    """Scheduler selection (schema §Experiment configuration)."""

    type: Literal[
        "fifo", "rr", "priority", "priority_p", "sjf", "srtf", "rm", "edf"
    ]
    quantum: int | None = Field(default=None, ge=1, le=1000)

    @model_validator(mode="after")
    def _check_quantum(self) -> SchedulerCfg:
        if self.quantum is not None and self.type != "rr":
            raise ValueError("'quantum' is only valid for scheduler type 'rr'")
        return self


class ExperimentConfig(StrictModel):
    """The full experiment configuration document (micrort-config/1)."""

    schema_: Literal["micrort-config/1"] = Field(
        default="micrort-config/1", alias="schema"
    )
    name: str = Field(min_length=1, max_length=200)
    description: str | None = None
    seed: int = Field(default=0, ge=0)
    duration: int = Field(ge=1, le=DUR_LIMIT)
    cpus: int = Field(default=1, ge=1, le=1)
    contextSwitchCost: int = Field(default=0, ge=0, le=10)
    scheduler: SchedulerCfg
    aging: AgingCfg | None = None
    memory: MemoryCfg | None = None
    tasks: list[TaskCfg] = Field(min_length=1, max_length=64)
    resources: list[ResourceCfg] = Field(default_factory=list, max_length=32)

    @model_validator(mode="after")
    def _check_cross_refs(self) -> ExperimentConfig:
        task_ids = [t.id for t in self.tasks]
        if len(set(task_ids)) != len(task_ids):
            dupes = sorted({tid for tid in task_ids if task_ids.count(tid) > 1})
            raise ValueError(f"duplicate task ids: {', '.join(dupes)}")
        res_ids = [r.id for r in self.resources]
        if len(set(res_ids)) != len(res_ids):
            dupes = sorted({rid for rid in res_ids if res_ids.count(rid) > 1})
            raise ValueError(f"duplicate resource ids: {', '.join(dupes)}")
        kinds = {r.id: r.type for r in self.resources}
        for ti, task in enumerate(self.tasks):
            for si, step in enumerate(task.steps):
                if step.op in ("alloc", "free") and self.memory is None:
                    raise ValueError(
                        f"tasks[{ti}].steps[{si}]: op '{step.op}' requires a "
                        "'memory' block in the configuration"
                    )
                if step.res is None:
                    continue
                if step.res not in kinds:
                    raise ValueError(
                        f"tasks[{ti}].steps[{si}]: unknown resource '{step.res}'"
                    )
                want = OP_RESOURCE_KIND.get(step.op)
                if want and kinds[step.res] != want:
                    raise ValueError(
                        f"tasks[{ti}].steps[{si}]: op '{step.op}' requires a "
                        f"resource of kind '{want}' but '{step.res}' is a "
                        f"'{kinds[step.res]}'"
                    )
        return self


# ---------------------------------------------------------------------------
# Simulation lifecycle + API payloads
# ---------------------------------------------------------------------------


class SimulationStatus(StrEnum):
    """Simulation lifecycle status (persisted as its value string)."""

    CREATED = "created"
    RUNNING = "running"
    COMPLETED = "completed"
    FAILED = "failed"
    STOPPED = "stopped"


class SimulationCreate(StrictModel):
    """POST /api/v1/simulations body."""

    name: str | None = Field(default=None, max_length=200)
    config: ExperimentConfig


class ExperimentCreate(StrictModel):
    """POST /api/v1/experiments body (custom user experiments)."""

    name: str | None = Field(default=None, min_length=1, max_length=200)
    description: str | None = Field(default=None, max_length=2000)
    config: ExperimentConfig


class VariantSpec(StrictModel):
    """One comparison variant: config sections to deep-merge over the base.

    Merge semantics: dict values merge recursively, lists and scalars are
    replaced wholesale (documented in README.md).
    """

    label: str = Field(min_length=1, max_length=100)
    scheduler: dict[str, Any] | None = None
    memory: dict[str, Any] | None = None
    aging: dict[str, Any] | None = None


class ComparisonCreate(StrictModel):
    """POST /api/v1/comparisons body: base config OR experimentId + variants."""

    name: str | None = Field(default=None, max_length=200)
    config: ExperimentConfig | None = None
    experimentId: str | None = Field(default=None, max_length=100)
    variants: list[VariantSpec] = Field(min_length=1, max_length=16)

    @model_validator(mode="after")
    def _check_base(self) -> ComparisonCreate:
        if (self.config is None) == (self.experimentId is None):
            raise ValueError(
                "exactly one of 'config' or 'experimentId' must be provided"
            )
        return self
