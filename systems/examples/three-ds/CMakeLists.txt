cmake_minimum_required(VERSION 3.25)
project(BergamotThreeDSConsumer LANGUAGES CXX OBJCXX)
set(BERGAMOT_THREEDS_SOURCE "${CMAKE_CURRENT_LIST_DIR}/../../3ds" CACHE PATH "Standalone 3DS SDK source")
add_subdirectory("${BERGAMOT_THREEDS_SOURCE}" bergamot)
add_executable(independent-three-ds main.cpp)
target_link_libraries(independent-three-ds PRIVATE Bergamot::ThreeDS)

# The build-tree consumer finds the core without requiring environment overrides.
get_filename_component(BERGAMOT_CORE_DIRECTORY "${BERGAMOT_THREEDS_SOURCE}/../../build/macos/bin/Release" ABSOLUTE)
set_target_properties(independent-three-ds PROPERTIES BUILD_RPATH "${BERGAMOT_CORE_DIRECTORY}")
