project(rime-witogram)
cmake_minimum_required(VERSION 3.10)

option(BUILD_TOOLS "Build tools" ON)

# Add KenLM
set(COMPILE_TESTS OFF CACHE BOOL "Don't build KenLM tests" FORCE)
set(COMPILE_TOOLS OFF CACHE BOOL "Don't build KenLM tools" FORCE)
set(KENLM_MAX_ORDER 16 CACHE STRING "Maximum supported ngram order")
add_subdirectory(third_party/kenlm)

# Add SentencePiece
set(SPM_ENABLE_SHARED OFF CACHE BOOL " " FORCE)
set(SPM_BUILD_TEST OFF CACHE BOOL " " FORCE)
set(SPM_PROTOBUF_PROVIDER "internal" CACHE STRING " ")
set(SPM_ABSL_PROVIDER "module" CACHE STRING " ")
add_subdirectory(third_party/sentencepiece)

aux_source_directory(src witogram_src)

add_library(rime-witogram-objs OBJECT ${witogram_src})
if(BUILD_SHARED_LIBS)
  set_target_properties(rime-witogram-objs
    PROPERTIES
    POSITION_INDEPENDENT_CODE ON)
endif()

target_include_directories(rime-witogram-objs PRIVATE src third_party/kenlm third_party/sentencepiece/src)

set(plugin_name rime-witogram PARENT_SCOPE)
set(plugin_objs $<TARGET_OBJECTS:rime-witogram-objs> PARENT_SCOPE)
set(plugin_deps ${rime_library} kenlm kenlm_util sentencepiece-static PARENT_SCOPE)
set(plugin_modules "witogram" PARENT_SCOPE)

if(BUILD_TOOLS)
  add_executable(dump_to_arpa tools/dump_to_arpa.cc tools/gram_db.cc tools/gram_encoding.cc)
  target_include_directories(dump_to_arpa PRIVATE src tools third_party/kenlm)
  target_link_libraries(dump_to_arpa rime-witogram-objs kenlm ${rime_library} ${Marisa_LIBRARY})

  install(TARGETS dump_to_arpa DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
