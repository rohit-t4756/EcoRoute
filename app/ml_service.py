import redis
import os
import time
import numpy as np
from datetime import datetime
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
        self.pipe = self.r.pipeline()

        # ML predictor setup
        model_path = os.path.join("app", "ml", "traffic_model.onnx")
        self.predictor = get_predictor(model_path)

        # POSTGIS connection setup
        db_url = os.getenv("DATABASE_URL") or "postgresql+psycopg2://my_ADMIN:helloEcoroute@localhost:5432/ecorouteDB"
        self.engine = create_engine(db_url)

        self.category_map = {
            'express_and_highways': 2,
            'arterial_roads': 2,
            'district_roads': 1,
            'rural_roads': 0,
            'alleyways': 0
        }

    def connection_verification(self) -> bool:
        try:
            return self.r.ping()
        except redis.ConnectionError:
            return False

    def fetch_all_edge_ids(self) -> list[int]:
        """Fetches all edge IDs dynamically from the PostGIS database."""
        with self.engine.connect() as conn:
            result = conn.execute(text("SELECT id FROM edges"))
            return [row.id for row in result]

    def update_edge_weights(self, edgeIDs: list[int]):
        now = datetime.now()
        hour = now.hour
        day = now.weekday()
        is_peak = 1 if (8 <= hour <= 11 or 17 <= hour <= 21) else 0

        # TODO: Turn these into API calls @Pranav
        rain_mm = 0.0  
        temperature_2m = 28.0  

        # 1. Fetch real road classifications and speeds from PostGIS
        edge_data = {}
        with self.engine.connect() as conn:
            query = text("SELECT id, road_category, speed_limit FROM edges WHERE id = ANY(:ids)")
            result = conn.execute(query, {"ids": edgeIDs})
            
            for row in result:
                tier = self.category_map.get(row.road_category, 0)
                speed_limit = row.speed_limit if row.speed_limit else (30 if tier == 0 else (50 if tier == 1 else 80))
                
                lanes = 1 if tier == 0 else (2 if tier == 1 else 3)
                is_oneway = 1 if tier == 2 else 0
                
                edge_data[row.id] = {
                    "tier": tier,
                    "speed": speed_limit,
                    "lanes": lanes,
                    "oneway": is_oneway
                }

        # 2. Build the 9-feature matrix
        features = []
        for edge_id in edgeIDs:
            ed = edge_data.get(edge_id, {"tier": 0, "speed": 30, "lanes": 1, "oneway": 0})
            features.append([
                hour, day, is_peak, ed["tier"], ed["speed"], ed["lanes"], ed["oneway"], rain_mm, temperature_2m
            ])

        # 3. Execute batch inference
        predictions = self.predictor.predict_batch(np.array(features))
        
        # 4. Push real predictions to Redis
        for i, edge_id in enumerate(edgeIDs):
            try:
                self.pipe.set(
                    name = f"edge_weight:{edge_id}",
                    value = round(float(predictions[i]), 3),
                    ex = 300
                )
            except Exception:
                pass

        self.pipe.execute()
        return f"Successfully cached ML weights for {len(edgeIDs)} edges."


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
                
            print("Cycle complete. Sleeping for 5 minutes...")
            time.sleep(20) # Sleep for 300 seconds (5 minutes)