
cmake_minimum_required(VERSION 2.8)
project(nginx_learning)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_USE_RELATIVE_PATHS ON)
include_directories(${PROJECT_SOURCE_DIR}/src/core)
include_directories(${PROJECT_SOURCE_DIR}/src/event)
include_directories(${PROJECT_SOURCE_DIR}/src/http)
include_directories(${PROJECT_SOURCE_DIR}/src/mail)
include_directories(${PROJECT_SOURCE_DIR}/src/misc)
include_directories(${PROJECT_SOURCE_DIR}/src/os/unix)

aux_source_directory(${PROJECT_SOURCE_DIR}/src/core SOURCE_FILES)
aux_source_directory(${PROJECT_SOURCE_DIR}/src/event SOURCE_FILES)
aux_source_directory(${PROJECT_SOURCE_DIR}/src/event/modules SOURCE_FILES)
aux_source_directory(${PROJECT_SOURCE_DIR}/src/http SOURCE_FILES)
aux_source_directory(${PROJECT_SOURCE_DIR}/src/http/modules SOURCE_FILES)
aux_source_directory(${PROJECT_SOURCE_DIR}/src/mail SOURCE_FILES)
aux_source_directory(${PROJECT_SOURCE_DIR}/src/misc SOURCE_FILES)
aux_source_directory(${PROJECT_SOURCE_DIR}/src/os/unix SOURCE_FILES)

add_executable(nginx_learning ${SOURCE_FILES})




