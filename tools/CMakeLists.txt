set(EXECUTABLE_OUTPUT_PATH ${PROJECT_BINARY_DIR}/bin)

include_directories(../src)

# msvc doesn't export all symbols
if(NOT (WIN32 AND BUILD_SHARED_LIBS))
  add_executable(build_grammar
    build_grammar.cc
    $<TARGET_OBJECTS:rime-witogram-objs>)
  target_link_libraries(build_grammar
    ${rime_library}
    ${rime_dict_library})

  add_executable(convert_grammar
    convert_grammar.cc
    $<TARGET_OBJECTS:rime-witogram-objs>)
  target_link_libraries(convert_grammar
    ${rime_library}
    ${rime_dict_library})

  add_executable(dump_to_arpa
    dump_to_arpa.cc
    $<TARGET_OBJECTS:rime-witogram-objs>)
  target_link_libraries(dump_to_arpa
    ${rime_library}
    ${rime_dict_library})
endif()
