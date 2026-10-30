cmake_minimum_required(VERSION 3.16)
project(JuegoVoxel CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

# GLFW (ventana y teclado): CMake lo descarga solo la primera vez (necesita internet, NO necesita git)
include(FetchContent)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glfw
  URL https://github.com/glfw/glfw/archive/refs/tags/3.4.zip
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(glfw)

find_package(Threads REQUIRED)

add_executable(juego src/main.cpp src/red_sock.cpp src/mundo.h src/luz.h src/gl_loader.h src/red.h src/jugadores.h src/skin.h)
set(SUB_VAL 2 CACHE STRING "cubitos por lado de un bloque (1, 2, 4 u 8)")
target_compile_definitions(juego PRIVATE SUB_VAL=${SUB_VAL})
target_link_libraries(juego PRIVATE glfw Threads::Threads)
if(WIN32)
  target_link_libraries(juego PRIVATE ws2_32)
endif()

# prueba del mundo sin ventana (opcional)
add_executable(prueba_mundo src/prueba_mundo.cpp src/mundo.h)

if(MSVC)
  target_compile_options(juego PRIVATE /O2 /W3 /utf-8)
  target_compile_options(prueba_mundo PRIVATE /O2 /W3 /utf-8)
  set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT juego)
else()
  target_compile_options(juego PRIVATE -O2 -Wall)
endif()
