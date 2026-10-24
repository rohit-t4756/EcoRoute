import os
import pandas as pd
import numpy as np
from sklearn.model_selection import train_test_split
from sklearn.metrics import mean_absolute_error, mean_squared_error, r2_score
import xgboost as xgb
from onnxmltools import convert_xgboost
from onnxconverter_common.data_types import FloatTensorType

def load_pune_data(csv_path="pune_traffic_data.csv"):
    """
    Loads the Pune traffic dataset. If not found, generates it automatically.
    """
    if not os.path.exists(csv_path):
        from scripts.ml_pipeline.fetch_pune_data import generate_pune_traffic_dataset
        print(f"{csv_path} not found. Generating fresh Pune traffic data...")
        df = generate_pune_traffic_dataset(output_path=csv_path)
    else:
        print(f"Loading Pune traffic dataset from {csv_path}...")
        df = pd.read_csv(csv_path)
    
    print(f"Loaded {len(df)} records.")
    return df

def train_pune_model(df):
    print("\n--- Training Pune XGBoost Congestion Model ---")
    
    # 9 Enhanced Features (Temporal + Spatial/Road + Environmental)
    feature_cols = [
        'hour', 
        'day_of_week', 
        'is_peak_hour', 
        'road_type', 
        'speed_limit_kmh', 
        'lanes', 
        'is_oneway', 
        'rain_mm', 
        'temperature_2m'
    ]
    target_col = 'congestion_multiplier'
    
    X = df[feature_cols].astype(np.float32)
    y = df[target_col].astype(np.float32)
    
    # 80/20 train-test split
    X_train, X_test, y_train, y_test = train_test_split(
        X, y, test_size=0.20, random_state=42
    )
    print(f"Train set: {X_train.shape[0]} samples | Test set: {X_test.shape[0]} samples")
    
    # Use NumPy arrays to ensure standard feature naming for ONNX export
    X_train_np = X_train.values.astype(np.float32)
    y_train_np = y_train.values.astype(np.float32)
    X_test_np = X_test.values.astype(np.float32)
    y_test_np = y_test.values.astype(np.float32)

    # Initialize XGBRegressor with tuned hyper-parameters
    model = xgb.XGBRegressor(
        n_estimators=150,
        max_depth=6,
        learning_rate=0.08,
        subsample=0.85,
        colsample_bytree=0.85,
        random_state=42,
        objective='reg:squarederror'
    )
    
    model.fit(X_train_np, y_train_np)
    print("Training finished.")
    
    # Evaluate model
    y_pred = model.predict(X_test_np)
    mae = mean_absolute_error(y_test_np, y_pred)
    mse = mean_squared_error(y_test_np, y_pred)
    rmse = np.sqrt(mse)
    r2 = r2_score(y_test_np, y_pred)
    
    print("\n--- Pune Model Evaluation Metrics ---")
    print(f"MAE  (Mean Absolute Error):     {mae:.4f}")
    print(f"RMSE (Root Mean Squared Error):  {rmse:.4f}")
    print(f"R² Score:                       {r2:.4f}")
    
    return model, feature_cols

def export_to_onnx(model, num_features=9, output_paths=None):
    if output_paths is None:
        output_paths = [
            os.path.join("app", "ml", "traffic_model.onnx"),
            "traffic_model.onnx"
        ]
        
    print(f"\n--- Exporting to ONNX ({num_features} features) ---")
    initial_type = [('float_input', FloatTensorType([None, num_features]))]
    onnx_model = convert_xgboost(model, initial_types=initial_type)
    
    for path in output_paths:
        os.makedirs(os.path.dirname(path) if os.path.dirname(path) else ".", exist_ok=True)
        with open(path, "wb") as f:
            f.write(onnx_model.SerializeToString())
        print(f"Successfully serialized and saved ONNX model to {path}")

if __name__ == "__main__":
    # 1. Load Pune dataset
    pune_data = load_pune_data("pune_traffic_data.csv")
    
    # 2. Train model
    model, features = train_pune_model(pune_data)
    
    # 3. Export to ONNX
    export_to_onnx(model, num_features=len(features))
