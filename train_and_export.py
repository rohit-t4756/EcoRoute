import pandas as pd
import numpy as np
from sklearn.model_selection import train_test_split
from sklearn.metrics import mean_absolute_error, mean_squared_error, r2_score
import xgboost as xgb
from onnxmltools import convert_xgboost
from onnxconverter_common.data_types import FloatTensorType

def load_and_preprocess_data(traffic_path, weather_path):
    print("Loading datasets...")
    # Load data
    traffic_df = pd.read_csv(traffic_path)
    weather_df = pd.read_csv(weather_path)

    # Convert time columns to standard pandas datetime format
    traffic_df['data_as_of'] = pd.to_datetime(traffic_df['data_as_of'])
    weather_df['time'] = pd.to_datetime(weather_df['time'])

    # Round traffic timestamps to the nearest hour for merging
    traffic_df['hour_key'] = traffic_df['data_as_of'].dt.round('h')
    
    print("Merging datasets...")
    # Merge traffic and weather data on the hourly timestamp key
    merged_df = pd.merge(
        traffic_df, 
        weather_df, 
        left_on='hour_key', 
        right_on='time', 
        how='inner'
    )

    # Clean bad data (drop NaNs, remove rows where recorded speed is <= 0)
    initial_len = len(merged_df)
    merged_df = merged_df.dropna()
    merged_df = merged_df[merged_df['speed'] > 0]
    
    print(f"Data cleaned. Dropped {initial_len - len(merged_df)} invalid rows.")
    return merged_df

def engineer_features(df):
    print("Engineering features and calculating target...")
    
    # 1. Target Calculation (y): posted_speed_limit / speed
    df['congestion_multiplier'] = df['posted_speed_limit'] / df['speed']
    # Cap extreme outliers at 5.0
    df['congestion_multiplier'] = df['congestion_multiplier'].clip(upper=5.0)
    
    # 2. Extract Time Features (X)
    df['hour'] = df['data_as_of'].dt.hour
    df['day_of_week'] = df['data_as_of'].dt.dayofweek
    
    # 3. Engineer road_type Feature (X)
    def map_road_type(limit):
        if limit <= 25:
            return 0  # Local/Residential
        elif limit <= 45:
            return 1  # Secondary
        else:
            return 2  # Primary/Highway
            
    df['road_type'] = df['posted_speed_limit'].apply(map_road_type)
    
    # 4. Filter the Final Dataset
    features_and_target = ['hour', 'day_of_week', 'road_type', 'rain', 'congestion_multiplier']
    final_df = df[features_and_target].copy()
    
    # Drop any remaining NaNs after feature engineering
    final_df = final_df.dropna()
    
    print(f"Feature engineering complete. Final dataset shape: {final_df.shape}")
    return final_df

def train_model(df):
    print("\n--- Training XGBoost Model ---")
    feature_cols = ['hour', 'day_of_week', 'road_type', 'rain']
    target_col = 'congestion_multiplier'
    
    X = df[feature_cols].astype(np.float32)
    y = df[target_col].astype(np.float32)
    
    # 80/20 train-test split
    X_train, X_test, y_train, y_test = train_test_split(
        X, y, test_size=0.20, random_state=42
    )
    print(f"Train set: {X_train.shape[0]} samples | Test set: {X_test.shape[0]} samples")
    
    # Train using NumPy arrays to ensure standard feature naming for ONNX export
    X_train_np = X_train.values.astype(np.float32)
    y_train_np = y_train.values.astype(np.float32)
    X_test_np = X_test.values.astype(np.float32)
    y_test_np = y_test.values.astype(np.float32)

    # Initialize XGBRegressor
    model = xgb.XGBRegressor(
        n_estimators=100,
        max_depth=6,
        learning_rate=0.1,
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
    
    print("\n--- Evaluation Metrics ---")
    print(f"MAE  (Mean Absolute Error):     {mae:.4f}")
    print(f"RMSE (Root Mean Squared Error):  {rmse:.4f}")
    print(f"R² Score:                       {r2:.4f}")
    
    return model

def export_to_onnx(model, output_path="traffic_model.onnx"):
    print(f"\n--- Exporting to ONNX ({output_path}) ---")
    # Define the input type: 4 float features [hour, day_of_week, road_type, rain]
    initial_type = [('float_input', FloatTensorType([None, 4]))]
    
    # Convert XGBoost model to ONNX
    onnx_model = convert_xgboost(model, initial_types=initial_type)
    
    # Serialize and save to disk
    with open(output_path, "wb") as f:
        f.write(onnx_model.SerializeToString())
    print(f"Successfully serialized and saved ONNX model to {output_path}")

if __name__ == "__main__":
    # 1. Load and merge
    merged_data = load_and_preprocess_data('traffic_data.csv', 'weather_data.csv')
    
    # 2. Engineer features
    engineered_data = engineer_features(merged_data)
    
    # 3. Train and evaluate
    model = train_model(engineered_data)
    
    # 4. Export to ONNX
    export_to_onnx(model, "traffic_model.onnx")

