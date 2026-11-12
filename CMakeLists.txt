cmake_minimum_required(VERSION 3.5)

# Set extension name here
set(TARGET_NAME spalm)

# DuckDB's extension distribution supports vcpkg. As such, dependencies can be added in ./vcpkg.json and then
# used in cmake with find_package. Feel free to remove or replace with other dependencies.
# Note that it should also be removed from vcpkg.json to prevent needlessly installing it..
find_package(OpenSSL REQUIRED)
find_package(OpenMP REQUIRED)

# OpenMP runtime, the same as the SPALM libraries (OMP_RUNTIME in
# spalm-release/Makefile): one runtime per process.
# - iomp5 (default): compile with -fopenmp but link libiomp5 instead of
#   libgomp (libiomp5 serves the GOMP_* calls), so DuckDB, the kernels and
#   MKL's Intel threading layer share libiomp5.
# - gomp: OpenMP::OpenMP_CXX (libgomp), as in m0-m3.
set(SPALM_OMP_RUNTIME "iomp5" CACHE STRING "OpenMP runtime: iomp5 or gomp")
if(SPALM_OMP_RUNTIME STREQUAL "iomp5")
  find_library(IOMP5_LIBRARY iomp5
    HINTS "$ENV{ONEAPI_ROOT}/compiler/latest/lib" /opt/intel/oneapi/compiler/latest/lib)
  if(NOT IOMP5_LIBRARY)
    message(FATAL_ERROR "libiomp5 not found: install intel-oneapi-openmp or pass -DIOMP5_LIBRARY=/path/libiomp5.so")
  endif()
  message(STATUS "SPALM OpenMP runtime: ${IOMP5_LIBRARY}")
  set(SPALM_OMP_LINK ${IOMP5_LIBRARY})
elseif(SPALM_OMP_RUNTIME STREQUAL "gomp")
  set(SPALM_OMP_LINK OpenMP::OpenMP_CXX)
else()
  message(FATAL_ERROR "SPALM_OMP_RUNTIME must be iomp5 or gomp, not '${SPALM_OMP_RUNTIME}'")
endif()

set(EXTENSION_NAME ${TARGET_NAME}_extension)
set(LOADABLE_EXTENSION_NAME ${TARGET_NAME}_loadable_extension)

project(${TARGET_NAME})
include_directories(src/include)

set(EXTENSION_SOURCES
  src/spalm_extension.cpp
  src/physical_spalm.cpp
  src/logical_spalm.cpp
)

build_static_extension(${TARGET_NAME} ${EXTENSION_SOURCES})
build_loadable_extension(${TARGET_NAME} " " ${EXTENSION_SOURCES})

# Link OpenSSL in both the static library as the loadable extension
target_link_libraries(${EXTENSION_NAME} OpenSSL::SSL OpenSSL::Crypto ${SPALM_OMP_LINK})
target_link_libraries(${LOADABLE_EXTENSION_NAME} OpenSSL::SSL OpenSSL::Crypto ${SPALM_OMP_LINK})
if(SPALM_OMP_RUNTIME STREQUAL "iomp5")
  # -fopenmp for compiling only: on the link line g++ would add libgomp.
  target_compile_options(${EXTENSION_NAME} PRIVATE ${OpenMP_CXX_FLAGS})
  target_compile_options(${LOADABLE_EXTENSION_NAME} PRIVATE ${OpenMP_CXX_FLAGS})
endif()

install(
  TARGETS ${EXTENSION_NAME}
  EXPORT "${DUCKDB_EXPORT_SET}"
  LIBRARY DESTINATION "${INSTALL_LIB_DIR}"
  ARCHIVE DESTINATION "${INSTALL_LIB_DIR}")
