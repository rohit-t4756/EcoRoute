import redis
import os
import time
import csv
import json
import uuid
from pathlib import Path
import requests
import numpy as np
from datetime import datetime, timezone
from dotenv import load_dotenv
from scripts.ml_pipeline.inference import get_predictor
from sqlalchemy import create_engine, text

load_dotenv()

class MLTrafficBridge:
    def __init__(self):
        # Redis connection setup
        self.r = redis.Redis(
            host = os.getenv("REDIS_HOST") or 'localhost',
            port = int(os.getenv("REDIS_CONTAINER_PORT") or 6379),
            decode_responses = True
        )
        self.pipe = self.r.pipeline(transaction=True)

        # ML predictor setup
        model_path = os.path.join("app", "ml", "traffic_model.onnx")
        self.predictor = get_predictor(model_path)

        # POSTGIS connection setup
        db_url = os.getenv("DATABASE_URL")
        if not db_url:
            raise ValueError("DATABASE_URL must be configured; do not use hardcoded credentials")
        self.engine = create_engine(db_url)

        self.category_map = {
            'express_and_highways': 2,
            'arterial_roads': 2,
            'district_roads': 1,
            'rural_roads': 0,
            'alleyways': 0
        }

        # In-memory weather cache (5-minute TTL)
        self._weather_cache = None
        self._weather_cache_time = 0

    def connection_verification(self) -> bool:
        try:
            return self.r.ping()
        except redis.ConnectionError:
            return False

    def fetch_all_edge_ids(self) -> list[int]:
        """Live PostGIS edges, or demo snapshot IDs when database is empty."""
        with self.engine.connect() as conn:
            rows = conn.execute(text("SELECT id FROM edges ORDER BY id")).fetchall()
        if rows:
            return [row.id for row in rows]
        graph = json.loads(Path('/data/active_graph.json').read_text(encoding='utf-8'))
        if graph['source'] != 'synthetic-demo':
            return []
        with (Path(graph['directory'])/'edges.csv').open(newline='',encoding='utf-8') as fp:
            return [int(row['edge_id']) for row in csv.DictReader(fp)]

    def get_current_weather(self, lat: float = 18.5204, lon: float = 73.8567) -> tuple[float, float]:
        """
        Fetches real-time precipitation (mm) and temperature (C) for Pune from Open-Meteo API.
        Includes a lightweight 5-minute memory cache to prevent spamming the API.
        """
        current_ts = time.time()
        if self._weather_cache is not None and (current_ts - self._weather_cache_time < 300):
            return self._weather_cache

        url = f"https://api.open-meteo.com/v1/forecast?latitude={lat}&longitude={lon}&current=temperature_2m,precipitation"
        try:
            res = requests.get(url, timeout=3)
            if res.status_code == 200:
                current_data = res.json().get("current", {})
                rain = float(current_data.get("precipitation", 0.0))
                temp = float(current_data.get("temperature_2m", 28.0))
                self._weather_cache = (rain, temp)
                self._weather_cache_time = current_ts
                return rain, temp
        except Exception as e:
            print(f"Weather API notice: {e}. Using fallback defaults.")

        return 0.0, 28.0

    def update_edge_weights(self, edgeIDs: list[int]):
        now = datetime.now(timezone.utc)
        try:
            from zoneinfo import ZoneInfo
            now = now.astimezone(ZoneInfo("Asia/Kolkata"))
        except Exception:
            now = now.astimezone()
        hour = now.hour
        day = now.weekday()
        is_peak = 1 if (8 <= hour <= 11 or 17 <= hour <= 21) else 0

        # Real-time weather fetched live via Open-Meteo API for Pune (@Pranav)
        rain_mm, temperature_2m = self.get_current_weather()

        # 1. Fetch real road classifications and speeds from PostGIS
        edge_data = {}
        with self.engine.connect() as conn:
            query = text("SELECT id, road_category, speed_limit FROM edges WHERE id = ANY(:ids)")
            result = conn.execute(query, {"ids": edgeIDs})
            
            for row in result:
                tier = self.category_map.get(str(row.road_category), 0)
                speed_limit = row.speed_limit if row.speed_limit else (30 if tier == 0 else (50 if tier == 1 else 80))
                
                lanes = 1 if tier == 0 else (2 if tier == 1 else 3)
                is_oneway = 1 if tier == 2 else 0
                
                edge_data[row.id] = {
                    "tier": tier,
                    "speed": speed_limit,
                    "lanes": lanes,
                    "oneway": is_oneway
                }

        # In the synthetic demonstration, the DB intentionally has no roads.
        # Use the graph snapshot's actual directed edge IDs and attributes.
        if not edge_data and edgeIDs:
            try:
                graph = json.loads(Path('/data/active_graph.json').read_text(encoding='utf-8'))
                if graph['source'] == 'synthetic-demo':
                    with (Path(graph['directory'])/'edges.csv').open(newline='',encoding='utf-8') as fp:
                        for row in csv.DictReader(fp):
                            tier = self.category_map.get(row['road_class'], 1)
                            edge_data[int(row['edge_id'])] = {
                                'tier':tier, 'speed':float(row['speed_limit_kph'] or 30),
                                'lanes':1 if tier == 0 else (2 if tier == 1 else 3),
                                'oneway':0}
            except (OSError,KeyError,ValueError):
                pass

        # 2. Build the 9-feature matrix
        features = []
        for edge_id in edgeIDs:
            ed = edge_data.get(edge_id, {"tier": 0, "speed": 30, "lanes": 1, "oneway": 0})
            features.append([
                hour, day, is_peak, ed["tier"], ed["speed"], ed["lanes"], ed["oneway"], rain_mm, temperature_2m
            ])

        # 3. Execute batch inference
        predictions = self.predictor.predict_batch(np.array(features))
        
        # 4. Publish one IMMUTABLE batch; the latest pointer is written last.
        # This prevents C++ from observing a mixture of old/new factor values.
        import math
        values = {}
        for i, edge_id in enumerate(edgeIDs):
            value = float(predictions[i])
            if not math.isfinite(value):
                raise ValueError(f'Nonfinite prediction for edge {edge_id}')
            values[str(edge_id)] = str(round(max(1.0, min(value, 5.0)), 5))
        if len(values) != len(edgeIDs):
            raise ValueError('Duplicate edge identifiers; cannot publish traffic')
        graph = json.loads(Path('/data/active_graph.json').read_text(encoding='utf-8'))
        published = int(time.time())
        version = f'ml-{published}-{uuid.uuid4().hex[:8]}'
        ttl = max(900, int(os.getenv('ML_INTERVAL_SECONDS','300'))*3)
        metadata = {
            'graph_version':graph['graph_version'], 'traffic_version':version,
            'valid_from_unix_seconds':max(0,published-120),
            'valid_until_unix_seconds':published+ttl-60}
        key='traffic:batch:'+version
        with self.r.pipeline(transaction=True) as pipe:
            for start in range(0,len(values),2000):
                batch=dict(list(values.items())[start:start+2000])
                pipe.hset(key,mapping=batch)
            pipe.expire(key,ttl)
            # Legacy per-edge keys retained for teammate compatibility (not used by C++).
            for edge_id, factor in values.items():
                pipe.set('edge_weight:'+edge_id,factor,ex=ttl)
            pipe.set('traffic:latest',json.dumps(metadata,separators=(',',':')),ex=ttl)
            pipe.execute()
        return f'Published ML traffic batch {version} for {len(edgeIDs)} directed edges.'



if __name__ == "__main__":
    bridge = MLTrafficBridge()
    if not bridge.connection_verification():
        print("Redis ping failed. Worker cannot start.")
    else:
        print("Redis ping successful. Starting the ML Traffic worker daemon...")
        # The continuous background worker loop
        while True:
            try:
                print(f"\n--- [{datetime.now().strftime('%H:%M:%S')}] Traffic Update Cycle ---")
                
                all_edges = bridge.fetch_all_edge_ids()
                if not all_edges:
                    print("No edges found in the database. Retrying next cycle.")
                else:
                    print(f"Processing {len(all_edges)} road segments from PostGIS...")
                    result = bridge.update_edge_weights(all_edges)
                    print(f"{result}")
                    
                    # Verify a random edge to prove it worked
                    sample_edge = all_edges[0]
                    val = bridge.r.get(f"edge_weight:{sample_edge}")
                    print(f"Verification -> Edge {sample_edge} CF: {val}")
                    
                    # TODO: Trigger gRPC LoadTrafficSnapshot to C++ engine here
    
            except Exception as e:
                print(f"Worker Error: {e}")
                
            print("Cycle complete. Waiting for next prediction interval...")
            time.sleep(int(os.getenv("ML_INTERVAL_SECONDS", "300")))