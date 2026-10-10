import osmnx as ox
import geopandas as gpd

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