import React, { useState, useEffect, useRef } from 'react';
import Map, { Marker } from 'react-map-gl';
import MapboxGeocoder from '@mapbox/mapbox-gl-geocoder';
import '@mapbox/mapbox-gl-geocoder/dist/mapbox-gl-geocoder.css';
import 'mapbox-gl/dist/mapbox-gl.css';
import mapboxgl from 'mapbox-gl';

// Paste your token here (or use import.meta.env.VITE_MAPBOX_TOKEN with a .env file)
const MAPBOX_TOKEN = 'pk.eyJ1Ijoicmo3Mzc2IiwiYSI6ImNtdmxhcG80NzBjeHoyeXNpYWZzMmZnNzIifQ.RNNrLWyS_gmmG3OQZeGwog';

// Backend address (uvicorn default)
const API_URL = 'http://localhost:8000/api/v1/route';

// Your dropdown values -> backend VehicleEnum values
const VEHICLE_MAP = { petrol: 'ice', diesel: 'ice', hybrid: 'hev', electric: 'ev' };

// Fetches ALL routes once. Backend format:
// { "route": [ { "1": LineString }, { "5": LineString }, { "10": LineString } ] }
// Lowest key = most eco, highest key = fastest.
async function fetchRoute(origin, dest, vehicle) {
  const res = await fetch(API_URL, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({
      start_lon: origin.longitude,
      start_lat: origin.latitude,
      end_lon: dest.longitude,
      end_lat: dest.latitude,
      vehicle_type: VEHICLE_MAP[vehicle],
      optimise_for: 'balanced', // placeholder: remove once backend no longer requires it
    }),
  });
  if (!res.ok) throw new Error(`Backend error ${res.status}`);
  const data = await res.json();

  // [{ "1": geometry }, { "5": geometry }] -> [{ value: 1, geometry }, { value: 5, geometry }]
  return (data.route ?? [])
    .map((item) => {
      const [key, geometry] = Object.entries(item)[0];
      return { value: Number(key), geometry };
    })
    .sort((a, b) => a.value - b.value);
}

function App() {
  const [viewState, setViewState] = useState({
    longitude: 73.8567,
    latitude: 18.5204,
    zoom: 12,
  });

  const [mapInstance, setMapInstance] = useState(null);

  // Pin coordinates
  const [originCoords, setOriginCoords] = useState(null);
  const [destinationCoords, setDestinationCoords] = useState(null);

  // Controls
  const [urgency, setUrgency] = useState(50);
  const [vehicle, setVehicle] = useState('petrol');

  // Route results
  const [routes, setRoutes] = useState([]); // all routes from backend, kept in memory
  const [fastestRoute, setFastestRoute] = useState(null); // gray line
  const [ecoRoute, setEcoRoute] = useState(null); // green line (selected by slider)

  // Analytics stay at zero until the real numbers are available
  const [analytics, setAnalytics] = useState({ co2: 0, fuel: 0, money: '0.00' });

  const originRef = useRef(null);
  const destinationRef = useRef(null);

  // Search boxes (geocoders)
  useEffect(() => {
    if (!mapInstance) return;

    if (originRef.current) originRef.current.innerHTML = '';
    if (destinationRef.current) destinationRef.current.innerHTML = '';

    const originGeocoder = new MapboxGeocoder({
      accessToken: MAPBOX_TOKEN,
      mapboxgl: mapboxgl,
      placeholder: 'Search source...',
      marker: false,
    });

    const destinationGeocoder = new MapboxGeocoder({
      accessToken: MAPBOX_TOKEN,
      mapboxgl: mapboxgl,
      placeholder: 'Search destination...',
      marker: false,
    });

    originGeocoder.on('result', (e) => {
      setOriginCoords({
        longitude: e.result.center[0],
        latitude: e.result.center[1],
      });
    });

    destinationGeocoder.on('result', (e) => {
      setDestinationCoords({
        longitude: e.result.center[0],
        latitude: e.result.center[1],
      });
    });

    originGeocoder.on('clear', () => setOriginCoords(null));
    destinationGeocoder.on('clear', () => setDestinationCoords(null));

    if (originRef.current) originRef.current.appendChild(originGeocoder.onAdd(mapInstance));
    if (destinationRef.current) destinationRef.current.appendChild(destinationGeocoder.onAdd(mapInstance));

    return () => {
      if (originRef.current) originRef.current.innerHTML = '';
      if (destinationRef.current) destinationRef.current.innerHTML = '';
    };
  }, [mapInstance]);

  // Create route sources and layers once the map is ready
  useEffect(() => {
    if (!mapInstance) return;
    if (mapInstance.getSource('fastest')) return; // avoid duplicates

    const empty = { type: 'Feature', geometry: { type: 'LineString', coordinates: [] } };

    mapInstance.addSource('fastest', { type: 'geojson', data: empty });
    mapInstance.addSource('eco', { type: 'geojson', data: empty });

    mapInstance.addLayer({
      id: 'fastest-line',
      type: 'line',
      source: 'fastest',
      layout: { 'line-cap': 'round', 'line-join': 'round' },
      paint: { 'line-color': '#64748b', 'line-width': 5, 'line-opacity': 0.8 },
    });
    mapInstance.addLayer({
      id: 'eco-line',
      type: 'line',
      source: 'eco',
      layout: { 'line-cap': 'round', 'line-join': 'round' },
      paint: { 'line-color': '#22c55e', 'line-width': 6 },
    });
  }, [mapInstance]);

  // Fetch all routes once, when pins or vehicle change (slider does NOT trigger this)
  useEffect(() => {
    if (!originCoords || !destinationCoords) {
      setRoutes([]);
      return;
    }
    let cancelled = false;
    const timer = setTimeout(async () => {
      try {
        const r = await fetchRoute(originCoords, destinationCoords, vehicle);
        if (!cancelled) setRoutes(r);
      } catch (err) {
        console.error('Route fetch failed', err);
      }
    }, 200);
    return () => {
      cancelled = true;
      clearTimeout(timer);
    };
  }, [originCoords, destinationCoords, vehicle]);

  // Slider picks the nearest route from memory (no API call)
  useEffect(() => {
    if (routes.length === 0) {
      setFastestRoute(null);
      setEcoRoute(null);
      return;
    }
    const min = routes[0].value;
    const max = routes[routes.length - 1].value;
    const target = min + (urgency / 100) * (max - min);
    const selected = routes.reduce((best, r) =>
      Math.abs(r.value - target) < Math.abs(best.value - target) ? r : best
    );
    setEcoRoute(selected.geometry); // green line = selected route
    setFastestRoute(routes[routes.length - 1].geometry); // gray line = fastest route
  }, [routes, urgency]);

  // Smooth update with setData (no map remount)
  useEffect(() => {
    if (!mapInstance || !mapInstance.getSource('fastest')) return;
    const toFeature = (g) => ({
      type: 'Feature',
      geometry: g || { type: 'LineString', coordinates: [] },
    });
    mapInstance.getSource('fastest').setData(toFeature(fastestRoute));
    mapInstance.getSource('eco').setData(toFeature(ecoRoute));
  }, [fastestRoute, ecoRoute, mapInstance]);

  return (
    <div className="relative w-screen h-screen overflow-hidden">
      <Map
        {...viewState}
        onMove={(evt) => setViewState(evt.viewState)}
        onLoad={(evt) => setMapInstance(evt.target)}
        mapStyle="mapbox://styles/mapbox/standard" // change to light-v11, streets-v12, dark-v11, etc.
        mapboxAccessToken={MAPBOX_TOKEN}
        style={{ width: '100%', height: '100%' }}
      >
        {/* SOURCE PIN (blue) */}
        {originCoords && (
          <Marker
            longitude={originCoords.longitude}
            latitude={originCoords.latitude}
            anchor="bottom"
            color="#3b82f6"
          />
        )}

        {/* DESTINATION PIN (green) */}
        {destinationCoords && (
          <Marker
            longitude={destinationCoords.longitude}
            latitude={destinationCoords.latitude}
            anchor="bottom"
            color="#22c55e"
          />
        )}
      </Map>

      {/* CONTROL PANEL */}
      <div className="absolute top-5 left-5 w-80 z-10 rounded-2xl bg-gray-900/85 backdrop-blur-md border border-white/10 shadow-2xl p-6">
        <div className="mb-6">
          <h1 className="text-2xl font-bold text-green-400">EcoRoute</h1>
          <p className="text-xs text-gray-400 mt-1">Greener routes, at your pace</p>
        </div>

        <div className="mb-4 relative z-30">
          <label className="flex items-center gap-2 text-xs font-medium uppercase tracking-wider text-gray-400 mb-2">
            <span className="w-2 h-2 rounded-full bg-blue-500"></span> Source
          </label>
          <div ref={originRef}></div>
        </div>

        <div className="mb-4 relative z-20">
          <label className="flex items-center gap-2 text-xs font-medium uppercase tracking-wider text-gray-400 mb-2">
            <span className="w-2 h-2 rounded-full bg-green-500"></span> Destination
          </label>
          <div ref={destinationRef}></div>
        </div>

        {/* VEHICLE SELECTOR */}
        <div className="mb-6">
          <label className="block text-xs font-medium uppercase tracking-wider text-gray-400 mb-2">
            Vehicle
          </label>
          <select
            value={vehicle}
            onChange={(e) => setVehicle(e.target.value)}
            className="w-full h-[42px] px-3 rounded-[10px] bg-white text-black text-sm outline-none cursor-pointer"
          >
            <option value="petrol">Petrol</option>
            <option value="diesel">Diesel</option>
            <option value="hybrid">Hybrid</option>
            <option value="electric">Electric (EV)</option>
          </select>
        </div>

        {/* URGENCY SLIDER */}
        <div className="pt-5 border-t border-white/10">
          <div className="flex justify-between items-center mb-3">
            <label className="text-xs font-medium uppercase tracking-wider text-gray-400">Urgency</label>
            <span className="text-xs font-semibold text-green-400">
              {urgency < 33 ? 'Eco focused' : urgency < 67 ? 'Balanced' : 'Speed focused'}
            </span>
          </div>

          <input
            type="range"
            min="0"
            max="100"
            value={urgency}
            onChange={(e) => setUrgency(Number(e.target.value))}
            className="urgency-slider w-full"
            style={{
              background: `linear-gradient(to right, #22c55e ${urgency}%, rgba(255,255,255,0.15) ${urgency}%)`,
            }}
          />

          <div className="flex justify-between text-xs text-gray-500 mt-2">
            <span>Max Eco</span>
            <span>Pure Speed</span>
          </div>
        </div>
      </div>

      {/* ANALYTICS CARD (shows as soon as both pins are set; zeros for now) */}
      {originCoords && destinationCoords && (
        <div className="absolute bottom-6 left-1/2 -translate-x-1/2 z-10 w-[92%] max-w-xl rounded-2xl bg-gray-900/85 backdrop-blur-md border border-white/10 shadow-2xl p-5">
          <div className="flex items-center justify-between mb-4">
            <h2 className="text-sm font-semibold text-green-400">Your eco impact</h2>
            <div className="flex gap-3 text-xs text-gray-400">
              <span className="flex items-center gap-1">
                <span className="w-3 h-1 rounded bg-slate-500"></span>Fastest
              </span>
              <span className="flex items-center gap-1">
                <span className="w-3 h-1 rounded bg-green-500"></span>Eco
              </span>
            </div>
          </div>
          <div className="grid grid-cols-3 gap-3 text-center">
            <div className="rounded-xl bg-white/5 py-3">
              <p className="text-xl font-bold text-white">{analytics.co2} g</p>
              <p className="text-[11px] text-gray-400 mt-1">CO₂ avoided</p>
            </div>
            <div className="rounded-xl bg-white/5 py-3">
              <p className="text-xl font-bold text-white">{analytics.fuel} ml</p>
              <p className="text-[11px] text-gray-400 mt-1">Fuel saved</p>
            </div>
            <div className="rounded-xl bg-white/5 py-3">
              <p className="text-xl font-bold text-green-400">₹{analytics.money}</p>
              <p className="text-[11px] text-gray-400 mt-1">Money saved</p>
            </div>
          </div>
        </div>
      )}
    </div>
  );
}

export default App;