cmake_minimum_required(VERSION 3.0.0)
project(RelayServer VERSION 0.1.0)

include(CTest)
enable_testing()

aux_source_directory(common COMMON_SRC)

list(APPEND EXE_DIR RelayServer PressureGenerator)

SET(GPROF_FLAGS "-pg")
SET(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} ${GPROF_FLAGS}")
add_compile_options(-g -Wall)

foreach(DIR ${EXE_DIR})
    aux_source_directory(${DIR} ${DIR})
    add_executable(${DIR} ${COMMON_SRC} ${${DIR}})
endforeach(DIR ${EXE_DIR})

# aux_source_directory(RelayServer EXE_SRC1)
# add_executable(RelayServer ${COMMON_SRC} ${EXE_SRC1})

# aux_source_directory(PressureGenerator EXE_SRC2)
# add_executable(PressureGenerator ${COMMON_SRC} ${EXE_SRC2})

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)