"""Validate a complete, immutable ML Redis batch and publish a C++ traffic snapshot."""
import csv
import json
import math
import os
import re
import tempfile
from pathlib import Path

class TrafficBatchError(ValueError): pass

class RedisTrafficAdapter:
    def __init__(self, redis_client, graph_directory, output_directory):
        self.redis = redis_client
        self.graph_dir = Path(graph_directory)
        self.output = Path(output_directory)
        self.output.mkdir(parents=True, exist_ok=True)
        self.graph_version = dict(line.strip().split('=',1) for line in
                (self.graph_dir/'metadata.txt').read_text(encoding='utf-8').splitlines()
                if '=' in line)['graph_version']
        with (self.graph_dir/'edges.csv').open(newline='',encoding='utf-8') as fp:
            self.edge_ids = {row['edge_id'] for row in csv.DictReader(fp)}

    def prepare_latest(self):
        raw = self.redis.get('traffic:latest')
        if not raw: return None
        meta = json.loads(raw)
        version = meta.get('traffic_version','')
        if not re.fullmatch('[a-zA-Z0-9_.-]{1,100}',version):
            raise TrafficBatchError('Invalid traffic batch version')
        if meta.get('graph_version') != self.graph_version: return None
        destination = self.output/version
        if destination.is_dir(): return str(destination),version
        raw_factors = self.redis.hgetall('traffic:batch:'+version)
        if len(raw_factors) != len(self.edge_ids) or set(raw_factors) != self.edge_ids:
            raise TrafficBatchError('Incomplete or inconsistent Redis factor batch')
        factors = {}
        for k,v in raw_factors.items():
            factor = float(v)
            if not math.isfinite(factor) or factor < 1 or factor > 100:
                raise TrafficBatchError('Invalid congestion factor')
            factors[k] = factor
        lower = int(meta['valid_from_unix_seconds'])
        upper = int(meta['valid_until_unix_seconds'])
        if lower < 0 or upper <= lower:
            raise TrafficBatchError('Invalid traffic batch validity interval')
        staging = Path(tempfile.mkdtemp(prefix='.traffic-',dir=self.output))
        try:
            (staging/'metadata.txt').write_text('\n'.join([
                'schema_version=1',f'graph_version={self.graph_version}',f'traffic_version={version}',
                f'valid_from_unix_seconds={lower}',f'valid_until_unix_seconds={upper}','']),encoding='utf-8')
            with (staging/'factors.csv').open('w',newline='',encoding='utf-8') as fp:
                w=csv.writer(fp,lineterminator='\n')
                w.writerow(['edge_id','travel_time_multiplier'])
                for edge_id in sorted(self.edge_ids,key=int):w.writerow([edge_id,factors[edge_id]])
            # A batch is only visible after all files exist.
            staging.rename(destination)
        except Exception:
            import shutil
            if staging.exists():shutil.rmtree(staging)
            raise
        return str(destination),version
