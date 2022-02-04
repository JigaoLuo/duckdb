include_directories(include)
include_directories(../third_party/perfevent)
include_directories(../third_party/zipf)

add_definitions(-DDUCKDB_ROOT_DIRECTORY="${PROJECT_SOURCE_DIR}")

add_executable(
        art_standalone
        art_standalone.cpp
        ${ALL_OBJECT_FILES}
)
target_link_libraries(
        art_standalone
        duckdb
)
