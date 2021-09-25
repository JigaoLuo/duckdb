include_directories(include)

add_definitions(-DDUCKDB_ROOT_DIRECTORY="${PROJECT_SOURCE_DIR}")

include(FetchContent)
FetchContent_Declare(
        googlebenchmark
        URL https://github.com/google/benchmark/archive/refs/tags/v1.6.0.zip
)
FetchContent_MakeAvailable(googlebenchmark)
find_package(benchmark REQUIRED)

add_executable(
        hello_benchmark
        test.cpp
        ${ALL_OBJECT_FILES}
)
target_link_libraries(
        hello_benchmark
        benchmark::benchmark
)

