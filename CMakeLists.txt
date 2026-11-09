# skills_unleashed, built against a Kyubu checkout: cmake -S <kyubu> -B build -DKYUBU_MODS=<this folder> (README.md).
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  message(FATAL_ERROR "Build this mod through a Kyubu checkout: cmake -S <kyubu> -B build -DKYUBU_MODS=${CMAKE_CURRENT_SOURCE_DIR}")
endif()
# The mod is Lua only: its scripts/ are bundled from src/ by Kyubu's tools/bundle_lua.py, so there is nothing to
# build for it. The unit tests run its Lua in a LuaJIT with a recording stub of `kyubu` (ctest name
# skills_unleashed_tests); they need EMTweaks' api/ header, from an EMTweaks checkout beside this one.
set(EMTWEAKS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../emtweaks" CACHE PATH "a checkout of the EMTweaks repository")
if(KYUBU_BUILD_TESTS)
  if(NOT EXISTS "${EMTWEAKS_DIR}/emtweaks/api/emtweaks.h")
    message(FATAL_ERROR "skills_unleashed_tests need EMTweaks: pass -DEMTWEAKS_DIR=<EMTweaks checkout>")
  endif()
  file(GLOB SU_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_*.cpp)
  add_executable(skills_unleashed_tests ${SU_TEST_SOURCES})
  target_include_directories(skills_unleashed_tests PRIVATE ${TP}/doctest ${EMTWEAKS_DIR}/emtweaks/api ${CMAKE_SOURCE_DIR}/sdk/include)
  target_compile_definitions(skills_unleashed_tests PRIVATE
    SU_MOD_DIR="${CMAKE_CURRENT_SOURCE_DIR}/skills_unleashed"
    SU_TEST_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests")
  target_link_libraries(skills_unleashed_tests PRIVATE luajit ${KYUBU_STATIC_LINK})
  add_test(NAME skills_unleashed_tests COMMAND skills_unleashed_tests)
endif()
