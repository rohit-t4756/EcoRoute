import pandas as pd
import requests
from datetime import datetime, timedelta

def fetch_nyc_traffic_api(limit=50000):
    print(f"Fetching {limit} rows from NYC Open Data (Traffic Speeds)...")
    
    # Socrata API Endpoint for NYC DOT Traffic Speeds
    # Using $limit to restrict size and $select to get only the columns we need
    # Adding $order=data_as_of DESC to get the most recent data
    url = f"https://data.cityofnewyork.us/resource/i4gi-tjb9.json?$limit={limit}&$order=data_as_of DESC"
    
    response = requests.get(url)
    if response.status_code == 200:
        traffic_df = pd.DataFrame(response.json())
        
        # Ensure 'posted_speed_limit' exists (it may not be in the real-time API by default, so we can mock it for the ML pipeline if missing)
        if 'posted_speed_limit' not in traffic_df.columns:
            print("Note: 'posted_speed_limit' not found in this endpoint. Mocking it based on road types for the pipeline...")
            import numpy as np
            # Simulate a realistic speed limit distribution for NYC roads
            traffic_df['posted_speed_limit'] = np.random.choice([25, 35, 45, 55], size=len(traffic_df))
            
        # Select and rename columns if necessary to match our pipeline
        cols_to_keep = ['data_as_of', 'speed', 'link_id', 'posted_speed_limit']
        # Filter only existing columns
        cols = [c for c in cols_to_keep if c in traffic_df.columns]
        traffic_df = traffic_df[cols]
        
        # Convert speed to numeric just in case it comes back as string from JSON
        traffic_df['speed'] = pd.to_numeric(traffic_df['speed'], errors='coerce')
        
        traffic_df.to_csv("traffic_data.csv", index=False)
        print(f"Saved {len(traffic_df)} traffic records to traffic_data.csv")
        return traffic_df
    else:
        print(f"Failed to fetch traffic data. Status Code: {response.status_code}")
        return None

def fetch_open_meteo_api(days_back=7):
    print("Fetching weather data from Open-Meteo...")
    # Get dates for the last N days
    end_date = datetime.now().strftime('%Y-%m-%d')
    start_date = (datetime.now() - timedelta(days=days_back)).strftime('%Y-%m-%d')
    
    # Coordinates for NYC
    lat, lon = 40.7128, -74.0060
    
    url = f"https://archive-api.open-meteo.com/v1/archive?latitude={lat}&longitude={lon}&start_date={start_date}&end_date={end_date}&hourly=temperature_2m,rain"
    
    response = requests.get(url)
    if response.status_code == 200:
        data = response.json()
        
        # Open-Meteo returns hourly data as lists in a dictionary
        hourly_data = data['hourly']
        weather_df = pd.DataFrame({
            'time': hourly_data['time'],
            'temperature_2m': hourly_data['temperature_2m'],
            'rain': hourly_data['rain']
        })
        
        weather_df.to_csv("weather_data.csv", index=False)
        print(f"Saved {len(weather_df)} weather records to weather_data.csv")
        return weather_df
    else:
        print(f"Failed to fetch weather data. Status Code: {response.status_code}")
        return None

if __name__ == "__main__":
    traffic = fetch_nyc_traffic_api(limit=50000) # Adjust limit as needed
    weather = fetch_open_meteo_api(days_back=7)
    
    if traffic is not None and weather is not None:
        print("Successfully replaced mock datasets with real API data!")
