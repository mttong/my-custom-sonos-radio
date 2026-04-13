cmake_minimum_required(VERSION 3.20)
project(maggie_sonos VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type")

include(FetchContent)

# cpp-httplib (header-only HTTP server/client)
FetchContent_Declare(
  httplib
  GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
  GIT_TAG        v0.18.0
)
FetchContent_MakeAvailable(httplib)

# tinyxml2 (SOAP XML parsing)
FetchContent_Declare(
  tinyxml2
  GIT_REPOSITORY https://github.com/leethomason/tinyxml2.git
  GIT_TAG        9.0.0
)
FetchContent_MakeAvailable(tinyxml2)

# nlohmann/json (Control API JSON)
FetchContent_Declare(
  json
  URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
  URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
)
FetchContent_MakeAvailable(json)

add_executable(maggie_sonos
  src/main.cpp
  src/smapi_handler.cpp
  src/media_handler.cpp
  src/sonos_discovery.cpp
  src/control_api.cpp
  src/oauth_handler.cpp
)

find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

target_compile_definitions(maggie_sonos PRIVATE CPPHTTPLIB_OPENSSL_SUPPORT)

target_link_libraries(maggie_sonos
  PRIVATE
    httplib::httplib
    tinyxml2
    nlohmann_json::nlohmann_json
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
)

target_include_directories(maggie_sonos PRIVATE src)
