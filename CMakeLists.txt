cmake_minimum_required(VERSION 3.25)
project(doxidown
  VERSION 0.10.0
  DESCRIPTION "Modern documentation tool for C++"
)
cmake_policy(SET CMP0074 NEW) # use <PackageName>_ROOT to look for packages

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# try "Config" mode before "Module" mode
set(CMAKE_FIND_PACKAGE_PREFER_CONFIG ON)
list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

find_package(yaml REQUIRED)
find_package(tree-sitter-cuda REQUIRED)
find_package(CLI11 REQUIRED)
find_package(p-ranav-glob REQUIRED)

try_compile(COMPILE_RESULT
  SOURCES ${PROJECT_SOURCE_DIR}/cmake/extract_ts_ids.cpp
  CMAKE_FLAGS
    "-DINCLUDE_DIRECTORIES=${PROJECT_SOURCE_DIR}/doxydown/doxydown"
  LINK_LIBRARIES tree-sitter-cuda::tree-sitter-cuda
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED TRUE
  OUTPUT_VARIABLE COMPILE_LOG
  COPY_FILE
    "${CMAKE_BINARY_DIR}/generated/extract_ts_ids${CMAKE_EXECUTABLE_SUFFIX}"
)
if(NOT COMPILE_RESULT)
  message(FATAL_ERROR "Compilation failed with code '${COMPILE_RESULT}'"
  " and log\n--------\n${COMPILE_LOG}\n--------\n")
endif()

foreach(VAR IN ITEMS QueryCppNodes QueryCppExcludeNodes QueryCppIncludeNodes)
  execute_process(
    COMMAND
      "${CMAKE_BINARY_DIR}/generated/extract_ts_ids${CMAKE_EXECUTABLE_SUFFIX}"
        ${VAR}
    RESULT_VARIABLE RUN_RESULT
    OUTPUT_VARIABLE ${VAR}_Code
  )
  if(RUN_RESULT)
    message(FATAL_ERROR "Run failed with code '${RUN_RESULT}'"
      " and stdout\n--------\n${${VAR}_Code}\n--------\n")
  endif()
endforeach()

configure_file(
  ${CMAKE_SOURCE_DIR}/doxydown/doxydown/cpp_query_enums.hpp.in
  ${CMAKE_BINARY_DIR}/generated/doxydown/config/cpp_query_enums.hpp
)
configure_file(
  ${CMAKE_SOURCE_DIR}/doxydown/doxydown/config.h.in
  ${CMAKE_BINARY_DIR}/generated/doxydown/config/config.h
)

add_executable(doxydown
  doxydown/doxydown/doxydown.cpp
  doxydown/doxydown/CppParser.cpp
  doxydown/doxydown/Driver.cpp
  doxydown/doxydown/DriverConfigParser.cpp
  doxydown/doxydown/FileSystemDeleter.cpp
  doxydown/doxydown/FileSystemStatsInfo.cpp
  doxydown/doxydown/FileUtils.cpp
  doxydown/doxydown/GcovCounter.cpp
  doxydown/doxydown/JSONCounter.cpp
  doxydown/doxydown/JSONGenerator.cpp
  doxydown/doxydown/SourceWatcher.cpp
  doxydown/doxydown/StyleOption.cpp
  doxydown/doxydown/TextLineCursor.cpp
  doxydown/doxydown/Utils.cpp
  doxydown/doxydown/YAMLNode.cpp
  doxydown/doxydown/YAMLParser.cpp
  doxydown/doxydown/doc_parser/DocToken.cpp
  doxydown/doxydown/doc_parser/DocTokenizer.cpp
  doxydown/doxydown/doc_parser/StyleParsers.cpp
  doxydown/doxydown/generation/DocRenderers.cpp
  doxydown/doxydown/generation/MarkdownGenerator.cpp
  doxydown/doxydown/generation/MdCoverageGenerator.cpp
  doxydown/doxydown/generation/Sanitize.cpp
)
target_include_directories(doxydown
  PRIVATE doxydown ${CMAKE_BINARY_DIR}/generated
)

target_link_libraries(doxydown
  yaml tree-sitter-cuda::tree-sitter-cuda
  CLI11::CLI11 p-ranav-glob::p-ranav-glob
)

include(GNUInstallDirs)
install(TARGETS doxydown RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
