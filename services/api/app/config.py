"""Service settings (env-overridable, path defaults resolved relative to services/api)."""

from __future__ import annotations

import os
from dataclasses import dataclass, field
from pathlib import Path

# services/api — engine/experiments defaults are siblings of this directory.
BASE_DIR = Path(__file__).resolve().parent.parent


def _resolve(raw: str | None, default: Path) -> Path:
    path = Path(raw) if raw else default
    return path if path.is_absolute() else (BASE_DIR / path).resolve()


@dataclass(frozen=True)
class Settings:
    """Runtime settings; every path may be overridden via environment variables."""

    engine_bin: Path = field(
        default_factory=lambda: _resolve(
            None, Path("../../engine/build/engine/micrort-engine")
        )
    )
    data_dir: Path = field(default_factory=lambda: _resolve(None, Path("./data")))
    experiments_dir: Path = field(
        default_factory=lambda: _resolve(None, Path("../../experiments"))
    )

    @classmethod
    def from_env(cls) -> Settings:
        return cls(
            engine_bin=_resolve(
                os.environ.get("MICRORT_ENGINE_BIN"),
                Path("../../engine/build/engine/micrort-engine"),
            ),
            data_dir=_resolve(os.environ.get("MICRORT_DATA_DIR"), Path("./data")),
            experiments_dir=_resolve(
                os.environ.get("MICRORT_EXPERIMENTS_DIR"), Path("../../experiments")
            ),
        )
