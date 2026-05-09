project(rime-witogram)
cmake_minimum_required(VERSION 3.10)

option(BUILD_TOOLS "Build tools" ON)

aux_source_directory(src witogram_src)

add_library(rime-witogram-objs OBJECT ${witogram_src})
if(BUILD_SHARED_LIBS)
  set_target_properties(rime-witogram-objs
    PROPERTIES
    POSITION_INDEPENDENT_CODE ON)
endif()

target_include_directories(rime-witogram-objs PRIVATE src)

set(plugin_name rime-witogram PARENT_SCOPE)
set(plugin_objs $<TARGET_OBJECTS:rime-witogram-objs> PARENT_SCOPE)
set(plugin_deps ${rime_library} PARENT_SCOPE)
set(plugin_modules "witogram" PARENT_SCOPE)

if(BUILD_TOOLS)
  add_executable(build_grammar tools/build_grammar.cc)
  target_include_directories(build_grammar PRIVATE src)
  target_link_libraries(build_grammar rime-witogram-objs ${rime_library} ${Marisa_LIBRARY})
  
  add_executable(convert_grammar tools/convert_grammar.cc)
  target_include_directories(convert_grammar PRIVATE src)
  target_link_libraries(convert_grammar rime-witogram-objs ${rime_library} ${Marisa_LIBRARY})

  install(TARGETS build_grammar convert_grammar DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
