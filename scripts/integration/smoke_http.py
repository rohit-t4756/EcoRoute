"""Verify an already started Docker prototype using the real HTTP API."""
import json
import os
import urllib.error
import urllib.request

URL=os.getenv('ECOROUTE_API_URL','http://localhost:8000').rstrip('/')

def request(path,body=None):
    data=None if body is None else json.dumps(body).encode()
    req=urllib.request.Request(URL+path,data=data,headers={'Content-Type':'application/json'})
    with urllib.request.urlopen(req,timeout=120) as response:
        return response.status,json.load(response)

def main():
    status,health=request('/health')
    assert status==200 and health['ready']
    print('Routing health:',health['graph_version'],'traffic:',health['traffic_version'],'source:',health.get('source'))
    if health.get('source')!='synthetic-demo':
        print('Real PostGIS graph loaded; use coordinates within that graph for route verification.')
        return
    base={'start_lon':73.844,'start_lat':18.513,'end_lon':73.868,'end_lat':18.537,'preference':0.45}
    for vehicle in ('petrol','diesel','hybrid','electric'):
        status,answer=request('/api/v1/route',{**base,'vehicle_type':vehicle})
        assert status==200 and 1<=len(answer['routes'])<=3
        assert answer['recommended_route_id'] in {r['id'] for r in answer['routes']}
        for route in answer['routes']:
            assert route['geometry']['type']=='LineString'
            assert route['metrics']['duration_seconds']>=0
        print('OK',vehicle, 'route count',len(answer['routes']))
    try:request('/api/v1/route',{**base,'vehicle_type':'petrol','preference':1.2})
    except urllib.error.HTTPError as exc:
        assert exc.code==422
    else:raise AssertionError('Invalid preference was accepted')
    print('PASS: HTTP -> gRPC -> C++ routes and error handling')

if __name__=='__main__':main()
