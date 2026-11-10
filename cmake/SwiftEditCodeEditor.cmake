# SwiftEdit's reusable editing pieces, for SwiftEdit itself and for programs
# that consume a pinned SwiftEdit checkout (the theme editor in
# malkuth-tools):
#
#   swiftedit_highlight    languages, incremental line lexing, decorations,
#                          the CSS outline and CodeDocument. No GUI.Forms
#                          header, so command tools can link it alone.
#   swiftedit_code_editor  CodeEditor, its overview strip and the theme
#                          engine bridge, over GUI.Forms' Application (its
#                          shared Core, as SwiftEdit's editor uses).
#
# A consumer finds GUIForms (COMPONENTS Application) first, then includes
# this file. Including it twice defines nothing more. Headers are included
# as "highlight/..." and "code_editor/...".
if(TARGET swiftedit_code_editor)
  return()
endif()
set(SWIFTEDIT_SOURCE_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")
add_library(swiftedit_highlight STATIC
  "${SWIFTEDIT_SOURCE_ROOT}/src/highlight/language.cpp"
  "${SWIFTEDIT_SOURCE_ROOT}/src/highlight/css_language.cpp"
  "${SWIFTEDIT_SOURCE_ROOT}/src/highlight/highlighter.cpp"
  "${SWIFTEDIT_SOURCE_ROOT}/src/highlight/decorations.cpp"
  "${SWIFTEDIT_SOURCE_ROOT}/src/highlight/css_outline.cpp"
  "${SWIFTEDIT_SOURCE_ROOT}/src/highlight/code_document.cpp")
target_include_directories(swiftedit_highlight PUBLIC "${SWIFTEDIT_SOURCE_ROOT}/src")
target_compile_features(swiftedit_highlight PUBLIC cxx_std_20)
add_library(swiftedit_code_editor STATIC
  "${SWIFTEDIT_SOURCE_ROOT}/src/code_editor/code_editor.cpp"
  "${SWIFTEDIT_SOURCE_ROOT}/src/code_editor/marker_strip.cpp"
  "${SWIFTEDIT_SOURCE_ROOT}/src/code_editor/style_painter.cpp"
  "${SWIFTEDIT_SOURCE_ROOT}/src/code_editor/theme_bridge.cpp")
target_compile_definitions(swiftedit_code_editor PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(swiftedit_code_editor PUBLIC swiftedit_highlight GUIForms::Application)
