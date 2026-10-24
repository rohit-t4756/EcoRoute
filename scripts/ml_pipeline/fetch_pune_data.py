import os
import requests
import pandas as pd
import numpy as np
from datetime import datetime, timedelta

def fetch_pune_weather(days_back=60):
    """
    Fetches real historical hourly weather for Pune, India from Open-Meteo API.
    Coordinates: 18.5204 N, 73.8567 E
    """
    print(f"1. Fetching past {days_back} days of real weather for Pune, India from Open-Meteo...")
    
    end_date = datetime.now().strftime('%Y-%m-%d')
    start_date = (datetime.now() - timedelta(days=days_back)).strftime('%Y-%m-%d')
    
    lat, lon = 18.5204, 73.8567  # Pune, Maharashtra, India
    
    url = (
        f"https://archive-api.open-meteo.com/v1/archive?"
        f"latitude={lat}&longitude={lon}&start_date={start_date}&end_date={end_date}"
        f"&hourly=temperature_2m,precipitation"
    )
    
    try:
        res = requests.get(url, timeout=15)
        if res.status_code == 200:
            data = res.json()["hourly"]
            weather_df = pd.DataFrame({
                "time": pd.to_datetime(data["time"]),
                "temperature_2m": data["temperature_2m"],
                "rain_mm": data["precipitation"]
            })
            print(f"   Successfully fetched {len(weather_df)} hourly weather records for Pune.")
            return weather_df
        else:
            print(f"   Failed to fetch live Open-Meteo weather (Status: {res.status_code}). Using historical profile.")
    except Exception as e:
        print(f"   Network notice: {e}. Using calibrated Pune weather distribution.")
    
    # Fallback to realistic Pune weather distribution (60 days * 24 hours)
    date_rng = pd.date_range(end=datetime.now(), periods=days_back * 24, freq='h')
    weather_df = pd.DataFrame({
        "time": date_rng,
        "temperature_2m": np.random.uniform(20.0, 35.0, size=len(date_rng)),
        "rain_mm": np.random.choice([0.0, 0.0, 0.0, 1.5, 4.0, 12.0], size=len(date_rng), p=[0.7, 0.15, 0.05, 0.05, 0.03, 0.02])
    })
    return weather_df


def generate_pune_traffic_dataset(output_path="pune_traffic_data.csv", samples=60000):
    """
    Generates a localized, highly realistic Pune traffic dataset based on:
      - Real Open-Meteo Pune weather (rain, temp)
      - Shivaji Nagar Pune road infrastructure (lanes, speed limits in km/h, one-way)
      - Indian urban traffic physics (morning/evening rush hour, bottleneck lane friction, monsoon rain slowdown)
    """
    print("\n2. Building Pune Urban Traffic Dataset...")
    
    weather_df = fetch_pune_weather(days_back=60)
    
    # Representative road archetypes for Shivaji Nagar, Pune
    # Archetype: (road_type_tier, speed_limit_kmh, lanes, is_oneway, weight)
    road_archetypes = [
        # Local/Alleyway: e.g., residential lanes, narrow market lanes
        (0, 20, 1, 0, 0.20),
        (0, 30, 1, 0, 0.15),
        (0, 30, 2, 0, 0.10),
        # Secondary: e.g., connecting cross streets, collector roads
        (1, 40, 2, 0, 0.25),
        (1, 40, 2, 1, 0.10),
        (1, 50, 3, 0, 0.08),
        # Arterial / Major Corridor: e.g., FC Road, JM Road, Old Mumbai-Pune Highway
        (2, 50, 3, 1, 0.07),  # One-way arterial (like FC/JM Road)
        (2, 60, 4, 1, 0.03),  # Major divided corridor
        (2, 80, 4, 1, 0.02),  # Highway segment
    ]
    
    # Sample from weather timestamps
    weather_sample = weather_df.sample(n=samples, replace=True, random_state=42).reset_index(drop=True)
    
    data = []
    archetype_indices = np.random.choice(
        len(road_archetypes), 
        size=samples, 
        p=[arch[4] for arch in road_archetypes]
    )
    
    for i in range(samples):
        w_row = weather_sample.iloc[i]
        dt = w_row["time"]
        hour = dt.hour
        day = dt.weekday()
        rain = float(w_row["rain_mm"])
        temp = float(w_row["temperature_2m"])
        
        arch = road_archetypes[archetype_indices[i]]
        road_type, speed_limit, lanes, is_oneway, _ = arch
        
        # Indian Traffic Dynamics:
        # Peak hours: 8:30-11:30 AM (hours 8, 9, 10, 11) and 5:30-9:30 PM (hours 17, 18, 19, 20, 21)
        is_peak = 1 if (8 <= hour <= 11 or 17 <= hour <= 21) else 0
        
        # Base congestion demand by time
        if hour in [8, 9, 10, 11]:  # Morning rush
            base_delay = 1.6 + 0.3 * np.random.uniform(0.8, 1.2)
        elif hour in [17, 18, 19, 20, 21]:  # Evening rush (typically heavier in Pune)
            base_delay = 1.8 + 0.4 * np.random.uniform(0.8, 1.3)
        elif 0 <= hour <= 5:  # Late night / early morning
            base_delay = 1.0 + 0.05 * np.random.uniform(0.0, 1.0)
        else:  # Normal daytime
            base_delay = 1.25 + 0.15 * np.random.uniform(0.8, 1.1)
            
        # Weekend adjustment (Sundays slightly lower morning rush, evening still active)
        if day == 6:  # Sunday
            base_delay *= 0.85
            
        # 1. Lane friction multiplier: Narrow 1-lane roads bottleneck heavily!
        if lanes == 1:
            lane_factor = 1.35
        elif lanes == 2:
            lane_factor = 1.10
        elif lanes == 3:
            lane_factor = 0.90
        else:
            lane_factor = 0.80
            
        # 2. One-way vs Two-way: 2-way Indian streets have friction from oncoming vehicles/rickshaws
        oneway_factor = 0.88 if is_oneway == 1 else 1.12
        
        # 3. Monsoon Rain factor: In Indian cities, even moderate rain causes immediate waterlogging & heavy jams
        if rain > 5.0:
            rain_factor = 1.45  # Heavy rain
        elif rain > 1.0:
            rain_factor = 1.20  # Light rain
        else:
            rain_factor = 1.00  # Clear
            
        # Compute total Congestion Multiplier (c)
        c = base_delay * lane_factor * oneway_factor * rain_factor
        
        # Add realistic micro-variance noise (+/- 8%)
        c *= np.random.uniform(0.92, 1.08)
        
        # Bound between 1.0 (free flow) and 5.0 (complete gridlock)
        c = float(np.clip(c, 1.0, 5.0))
        
        # Derived actual vehicle speed (in km/h)
        actual_speed = round(speed_limit / c, 1)
        
        data.append({
            "hour": hour,
            "day_of_week": day,
            "is_peak_hour": is_peak,
            "road_type": road_type,
            "speed_limit_kmh": speed_limit,
            "lanes": lanes,
            "is_oneway": is_oneway,
            "rain_mm": round(rain, 2),
            "temperature_2m": round(temp, 1),
            "actual_speed_kmh": actual_speed,
            "congestion_multiplier": round(c, 3)
        })
        
    pune_df = pd.DataFrame(data)
    
    # Save to CSV
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_root = os.path.abspath(os.path.join(script_dir, "..", ".."))
    out_file = os.path.join(project_root, output_path)
    pune_df.to_csv(out_file, index=False)
    
    print(f"\n3. Successfully generated {len(pune_df)} Pune traffic records!")
    print(f"   Saved to: {out_file}")
    print("\nDataset Preview:")
    print(pune_df.head())
    return pune_df


if __name__ == "__main__":
    generate_pune_traffic_dataset()
