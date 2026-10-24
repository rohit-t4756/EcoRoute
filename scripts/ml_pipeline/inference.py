import os
import numpy as np
import onnxruntime as ort

class TrafficCongestionPredictor:
    """
    Production helper class to load traffic_model.onnx and perform
    ultra-fast, low-latency inference using ONNX Runtime.
    """
    def __init__(self, model_path: str = "traffic_model.onnx"):
        if not os.path.exists(model_path):
            raise FileNotFoundError(
                f"ONNX model file not found at '{model_path}'. "
                "Ensure train_and_export.py has been run to generate the model."
            )
        
        # Optimize session options for production inference
        options = ort.SessionOptions()
        options.log_severity_level = 3  # Suppress informational warnings
        options.graph_optimization_level = ort.GraphOptimizationLevel.ORT_ENABLE_ALL
        
        self.session = ort.InferenceSession(model_path, sess_options=options)
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
        
        Parameters:
            features (np.ndarray): 2D array of shape (N, 4) with columns
                                   [hour, day_of_week, road_type, rain_mm]
        Returns:
            np.ndarray: 1D array of shape (N,) containing predicted multipliers
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
    
    Accepts inputs and returns the predicted congestion multiplier from traffic_model.onnx.
    """
    predictor = get_predictor()
    return predictor.predict(hour, day_of_week, road_type, rain_mm)


if __name__ == "__main__":
    print("=" * 60)
    print("EcoRoute ONNX Inference Engine Verification")
    print("=" * 60)

    # 1. Test single prediction
    print("\n1. Testing Single Prediction:")
    # Friday (4), 5 PM (17), Highway (2), Heavy Rain (5.0 mm)
    pred_rush_rain = predict_congestion(hour=17, day_of_week=4, road_type=2, rain_mm=5.0)
    print(f"   Friday 5 PM | Highway | Heavy Rain (5mm)  --> Congestion Factor: {pred_rush_rain:.3f}")

    # Sunday (6), 3 AM (3), Highway (2), Clear Weather (0.0 mm)
    pred_night_clear = predict_congestion(hour=3, day_of_week=6, road_type=2, rain_mm=0.0)
    print(f"   Sunday 3 AM | Highway | Clear (0mm)        --> Congestion Factor: {pred_night_clear:.3f}")

    # Monday (0), 8 AM (8), Residential (0), Light Rain (1.5 mm)
    pred_morning_local = predict_congestion(hour=8, day_of_week=0, road_type=0, rain_mm=1.5)
    print(f"   Monday 8 AM | Residential | Light Rain (1.5mm) --> Congestion Factor: {pred_morning_local:.3f}")

    # 2. Test batch prediction (vectorized across multiple road segments)
    print("\n2. Testing Batch Prediction (Simulating road network):")
    sample_edges = np.array([
        [17, 4, 0, 5.0],  # Edge 1: Local road, Friday evening rain
        [17, 4, 1, 5.0],  # Edge 2: Secondary road, Friday evening rain
        [17, 4, 2, 5.0],  # Edge 3: Highway, Friday evening rain
        [3,  6, 2, 0.0],  # Edge 4: Highway, Sunday night clear
    ], dtype=np.float32)

    predictor = get_predictor()
    batch_predictions = predictor.predict_batch(sample_edges)
    
    for i, cf in enumerate(batch_predictions):
        print(f"   Edge {i + 1} features {sample_edges[i]} --> Predicted CF: {cf:.3f}")

    print("\nInference verification completed successfully!")
