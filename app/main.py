"""Existing FastAPI gateway integrated with the CCH gRPC routing service.

Redis is an optional shared, traffic-versioned cache; failure never invents a route.
"""
from __future__ import annotations
import hashlib
import json
import logging
import os
import time
from concurrent.futures import ThreadPoolExecutor
import threading
from contextlib import asynccontextmanager
from datetime import datetime, timezone
from pathlib import Path

import grpc
import asyncpg
from fastapi import FastAPI, HTTPException
from fastapi.middleware.cors import CORSMiddleware

from .models import RouteRequest

import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'routing-engine' / 'integration'))
from grpc_transport import call

LOG = logging.getLogger('ecoroute_api')
CHANNEL = None
CACHE = None
DB_POOL = None
PREFETCH = ThreadPoolExecutor(max_workers=2,thread_name_prefix='route-prefetch')
PREFETCH_LIMIT = threading.BoundedSemaphore(12)

@asynccontextmanager
async def lifespan(app):
    global CHANNEL,CACHE,DB_POOL
    CHANNEL=grpc.insecure_channel(os.getenv('ROUTING_GRPC_TARGET','routing:50051'),
                                 options=[('grpc.max_receive_message_length',16*1024*1024)])
    try:
        import redis
        CACHE=redis.Redis(host=os.getenv('REDIS_HOST','redis'),port=int(os.getenv('REDIS_PORT','6379')),
                         decode_responses=True,socket_connect_timeout=1,socket_timeout=2)
        CACHE.ping()
    except Exception:
        LOG.warning('Redis unavailable; responses will not be cached')
        CACHE=None
    try:
        DB_POOL=await asyncpg.create_pool(host=os.getenv('POSTGRES_HOST','database'),
            port=int(os.getenv('POSTGRES_PORT','5432')),
            database=os.getenv('POSTGRES_DB','ecoroute'),user=os.getenv('POSTGRES_USER','ecoroute'),
            password=os.getenv('POSTGRES_PASSWORD','ecoroute_demo_change_me'),
            min_size=1,max_size=3,timeout=5)
    except Exception:
        LOG.warning('Database unavailable to HTTP API; routing remains available')
    yield
    if DB_POOL is not None: await DB_POOL.close()
    CHANNEL.close()
    PREFETCH.shutdown(wait=False,cancel_futures=True)

app=FastAPI(title='EcoRoute API Gateway',version='2.0.0',lifespan=lifespan)
app.add_middleware(CORSMiddleware,
   allow_origins=[s.strip() for s in os.getenv('CORS_ORIGINS','http://localhost:5173').split(',') if s.strip()],
   allow_methods=['GET','POST'],allow_headers=['Content-Type'])

PROFILE={'petrol':'petrol_demo','diesel':'diesel_demo','hybrid':'hybrid_demo',
         'electric':'electric_demo','ice':'petrol_demo','hev':'hybrid_demo','ev':'electric_demo'}

def rpc(name,payload):
    try:
        return call(CHANNEL,name,payload,timeout=float(os.getenv('ROUTING_DEADLINE_SECONDS','65')))
    except grpc.RpcError as exc:
        LOG.warning('Routing service failed: %s',exc.code())
        raise HTTPException(503,detail='Routing service unavailable. Check routing container logs.') from exc

def _cached(key):
    if CACHE is None:return None
    try:
        result=CACHE.get('route:v2:'+key)
        return json.loads(result) if result else None
    except Exception:
        LOG.warning('Redis read failed; bypassing cache')
        return None

def _store(key,result):
    if CACHE is None:return
    try:CACHE.setex('route:v2:'+key,180,json.dumps(result,separators=(',',':'),allow_nan=False))
    except Exception:LOG.warning('Redis write failed; bypassing cache')

def _key(payload,health):
    # Exact preference included. Traffic and graph versions prevent stale reuse.
    content={'request':payload,'graph':health['graph_version'],'traffic':health['traffic_version']}
    encoded=json.dumps(content,sort_keys=True,separators=(',',':'),allow_nan=False)
    return hashlib.sha256(encoded.encode()).hexdigest()

def request_payload(req,preference=None):
    pref=req.preference if preference is None else preference
    return {'origin':[req.start_lat,req.start_lon], 'destination':[req.end_lat,req.end_lon],
            'vehicle_profile_id':PROFILE[req.vehicle_type.value],
            'preference':pref,'departure_time_unix_seconds':int(time.time()),'max_routes':3}

def engine_route(payload,health):
    # Requests in one time window reuse the same traffic version and 5-minute time bucket.
    # The underlying engine's model is snapshot-based, not time-dependent within a batch.
    payload={**payload,'departure_time_unix_seconds':int(payload['departure_time_unix_seconds']//60*60)}
    key=_key(payload,health)
    hit=_cached(key)
    if hit is not None:return hit
    data=rpc('Route',payload)
    if 'error' in data:
        detail=data['error']
        code=detail.get('code','InternalError') if isinstance(detail,dict) else 'InternalError'
        status={'NoRoute':404,'OutOfCoverage':422,'SnapFailed':422,'InvalidArgument':422,
                'UnsupportedProfile':422,'InvalidTrafficSnapshot':503,
                'DeadlineExceeded':504}.get(code,503)
        raise HTTPException(status,detail=detail)
    if not isinstance(data.get('routes'),list) or not data.get('graph_version'):
        raise HTTPException(502,detail='Invalid routing service response')
    # Don't cache responses computed against a different traffic version.
    if data.get('traffic_version')==health.get('traffic_version') and data.get('graph_version')==health.get('graph_version'):
        _store(key,data)
    return data

def _prefetch(payload,health):
    try:
        for delta in (-0.05,0.05):
            pref=round(max(0,min(1,payload['preference']+delta)),6)
            if pref==payload['preference']:continue
            try:engine_route({**payload,'preference':pref},health)
            except Exception:LOG.debug('Neighboring slider route prefetch unavailable')
    finally:
        PREFETCH_LIMIT.release()

@app.get('/')
def root():return {'service':'EcoRoute API Gateway','status':'online'}

@app.get('/health')
def health():
    answer=rpc('Health',{})
    if not answer.get('ready'):
        raise HTTPException(503,detail='Routing engine not ready')
    return answer

@app.get('/db-status')
async def db_status():
    if DB_POOL is None:
        raise HTTPException(503,detail='PostGIS database pool unavailable')
    try:
        async with DB_POOL.acquire() as conn:
            count=await conn.fetchval('SELECT COUNT(*) FROM nodes')
            edges=await conn.fetchval('SELECT COUNT(*) FROM edges')
        return {'database_status':'connected','total_nodes':count,'total_edges':edges}
    except Exception as exc:
        LOG.error('Database status query failed: %s',type(exc).__name__)
        raise HTTPException(503,detail='PostGIS query failed') from exc

@app.post('/api/v1/route')
def get_route(req:RouteRequest):
    status=health()
    payload=request_payload(req)
    result=engine_route(payload,status)
    if os.getenv('ROUTE_PREFETCH','true').lower()=='true' and PREFETCH_LIMIT.acquire(blocking=False):
        # Bounded speculative cache warmer; never delays the visible response.
        PREFETCH.submit(_prefetch,payload,status)
    return result
