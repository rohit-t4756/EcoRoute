"""Prepare an immutable C++ graph snapshot from the team's actual PostGIS schema.

If no roads are ingested yet, provide a clearly marked, synthetic Pune demo grid.
No false claim is made that synthetic segments correspond to legal real streets.
"""
from __future__ import annotations
import json
import math
import os
import sys
import subprocess
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'routing-engine' / 'scripts'))
from export_snapshot import export_snapshot

DATA = Path(os.getenv('ECOROUTE_DATA_DIR', '/data'))

def demo_graph():
    # A connected two-way synthetic network around Pune, about 3 km across.
    # It is exclusively a fallback when the team's database has no ingested roads.
    lon0, lat0, step, side = 73.840, 18.509, 0.004, 9
    nodes, edges = [], []
    for r in range(side):
        for c in range(side):
            nodes.append({'node_id': 10000 + r * side + c,
                          'longitude': round(lon0 + c * step, 7),
                          'latitude': round(lat0 + r * step, 7),
                          'elevation_meters': None})
    next_id = 100000
    for r in range(side):
        for c in range(side):
            a = 10000 + r * side + c
            for nr, nc in ((r + 1, c), (r, c + 1)):
                if nr >= side or nc >= side: continue
                b = 10000 + nr * side + nc
                category = 'arterial_roads' if (r % 3 == 0 or c % 3 == 0) else 'district_roads'
                speed = 45 if category == 'arterial_roads' else 25
                # Haversine metric distance between node positions.
                n1, n2 = nodes[a-10000], nodes[b-10000]
                lat1, lat2 = map(math.radians, (n1['latitude'], n2['latitude']))
                dlat = lat2-lat1
                dlon = math.radians(n2['longitude']-n1['longitude'])
                h = math.sin(dlat/2)**2 + math.cos(lat1)*math.cos(lat2)*math.sin(dlon/2)**2
                length = 2*6371000*math.asin(math.sqrt(h))
                for src,dst in ((a,b),(b,a)):
                    next_id += 1
                    p, q = nodes[src-10000], nodes[dst-10000]
                    edges.append({'edge_id':next_id,'from_node_id':src,'to_node_id':dst,
                                  'length_meters': length,'speed_limit_kph':speed,
                                  'road_class':category,
                                  'geometry':[[p['longitude'],p['latitude']],[q['longitude'],q['latitude']]]})
    return nodes, edges


def live_graph(dsn):
    import psycopg
    from psycopg.rows import dict_row
    with psycopg.connect(dsn, row_factory=dict_row, connect_timeout=8) as conn:
        with conn.transaction():
            conn.execute('SET TRANSACTION ISOLATION LEVEL REPEATABLE READ READ ONLY')
            counts = conn.execute(
                'SELECT (SELECT count(*) FROM nodes) AS node_count, '
                '(SELECT count(*) FROM edges) AS edge_count'
            ).fetchone()

            node_count = counts['node_count']
            edge_count = counts['edge_count']
            if not node_count or not edge_count:
                return None
            # Validate the actual teammate schema explicitly. Never invent table/column aliases.
            needed = {'nodes':{'id','location','elevation'}, 'edges':{'id','source_id','target_id','distance','speed_limit','road_category','geometry'}}
            for table,required in needed.items():
                actual = {r['column_name'] for r in conn.execute('SELECT column_name FROM information_schema.columns WHERE table_schema=current_schema() AND table_name=%s', (table,))}
                if not required <= actual:
                    raise RuntimeError(f'PostGIS table {table} is missing columns: {sorted(required-actual)}')
            nodes = list(conn.execute('''SELECT id AS node_id, ST_Y(location) AS latitude,
                                 ST_X(location) AS longitude, elevation AS elevation_meters
                                 FROM nodes ORDER BY id'''))
            # Geometry can be reversed relative to the directed OSM arc; normalize it.
            rows = list(conn.execute('''SELECT e.id AS edge_id, e.source_id AS from_node_id,
                e.target_id AS to_node_id, e.distance AS length_meters,
                e.speed_limit AS speed_limit_kph, e.road_category::text AS road_class,
                CASE WHEN e.geometry IS NULL THEN NULL
                     ELSE ST_AsGeoJSON(CASE
                         WHEN ST_Distance(ST_StartPoint(e.geometry),n1.location) <=
                              ST_Distance(ST_EndPoint(e.geometry),n1.location)
                         THEN e.geometry ELSE ST_Reverse(e.geometry) END)::json->'coordinates'
                END AS geometry
                FROM edges e JOIN nodes n1 ON n1.id=e.source_id
                JOIN nodes n2 ON n2.id=e.target_id ORDER BY e.id'''))
            # Reject non-LineStrings and mismatched endpoints in export_snapshot; don't silently corrupt paths.
            return nodes, rows


def main():
    DATA.mkdir(parents=True, exist_ok=True)
    dsn = os.getenv('ECOROUTE_POSTGIS_DSN')
    live = live_graph(dsn) if dsn else None
    if live:
        nodes, edges = live
        source = 'postgis'
    else:
        if os.getenv('ECOROUTE_ALLOW_DEMO_GRAPH','true').lower() != 'true':
            raise RuntimeError('No PostGIS road data; demo graph disabled. Ingest roads before starting routing.')
        nodes, edges = demo_graph()
        source = 'synthetic-demo'
    version = f'{source}-{int(time.time())}'
    destination = DATA / f'graph-{version}'
    export_snapshot(destination, version, nodes, edges)
    (DATA / 'active_graph.json').write_text(json.dumps({'directory':str(destination), 'graph_version':version,
                                    'source':source, 'node_count':len(nodes), 'edge_count':len(edges)}), encoding='utf-8')
    profile = ROOT / 'routing-engine' / 'config' / 'vehicle_profiles.example.csv'
    order = DATA / f'cch-order-{version}.txt'
    executable = ROOT / 'routing-engine' / 'build' / 'release' / 'ecoroute_preprocess'
    # Compute nested-dissection only once, not independently for each C++ worker.
    if executable.exists():
        subprocess.run([str(executable), str(destination), str(order)],check=True,timeout=1800)
    cfg = DATA / 'engine.conf'
    cfg.write_text('\n'.join([
        f'graph_file={destination}', f'vehicle_profiles_file={profile}',
        *([f'cch_topology_file={order}'] if order.exists() else []), 'allow_free_flow=true',
        'allow_missing_traffic=true','fallback_speed_kph=30', 'max_snap_distance_meters=1500',
        'max_cached_objectives=12', 'max_cached_routes=128',
        'reference_time_seconds=60', 'reference_co2e_grams=100', 'integer_scale=1000000',
        'max_traffic_multiplier=100', 'cost_model_version=additive-v1-uncalibrated',
        'objective_version=normalized-v1','']) , encoding='utf-8')
    print(json.dumps({'event':'graph_prepared', 'graph_source':source, 'nodes':len(nodes),
                       'edges':len(edges), 'graph_version':version}),flush=True)

if __name__=='__main__':main()
