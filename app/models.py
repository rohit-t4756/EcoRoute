from pydantic import BaseModel, Field, AfterValidator
from typing import Annotated
from enum import Enum

# Helper functions
# Rounding function for floating point precision in latitude longitude
def round_coords(v: float) -> float:
    return round(v, 6)

class VehicleEnum(str, Enum):
    ICE = "ice"
    HEV = "hev"
    EV  = "ev"

# NOTE: Subject to change to a scale between 1 to 10
class OptimisationEnum(str, Enum):
    TIME        = "time"
    EMISSION    = "emission"
    BALANCE     = "balanced"

class RouteRequest(BaseModel):
    start_lon: Annotated[float, Field(ge=-180, le=180), AfterValidator(round_coords)]
    start_lat: Annotated[float, Field(ge=-90, le=90), AfterValidator(round_coords)]
    end_lon: Annotated[float, Field(ge=-180, le=180), AfterValidator(round_coords)]
    end_lat: Annotated[float, Field(ge=-90, le=90), AfterValidator(round_coords)]
    vehicle_type: VehicleEnum
    optimise_for: OptimisationEnum