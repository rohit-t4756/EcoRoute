import osmnx as ox
import geopandas as gpd
import pandas as pd
from sqlalchemy import create_engine, text

class IngestMap:
    def __init__(self, place_query: str ="", radius: int = 0, network_type: str = None):
        self._place_query = place_query
        self._graph = None
        self._radius = radius
        self._network_type = network_type

    def get_graph(self):
        if not self._place_query:
            raise ValueError("Place query cannot be empty.")

        self._graph = ox.graph.graph_from_address(
            address=self._place_query,
            dist=self._radius,
            network_type=self._network_type or "drive",
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

        if "maxspeed" in edges.columns:
            numeric_speeds = pd.to_numeric(
                edges["maxspeed"], 
                errors="coerce"
            )
            edges_to_save["speed_limit"] = numeric_speeds.round().astype("Int64")

        # .to_postgis process
        engine = create_engine(db_url)
        try:
            with engine.begin() as connection:
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
                    )
        finally:
            engine.dispose()

if __name__ == "__main__":
    map_ingester = IngestMap(
        place_query = "Shivaji Nagar, Pune, India",
        radius = 2000,
        network_type = "drive",
    )

    print("Downloading map from OSM...")
    map_ingester.get_graph()

    print("Saving to PostGIS...")
    db_url = "postgresql+psycopg2://my_ADMIN:helloEcoroute@localhost:5432/ecorouteDB"
    map_ingester.save_to_database(db_url = db_url)

    print("Ingestion complete. Saved to db.")