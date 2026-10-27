import osmnx as ox
import geopandas as gpd
import pandas as pd
from sqlalchemy import create_engine, text
from sqlalchemy.dialects.postgresql import ENUM

class IngestMap:
    def __init__(self, place_query: str ="", radius: int = 0, network_type: str = None):
        self._place_query = place_query
        self._graph = None
        self._radius = radius
        self._network_type = network_type

    def get_graph(self):
        ox.settings.use_cache = True
        ox.settings.cache_folder = "/app/osm_cache"
        ox.settings.log_console = True

        boundary = ox.geocoder.geocode_to_gdf(
            "R10351626",
            by_osmid=True
        )

        projected = ox.projection.project_gdf(boundary)
        area_km2 = projected.geometry.area.sum() / 1_000_000

        print(f"Selected boundary area: {area_km2:.2f} km²")

        if not 300 <= area_km2 <= 330:
            raise ValueError(
                "Unexpected Pune City boundary. Import cancelled."
            )

        self._graph = ox.graph.graph_from_polygon(
            polygon=boundary.geometry.iloc[0],
            network_type="drive",
            simplify=True,
            retain_all=False
        )

        print(
            f"Downloaded {self._graph.number_of_nodes()} nodes "
            f"and {self._graph.number_of_edges()} directed edges."
        )

        return self._graph

    def get_geo_data_frames(self):
        if self._graph is None:
            self.get_graph()

        nodes_gdf, edges_gdf = ox.convert.graph_to_gdfs(
            G=self._graph,
            nodes=True,
            edges=True,
            node_geometry=True,
            fill_edge_geometry=True,
        )
        return nodes_gdf, edges_gdf

    def print_geo_info(self):
        nodes_gdf, edges_gdf = self.get_geo_data_frames()

        print("Nodes Info:")
        nodes_gdf.info()

        print("Edges Info:")
        edges_gdf.info()

    def save_to_database(self, db_url: str):
        nodes_gdf, edges_gdf = self.get_geo_data_frames()

        def clean_frame(frame):
            cleaned = frame.reset_index()
            for column in cleaned.columns:
                cleaned[column] = cleaned[column].map(
                    lambda value: ", ".join(map(str, value))
                    if isinstance(value, list)
                    else value
                )
            return cleaned

        nodes = clean_frame(nodes_gdf)
        edges = clean_frame(edges_gdf)

        node_columns = ["osmid", "geometry"]
        if "name" in nodes.columns:
            node_columns.append("name")

        elevation_column = next(
            (column for column in ("elevation", "ele") if column in nodes.columns),
            None,
        )
        if elevation_column:
            node_columns.append(elevation_column)

        # Renaming columns to match the PostGIS format
        nodes_to_save = nodes[node_columns].rename(
            columns={
                "osmid"     : "id",
                "geometry"  : "location",
                "ele"       : "elevation",
            }
        )
        nodes_to_save = gpd.GeoDataFrame(
            nodes_to_save,
            geometry        = "location",
            crs             = nodes_gdf.crs,
        )

        edges_to_save = edges[["u", "v", "length", "geometry"]].rename(
            columns={
                "u"         : "source_id",
                "v"         : "target_id",
                "length"    : "distance",
            }
        )
        
        edges_to_save = gpd.GeoDataFrame(
            edges_to_save,
            geometry        = "geometry",
            crs             = edges_gdf.crs,
        )

        # Preserve the road properties used by the ML model and C++ cost model.
        # Without this mapping every OSM road would inherit the schema's
        # district_roads / unpaved defaults, distorting both cost and inference.
        category = {
            "motorway": "express_and_highways", "motorway_link": "express_and_highways",
            "trunk": "express_and_highways", "trunk_link": "express_and_highways",
            "primary": "arterial_roads", "primary_link": "arterial_roads",
            "secondary": "arterial_roads", "secondary_link": "arterial_roads",
            "tertiary": "district_roads", "tertiary_link": "district_roads",
            "residential": "district_roads", "unclassified": "district_roads",
            "service": "alleyways", "living_street": "alleyways", "track": "rural_roads",
        }
        if "highway" in edges.columns:
            edges_to_save["road_category"] = (
                edges["highway"].astype(str).str.split(",").str[0].str.strip()
                .map(category).fillna("district_roads").to_numpy()
            )
        if "surface" in edges.columns:
            dirt = {"dirt", "earth", "ground", "mud", "sand"}
            unpaved = {"unpaved", "gravel", "fine_gravel", "compacted", "pebblestone"}
            def surface_class(value):
                kind = str(value).split(",")[0].strip().lower()
                return "dirt" if kind in dirt else ("unpaved" if kind in unpaved else "paved")
            edges_to_save["road_type"] = edges["surface"].map(surface_class).to_numpy()
        else:
            edges_to_save["road_type"] = "paved"  # unknown; provisional estimate, not verified
        if "maxspeed" in edges.columns:
            import re
            def speed_kph(value):
                value = str(value).split(",")[0].strip().lower()
                found = re.search(r"\d+(?:\.\d+)?", value)
                if not found: return None
                speed = float(found.group()) * (1.609344 if "mph" in value else 1.0)
                return round(speed) if 0 < speed <= 200 else None
            edges_to_save["speed_limit"] = pd.Series(
            [
                int(value) if pd.notna(value) else None
                for value in edges["maxspeed"].map(speed_kph)
            ],
            index=edges_to_save.index,
            dtype=object,
        )

        # .to_postgis process
        engine = create_engine(db_url)
        try:
            with engine.begin() as connection:

                existing_nodes, existing_edges = connection.execute(
                    text(
                        "SELECT "
                        "(SELECT COUNT(*) FROM nodes), "
                        "(SELECT COUNT(*) FROM edges)"
                    )
                ).one()

                if existing_nodes > 0 or existing_edges > 0:
                    raise RuntimeError(
                        "Database already contains road data. "
                        "Import cancelled to prevent duplicate records."
                    )

                if not nodes_to_save.empty:
                    nodes_to_save.to_postgis(
                        name        = "nodes",
                        con         = connection,
                        if_exists   = "append",
                        index       = False,
                    )

                if not edges_to_save.empty:
                    first_edge_id = connection.execute(
                        text("SELECT COALESCE(MAX(id), 0) + 1 FROM edges")
                    ).scalar_one()
                    
                    edges_to_save.insert(
                        0,
                        "id",
                        range(first_edge_id, first_edge_id + len(edges_to_save)),
                    )
                    
                    # Using .to_postgis instead of the .to_sql for the edges
                    edges_to_save.to_postgis(
                        name        = "edges",
                        con         = connection,
                        if_exists   = "append",
                        index       = False,
                        # PostgreSQL schema uses named enum types, not TEXT.
                        dtype = {
                            "road_category": ENUM("express_and_highways", "district_roads", "rural_roads",
                                                  "arterial_roads", "alleyways", name="road_cat", create_type=False),
                            "road_type": ENUM("paved", "unpaved", "dirt", name="road_t", create_type=False),
                        },
                    )
        finally:
            engine.dispose()

if __name__ == "__main__":
    import os

    map_ingester = IngestMap(
        place_query=os.getenv(
            "INGEST_ADDRESS", "Pune, Maharashtra, India"
        ),
        network_type="drive",
    )

    db_url = os.getenv("DATABASE_URL")
    if not db_url:
        raise SystemExit(
            "Set DATABASE_URL to the PostGIS connection URL."
        )

    # Optional boundary preview
    if os.getenv("INGEST_PREVIEW_ONLY", "false").lower() == "true":
        boundary = ox.geocoder.geocode_to_gdf(
            "R10351626",
            by_osmid=True
        )

        projected = ox.projection.project_gdf(boundary)
        area_km2 = projected.geometry.area.sum() / 1_000_000

        print("Resolved location:")
        print(boundary["display_name"].to_string(index=False))
        print(f"Boundary area: {area_km2:.2f} km^2")
        print(
            f"Geometry type: "
            f"{boundary.geometry.iloc[0].geom_type}"
        )

        raise SystemExit(0)

    # Actual road-network import
    print("Downloading map from OSM...", flush=True)
    map_ingester.get_graph()

    print("Saving to PostGIS...", flush=True)
    map_ingester.save_to_database(db_url=db_url)

    print("Ingestion complete. Saved to db.", flush=True)