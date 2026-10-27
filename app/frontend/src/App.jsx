import React, { useEffect, useRef, useState } from 'react';
import Map, { Marker } from 'react-map-gl';
import MapboxGeocoder from '@mapbox/mapbox-gl-geocoder';
import mapboxgl from 'mapbox-gl';
import '@mapbox/mapbox-gl-geocoder/dist/mapbox-gl-geocoder.css';
import 'mapbox-gl/dist/mapbox-gl.css';
import './index.css';

// Public Mapbox browser token from the team's prototype; override with VITE_MAPBOX_TOKEN.
const MAPBOX_TOKEN = import.meta.env.VITE_MAPBOX_TOKEN || 'pk.eyJ1Ijoicmo3Mzc2IiwiYSI6ImNtdmxhcG80NzBjeHoyeXNpYWZzMmZnNzIifQ.RNNrLWyS_gmmG3OQZeGwog';
const API = '/api/v1/route'; // Vite forwards /api to FastAPI, including for remote browsers.
const emptyLine = { type:'Feature', geometry:{type:'LineString',coordinates:[]} };
const numbers = (m) => ({
  time:(m.duration_seconds/60).toFixed(1),
  distance:(m.distance_meters/1000).toFixed(2),
  co2:Math.round(m.co2e_grams),
  fuel:Math.round(m.fuel_milliliters),
  electricity:Math.round(m.electricity_wh),
});

function App() {
  const [viewState,setViewState]=useState({longitude:73.856,latitude:18.526,zoom:13});
  const [mapInstance,setMapInstance]=useState(null);
  const [originCoords,setOriginCoords]=useState(null);
  const [destinationCoords,setDestinationCoords]=useState(null);
  const [vehicle,setVehicle]=useState('petrol');
  const [urgency,setUrgency]=useState(50);
  const [response,setResponse]=useState(null);
  const [error,setError]=useState('');
  const [loading,setLoading]=useState(false);
  const [demo,setDemo]=useState(false);
  const originRef=useRef(null),destinationRef=useRef(null),activeRequest=useRef(null);
  const initialQuery=useRef(false);

  useEffect(() => {
    fetch('/health').then(r=>r.json()).then(data=>setDemo(data.source==='synthetic-demo')).catch(()=>{});
  },[]);

  useEffect(()=>{
    if (!mapInstance) return;
    const instances=[];
    for (const [ref,placeholder,setter] of [
      [originRef,'Search source...',setOriginCoords],
      [destinationRef,'Search destination...',setDestinationCoords],
    ]) {
      if (!ref.current) continue;
      const geocoder=new MapboxGeocoder({accessToken:MAPBOX_TOKEN,mapboxgl,placeholder,marker:false});
      geocoder.on('result',e=>setter({longitude:e.result.center[0],latitude:e.result.center[1]}));
      geocoder.on('clear',()=>setter(null));
      ref.current.appendChild(geocoder.onAdd(mapInstance));
      instances.push([ref,geocoder]);
    }
    return ()=>instances.forEach(([ref,g])=>{
      try { g.onRemove(); } catch { /* geocoder already detached */ }
      if(ref.current)ref.current.replaceChildren();
    });
  },[mapInstance]);

  // All paths represent real computed, directed road arcs. Up to three unique routes.
  useEffect(()=>{
    if(!mapInstance)return;
    for(const [id,color,width] of [
  ['fastest', '#2563EB' , 5],
  ['lowest','#16A34A' , 4],
  ['selected', '#DC2626', 6]
]){
      if(!mapInstance.getSource(id)) mapInstance.addSource(id,{type:'geojson',data:emptyLine});
      if(!mapInstance.getLayer(`${id}-line`))mapInstance.addLayer({
        id:`${id}-line`,type:'line',source:id,layout:{'line-cap':'round','line-join':'round'},
        paint:{'line-color':color,'line-width':width,'line-opacity':id==='fastest'?0.75:0.95}
      });
    }
  },[mapInstance]);

  const routes=response?.routes||[];
  const selected=routes.find(r=>r.id===response?.recommended_route_id)||routes.find(r=>r.roles?.includes('selected'));
  const fastest=routes.find(r=>r.roles?.includes('fastest'));
  const lowest=routes.find(r=>r.roles?.includes('lowest_emissions'));
  useEffect(()=>{
    if(!mapInstance)return;
    for(const [id,route] of [['fastest',fastest],['lowest',lowest],['selected',selected]]){
      const src=mapInstance.getSource(id);
      if(src)src.setData(route?.geometry?.type==='LineString'?{type:'Feature',geometry:route.geometry}:emptyLine);
    }
  },[mapInstance,response]);

  const fetchRoutes=async(preference)=>{
    if(!originCoords||!destinationCoords)return;
    activeRequest.current?.abort();
    const controller=new AbortController();activeRequest.current=controller;
    setLoading(true);setError('');
    try{
      const responseHTTP=await fetch(API,{method:'POST',headers:{'Content-Type':'application/json'},
        signal:controller.signal,
        body:JSON.stringify({start_lon:originCoords.longitude,start_lat:originCoords.latitude,
          end_lon:destinationCoords.longitude,end_lat:destinationCoords.latitude,
          vehicle_type:vehicle,preference:preference/100})});
      const data=await responseHTTP.json();
      if(!responseHTTP.ok){
        const detail=data.detail;
        throw new Error(typeof detail==='string'?detail:(detail?.message||`Routing failed (${responseHTTP.status})`));
      }
      if(!Array.isArray(data.routes))throw new Error('Backend returned an invalid route response.');
      setResponse(data);initialQuery.current=true;
    }catch(err){
      if(err.name!=='AbortError') {setResponse(null);setError(err.message||'Unable to retrieve routes.');}
    }finally{if(activeRequest.current===controller)setLoading(false);}
  };

  // Slider represents an exact preference, not the nearest already-cached physical route.
  useEffect(()=>{
    if(!initialQuery.current)return;
    const timer=setTimeout(()=>fetchRoutes(urgency),380);
    return ()=>clearTimeout(timer);
  },[urgency]);
  useEffect(()=>{
    initialQuery.current=false;setResponse(null);setError('');
    activeRequest.current?.abort();
    setLoading(false);
  },[originCoords,destinationCoords,vehicle]);

  const demoRoute=()=>{
    // Coordinates correspond to endpoints in the explicitly synthetic demo grid.
    setOriginCoords({longitude:73.844,latitude:18.513});
    setDestinationCoords({longitude:73.868,latitude:18.537});
    setViewState(s=>({...s,longitude:73.856,latitude:18.525,zoom:13.5}));
  };
  const onMapClick=e=>{
    const p={longitude:e.lngLat.lng,latitude:e.lngLat.lat};
    if(!originCoords||destinationCoords){setOriginCoords(p);setDestinationCoords(null);}
    else setDestinationCoords(p);
  };
  const metrics=selected?numbers(selected.metrics):null;
  return <div className="relative w-screen h-screen overflow-hidden">
    <Map {...viewState} onMove={e=>setViewState(e.viewState)} onLoad={e=>setMapInstance(e.target)}
         onClick={onMapClick} mapStyle="mapbox://styles/mapbox/streets-v12"
         mapboxAccessToken={MAPBOX_TOKEN} style={{width:'100%',height:'100%'}}>
      {originCoords&&<Marker longitude={originCoords.longitude} latitude={originCoords.latitude} color="#3b82f6"/>}
      {destinationCoords&&<Marker longitude={destinationCoords.longitude} latitude={destinationCoords.latitude} color="#22c55e"/>}
    </Map>
    <div className="absolute top-4 left-4 z-10 w-[min(90vw,340px)] rounded-2xl border border-white/10 bg-gray-900/95 p-5 text-white shadow-2xl">
      <h1 className="text-2xl font-bold text-green-400">EcoRoute</h1>
      <p className="mt-1 text-xs text-gray-400">Traffic-aware routes · CCH routing engine</p>
      {demo&&<p className="mt-3 rounded-lg border border-yellow-500/30 bg-yellow-500/10 p-2 text-xs text-yellow-200">Synthetic demonstration network: routes are computed, but roads are not real streets.</p>}
      <p className="mt-2 text-xs text-gray-400">Click map to select a source, then destination, or use search.</p>
      <label className="mt-4 block text-xs text-gray-300">Source</label><div className="mt-1" ref={originRef}/>
      <label className="mt-3 block text-xs text-gray-300">Destination</label><div className="mt-1" ref={destinationRef}/>
      <div className="mt-3 flex items-center justify-between text-xs text-gray-400">
        <span>{originCoords&&destinationCoords?'Two positions selected':'Select two positions'}</span>
        {demo&&<button onClick={demoRoute} className="rounded bg-white/10 px-2 py-1 text-white hover:bg-white/20">Use demo points</button>}
      </div>
      <label className="mt-4 block text-xs text-gray-300">Vehicle</label>
      <select value={vehicle} onChange={e=>setVehicle(e.target.value)} className="mt-1 w-full rounded-lg bg-white p-2 text-sm text-gray-900">
        <option value="petrol">Petrol</option><option value="diesel">Diesel</option>
        <option value="hybrid">Hybrid</option><option value="electric">Electric</option>
      </select>
      <div className="mt-5 flex justify-between text-xs text-gray-300"><span>Preference</span><span>{urgency}% toward fastest</span></div>
      <input aria-label="Time preference" type="range" min="0" max="100" value={urgency}
        onChange={e=>setUrgency(Number(e.target.value))} className="urgency-slider mt-2 w-full" />
      <div className="flex justify-between text-[11px] text-gray-400"><span>Low emissions</span><span>Fastest</span></div>
      <button onClick={()=>fetchRoutes(urgency)} disabled={!originCoords||!destinationCoords||loading}
        className="mt-5 w-full rounded-lg bg-green-500 p-3 text-sm font-semibold text-gray-950 hover:bg-green-400 disabled:opacity-40">
        {loading?'Calculating...':'Find routes'}
      </button>
      {error&&<p role="alert" className="mt-3 rounded-lg bg-red-500/15 p-2 text-xs text-red-200">{error}</p>}
    </div>
    {selected&&metrics&&<div className="absolute bottom-4 left-1/2 z-10 w-[95vw] max-w-[740px] -translate-x-1/2 rounded-2xl border border-white/10 bg-gray-900/95 p-4 text-white shadow-2xl">
      <div className="mb-2 flex flex-wrap items-center justify-between gap-2 text-sm">
        <strong className="text-green-400">Recommended route</strong>
        <span className="text-xs text-gray-400">{routes.length} unique route{routes.length===1?'':'s'} · <span className="text-gray-200">{response.traffic_version}</span></span>
      </div>
      <div className="grid grid-cols-3 gap-2 text-center sm:grid-cols-5">
        {[[metrics.time,'min'],[metrics.distance,'km'],[metrics.co2,'g CO₂e'],[metrics.fuel,'ml fuel'],[metrics.electricity,'Wh electric']].map(([value,label])=><div key={label} className="rounded-lg bg-white/5 p-2"><div className="text-lg font-semibold">{value}</div><div className="text-[11px] text-gray-400">{label}</div></div>)}
      </div>
      <div className="mt-3 flex flex-wrap items-center gap-x-6 gap-y-3 text-xs text-gray-200">

  <div className="flex items-center gap-2">
    <span
      className="inline-block h-[4px] w-8 rounded-full"
      style={{ backgroundColor: '#2563EB' }}
    />
    <span>Fastest Route</span>
  </div>

  <div className="flex items-center gap-2">
    <span
      className="inline-block h-[4px] w-8 rounded-full"
      style={{ backgroundColor: '#16A34A' }}
    />
    <span>Lowest Emissions</span>
  </div>

  <div className="flex items-center gap-2">
    <span
      className="inline-block h-[5px] w-8 rounded-full"
      style={{ backgroundColor: '#DC2626' }}
    />
    <span>Selected Route</span>
  </div>

</div>
      {selected.exceeds_detour_limit&&<p className="mt-2 text-xs text-amber-300">This preference exceeds the recommended fastest-route detour limit.</p>}
      <details className="group mt-3 border-t border-white/10 pt-3">

  <summary
    className="flex cursor-pointer list-none items-center gap-2 rounded-md
               text-xs text-gray-300 transition-colors hover:text-white
               focus-visible:outline focus-visible:outline-2
               focus-visible:outline-green-400
               [&::-webkit-details-marker]:hidden"
  >
    <span
      aria-hidden="true"
      className="flex h-4 w-4 items-center justify-center
                 rounded-full border border-gray-400
                 text-[10px] font-semibold"
    >
      i
    </span>

    <span>About these estimates</span>

    <span
      aria-hidden="true"
      className="ml-auto text-gray-400 transition-transform
                 group-open:rotate-180"
    >
      &#8964;
    </span>
  </summary>

  <div
    className="mt-3 rounded-lg border border-white/5
               bg-white/5 p-3 text-xs leading-relaxed text-gray-300"
  >
    <p>
      Travel time, energy consumption and emissions are
      model-based estimates, not verified measurements.
    </p>

    {response.warnings?.length > 0 && (
      <ul className="mt-2 list-disc space-y-2 pl-4 text-gray-400">
        {response.warnings.map((warning, index) => (
          <li key={index}>
            {warning.includes('uncalibrated')
              ? 'Fuel, energy and emissions estimates have not yet been calibrated against real-world measurements.'
              : warning.includes('turn restrictions')
              ? 'Suggested routes may not account for every turn restriction or road-access rule. Always follow road signs and traffic regulations.'
              : warning.includes('Missing traffic factors')
              ? 'Traffic estimates are unavailable for some roads, so free-flow traffic values were used.'
              : warning}
          </li>
        ))}
      </ul>
    )}
  </div>

</details>
    </div>}
  </div>;
}
export default App;
