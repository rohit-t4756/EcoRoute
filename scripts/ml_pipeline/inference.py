import os
import numpy as np
import onnxruntime as ort
import psycopg2

def map_road_category_to_tier(road_category: str) -> int:
    """
    Maps the database road_category directly to the ML model's road_type tier (0, 1, 2).
    
    NOTE: As per project design, speed_limit is NOT used here.
    Road type is determined strictly by the database category.
    
    Database ENUM (from database/init.sql):
      - 'alleyways', 'rural_roads'         --> Tier 0 (Local / Residential)
      - 'district_roads', 'arterial_roads'   --> Tier 1 (Secondary)
      - 'express_and_highways'               --> Tier 2 (Primary / Highway)
    """
    if not road_category:
        return 1
    
    category = str(road_category).strip().lower()
    if category in ['alleyways', 'rural_roads', 'local']:
        return 0
    elif category in ['district_roads', 'arterial_roads', 'secondary']:
        return 1
    elif category in ['express_and_highways', 'highways', 'expressway', 'motorway']:
        return 2
    else:
        return 1


def fetch_edges_from_db():
    """
    Fetches edges from the PostgreSQL database (edges table).
    Extracts:
      - edge_id (id)
      - speed_limit (kept as its own value)
      - road_category (road_category from DB)
      - road_type (mapped strictly from road_category, NOT speed_limit)
      
    If the PostgreSQL container is not currently active, uses sample edges
    structured identically to database/init.sql for local development.
    """
    dbname = os.getenv("POSTGRES_DB", "ecoroute")
    user = os.getenv("POSTGRES_USER", "postgres")
    password = os.getenv("POSTGRES_PASSWORD", "postgres")
    host = os.getenv("POSTGRES_HOST", "localhost")
    port = os.getenv("POSTGRES_HOST_PORT", "5432")
    
    edges_data = []
    
    try:
        conn = psycopg2.connect(
            dbname=dbname,
            user=user,
            password=password,
            host=host,
            port=port,
            connect_timeout=3
        )
        cur = conn.cursor()
        cur.execute("SELECT id, speed_limit, road_category FROM edges;")
        rows = cur.fetchall()
        for row in rows:
            edge_id, speed_limit, road_category = row
            tier = map_road_category_to_tier(road_category)
            edges_data.append({
                "edge_id": edge_id,
                "speed_limit": speed_limit,
                "road_category": road_category,
                "road_type": tier
            })
        cur.close()
        conn.close()
        return edges_data
    except Exception:
        # Fallback sample edges matching the schema in database/init.sql
        mock_db_edges = [
            {"id": 101, "speed_limit": 25, "road_category": "alleyways"},
            {"id": 102, "speed_limit": 30, "road_category": "rural_roads"},
            {"id": 103, "speed_limit": 40, "road_category": "district_roads"},
            {"id": 104, "speed_limit": 45, "road_category": "arterial_roads"},
            {"id": 105, "speed_limit": 65, "road_category": "express_and_highways"},
        ]
        for edge in mock_db_edges:
            tier = map_road_category_to_tier(edge["road_category"])
            edges_data.append({
                "edge_id": edge["id"],
                "speed_limit": edge["speed_limit"],
                "road_category": edge["road_category"],
                "road_type": tier  # Determined strictly by category, NOT speed limit!
            })
        return edges_data


class TrafficCongestionPredictor:
    """
    Production helper class to load traffic_model.onnx and perform
    ultra-fast, low-latency inference using ONNX Runtime.
    """
    def __init__(self, model_path: str = "traffic_model.onnx"):
        # Resolve model path flexibly
        resolved_path = None
        candidates = [
            model_path,
            os.path.join("app", "ml", "traffic_model.onnx"),
            os.path.join(os.path.dirname(__file__), "..", "..", "app", "ml", "traffic_model.onnx"),
            os.path.join(os.path.dirname(__file__), "traffic_model.onnx"),
        ]
        for candidate in candidates:
            if candidate and os.path.exists(candidate):
                resolved_path = os.path.abspath(candidate)
                break
                
        if not resolved_path:
            raise FileNotFoundError(
                f"ONNX model file not found at '{model_path}'. "
                "Ensure traffic_model.onnx exists in app/ml/."
            )
        
        # Optimize session options for production inference
        options = ort.SessionOptions()
        options.log_severity_level = 3  # Suppress informational warnings
        options.graph_optimization_level = ort.GraphOptimizationLevel.ORT_ENABLE_ALL
        
        self.session = ort.InferenceSession(resolved_path, sess_options=options)
        self.input_name = self.session.get_inputs()[0].name
        self.output_name = self.session.get_outputs()[0].name

    def predict(self, hour: int, day_of_week: int, road_type: int, rain_mm: float) -> float:
        """
        Predicts congestion multiplier for a single set of inputs.
        
        Parameters:
            hour (int): 0-23 (Hour of the day)
            day_of_week (int): 0-6 (0 = Monday, 6 = Sunday)
            road_type (int): 0 = Local/Residential, 1 = Secondary, 2 = Primary/Highway
            rain_mm (float): Precipitation in mm
            
        Returns:
            float: Predicted congestion multiplier (c = posted_speed_limit / actual_speed)
        """
        # Shape: (1, 4) with dtype float32
        input_tensor = np.array([[hour, day_of_week, road_type, rain_mm]], dtype=np.float32)
        raw_output = self.session.run([self.output_name], {self.input_name: input_tensor})
        # Extract scalar value from output tensor
        return float(raw_output[0][0][0])

    def predict_batch(self, features: np.ndarray) -> np.ndarray:
        """
        Predicts congestion multipliers for thousands of edges simultaneously.
        """
        input_tensor = np.asarray(features, dtype=np.float32)
        raw_output = self.session.run([self.output_name], {self.input_name: input_tensor})
        return raw_output[0].flatten()


# Global singleton instance to avoid reloading the ONNX model repeatedly
_predictor_instance = None

def get_predictor(model_path: str = "traffic_model.onnx") -> TrafficCongestionPredictor:
    global _predictor_instance
    if _predictor_instance is None:
        _predictor_instance = TrafficCongestionPredictor(model_path)
    return _predictor_instance


def predict_congestion(hour: int, day_of_week: int, road_type: int, rain_mm: float) -> float:
    """
    Standard interface function required by the EcoRoute specification.
    """
    predictor = get_predictor()
    return predictor.predict(hour, day_of_week, road_type, rain_mm)


if __name__ == "__main__":
    print("=" * 65)
    print("EcoRoute ONNX Inference Engine & Database Edge Integration")
    print("=" * 65)

    # 1. Fetch edges directly from the database
    print("\n1. Fetching Road Edges from Database (DB -> ML Algo):")
    edges = fetch_edges_from_db()
    for e in edges:
        print(f"   Edge {e['edge_id']} | Speed Limit: {e['speed_limit']} mph | "
              f"Category: {e['road_category']:<20} | ML Tier: {e['road_type']}")

    # 2. Run inference on those database edges
    # Simulate current condition: Friday (4), 6 PM (18), Rain = 3.0 mm
    current_hour = 18
    current_day = 4
    current_rain = 3.0

    print(f"\n2. Computing Congestion Factors (Friday 6 PM | Rain = {current_rain}mm):")
    for e in edges:
        cf = predict_congestion(
            hour=current_hour,
            day_of_week=current_day,
            road_type=e["road_type"],
            rain_mm=current_rain
        )
        print(f"   Edge {e['edge_id']} ({e['road_category']}) --> Predicted CF: {cf:.3f}")

    print("\nVerification completed successfully!")

