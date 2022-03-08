cmake_minimum_required(VERSION 3.12)
project(TestLibArchive)

set(CMAKE_CXX_STANDARD 17)

add_executable(TestLibArchive src/main.cpp src/Archive.cpp)

find_package(LibArchive REQUIRED)
find_package(Vulkan REQUIRED)
target_link_libraries(TestLibArchive PRIVATE ${LibArchive_LIBRARIES})
target_link_libraries(TestLibArchive PRIVATE Vulkan::Headers)
target_include_directories(TestLibArchive PRIVATE ${LibArchive_INCLUDE_DIRS})
