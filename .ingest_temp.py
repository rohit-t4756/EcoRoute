import runpy
import osmnx as ox

ox.settings.overpass_url = "https://overpass.kumi.systems/api"

path = "scripts/database/ingest_map.py"
with open(path, encoding="utf-8") as f:
    source = f.read()

source = source.replace("@localhost:", "@host.docker.internal:")

exec(compile(source, path, "exec"), {"__name__": "__main__", "__file__": path})
