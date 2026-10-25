
#pragma once

/*
 * File: graph_loader.hpp
 * Purpose: Loads a versioned road-network snapshot from
 *          exported files and constructs EcoRoute's
 *          physical network and compact routing graph.
 *
 * Status: IMPLEMENTED
 * Remaining: Verification against the team's real PostGIS exports.
 *
 * Design:
 * - Graph topology is loaded as an immutable snapshot.
 * - IDs are preserved from the exported graph.
 * - CSR indices are generated internally.
 * - Traffic weights are loaded separately.
 * - Schema v2 adds directed WKT geometry and file-integrity checks.
 */

#include "ecoroute/result.hpp"

#include "graph/road_network.hpp"
#include "graph/routing_graph.hpp"

#include <filesystem>
#include <string>

namespace ecoroute {

// Paths to the three files belonging to one export.
struct GraphSnapshotFiles {
    std::filesystem::path metadata_file;
    std::filesystem::path nodes_file;
    std::filesystem::path edges_file;
};

// The fully constructed graph and its source version.
struct LoadedGraphSnapshot {

    // Identifies the immutable topology/edge-ID snapshot.
    std::string graph_version;

    // Validated physical nodes and directed segments.
    RoadNetwork network;

    // Compact CSR representation of the same network.
    RoutingGraph routing_graph;
};

// Load a road-network snapshot.
//
// Processing:
//   1. Validate file paths and metadata.
//   2. Check supported export schema version.
//   3. Parse physical road nodes.
//   4. Parse directed road segments.
//   5. Validate IDs, coordinates and edge attributes.
//   6. Construct RoadNetwork.
//   7. Construct RoutingGraph.
//   8. Return both graphs with their snapshot version.
//
// Important:
//   External segment IDs must correspond to the same
//   graph version used by traffic and geometry data.
//
//   CSV files are an interchange format, not a
//   requirement for the final deployment storage format.
//
//   No PostGIS connection is established here.
//
// Schema v1 keeps the original CSV columns. Schema v2 appends
// geometry_wkt to edges.csv and requires node_count, edge_count,
// nodes_crc32 and edges_crc32 in metadata. CRC32 is over the exact
// bytes consumed by the parser; it detects accidental corruption,
// not malicious modification. Publish immutable directories atomically.

Result<LoadedGraphSnapshot> loadGraphSnapshot(
    const GraphSnapshotFiles& files
);

} // namespace ecoroute
