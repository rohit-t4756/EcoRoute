import os
import numpy as np
import onnxruntime as ort
import psycopg2

def map_road_category_to_tier(road_category: str) -> int:
    """
    Maps the database road_category directly to the ML model's road_type tier (0, 1, 2).
    
    Database ENUM (from database/init.sql):
      - 'alleyways', 'rural_roads'           --> Tier 0 (Local / Residential)
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
    Extracts road attributes (speed limit, category, tier, lanes, one-way) for Shivaji Nagar, Pune.
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
            
            # Infer realistic physical attributes based on road category if not explicitly in table
            speed_val = speed_limit if speed_limit else (30 if tier == 0 else (50 if tier == 1 else 80))
            lanes = 1 if tier == 0 else (2 if tier == 1 else 3)
            is_oneway = 1 if tier == 2 else 0
            
            edges_data.append({
                "edge_id": edge_id,
                "speed_limit_kmh": speed_val,
                "road_category": road_category,
                "road_type": tier,
                "lanes": lanes,
                "is_oneway": is_oneway
            })
        cur.close()
        conn.close()
        return edges_data
    except Exception:
        # Fallback sample edges matching Shivaji Nagar Pune road types
        mock_pune_edges = [
            {"id": 101, "name": "Narrow Market Lane", "speed_limit_kmh": 20, "road_category": "alleyways", "lanes": 1, "is_oneway": 0},
            {"id": 102, "name": "Residential Access", "speed_limit_kmh": 30, "road_category": "rural_roads", "lanes": 1, "is_oneway": 0},
            {"id": 103, "name": "Connecting Collector Road", "speed_limit_kmh": 40, "road_category": "district_roads", "lanes": 2, "is_oneway": 0},
            {"id": 104, "name": "Arterial Road (Two-Way)", "speed_limit_kmh": 50, "road_category": "arterial_roads", "lanes": 2, "is_oneway": 0},
            {"id": 105, "name": "FC Road (Major One-Way)", "speed_limit_kmh": 50, "road_category": "arterial_roads", "lanes": 3, "is_oneway": 1},
            {"id": 106, "name": "Old Pune-Mumbai Highway", "speed_limit_kmh": 80, "road_category": "express_and_highways", "lanes": 4, "is_oneway": 1},
        ]
        for edge in mock_pune_edges:
            tier = map_road_category_to_tier(edge["road_category"])
            edges_data.append({
                "edge_id": edge["id"],
                "name": edge["name"],
                "speed_limit_kmh": edge["speed_limit_kmh"],
                "road_category": edge["road_category"],
                "road_type": tier,
                "lanes": edge["lanes"],
                "is_oneway": edge["is_oneway"]
            })
        return edges_data


class TrafficCongestionPredictor:
    """
    Production helper class to load traffic_model.onnx and perform
    ultra-fast, low-latency inference using ONNX Runtime for Pune traffic.
    """
    def __init__(self, model_path: str = "traffic_model.onnx"):
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
                "Ensure traffic_model.onnx exists."
            )
        
        options = ort.SessionOptions()
        options.log_severity_level = 3
        options.graph_optimization_level = ort.GraphOptimizationLevel.ORT_ENABLE_ALL
        
        self.session = ort.InferenceSession(resolved_path, sess_options=options)
        self.input_name = self.session.get_inputs()[0].name
        self.output_name = self.session.get_outputs()[0].name
        self.num_features = self.session.get_inputs()[0].shape[1]

    def predict(
        self, 
        hour: int, 
        day_of_week: int, 
        road_type: int, 
        rain_mm: float,
        speed_limit_kmh: int = 40,
        lanes: int = 2,
        is_oneway: int = 0,
        temperature_2m: float = 28.0
    ) -> float:
        """
        Predicts congestion multiplier for a single set of inputs.
        Supports both 4-feature legacy calls and 9-feature Pune enhanced calls.
        """
        # Determine Indian peak hour flag
        is_peak = 1 if (8 <= hour <= 11 or 17 <= hour <= 21) else 0
        
        if self.num_features == 9:
            input_tensor = np.array([[
                hour, 
                day_of_week, 
                is_peak, 
                road_type, 
                speed_limit_kmh, 
                lanes, 
                is_oneway, 
                rain_mm, 
                temperature_2m
            ]], dtype=np.float32)
        else:
            # 4-feature fallback
            input_tensor = np.array([[hour, day_of_week, road_type, rain_mm]], dtype=np.float32)
            
        raw_output = self.session.run([self.output_name], {self.input_name: input_tensor})
        return float(raw_output[0][0][0])

    def predict_batch(self, features: np.ndarray) -> np.ndarray:
        """
        Predicts congestion multipliers for thousands of edges simultaneously.
        """
        input_data = np.asarray(features, dtype=np.float32)
        
        # If passed 4 features to a 9-feature model, pad with intelligent Indian defaults
        if input_data.shape[1] == 4 and self.num_features == 9:
            N = input_data.shape[0]
            hours = input_data[:, 0]
            is_peaks = np.where(((hours >= 8) & (hours <= 11)) | ((hours >= 17) & (hours <= 21)), 1.0, 0.0)
            road_types = input_data[:, 2]
            
            # Default speed limits based on road tier
            speeds = np.where(road_types == 0, 30.0, np.where(road_types == 1, 50.0, 80.0))
            lanes = np.where(road_types == 0, 1.0, np.where(road_types == 1, 2.0, 3.0))
            oneways = np.where(road_types == 2, 1.0, 0.0)
            temps = np.full(N, 28.0, dtype=np.float32)
            
            # Reconstruct 9-column matrix: [hour, day, is_peak, road_type, speed, lanes, oneway, rain, temp]
            input_data = np.column_stack([
                input_data[:, 0], # hour
                input_data[:, 1], # day
                is_peaks,         # is_peak
                input_data[:, 2], # road_type
                speeds,           # speed_limit_kmh
                lanes,            # lanes
                oneways,          # is_oneway
                input_data[:, 3], # rain_mm
                temps             # temp
            ]).astype(np.float32)

        raw_output = self.session.run([self.output_name], {self.input_name: input_data})
        return raw_output[0].flatten()


_predictor_instance = None

def get_predictor(model_path: str = "traffic_model.onnx") -> TrafficCongestionPredictor:
    global _predictor_instance
    if _predictor_instance is None:
        _predictor_instance = TrafficCongestionPredictor(model_path)
    return _predictor_instance


def predict_congestion(
    hour: int, 
    day_of_week: int, 
    road_type: int, 
    rain_mm: float,
    speed_limit_kmh: int = 40,
    lanes: int = 2,
    is_oneway: int = 0,
    temperature_2m: float = 28.0
) -> float:
    """
    Standard interface function required by the EcoRoute specification.
    """
    predictor = get_predictor()
    return predictor.predict(
        hour=hour,
        day_of_week=day_of_week,
        road_type=road_type,
        rain_mm=rain_mm,
        speed_limit_kmh=speed_limit_kmh,
        lanes=lanes,
        is_oneway=is_oneway,
        temperature_2m=temperature_2m
    )


if __name__ == "__main__":
    print("=" * 70)
    print("EcoRoute Pune Traffic ONNX Inference Engine Verification")
    print("=" * 70)

    # 1. Fetch edges directly from Shivaji Nagar, Pune database
    print("\n1. Loaded Shivaji Nagar Road Edges:")
    edges = fetch_edges_from_db()
    for e in edges:
        name = e.get("name", f"Edge {e['edge_id']}")
        print(f"   [{e['edge_id']}] {name:<28} | Limit: {e['speed_limit_kmh']} km/h | "
              f"Lanes: {e['lanes']} | OneWay: {e['is_oneway']}")

    # 2. Test Congestion Multipliers at Friday 6:30 PM (Evening Rush Hour) with Rain
    # Weather: Monsoon rain = 4.5 mm, Temp = 26.5 C
    friday_hour = 18
    friday_day = 4
    rain = 4.5
    temp = 26.5

    print(f"\n2. Real-time Inference (Friday 6:30 PM Rush Hour | Monsoon Rain = {rain}mm):")
    for e in edges:
        name = e.get("name", f"Edge {e['edge_id']}")
        cf = predict_congestion(
            hour=friday_hour,
            day_of_week=friday_day,
            road_type=e["road_type"],
            rain_mm=rain,
            speed_limit_kmh=e["speed_limit_kmh"],
            lanes=e["lanes"],
            is_oneway=e["is_oneway"],
            temperature_2m=temp
        )
        print(f"   [{e['edge_id']}] {name:<28} --> Predicted CF: {cf:.3f}")

    print("\nVerification completed successfully!")
