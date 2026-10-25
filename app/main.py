from fastapi import FastAPI, HTTPException
import asyncpg
import os
from dotenv import load_dotenv
from .models import RouteRequest

load_dotenv()

app = FastAPI(title="EcoRoute API Gateway", version="1.0.0")

db_pool = None

@app.on_event("startup")
async def startup():
    global db_pool
    try:
        db_pool = await asyncpg.create_pool(
            user=os.getenv("POSTGRES_USER"),
            password=os.getenv("POSTGRES_PASSWORD"),
            database=os.getenv("POSTGRES_DB"),
            host="127.0.0.1",
            port=os.getenv("POSTGRES_HOST_PORT")
        )
        print("Successfully connected to PostGIS Database Pool!")
    except Exception as e:
        print(f"Failed to connect to database: {e}")

@app.on_event("shutdown")
async def shutdown():
    if db_pool:
        await db_pool.close()

@app.get("/")
async def root():
    return {"status": "online", "service": "EcoRoute API Gateway"}

@app.get("/db-status")
async def db_status():
    if not db_pool:
        raise HTTPException(status_code=500, detail="Database pool not initialized")
    try:
        async with db_pool.acquire() as connection:
            count = await connection.fetchval("SELECT COUNT(*) FROM nodes;")
            return {"database_status": "connected", "total_nodes": count}
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))


# Route requests
@app.post("/api/v1/route")
async def get_route(request: RouteRequest):
    requestJSON = request.model_dump()  # Returns python dictionary with python objects
    # TODO: Get 10 routes from C++ engine and additonal analytics, then sendd them all to the frontend.
    '''
    output format:
    {
        "route": [
            { "1": { "type": "LineString", "coordinates": [...] } },
            { "5": { "type": "LineString", "coordinates": [...] } },
            { "10": { "type": "LineString", "coordinates": [...] } }
        ]
        "analytics": {}
    }
    '''
    return {
        "message" : "request received", 
        "route" : "fake_route_id", 
        "vehicle_type" : request.vehicle_type.value,
        "optimisation_choice" : request.optimise_for.value
    }