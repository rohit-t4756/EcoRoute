#pragma once
#include "graph/graph_loader.hpp"
#include <filesystem>
#include <vector>

namespace ecoroute {
// Versioned nested-dissection order, bound to exact CSR ordering and coordinates.
Result<std::vector<unsigned>> loadCchOrder(const std::filesystem::path& file,
                                         const LoadedGraphSnapshot& graph);
Result<void> saveCchOrder(const std::filesystem::path& file,
                          const LoadedGraphSnapshot& graph,
                          const std::vector<unsigned>& order);
} // namespace ecoroute
