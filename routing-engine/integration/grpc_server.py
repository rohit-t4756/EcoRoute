"""Multi-worker gRPC gateway keeping existing C++ CCH workers alive.

A Python gateway is intentional: the pinned C++ engine is unchanged, and gRPC
handles FastAPI -> service transport. This is not a native C++ gRPC server.
"""
import itertools
import json
import logging
import os
import signal
import threading
import time
from concurrent import futures
from pathlib import Path

import grpc
try:
    import redis
except ImportError:
    redis = None

from engine_client import EngineProcess, EngineError
from grpc_transport import SERVICE,serialize,deserialize
from redis_traffic_adapter import RedisTrafficAdapter

LOG=logging.getLogger('routing_gateway')

class Gateway:
    def __init__(self, clients, traffic_adapter=None):
        self.clients=clients
        self.adapter=traffic_adapter
        self.counter=itertools.count()
        self.update_lock=threading.RLock()
        self.version=None

    def health(self, request, context):
        try:
            with self.update_lock:
                result=self.clients[0].health()
            result['worker_count']=len(self.clients)
            try:
                result.update({k:v for k,v in json.loads((Path(os.getenv('ECOROUTE_DATA_DIR','/data'))/'active_graph.json').read_text(encoding='utf-8')).items() if k!='directory'})
            except OSError: pass
            return result
        except (EngineError, OSError) as err:
            context.set_code(grpc.StatusCode.UNAVAILABLE)
            return {'error':str(err),'ready':False}

    def route(self, request, context):
        try:
            pref=request.get('preference')
            with self.update_lock:
                worker=self.clients[next(self.counter)%len(self.clients)]
            return worker.route(vehicle_profile_id=request['vehicle_profile_id'], preference=pref,
                departure_time_unix_seconds=int(request['departure_time_unix_seconds']),
                origin=request.get('origin'),destination=request.get('destination'),
                max_routes=int(request.get('max_routes',3)))
        except EngineError as err:
            return {'error':{'code':err.code,'message':str(err)}}
        except (KeyError,ValueError,TypeError,OverflowError) as err:
            return {'error':{'code':'InvalidArgument','message':str(err)}}

    def reload(self, request, context):
        with self.update_lock:
            directory=request['directory']
            updated=[]
            try:
                for worker in self.clients:
                    updated.append(worker.update_traffic(directory))
            except Exception:
                LOG.exception('Failed to update all C++ workers')
                if context is not None: context.set_code(grpc.StatusCode.UNAVAILABLE)
                raise
            return updated[0]

    def poll_traffic(self, stop):
        if not self.adapter:return
        while not stop.wait(2):
            try:
                prepared=self.adapter.prepare_latest()
                if not prepared:continue
                directory,version=prepared
                if version == self.version: continue
                self.reload({'directory':directory},None)
                self.version=version
                LOG.info('Published traffic version %s across %d workers',version,len(self.clients))
            except Exception:
                LOG.exception('Traffic batch ingestion failed; retaining previous snapshot')

def serve():
    logging.basicConfig(level=logging.INFO,format='%(asctime)s %(levelname)s %(message)s')
    count=max(1,min(int(os.getenv('ROUTING_WORKERS','2')),8))
    executable=os.getenv('ECOROUTE_EXECUTABLE','/app/routing-engine/build/release/ecoroute_cli')
    cfg=os.getenv('ECOROUTE_CONFIG','/data/engine.conf')
    clients=[EngineProcess(executable,cfg,timeout_seconds=60) for _ in range(count)]
    health=clients[0].health()
    LOG.info('Engine ready: %s', health)
    adapter=None
    if os.getenv('REDIS_HOST') and redis is not None:
        try:
            graph = json.loads((Path(os.getenv('ECOROUTE_DATA_DIR','/data'))/'active_graph.json').read_text())['directory']
            adapter=RedisTrafficAdapter(redis.Redis(host=os.getenv('REDIS_HOST'),port=int(os.getenv('REDIS_PORT','6379')),
                          decode_responses=True,socket_timeout=3),graph,'/data/traffic')
        except Exception:
            LOG.exception('Redis traffic adapter setup failed; operating free-flow')
    gateway=Gateway(clients,adapter)
    server=grpc.server(futures.ThreadPoolExecutor(max_workers=max(8,count*4)),
                       options=[('grpc.max_receive_message_length',16*1024*1024),
                                ('grpc.max_send_message_length',16*1024*1024)])
    methods={'Route':gateway.route,'Health':gateway.health,'UpdateTraffic':gateway.reload}
    handlers={name:grpc.unary_unary_rpc_method_handler(func,request_deserializer=deserialize,
                response_serializer=serialize) for name,func in methods.items()}
    server.add_generic_rpc_handlers((grpc.method_handlers_generic_handler(SERVICE,handlers),))
    server.add_insecure_port('[::]:50051')
    stop=threading.Event()
    thread=threading.Thread(target=gateway.poll_traffic,args=(stop,),daemon=True)
    thread.start()
    server.start()
    LOG.info('Routing gRPC gateway started on port 50051')
    try:server.wait_for_termination()
    finally:
        stop.set()
        for worker in clients:worker.close()

if __name__=='__main__':serve()
