"""Dependency-light integration checks for C++ + Python + protobuf gRPC.

Run with a locally built CLI or under the routing image. No PostGIS/Redis
required for the explicitly synthetic scenario; a fake Redis tests batching.
"""
from __future__ import annotations
import json
import os
import subprocess
import sys
import tempfile
import threading
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'routing-engine'/'integration'))
sys.path.insert(0,str(ROOT/'scripts'/'integration'))
from prepare_graph import demo_graph
from export_snapshot import export_snapshot
from engine_client import EngineProcess,EngineError
from redis_traffic_adapter import RedisTrafficAdapter,TrafficBatchError

class RedisStub:
    def __init__(self):self.strings={};self.hashes={}
    def get(self,key):return self.strings.get(key)
    def hgetall(self,key):return self.hashes.get(key,{})


def check():
    executable=Path(os.environ.get('ECOROUTE_TEST_CLI',ROOT/'routing-engine'/'build'/'integration'/'ecoroute_cli'))
    if not executable.exists():raise SystemExit(f'Build executable before smoke test: {executable}')
    with tempfile.TemporaryDirectory() as directory:
        data=Path(directory);graph=data/'graph'
        nodes,edges=demo_graph();export_snapshot(graph,'smoke-v1',nodes,edges)
        config=data/'engine.conf'
        config.write_text('\n'.join((f'graph_file={graph}',f'vehicle_profiles_file={ROOT}/routing-engine/config/vehicle_profiles.example.csv',
            'allow_free_flow=true','allow_missing_traffic=true','max_snap_distance_meters=3000','')),encoding='utf-8')
        bridge=EngineProcess(executable,config,timeout_seconds=60)
        try:
            health=bridge.health()
            assert health['ready'] and health['traffic_version']=='explicit-free-flow'
            def query(profile='petrol_demo'):
                return bridge.route(origin=(18.513,73.844),destination=(18.537,73.868),
                    vehicle_profile_id=profile,preference=0.38,departure_time_unix_seconds=int(time.time()))
            with ThreadPoolExecutor(max_workers=8) as pool:
                futures=[pool.submit(query,profile) for profile in ['petrol_demo','diesel_demo','hybrid_demo','electric_demo']*3]
                results=[f.result() for f in futures]
            for result in results:
                assert result['routes'] and len(result['routes'])<=3
                assert result['recommended_route_id'] in [r['id'] for r in result['routes']]
                assert result['routes'][0]['geometry']['type']=='LineString'
            # Publish a complete fake ML batch with a different graph-wide factor.
            r=RedisStub();now=int(time.time())
            r.strings['traffic:latest']=json.dumps({'graph_version':'smoke-v1','traffic_version':'smoke-t2',
                'valid_from_unix_seconds':now-30,'valid_until_unix_seconds':now+900})
            r.hashes['traffic:batch:smoke-t2']={str(e['edge_id']):'1.8' for e in edges}
            adapter=RedisTrafficAdapter(r,graph,data/'traffic')
            staging,version=adapter.prepare_latest()
            assert version=='smoke-t2'
            assert adapter.prepare_latest()==(staging,version)
            updated=bridge.update_traffic(staging)
            assert updated['traffic_version']==version
            result=query()
            assert result['traffic_version']==version
            # Ensure a killed worker restarts and restores the latest traffic.
            bridge._stop()  # deliberate controlled crash simulation
            result=query()
            assert result['traffic_version']==version
            # Reject incomplete publication before calling C++.
            r.hashes['traffic:batch:smoke-t2'].pop(next(iter(r.hashes['traffic:batch:smoke-t2'])))
            r.strings['traffic:latest']=r.strings['traffic:latest'].replace('smoke-t2','smoke-t3')
            try:adapter.prepare_latest()
            except TrafficBatchError:pass
            else:raise AssertionError('Incomplete Redis snapshot was accepted')
        finally:bridge.close()
    print('PASS: concurrent callers, multi-profile routes, immutable traffic publication, worker recovery, incomplete batches')

if __name__=='__main__':check()
