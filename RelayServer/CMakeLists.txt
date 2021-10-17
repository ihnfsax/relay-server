cmake_minimum_required(VERSION 3.0.0)
project(RelayServer VERSION 0.1.0)

include(CTest)
enable_testing()

SET(GPROF_FLAGS "-pg")
SET(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} ${GPROF_FLAGS}")
# SET(CMAKE_EXE_LINKER_FLAGS  "${CMAKE_EXE_LINKER_FLAGS} ${GPROF_FLAGS}")

add_compile_options(-g -Wall)
aux_source_directory(. DIRSRCS)
add_executable(RelayServer ${DIRSRCS})

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)