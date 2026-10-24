import redis
import os
import random
from dotenv import load_dotenv

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

    def connection_verification(self) -> bool:
        try:
            return self.r.ping()
        except redis.ConnectionError:
            return False

    def update_edge_weights(self, edgeIDs: list[int]):
        # looping through the edgeIDs and inserting them into the redis
        # TODO: Get this congestion factor from ML pipeline.
        for id in edgeIDs:
            congestion_factor = round(random.uniform(1.0, 3.0), 2)
            try:
                self.pipe.set(
                    name = f"edge_weight:{id}",
                    value = congestion_factor,
                    ex = 300
                )
            except Exception as e:
                pass

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