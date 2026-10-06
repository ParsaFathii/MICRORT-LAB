"""FastAPI application factory: CORS, routers, lifespan (engine availability check)."""

from __future__ import annotations

import logging
from collections.abc import AsyncIterator
from contextlib import asynccontextmanager
from typing import Any

from fastapi import FastAPI, Request
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import JSONResponse

from app.api import (
    routes_comparisons,
    routes_experiments,
    routes_health,
    routes_reports,
    routes_simulations,
)
from app.config import Settings
from app.engine import ApiError, EngineRunner
from app.store import Store

logger = logging.getLogger("micrort.api")


def _configure_logging() -> None:
    if not logging.getLogger().handlers:
        logging.basicConfig(
            level=logging.INFO,
            format="%(asctime)s %(levelname)s %(name)s: %(message)s",
        )


def create_app(settings: Settings | None = None) -> FastAPI:
    """Build the service: store + engine runner are per-app instances."""
    _configure_logging()
    settings = settings or Settings.from_env()
    store = Store(settings.data_dir)
    engine = EngineRunner(settings.engine_bin)

    @asynccontextmanager
    async def lifespan(app: FastAPI) -> AsyncIterator[None]:
        app.state.settings = settings
        app.state.store = store
        app.state.engine = engine
        app.state.health_cache: dict[str, Any] = {}
        available = engine.available()
        app.state.engine_available = available
        if available:
            logger.info("engine binary found at %s", settings.engine_bin)
        else:
            logger.warning(
                "engine binary NOT found at %s — run endpoints will answer 503 "
                "(build it with scripts/build-native.sh)",
                settings.engine_bin,
            )
        yield
        store.close()

    app = FastAPI(
        title="MicroRT-Lab Simulation API",
        version="1.0.0",
        description=(
            "Experiment management, simulation execution, trace analysis, "
            "comparisons and reports for the micrort-engine deterministic "
            "real-time scheduling simulator."
        ),
        lifespan=lifespan,
    )
    app.add_middleware(
        CORSMiddleware,
        allow_origins=["*"],
        allow_credentials=False,
        allow_methods=["*"],
        allow_headers=["*"],
        expose_headers=["*"],
    )
    app.include_router(routes_simulations.router)
    app.include_router(routes_experiments.router)
    app.include_router(routes_comparisons.router)
    app.include_router(routes_reports.router)
    app.include_router(routes_health.router)

    @app.exception_handler(ApiError)
    async def api_error_handler(request: Request, exc: ApiError) -> JSONResponse:
        payload: dict[str, Any] = {"error": exc.message}
        if exc.details:
            payload["details"] = exc.details
        return JSONResponse(status_code=exc.status_code, content=payload)

    @app.get("/api/v1", include_in_schema=False)
    def root() -> dict[str, Any]:
        return {
            "service": "micrort-sim-api",
            "version": "1.0.0",
            "docs": "/docs",
            "endpoints": [
                "/api/v1/simulations",
                "/api/v1/experiments",
                "/api/v1/comparisons",
                "/api/v1/reports",
                "/api/v1/health",
            ],
        }

    return app


app = create_app()
