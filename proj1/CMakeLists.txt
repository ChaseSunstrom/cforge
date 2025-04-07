cmake_minimum_required(VERSION 3.14)

project(proj1 VERSION 0.1.0 LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Set output directories
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# vcpkg integration - uncomment the line below if using vcpkg
# set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")

# Find dependencies (if using vcpkg)
# find_package(SomePackage REQUIRED)

# Add source files
file(GLOB_RECURSE SOURCES src/*.cpp)

# Define executable name with configuration suffix
string(TOLOWER "${CMAKE_BUILD_TYPE}" build_type_lower)
set(EXECUTABLE_NAME ${PROJECT_NAME}_${build_type_lower})

# Add executable
add_executable(${EXECUTABLE_NAME} ${SOURCES})

# Include directories
target_include_directories(${EXECUTABLE_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Link libraries
# target_link_libraries(${EXECUTABLE_NAME} PRIVATE SomePackage)

# Enable compiler warnings
if(MSVC)
    target_compile_options(${EXECUTABLE_NAME} PRIVATE /W4)
else()
    target_compile_options(${EXECUTABLE_NAME} PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Testing configuration
enable_testing()
option(BUILD_TESTING "Build the testing tree." ON)

if(BUILD_TESTING)
    # Add test directory
    add_subdirectory(tests)
endif()

# Installation configuration
install(TARGETS ${EXECUTABLE_NAME}
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

# Install headers
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
    DESTINATION include
    FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp"
)

# Packaging configuration with CPack
include(CPack)
set(CPACK_PACKAGE_NAME ${PROJECT_NAME})
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_NAME} - A C++ project created with cforge")
set(CPACK_PACKAGE_VENDOR "Your Organization")
set(CPACK_PACKAGE_DESCRIPTION_FILE "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")

# OS specific packaging settings
if(WIN32)
    set(CPACK_GENERATOR "ZIP;NSIS")
    set(CPACK_NSIS_PACKAGE_NAME "${PROJECT_NAME}")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
elseif(APPLE)
    set(CPACK_GENERATOR "TGZ;DragNDrop")
else()
    set(CPACK_GENERATOR "TGZ;DEB")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Your Name")
    set(CPACK_DEBIAN_PACKAGE_SECTION "devel")
endif()
