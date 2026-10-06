"""API v1 package: shared FastAPI dependencies + route modules."""

from __future__ import annotations

from typing import Annotated

from fastapi import Depends, Request

from app.engine import EngineRunner
from app.store import Store


def get_store(request: Request) -> Store:
    """Return the per-app SQLite store (set up in create_app)."""
    return request.app.state.store


def get_engine(request: Request) -> EngineRunner:
    """Return the per-app engine runner (set up in create_app)."""
    return request.app.state.engine


StoreDep = Annotated[Store, Depends(get_store)]
EngineDep = Annotated[EngineRunner, Depends(get_engine)]
