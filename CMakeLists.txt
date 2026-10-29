cmake_minimum_required(VERSION 3.30)
project(hft_infra_optimizer VERSION 0.1.0 LANGUAGES C CXX)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
  message(FATAL_ERROR "Use an out-of-source build: cmake --preset debug")
endif()
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  message(FATAL_ERROR "This foundation targets Linux. On Windows use WSL2; other ports are not verified.")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
option(HFT_SANITIZERS "ASan/UBSan on handwritten targets (not the driver)" OFF)
include(CTest)
include(cmake/Dependencies.cmake)

function(hft_target target)
  target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror)
  if(HFT_SANITIZERS)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
      message(FATAL_ERROR "HFT_SANITIZERS requires GCC or Clang on Linux")
    endif()
    target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(${target} PRIVATE -fsanitize=address,undefined)
  endif()
endfunction()

set(codec_dir "${CMAKE_BINARY_DIR}/generated")
set(codecs "${codec_dir}/hft_sbe/MessageHeader.h" "${codec_dir}/hft_sbe/Ping.h"
  "${codec_dir}/hft_sbe/Pong.h" "${codec_dir}/hft_sbe/NewOrder.h" "${codec_dir}/hft_sbe/Side.h")
add_custom_command(OUTPUT ${codecs}
  COMMAND ${CMAKE_COMMAND} -E make_directory "${codec_dir}"
  COMMAND "${Java_JAVA_EXECUTABLE}" --add-opens java.base/jdk.internal.misc=ALL-UNNAMED
    -Dsbe.target.language=Cpp -Dsbe.output.dir=${codec_dir}
    -Dsbe.validation.stop.on.error=true -Dsbe.validation.warnings.fatal=true
    -jar "${HFT_SBE_JAR}" "${CMAKE_CURRENT_SOURCE_DIR}/schemas/trading.xml"
  DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/schemas/trading.xml" "${HFT_SBE_JAR}"
  COMMENT "Generating C++ codecs with SBE 1.35.0" VERBATIM)
add_custom_target(sbe_codecs DEPENDS ${codecs})

# Shared between tools/tests; a real shared library, with no Aeron dependency.
add_library(hft_common SHARED cpp/common/src/options.cpp cpp/common/src/protocol.cpp)
add_dependencies(hft_common sbe_codecs)
target_include_directories(hft_common PUBLIC cpp/common/include)
target_include_directories(hft_common SYSTEM PUBLIC "${codec_dir}")
hft_target(hft_common)

add_library(hft_transport STATIC cpp/common/src/transport.cpp)
target_link_libraries(hft_transport PUBLIC hft_common aeron_client_wrapper aeron_static)
hft_target(hft_transport)

foreach(role IN ITEMS ping pong)
  add_executable(aeron_${role} cpp/tools/aeron_${role}/main.cpp)
  target_link_libraries(aeron_${role} PRIVATE hft_transport)
  hft_target(aeron_${role})
endforeach()
# Use the statically linked native driver, avoiding runtime library-path setup.
set_target_properties(aeronmd_s PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
add_custom_target(hft_driver ALL DEPENDS aeronmd_s)

if(BUILD_TESTING)
  add_executable(hft_unit_tests tests/common_test.cpp tests/protocol_test.cpp)
  target_link_libraries(hft_unit_tests PRIVATE hft_common GTest::gtest_main)
  hft_target(hft_unit_tests)
  add_test(NAME unit COMMAND hft_unit_tests)
  set_tests_properties(unit PROPERTIES LABELS unit TIMEOUT 20)

  add_executable(aeron_fault_peer tests/aeron_fault_peer.cpp)
  target_link_libraries(aeron_fault_peer PRIVATE hft_transport)
  hft_target(aeron_fault_peer)
  foreach(scenario IN ITEMS happy no-peer no-driver no-response truncated wrong-correlation wrong-template)
    add_test(NAME ipc_${scenario}
      COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/scripts/ipc_smoke.sh"
        "${CMAKE_BINARY_DIR}" "${scenario}")
    set_tests_properties(ipc_${scenario} PROPERTIES LABELS integration TIMEOUT 45)
  endforeach()
endif()

add_custom_target(format-check
  COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/scripts/format.sh" --check
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" VERBATIM)
