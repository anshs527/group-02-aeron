# All acquisition is owned by CMake; no global installs or moving branches.
find_package(Git REQUIRED)
find_package(Java 17 REQUIRED COMPONENTS Runtime)
include(FetchContent)
set(BUILD_AERON_DRIVER ON CACHE BOOL "" FORCE)
set(BUILD_AERON_ARCHIVE_API OFF CACHE BOOL "" FORCE)
foreach(flag IN ITEMS AERON_TESTS AERON_UNIT_TESTS AERON_SYSTEM_TESTS
    AERON_BUILD_SAMPLES AERON_BUILD_DOCUMENTATION AERON_INSTALL_TARGETS)
  set(${flag} OFF CACHE BOOL "" FORCE)
endforeach()
FetchContent_Declare(aeron_dep
  GIT_REPOSITORY https://github.com/aeron-io/aeron.git
  GIT_TAG d800d60405738dc254eb47a6e0a14e4b86387981
  GIT_SUBMODULES ""
  SYSTEM EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(aeron_dep)

if(BUILD_TESTING)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG 6910c9d9165801d8827d628cb72eb7ea9dd538c5
    GIT_SUBMODULES ""
    SYSTEM EXCLUDE_FROM_ALL)
  FetchContent_MakeAvailable(googletest)
endif()

# Maven Central release artifact. Set HFT_SBE_JAR for an offline copy of this
# exact version; see docs/dependencies.md for the acquisition/trust boundary.
set(HFT_SBE_JAR "${CMAKE_BINARY_DIR}/_deps/sbe-all-1.35.0.jar" CACHE FILEPATH "SBE 1.35.0 generator jar")
if(NOT EXISTS "${HFT_SBE_JAR}")
  get_filename_component(sbe_parent "${HFT_SBE_JAR}" DIRECTORY)
  file(MAKE_DIRECTORY "${sbe_parent}")
  file(DOWNLOAD
    https://repo.maven.apache.org/maven2/uk/co/real-logic/sbe-all/1.35.0/sbe-all-1.35.0.jar
    "${HFT_SBE_JAR}.part" TLS_VERIFY ON TIMEOUT 120 STATUS sbe_download)
  list(GET sbe_download 0 sbe_status)
  if(NOT sbe_status EQUAL 0)
    file(REMOVE "${HFT_SBE_JAR}.part")
    message(FATAL_ERROR "SBE 1.35.0 download failed: ${sbe_download}. Supply -DHFT_SBE_JAR=/absolute/path/sbe-all-1.35.0.jar for offline builds.")
  endif()
  file(RENAME "${HFT_SBE_JAR}.part" "${HFT_SBE_JAR}")
endif()
file(SHA256 "${HFT_SBE_JAR}" HFT_SBE_SHA256)
file(WRITE "${CMAKE_BINARY_DIR}/dependency-manifest.txt"
  "Aeron 1.48.0 d800d60405738dc254eb47a6e0a14e4b86387981\nGoogleTest 1.16.0 6910c9d9165801d8827d628cb72eb7ea9dd538c5\nSBE 1.35.0 sha256=${HFT_SBE_SHA256}\nJava=${Java_VERSION_STRING}\nCompiler=${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}\n")
