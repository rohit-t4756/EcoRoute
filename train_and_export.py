import pandas as pd
import numpy as np

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

if __name__ == "__main__":
    # Test the pipeline so far
    df = load_and_preprocess_data('traffic_data.csv', 'weather_data.csv')
    
    # Apply feature engineering
    final_df = engineer_features(df)
    
    print("\nSample of engineered features and target:")
    print(final_df.head())
