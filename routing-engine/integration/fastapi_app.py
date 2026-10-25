"""Optional FastAPI adapter. Install fastapi and uvicorn separately.
ECOROUTE_EXECUTABLE and ECOROUTE_CONFIG select the local worker.
This is an integration example, not the unseen teammate backend.
"""
import os
from contextlib import asynccontextmanager
from fastapi import FastAPI, HTTPException
from pydantic import BaseModel, ConfigDict, Field, StrictInt
try:
    from .engine_client import EngineProcess, EngineError
except ImportError:
    from engine_client import EngineProcess, EngineError

client = None

@asynccontextmanager
async def lifespan(app):
    global client
    client = EngineProcess(os.environ["ECOROUTE_EXECUTABLE"], os.environ["ECOROUTE_CONFIG"])
    try:
        yield
    finally:
        client.close()

app = FastAPI(title="EcoRoute local integration adapter", lifespan=lifespan)

class RouteInput(BaseModel):
    model_config = ConfigDict(extra="forbid", allow_inf_nan=False)
    vehicle_profile_id: str
    preference: float = Field(ge=0, le=1)
    departure_time_unix_seconds: StrictInt = Field(ge=0, lt=2**63)
    source_node_id: str | None = None
    target_node_id: str | None = None
    origin: tuple[float, float] | None = None
    destination: tuple[float, float] | None = None
    max_routes: StrictInt = Field(default=3, ge=1, le=3)
    max_extra_time_ratio: float = Field(default=0.25, ge=0)
    max_extra_time_seconds: float = Field(default=600, ge=0)

@app.get("/health")
def health():
    try:
        return client.health()
    except EngineError as error:
        raise HTTPException(503, {"code": error.code, "message": str(error)}) from error

@app.post("/api/v1/route")
def route(request: RouteInput):
    try:
        return client.route(**request.model_dump())
    except ValueError as error:
        raise HTTPException(422, str(error)) from error
    except EngineError as error:
        status = {"NoRoute": 404, "SnapFailed": 422, "OutOfCoverage": 422,
                  "InvalidArgument": 422, "UnsupportedProfile": 422,
                  "DeadlineExceeded": 504}.get(error.code, 503)
        raise HTTPException(status, {"code": error.code, "message": str(error)}) from error