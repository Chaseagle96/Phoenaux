cmake_minimum_required(VERSION 3.25)

project(PhoenauxDSP VERSION 0.1.0 LANGUAGES CXX)

option(PHOENAUX_BUILD_TESTS "Build PhoenauxDSP tests" ON)

add_library(PhoenauxDSP STATIC
    Sources/PhoenauxDSP/Apple/PhoenauxDSPApple.cpp
    Sources/PhoenauxDSP/PhoenauxDSP.cpp
)

target_include_directories(PhoenauxDSP
    PUBLIC
        Sources/PhoenauxDSP/include
)

target_compile_features(PhoenauxDSP PUBLIC cxx_std_20)
set_target_properties(PhoenauxDSP PROPERTIES
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
)

if(MSVC)
    target_compile_options(PhoenauxDSP PRIVATE /W4 /permissive-)
else()
    target_compile_options(PhoenauxDSP PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wshadow
    )
endif()

if(PHOENAUX_BUILD_TESTS)
    enable_testing()
    add_executable(PhoenauxDSPTests
        Tests/PhoenauxDSPTests/PhoenauxDSPTests.cpp
    )
    target_link_libraries(PhoenauxDSPTests PRIVATE PhoenauxDSP)
    add_test(NAME PhoenauxDSPTests COMMAND PhoenauxDSPTests)

    add_executable(PhoenauxDSPObjectiveTests
        Tests/PhoenauxDSPObjectiveTests/PhoenauxDSPObjectiveTests.cpp
    )
    target_link_libraries(PhoenauxDSPObjectiveTests PRIVATE PhoenauxDSP)
    add_test(NAME PhoenauxDSPObjectiveTests COMMAND PhoenauxDSPObjectiveTests)
endif()
