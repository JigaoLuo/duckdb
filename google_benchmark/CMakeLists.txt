include_directories(include)

add_definitions(-DDUCKDB_ROOT_DIRECTORY="${PROJECT_SOURCE_DIR}")

include(FetchContent)
FetchContent_Declare(
        benchmark
        URL https://github.com/google/benchmark/archive/refs/tags/v1.5.6.zip
)
FetchContent_MakeAvailable(benchmark)
#find_package(benchmark REQUIRED)

add_executable(
        art_benchmark
        art_insert_lookup_benchmark.cpp
        ${ALL_OBJECT_FILES}
)
target_link_libraries(
        art_benchmark
        benchmark::benchmark
        duckdb
)

