if(ESP_PLATFORM)
  idf_component_register(
      SRCS "poe_json.c"
      INCLUDE_DIRS "."
  )
  return()
endif()

cmake_minimum_required(VERSION 3.16)
project(xPOE_JSON C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(poe_json STATIC poe_json.c poe_json.h)
target_include_directories(poe_json PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(poe_json PRIVATE -Wall -Wextra -Werror -pedantic)
target_link_libraries(poe_json PUBLIC m)

option(POE_JSON_BUILD_TESTS "Собирать юнит-тесты poe_json" ON)
if(POE_JSON_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
