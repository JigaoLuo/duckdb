include_directories(include)

add_definitions(-DDUCKDB_ROOT_DIRECTORY="${PROJECT_SOURCE_DIR}")

include(FetchContent)
FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/609281088cfefc76f9d0ce82e1ff6c30cc3591e5.zip
)
FetchContent_MakeAvailable(googletest)

enable_testing()


add_executable(
        art_test
        art_insert_lookup_test.cpp
        ${ALL_OBJECT_FILES}
)
target_link_libraries(
        art_test
        gtest_main
        duckdb
)

include(GoogleTest)
gtest_discover_tests(art_test)