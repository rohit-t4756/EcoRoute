"""Shared browser request contract. Coordinates are WGS84 [lon,lat] in HTTP."""
from enum import Enum
from math import isfinite
from pydantic import BaseModel, ConfigDict, Field, model_validator

class VehicleEnum(str,Enum):
    PETROL='petrol'
    DIESEL='diesel'
    HYBRID='hybrid'
    ELECTRIC='electric'
    ICE='ice'  # Legacy requests
    HEV='hev'
    EV='ev'

class OptimisationEnum(str,Enum):
    TIME='time'
    EMISSION='emission'
    BALANCE='balanced'

class RouteRequest(BaseModel):
    model_config=ConfigDict(extra='forbid',allow_inf_nan=False)
    start_lon:float=Field(ge=-180,le=180)
    start_lat:float=Field(ge=-90,le=90)
    end_lon:float=Field(ge=-180,le=180)
    end_lat:float=Field(ge=-90,le=90)
    vehicle_type:VehicleEnum
    preference:float|None=Field(default=None,ge=0,le=1)
    optimise_for:OptimisationEnum|None=None

    @model_validator(mode='after')
    def normalize(self):
        if self.preference is None:
            self.preference={'time':1.,'emission':0.,'balanced':0.5}.get(
                self.optimise_for.value if self.optimise_for else 'balanced',0.5)
        # Keeping coordinates at 6 digits stabilizes near-identical cache keys.
        for key in ('start_lon','start_lat','end_lon','end_lat'):
            setattr(self,key,round(getattr(self,key),6))
        return self
