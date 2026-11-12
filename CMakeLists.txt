cmake_minimum_required(VERSION 3.20)
project(gadget_quick_use LANGUAGES C CXX)

if(NOT OpenRACMod_DIR)
  if(DEFINED OPENRAC_SDK)
    set(OpenRACMod_DIR "${OPENRAC_SDK}/cmake")
  elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/sdk.lock")
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/sdk.lock" lock)
    string(JSON sdk_version GET "${lock}" version)
    set(OpenRACMod_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.sdk/openrac-mod-sdk-${sdk_version}/cmake")
    if(NOT EXISTS "${OpenRACMod_DIR}/OpenRACModConfig.cmake")
      message(FATAL_ERROR "No OpenRAC mod SDK ${sdk_version}: run python3 tools/fetch_sdk.py first "
                          "(or pass -DOPENRAC_SDK=<an unpacked SDK>)")
    endif()
  endif()
endif()
find_package(OpenRACMod 1.12 REQUIRED)

openrac_add_mod(gadget_quick_use MANIFEST openrac-mod.json SOURCES src/mod.cpp)

add_executable(gadget_quick_use_phase_test src/phase_test.cpp)
target_compile_features(gadget_quick_use_phase_test PRIVATE cxx_std_17)
target_include_directories(gadget_quick_use_phase_test PRIVATE src)
