cmake_minimum_required(VERSION 3.1)
project(GLcompute-demo)
include_directories(deps)
set(CMAKE_C_STANDARD 14)
set(CMAKE_CXX_STANDARD 14)
add_compile_definitions(SOKOL_GLCORE33)
add_executable(demo 
	deps/imgui_app.cpp
	deps/glad.c
	main.cpp)
