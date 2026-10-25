#include "ecoroute/engine.hpp"
#include <charconv>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <locale>
#include <map>
#include <sstream>
#include <stdexcept>

using namespace ecoroute;
namespace {
template<class T> T integer(const std::string& text) {
    T value{};
    auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || result.ec != std::errc{} || result.ptr != text.data() + text.size())
        throw std::invalid_argument("Invalid integer: " + text);
    return value;
}
double number(const std::string& text) {
    std::istringstream in(text); in.imbue(std::locale::classic());
    double value;
    if (!(in >> value) || !(in >> std::ws).eof() || !std::isfinite(value))
        throw std::invalid_argument("Invalid finite decimal: " + text);
    return value;
}
std::string word(std::istream& in) {
    std::string value;
    if (!(in >> std::quoted(value))) throw std::invalid_argument("Missing command argument.");
    return value;
}
void exhausted(std::istream& in) {
    if (!(in >> std::ws).eof()) throw std::invalid_argument("Unexpected command argument.");
}
std::string execute(Engine& engine, const std::string& line) {
    std::istringstream in(line); in.imbue(std::locale::classic());
    const auto operation = word(in);
    if (operation == "health") { exhausted(in); return engineStatusJson(engine.status()); }
    if (operation == "update") {
        const auto path = word(in); exhausted(in);
        auto updated = engine.updateTraffic(path);
        return updated.ok() ? engineStatusJson(engine.status()) : errorJson(updated.error());
    }
    if (operation != "nodes" && operation != "coordinates")
        throw std::invalid_argument("Expected nodes, coordinates, update or health.");
    RouteRequest request;
    std::uint64_t source = 0, target = 0;
    if (operation == "nodes") { source = integer<std::uint64_t>(word(in)); target = integer<std::uint64_t>(word(in)); }
    else {
        request.origin.latitude = number(word(in)); request.origin.longitude = number(word(in));
        request.destination.latitude = number(word(in)); request.destination.longitude = number(word(in));
    }
    request.vehicle_profile_id = word(in);
    request.urgency_preference = number(word(in));
    request.departure_time_unix_seconds = integer<std::int64_t>(word(in));
    request.max_routes = integer<std::uint32_t>(word(in));
    request.max_extra_time_ratio = number(word(in));
    request.max_extra_time_seconds = number(word(in));
    exhausted(in);
    auto result = operation == "nodes" ? engine.routeNodes(source, target, request) : engine.route(request);
    return result.ok() ? routeResponseJson(result.value()) : errorJson(result.error());
}
}
int main(int argc, char** argv) {
    try {
        std::map<std::string, std::string> args;
        bool serve = false, health = false;
        for (int i = 1; i < argc; ++i) {
            const std::string key = argv[i];
            if (key == "--help") {
                std::cout << "ecoroute_cli --config FILE [--health|--serve|--source ID --target ID "
                    "--vehicle ID --preference NUMBER --departure UNIX_SECONDS]\n"
                    "Coordinates: replace --source/--target with --origin-lat --origin-lon "
                    "--destination-lat --destination-lon. Optional --max-routes 1..3.\n";
                return 0;
            }
            if (key == "--serve") { serve = true; continue; }
            if (key == "--health") { health = true; continue; }
            if (key != "--config" && key != "--source" && key != "--target" && key != "--vehicle" &&
                key != "--preference" && key != "--departure" && key != "--max-routes" &&
                key != "--max-extra-time-ratio" && key != "--max-extra-time-seconds" &&
                key != "--origin-lat" && key != "--origin-lon" &&
                key != "--destination-lat" && key != "--destination-lon")
                throw std::invalid_argument("Unknown option: " + key);
            if (i + 1 == argc || !args.emplace(key, argv[++i]).second)
                throw std::invalid_argument("Missing or duplicate option: " + key);
        }
        if (!args.count("--config")) throw std::invalid_argument("--config is required.");
        if (serve && health) throw std::invalid_argument("--serve and --health are mutually exclusive.");
        auto options = loadEngineOptions(args.at("--config"));
        if (!options.ok()) { std::cout << errorJson(options.error()) << '\n'; return 2; }
        auto engine = Engine::create(options.value());
        if (!engine.ok()) { std::cout << errorJson(engine.error()) << '\n'; return 2; }
        if (health) { std::cout << engineStatusJson(engine.value().status()) << '\n'; return 0; }
        if (serve) {
            // One bounded command -> one JSON response. EOF/quit cleanly shuts down.
            while (true) {
                std::string line;
                bool too_long = false;
                char c;
                while (std::cin.get(c) && c != '\n') {
                    if (line.size() < 65536) line.push_back(c); else too_long = true;
                }
                if (line.empty() && !std::cin) break;
                if (!too_long && (line == "quit" || line == "quit\r")) break;
                try {
                    if (too_long) throw std::invalid_argument("Command exceeds 64 KiB.");
                    std::cout << execute(engine.value(), line) << '\n' << std::flush;
                } catch (const std::bad_alloc&) {
                    std::cout << errorJson({ErrorCode::ResourceExhausted, "Request exhausted memory."}) << '\n' << std::flush;
                } catch (const std::exception& e) {
                    std::cout << errorJson({ErrorCode::InvalidArgument, e.what()}) << '\n' << std::flush;
                }
            }
            return 0;
        }
        auto required = [&](const std::string& key) -> std::string {
            if (!args.count(key)) throw std::invalid_argument("Required option: " + key);
            return args.at(key);
        };
        RouteRequest request;
        request.vehicle_profile_id = required("--vehicle");
        request.urgency_preference = number(required("--preference"));
        request.departure_time_unix_seconds = integer<std::int64_t>(required("--departure"));
        if (args.count("--max-routes")) request.max_routes = integer<std::uint32_t>(args.at("--max-routes"));
        if (args.count("--max-extra-time-ratio")) request.max_extra_time_ratio = number(args.at("--max-extra-time-ratio"));
        if (args.count("--max-extra-time-seconds")) request.max_extra_time_seconds = number(args.at("--max-extra-time-seconds"));
        auto result = [&]() -> Result<RouteResponse> {
            if (args.count("--source") || args.count("--target")) {
                if (args.count("--origin-lat") || args.count("--origin-lon") ||
                    args.count("--destination-lat") || args.count("--destination-lon"))
                    throw std::invalid_argument("Choose node IDs or coordinates, not both.");
                return engine.value().routeNodes(integer<std::uint64_t>(required("--source")),
                    integer<std::uint64_t>(required("--target")), request);
            }
            request.origin = {number(required("--origin-lat")), number(required("--origin-lon"))};
            request.destination = {number(required("--destination-lat")), number(required("--destination-lon"))};
            return engine.value().route(request);
        }();
        std::cout << (result.ok() ? routeResponseJson(result.value()) : errorJson(result.error())) << '\n';
        return result.ok() ? 0 : 2;
    } catch (const std::bad_alloc&) {
        std::cout << errorJson({ErrorCode::ResourceExhausted, "CLI exhausted memory."}) << '\n'; return 2;
    } catch (const std::exception& e) {
        std::cout << errorJson({ErrorCode::InvalidArgument, e.what()}) << '\n'; return 2;
    }
}