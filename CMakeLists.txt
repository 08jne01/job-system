cmake_minimum_required (VERSION 3.20)

project(JobSystem DESCRIPTION "JobSystem")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)
FetchContent_Declare(
  MPMCQueue
  GIT_REPOSITORY https://github.com/rigtorp/MPMCQueue.git
  GIT_BRANCH master
)
FetchContent_MakeAvailable(MPMCQueue)

include(FetchContent)
FetchContent_Declare(
    etl
    GIT_REPOSITORY https://github.com/ETLCPP/etl.git
    GIT_TAG 20.48.1
)
FetchContent_MakeAvailable(etl)


#option ( TRACY_ENABLE "" ON )
FetchContent_Declare (
  tracy
  GIT_REPOSITORY https://github.com/wolfpld/tracy.git
  GIT_TAG master
  GIT_SHALLOW TRUE
  GIT_PROGRESS TRUE
)
FetchContent_MakeAvailable ( tracy )

# Executable
file(GLOB_RECURSE source_list "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c" "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp" )
file(GLOB_RECURSE headers_list "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h" "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp" )

add_executable( JobSystem ${source_list} )
target_link_libraries( JobSystem PRIVATE MPMCQueue::MPMCQueue etl Tracy::TracyClient )
#add_compile_definitions(JobSystem PRIVATE TRACY_ENABLE)

# target_compile_definitions(JobSystem PRIVATE
#   TRACY_ENABLE
#   TRACY_NO_EXIT
# )

set_property(TARGET JobSystem PROPERTY CXX_STANDARD 23)
set_property(TARGET JobSystem PROPERTY C_STANDARD 17)