import redis
import os
import numpy as np
from datetime import datetime
from dotenv import load_dotenv
from scripts.ml_pipeline.inference import get_predictor

# load the .env file in memory for os.getenv() to work
load_dotenv()

class MLTrafficBridge:
    def __init__(self):
        self.r = redis.Redis(
            host = os.getenv("REDIS_HOST") or 'localhost',
            port = os.getenv("REDIS_CONTAINER_PORT") or 6379,
            decode_responses = True
        )
        self.pipe = self.r.pipeline()
        model_path = os.path.join("app", "ml", "traffic_model.onnx")
        self.predictor = get_predictor(model_path)

    def connection_verification(self) -> bool:
        try:
            return self.r.ping()
        except redis.ConnectionError:
            return False

    def update_edge_weights(self, edgeIDs: list[int]):
        # Get real-time context for ML model
        now = datetime.now()
        hour = now.hour
        day = now.weekday()
        rain_mm = 0.0  # Defaulting to clear weather; TODO: Integrate with weather API for real-time precipitation data

        # Build feature matrix (Shape: (N, 4)) for batch prediction
        features = []
        for _ in edgeIDs:
            # TODO: Query PostGIS here to get the actual road_type (0, 1, or 2) for this specific edge
            mock_road_type = np.random.randint(0, 3) 
            features.append([hour, day, mock_road_type, rain_mm])

        # Execute inference
        congestion_factors = self.predictor.predict_batch(np.array(features))

        # Push predictions to Redis
        for i, id in enumerate(edgeIDs):
            try:
                self.pipe.set(
                    name = f"edge_weight:{id}",
                    value = round(float(congestion_factors[i]), 3),
                    ex = 300  # Cache for 5 minutes
                )
            except Exception as e:
                print(f"Error occurred while caching edge ID {id}: {e}")


        self.pipe.execute()
        return f"Successfully cached {len(edgeIDs)} edges."


if __name__ == "__main__":
    bridge = MLTrafficBridge()
    print("here")
    if bridge.connection_verification():
        print("redis ping successful.")

        test_edgeIDs = [4509, 4636, 7903, 9065]
        result = bridge.update_edge_weights(test_edgeIDs)
        print(result)

        checkID = 4636
        result = bridge.r.get(name=f"edge_weight:{checkID}")
        print(f"Redis stored weight for edgeID[{checkID}] : {result}")
    else:
        print("redis ping failed.")