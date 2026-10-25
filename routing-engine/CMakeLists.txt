cmake_minimum_required(VERSION 3.24)
project(EcoRouteEngine VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
include(CTest)

option(ECOROUTE_BUILD_BENCHMARKS "Build deterministic synthetic benchmarks" ON)
set(ECOROUTE_SANITIZERS "" CACHE STRING "GCC/Clang sanitizers, e.g. undefined or address,undefined")

# Build RoutingKit directly from the pinned Git submodule
set(ROUTINGKIT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/RoutingKit")

if(NOT EXISTS "${ROUTINGKIT_DIR}/include/routingkit/customizable_contraction_hierarchy.h")
    message(FATAL_ERROR
        "RoutingKit missing. Run: git submodule update --init --recursive")
endif()

find_package(ZLIB REQUIRED)
find_package(OpenMP REQUIRED COMPONENTS CXX)
find_package(Threads REQUIRED)

set(ROUTINGKIT_SOURCES
    bit_select
    bit_vector
    buffered_asynchronous_reader
    contraction_hierarchy
    customizable_contraction_hierarchy
    file_data_source
    geo_position_to_node
    google_polyline
    graph_util
    id_mapper
    nested_dissection
    osm_decoder
    osm_graph_builder
    osm_profile
    osm_simple
    protobuf
    strongly_connected_component
    timer
    vector_io
)

list(TRANSFORM ROUTINGKIT_SOURCES
    PREPEND "${ROUTINGKIT_DIR}/src/")
list(TRANSFORM ROUTINGKIT_SOURCES APPEND ".cpp")

add_library(ecoroute_routingkit STATIC ${ROUTINGKIT_SOURCES})

# Compatibility settings for RoutingKit on Windows with MinGW
if(MINGW)
    target_compile_definitions(ecoroute_routingkit PRIVATE
        ROUTING_KIT_NO_ALIGNED_ALLOC
        ROUTING_KIT_NO_POSIX
        ROUTING_KIT_ASSUME_LITTLE_ENDIAN
    )
endif()


target_include_directories(ecoroute_routingkit PUBLIC
    "${ROUTINGKIT_DIR}/include")

target_link_libraries(ecoroute_routingkit PUBLIC
    ZLIB::ZLIB
    OpenMP::OpenMP_CXX
    Threads::Threads)

add_library(RoutingKit::RoutingKit ALIAS ecoroute_routingkit)

function(ecoroute_compiler_options target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
        if(ECOROUTE_SANITIZERS)
            target_compile_options(${target} PRIVATE "-fsanitize=${ECOROUTE_SANITIZERS}" -fno-omit-frame-pointer)
            target_link_options(${target} PRIVATE "-fsanitize=${ECOROUTE_SANITIZERS}")
        endif()
    endif()
endfunction()

add_library(ecoroute_core STATIC
    src/engine.cpp
    src/graph/road_network.cpp src/graph/routing_graph.cpp src/graph/graph_loader.cpp
    src/graph/snapper.cpp
    src/cost/cost_model.cpp src/cost/traffic_snapshot.cpp src/cost/weight_builder.cpp
    src/search/dijkstra.cpp src/search/cch_adapter.cpp src/search/cch_order.cpp
    src/search/alternatives.cpp src/search/route_evaluator.cpp
    src/runtime/snapshot_manager.cpp src/runtime/configuration.cpp src/runtime/order_cache.cpp
    src/runtime/json.cpp)
target_include_directories(ecoroute_core PUBLIC "${PROJECT_SOURCE_DIR}/include"
    PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(ecoroute_core PRIVATE RoutingKit::RoutingKit Threads::Threads)
ecoroute_compiler_options(ecoroute_core)

function(ecoroute_executable target source)
    add_executable(${target} ${source})
    target_include_directories(${target} PRIVATE "${PROJECT_SOURCE_DIR}/src")
    target_link_libraries(${target} PRIVATE ecoroute_core RoutingKit::RoutingKit Threads::Threads)
    ecoroute_compiler_options(${target})
endfunction()

ecoroute_executable(ecoroute_cli apps/route_cli.cpp)
ecoroute_executable(ecoroute_preprocess apps/preprocess_main.cpp)
if(ECOROUTE_BUILD_BENCHMARKS)
    ecoroute_executable(ecoroute_bench benchmarks/bench_routes.cpp)
endif()

if(BUILD_TESTING)
    function(ecoroute_test suffix name)
        ecoroute_executable(ecoroute_test_${suffix} tests/test_${suffix}.cpp)
        target_compile_definitions(ecoroute_test_${suffix} PRIVATE
            ECOROUTE_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
        add_test(NAME ${name} COMMAND ecoroute_test_${suffix})
        set_tests_properties(${name} PROPERTIES TIMEOUT 120)
    endfunction()
    ecoroute_test(result ResultTests)
    ecoroute_test(types TypesTests)
    ecoroute_test(cch CCHTests)
    ecoroute_test(graph GraphTests)
    ecoroute_test(routing_graph RoutingGraphTests)
    ecoroute_test(dijkstra DijkstraTests)
    ecoroute_test(cch_adapter CCHAdapterTests)
    ecoroute_test(cch_order CCHOrderTests)
    ecoroute_test(cost CostTests)
    ecoroute_test(weight_builder WeightBuilderTests)
    ecoroute_test(objective_routing ObjectiveRoutingTests)
    ecoroute_test(alternatives AlternativeRoutesTests)
    ecoroute_test(graph_loader GraphLoaderTests)
    ecoroute_test(traffic_snapshot TrafficSnapshotTests)
    ecoroute_test(snapper SnapperTests)
    ecoroute_test(runtime RuntimeTests)
    ecoroute_test(engine EngineTests)
    ecoroute_test(service ServiceContractTests)
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        add_test(NAME ExporterTests COMMAND "${Python3_EXECUTABLE}"
            "${PROJECT_SOURCE_DIR}/tests/test_export_snapshot.py" --engine "$<TARGET_FILE:ecoroute_cli>")
        add_test(NAME CliIntegrationTests COMMAND "${Python3_EXECUTABLE}"
            "${PROJECT_SOURCE_DIR}/tests/test_cli.py" "$<TARGET_FILE:ecoroute_cli>"
            "$<TARGET_FILE:ecoroute_preprocess>" "${PROJECT_SOURCE_DIR}")
        set_tests_properties(ExporterTests CliIntegrationTests PROPERTIES TIMEOUT 120)
    endif()
endif()
