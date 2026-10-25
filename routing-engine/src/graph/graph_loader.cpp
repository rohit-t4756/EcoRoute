
/*
 * File: graph_loader.cpp
 * Purpose: Loads a versioned graph snapshot from metadata
 *          and CSV files into RoadNetwork and RoutingGraph.
 *
 * Status: IMPLEMENTED
 * Remaining: Real PostGIS export verification and large-graph benchmarks.
 */

#include "graph_loader.hpp"

#include <zlib.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <limits>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace ecoroute {

namespace {

struct SnapshotMetadata {
    unsigned schema_version = 0;
    std::string graph_version;
    std::optional<std::uint64_t> node_count;
    std::optional<std::uint64_t> edge_count;
    std::optional<std::uint32_t> nodes_crc32;
    std::optional<std::uint32_t> edges_crc32;
};

// Hash the bytes being parsed, rather than reopening a file for a
// separate checksum pass that could observe a different file version.
bool readLine(std::ifstream& input, std::string& line, uLong* checksum) {
    if (!std::getline(input, line)) {
        return false;
    }
    if (checksum) {
        std::size_t offset = 0;
        while (offset < line.size()) {
            const auto count = static_cast<uInt>(std::min(
                line.size() - offset,
                static_cast<std::size_t>(std::numeric_limits<uInt>::max())
            ));
            *checksum = crc32(*checksum,
                reinterpret_cast<const Bytef*>(line.data() + offset), count);
            offset += count;
        }
        // getline removes '\n', but leaves '\r' when opened in binary mode.
        if (!input.eof()) {
            const Bytef newline = '\n';
            *checksum = crc32(*checksum, &newline, 1);
        }
    }
    return true;
}

// --------------------------------------------------
// Basic text helpers.
// --------------------------------------------------

std::string trim(const std::string& text) {
    std::size_t first = 0;
    std::size_t last = text.size();

    while (first < last &&
           std::isspace(static_cast<unsigned char>(text[first]))) {
        ++first;
    }

    while (last > first &&
           std::isspace(static_cast<unsigned char>(text[last - 1]))) {
        --last;
    }

    return text.substr(first, last - first);
}

std::string location(
    const std::filesystem::path& file,
    std::size_t line
) {
    return file.string() + ":" + std::to_string(line);
}

// Parses one CSV record, including quoted fields and
// escaped double quotes.
//
// Multiline quoted CSV fields are not supported by
// the initial graph-export contract.
Result<std::vector<std::string>> parseCsvRow(
    const std::string& line
) {
    std::vector<std::string> fields;
    std::string current;

    bool quoted = false;
    bool after_quote = false;

    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];

        if (quoted) {
            if (c == '"') {
                // Two consecutive quotes represent one quote.
                if (i + 1 < line.size() && line[i + 1] == '"') {
                    current.push_back('"');
                    ++i;
                } else {
                    quoted = false;
                    after_quote = true;
                }
            } else {
                current.push_back(c);
            }

            continue;
        }

        if (after_quote) {
            if (c == ',') {
                fields.push_back(trim(current));
                current.clear();
                after_quote = false;
            } else if (!std::isspace(
                           static_cast<unsigned char>(c))) {
                return Result<std::vector<std::string>>::failure(
                    ErrorCode::InvalidGraph,
                    "Unexpected character after closing CSV quote."
                );
            }

            continue;
        }

        if (c == ',') {
            fields.push_back(trim(current));
            current.clear();
        } else if (c == '"') {
            if (!trim(current).empty()) {
                return Result<std::vector<std::string>>::failure(
                    ErrorCode::InvalidGraph,
                    "Unexpected quote inside unquoted CSV field."
                );
            }

            current.clear();
            quoted = true;
        } else {
            current.push_back(c);
        }
    }

    if (quoted) {
        return Result<std::vector<std::string>>::failure(
            ErrorCode::InvalidGraph,
            "Unclosed quoted CSV field."
        );
    }

    fields.push_back(trim(current));

    return Result<std::vector<std::string>>::success(
        std::move(fields)
    );
}

// Parse an unsigned 64-bit identifier.
// Rejects negative values, overflow and trailing text.
bool parseId(
    const std::string& text,
    std::uint64_t& output
) {
    std::string value = trim(text);

    if (value.empty()) {
        return false;
    }

    auto parsed = std::from_chars(
        value.data(),
        value.data() + value.size(),
        output
    );

    return parsed.ec == std::errc{} &&
           parsed.ptr == value.data() + value.size();
}

// Parse a finite floating-point number.
bool parseNumber(
    const std::string& text,
    double& output
) {
    std::string value = trim(text);

    if (value.empty()) {
        return false;
    }

    auto parsed = std::from_chars(
        value.data(),
        value.data() + value.size(),
        output,
        std::chars_format::general
    );

    return parsed.ec == std::errc{} &&
           parsed.ptr == value.data() + value.size() &&
           std::isfinite(output);
}

// Schema v2 uses the 2D WKT subset emitted by ST_AsText on a WGS84
// LineString. Longitude precedes latitude; Z/M/EMPTY and SRID prefixes
// are deliberately excluded so dimensional or CRS mistakes are errors.
Result<std::vector<Coordinate>> parseGeometry(const std::string& text) {
    const std::string value = trim(text);
    auto invalid = [] {
        return Result<std::vector<Coordinate>>::failure(
            ErrorCode::InvalidGraph, "Expected a finite 2D LINESTRING (longitude latitude,...)."
        );
    };
    if (value.empty()) {
        return Result<std::vector<Coordinate>>::success({});
    }
    if (value.compare(0, 10, "LINESTRING") != 0) {
        return invalid();
    }
    std::size_t position = 10;
    auto skipSpace = [&] {
        while (position < value.size() &&
               std::isspace(static_cast<unsigned char>(value[position]))) {
            ++position;
        }
    };
    auto number = [&](double& output) {
        const char* begin = value.data() + position;
        const auto parsed = std::from_chars(
            begin, value.data() + value.size(), output, std::chars_format::general
        );
        if (parsed.ec != std::errc{} || parsed.ptr == begin || !std::isfinite(output)) {
            return false;
        }
        position = static_cast<std::size_t>(parsed.ptr - value.data());
        return true;
    };
    skipSpace();
    if (position == value.size() || value[position++] != '(') {
        return invalid();
    }
    std::vector<Coordinate> geometry;
    while (true) {
        skipSpace();
        Coordinate point;
        if (!number(point.longitude) || position == value.size() ||
            !std::isspace(static_cast<unsigned char>(value[position]))) {
            return invalid();
        }
        skipSpace();
        if (!number(point.latitude)) {
            return invalid();
        }
        geometry.push_back(point);
        skipSpace();
        if (position == value.size()) {
            return invalid();
        }
        const char separator = value[position++];
        if (separator == ')') {
            skipSpace();
            if (position != value.size() || geometry.size() < 2) {
                return invalid();
            }
            return Result<std::vector<Coordinate>>::success(std::move(geometry));
        }
        if (separator != ',') {
            return invalid();
        }
    }
}

// --------------------------------------------------
// Verify the exact CSV header expected by schema v1.
// --------------------------------------------------

Result<void> verifyHeader(
    std::ifstream& input,
    const std::filesystem::path& file,
    const std::vector<std::string>& expected,
    uLong* checksum = nullptr
) {
    std::string line;

    if (!readLine(input, line, checksum)) {
        return Result<void>::failure(
            ErrorCode::InvalidGraph,
            "Missing CSV header: " + file.string()
        );
    }

    // Support a UTF-8 byte-order mark on the first line.
    if (line.size() >= 3 &&
        line.compare(0, 3, "\xEF\xBB\xBF") == 0) {
        line.erase(0, 3);
    }

    auto parsed = parseCsvRow(line);

    if (!parsed.ok() || parsed.value() != expected) {
        return Result<void>::failure(
            ErrorCode::InvalidGraph,
            "Unexpected CSV columns in " + file.string()
        );
    }

    return Result<void>::success();
}

// --------------------------------------------------
// Read metadata.txt.
// --------------------------------------------------

Result<SnapshotMetadata> readMetadata(
    const std::filesystem::path& file
) {
    std::ifstream input(file, std::ios::binary);

    if (!input) {
        return Result<SnapshotMetadata>::failure(
            ErrorCode::InvalidArgument,
            "Cannot open metadata file: " + file.string()
        );
    }

    bool has_schema = false;
    bool has_version = false;
    SnapshotMetadata metadata;

    std::string line;
    std::size_t line_number = 0;

    while (std::getline(input, line)) {
        ++line_number;

        if (line_number == 1 && line.compare(0, 3, "\xEF\xBB\xBF") == 0) {
            line.erase(0, 3);
        }

        line = trim(line);

        if (line.empty() || line[0] == '#') {
            continue;
        }

        std::size_t equals = line.find('=');

        if (equals == std::string::npos) {
            return Result<SnapshotMetadata>::failure(
                ErrorCode::InvalidGraph,
                "Invalid metadata entry at " +
                    location(file, line_number)
            );
        }

        std::string key = trim(line.substr(0, equals));
        std::string value = trim(line.substr(equals + 1));

        if (key == "schema_version") {
            if (has_schema || (value != "1" && value != "2")) {
                return Result<SnapshotMetadata>::failure(
                    ErrorCode::InvalidGraph,
                    "Duplicate or unsupported schema version."
                );
            }

            has_schema = true;
            metadata.schema_version = value == "1" ? 1 : 2;

        } else if (key == "graph_version") {
            if (has_version || value.empty()) {
                return Result<SnapshotMetadata>::failure(
                    ErrorCode::InvalidGraph,
                    "Duplicate or empty graph version."
                );
            }

            metadata.graph_version = value;
            has_version = true;

        } else if (key == "node_count" || key == "edge_count") {
            auto& count = key == "node_count" ? metadata.node_count : metadata.edge_count;
            std::uint64_t parsed_count = 0;
            if (count || !parseId(value, parsed_count)) {
                return Result<SnapshotMetadata>::failure(
                    ErrorCode::InvalidGraph, "Duplicate or invalid metadata count: " + key
                );
            }
            count = parsed_count;
        } else if (key == "nodes_crc32" || key == "edges_crc32") {
            auto& checksum = key == "nodes_crc32" ? metadata.nodes_crc32 : metadata.edges_crc32;
            std::uint32_t parsed_checksum = 0;
            const auto parsed = std::from_chars(
                value.data(), value.data() + value.size(), parsed_checksum, 16
            );
            if (checksum || value.size() != 8 || parsed.ec != std::errc{} ||
                parsed.ptr != value.data() + value.size()) {
                return Result<SnapshotMetadata>::failure(
                    ErrorCode::InvalidGraph, "Duplicate or invalid CRC32: " + key
                );
            }
            checksum = parsed_checksum;
        } else {
            return Result<SnapshotMetadata>::failure(
                ErrorCode::InvalidGraph,
                "Unknown metadata key: " + key
            );
        }
    }

    if (input.bad()) {
        return Result<SnapshotMetadata>::failure(
            ErrorCode::InvalidGraph,
            "Error reading metadata file."
        );
    }

    if (!has_schema || !has_version) {
        return Result<SnapshotMetadata>::failure(
            ErrorCode::InvalidGraph,
            "Missing schema_version or graph_version."
        );
    }

    const bool any_manifest = metadata.node_count || metadata.edge_count ||
        metadata.nodes_crc32 || metadata.edges_crc32;
    const bool full_manifest = metadata.node_count && metadata.edge_count &&
        metadata.nodes_crc32 && metadata.edges_crc32;
    if ((metadata.schema_version == 1 && any_manifest) ||
        (metadata.schema_version == 2 && !full_manifest)) {
        return Result<SnapshotMetadata>::failure(
            ErrorCode::InvalidGraph,
            "Schema v2 requires both counts and CRC32 checksums; v1 has no manifest."
        );
    }
    if (full_manifest &&
        (*metadata.node_count > std::numeric_limits<unsigned>::max() ||
         *metadata.edge_count > std::numeric_limits<unsigned>::max())) {
        return Result<SnapshotMetadata>::failure(
            ErrorCode::ResourceExhausted, "Manifest exceeds supported CSR index limits."
        );
    }
    return Result<SnapshotMetadata>::success(std::move(metadata));
}

// --------------------------------------------------
// Read nodes.csv.
// --------------------------------------------------

Result<std::vector<RoadNode>> readNodes(
    const std::filesystem::path& file,
    uLong* checksum
) {
    std::ifstream input(file, std::ios::binary);

    if (!input) {
        return Result<std::vector<RoadNode>>::failure(
            ErrorCode::InvalidArgument,
            "Cannot open nodes file: " + file.string()
        );
    }

    auto header = verifyHeader(
        input,
        file,
        {"node_id", "latitude", "longitude", "elevation_meters"},
        checksum
    );

    if (!header.ok()) {
        return Result<std::vector<RoadNode>>::failure(
            header.error().code,
            header.error().message
        );
    }

    std::vector<RoadNode> nodes;

    std::string line;
    std::size_t line_number = 1;

    while (readLine(input, line, checksum)) {
        ++line_number;

        if (trim(line).empty()) {
            continue;
        }

        auto parsed = parseCsvRow(line);

        if (!parsed.ok() || parsed.value().size() != 4) {
            return Result<std::vector<RoadNode>>::failure(
                ErrorCode::InvalidGraph,
                "Invalid node CSV row at " +
                    location(file, line_number)
            );
        }

        const auto& fields = parsed.value();

        std::uint64_t id = 0;
        double latitude = 0.0;
        double longitude = 0.0;

        if (!parseId(fields[0], id) ||
            !parseNumber(fields[1], latitude) ||
            !parseNumber(fields[2], longitude)) {

            return Result<std::vector<RoadNode>>::failure(
                ErrorCode::InvalidGraph,
                "Invalid node ID or coordinates at " +
                    location(file, line_number)
            );
        }

        RoadNode node;
        node.id = id;
        node.location = {latitude, longitude};

        if (!fields[3].empty()) {
            double elevation = 0.0;

            if (!parseNumber(fields[3], elevation)) {
                return Result<std::vector<RoadNode>>::failure(
                    ErrorCode::InvalidGraph,
                    "Invalid elevation at " +
                        location(file, line_number)
                );
            }

            node.elevation_meters = elevation;
        }

        if (nodes.size() == std::numeric_limits<unsigned>::max()) {
            return Result<std::vector<RoadNode>>::failure(
                ErrorCode::ResourceExhausted, "Too many nodes for CSR indices."
            );
        }
        nodes.push_back(std::move(node));
    }

    if (input.bad()) {
        return Result<std::vector<RoadNode>>::failure(
            ErrorCode::InvalidGraph,
            "Error reading nodes file."
        );
    }

    return Result<std::vector<RoadNode>>::success(
        std::move(nodes)
    );
}

// --------------------------------------------------
// Read edges.csv.
// --------------------------------------------------

Result<std::vector<RoadSegment>> readEdges(
    const std::filesystem::path& file,
    unsigned schema_version,
    uLong* checksum
) {
    std::ifstream input(file, std::ios::binary);

    if (!input) {
        return Result<std::vector<RoadSegment>>::failure(
            ErrorCode::InvalidArgument,
            "Cannot open edges file: " + file.string()
        );
    }

    std::vector<std::string> columns{
        "edge_id", "from_node_id", "to_node_id", "length_meters",
        "speed_limit_kph", "road_class"
    };
    if (schema_version == 2) {
        columns.push_back("geometry_wkt");
    }
    auto header = verifyHeader(input, file, columns, checksum);

    if (!header.ok()) {
        return Result<std::vector<RoadSegment>>::failure(
            header.error().code,
            header.error().message
        );
    }

    std::vector<RoadSegment> segments;

    std::string line;
    std::size_t line_number = 1;

    while (readLine(input, line, checksum)) {
        ++line_number;

        if (trim(line).empty()) {
            continue;
        }

        auto parsed = parseCsvRow(line);

        if (!parsed.ok() || parsed.value().size() != columns.size()) {
            return Result<std::vector<RoadSegment>>::failure(
                ErrorCode::InvalidGraph,
                "Invalid edge CSV row at " +
                    location(file, line_number)
            );
        }

        const auto& fields = parsed.value();

        std::uint64_t id = 0;
        std::uint64_t from_id = 0;
        std::uint64_t to_id = 0;
        double length = 0.0;

        if (!parseId(fields[0], id) ||
            !parseId(fields[1], from_id) ||
            !parseId(fields[2], to_id) ||
            !parseNumber(fields[3], length)) {

            return Result<std::vector<RoadSegment>>::failure(
                ErrorCode::InvalidGraph,
                "Invalid edge IDs or length at " +
                    location(file, line_number)
            );
        }

        RoadSegment segment;

        segment.id = id;
        segment.from = from_id;
        segment.to = to_id;
        segment.length_meters = length;

        // Blank speed means unknown, not zero.
        if (!fields[4].empty()) {
            double speed = 0.0;

            if (!parseNumber(fields[4], speed)) {
                return Result<std::vector<RoadSegment>>::failure(
                    ErrorCode::InvalidGraph,
                    "Invalid speed at " +
                        location(file, line_number)
                );
            }

            segment.speed_limit_kph = speed;
        }

        // Road class is preserved as raw text.
        // The cost model does not interpret it yet.
        if (!fields[5].empty()) {
            segment.road_class = fields[5];
        }

        if (schema_version == 2) {
            auto geometry = parseGeometry(fields[6]);
            if (!geometry.ok()) {
                return Result<std::vector<RoadSegment>>::failure(
                    geometry.error().code,
                    geometry.error().message + " At " + location(file, line_number)
                );
            }
            segment.geometry = std::move(geometry.value());
        }

        if (segments.size() == std::numeric_limits<unsigned>::max()) {
            return Result<std::vector<RoadSegment>>::failure(
                ErrorCode::ResourceExhausted, "Too many edges for CSR indices."
            );
        }
        segments.push_back(std::move(segment));
    }

    if (input.bad()) {
        return Result<std::vector<RoadSegment>>::failure(
            ErrorCode::InvalidGraph,
            "Error reading edges file."
        );
    }

    return Result<std::vector<RoadSegment>>::success(
        std::move(segments)
    );
}

} // namespace

// --------------------------------------------------
// Public graph snapshot loader.
// --------------------------------------------------

Result<LoadedGraphSnapshot> loadGraphSnapshot(
    const GraphSnapshotFiles& files
) try {
    // 1. Read and validate snapshot metadata.
    auto metadata = readMetadata(files.metadata_file);

    if (!metadata.ok()) {
        return Result<LoadedGraphSnapshot>::failure(
            metadata.error().code,
            metadata.error().message
        );
    }

    // 2. Parse physical nodes.
    const bool verify_integrity = metadata.value().schema_version == 2;
    uLong nodes_checksum = crc32(0L, Z_NULL, 0);
    uLong edges_checksum = crc32(0L, Z_NULL, 0);
    auto nodes = readNodes(files.nodes_file, verify_integrity ? &nodes_checksum : nullptr);

    if (!nodes.ok()) {
        return Result<LoadedGraphSnapshot>::failure(
            nodes.error().code,
            nodes.error().message
        );
    }

    // 3. Parse directed road segments.
    auto edges = readEdges(files.edges_file, metadata.value().schema_version,
                          verify_integrity ? &edges_checksum : nullptr);

    if (!edges.ok()) {
        return Result<LoadedGraphSnapshot>::failure(
            edges.error().code,
            edges.error().message
        );
    }

    if (verify_integrity &&
        (nodes.value().size() != *metadata.value().node_count ||
         edges.value().size() != *metadata.value().edge_count ||
         nodes_checksum != *metadata.value().nodes_crc32 ||
         edges_checksum != *metadata.value().edges_crc32)) {
        return Result<LoadedGraphSnapshot>::failure(
            ErrorCode::InvalidGraph, "Snapshot count or CRC32 mismatch."
        );
    }

    // 4. Let RoadNetwork validate graph structure:
    //    unique IDs, coordinates, endpoints, lengths,
    //    speed limits and other physical constraints.
    auto network = RoadNetwork::create(
        std::move(nodes.value()),
        std::move(edges.value())
    );

    if (!network.ok()) {
        return Result<LoadedGraphSnapshot>::failure(
            network.error().code,
            network.error().message
        );
    }

    // 5. Convert the validated graph into compact CSR.
    auto routing = RoutingGraph::create(network.value());

    if (!routing.ok()) {
        return Result<LoadedGraphSnapshot>::failure(
            routing.error().code,
            routing.error().message
        );
    }

    // 6. Return both representations and their version.
    LoadedGraphSnapshot snapshot{
        std::move(metadata.value().graph_version),
        std::move(network.value()),
        std::move(routing.value())
    };

    return Result<LoadedGraphSnapshot>::success(
        std::move(snapshot)
    );
} catch (const std::bad_alloc&) {
    return Result<LoadedGraphSnapshot>::failure(
        ErrorCode::ResourceExhausted, "Insufficient memory to load graph snapshot."
    );
} catch (const std::length_error&) {
    return Result<LoadedGraphSnapshot>::failure(
        ErrorCode::ResourceExhausted, "Graph snapshot exceeds container capacity."
    );
}

} // namespace ecoroute
