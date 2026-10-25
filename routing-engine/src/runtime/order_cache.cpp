#include "runtime/order_cache.hpp"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <locale>
#include <system_error>

namespace ecoroute {
namespace {
// FNV-1a over canonical little-endian integer encodings. Corruption guard,
// not authentication. Include coordinates because the cached order uses them.
std::uint64_t fingerprint(const LoadedGraphSnapshot& graph) {
    std::uint64_t hash = 14695981039346656037ull;
    auto integer = [&](std::uint64_t value) {
        for (unsigned i = 0; i < 8; ++i) { hash ^= value & 255; hash *= 1099511628211ull; value >>= 8; }
    };
    auto real = [&](double value) {
        std::uint64_t bits;
        static_assert(sizeof(bits) == sizeof(value), "64-bit doubles required");
        std::memcpy(&bits, &value, sizeof(bits));
        integer(bits);
    };
    integer(graph.routing_graph.nodeCount());
    integer(graph.routing_graph.arcCount());
    for (auto id : graph.routing_graph.nodeIds()) {
        integer(id);
        const auto* node = graph.network.findNode(id);
        real(node->location.latitude); real(node->location.longitude);
    }
    for (std::size_t i = 0; i < graph.routing_graph.arcCount(); ++i) {
        integer(graph.routing_graph.tail()[i]); integer(graph.routing_graph.head()[i]);
        integer(graph.routing_graph.segmentIds()[i]);
    }
    return hash;
}
bool permutation(const std::vector<unsigned>& order, std::size_t count) {
    if (order.size() != count) return false;
    std::vector<bool> seen(count, false);
    for (auto node : order) {
        if (node >= count || seen[node]) return false;
        seen[node] = true;
    }
    return true;
}
} // namespace

Result<std::vector<unsigned>> loadCchOrder(const std::filesystem::path& file,
                                         const LoadedGraphSnapshot& graph) {
    using R = Result<std::vector<unsigned>>;
    std::ifstream input(file);
    input.imbue(std::locale::classic());
    if (!input) return R::failure(ErrorCode::FileError, "Cannot open CCH order cache.");
    std::string magic, version;
    std::size_t count = 0;
    std::uint64_t hash = 0;
    if (!(input >> magic >> std::quoted(version) >> count >> hash) ||
        magic != "ECOROUTE_ORDER_1" || version != graph.graph_version ||
        count != graph.routing_graph.nodeCount() || hash != fingerprint(graph)) {
        return R::failure(ErrorCode::VersionMismatch, "CCH order cache does not match graph/CSR.");
    }
    std::vector<unsigned> order(count);
    for (auto& node : order) if (!(input >> node))
        return R::failure(ErrorCode::InvalidGraph, "Truncated CCH order cache.");
    if (!(input >> std::ws).eof() || !permutation(order, count))
        return R::failure(ErrorCode::InvalidGraph, "Invalid CCH order permutation or trailing content.");
    return R::success(std::move(order));
}

Result<void> saveCchOrder(const std::filesystem::path& file,
                          const LoadedGraphSnapshot& graph,
                          const std::vector<unsigned>& order) {
    if (!permutation(order, graph.routing_graph.nodeCount()))
        return Result<void>::failure(ErrorCode::InvalidArgument, "Invalid CCH order permutation.");
    // Caller chooses a new destination. Never overwrite an existing cache.
    std::error_code error;
    if (std::filesystem::exists(file, error) || error)
        return Result<void>::failure(ErrorCode::FileError, "Order cache destination already exists or is inaccessible.");
    std::ofstream output(file);
    output.imbue(std::locale::classic());
    if (!output) return Result<void>::failure(ErrorCode::FileError, "Cannot create CCH order cache.");
    output << "ECOROUTE_ORDER_1\n" << std::quoted(graph.graph_version) << '\n'
           << order.size() << '\n' << fingerprint(graph) << '\n';
    for (auto node : order) output << node << '\n';
    output.close();
    if (!output) return Result<void>::failure(ErrorCode::FileError, "Cannot write CCH order cache.");
    return Result<void>::success();
}
} // namespace ecoroute
