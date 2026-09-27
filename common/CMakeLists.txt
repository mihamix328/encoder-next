add_library(encoder_common)

set(COMMON_HEADERS
  include/encoder/base64.h
  include/encoder/bytes.h
  include/encoder/config.h
  include/encoder/protocol.h
  include/encoder/secure_memory.h
  include/encoder/crypto.h
  include/encoder/gost_cli.h
  include/encoder/auth.h
  include/encoder/net.h
  include/encoder/tls.h
  include/encoder/wifi_profile.h
  include/encoder/wifi_change.h
  include/encoder/wifi_config_draft.h
  include/encoder/wifi_link_probe.h
)

set(COMMON_SOURCES
  src/base64.cpp
  src/bytes.cpp
  src/config.cpp
  src/protocol.cpp
  src/secure_memory.cpp
  src/crypto.cpp
  src/gost_cli.cpp
  src/auth.cpp
  src/net.cpp
  src/tls.cpp
  src/wifi_profile.cpp
  src/wifi_change.cpp
  src/wifi_session.cpp
  src/wifi_rpc.cpp
  src/wifi_config_draft.cpp
  src/wifi_link_probe.cpp
)

target_sources(encoder_common PRIVATE ${COMMON_HEADERS} ${COMMON_SOURCES})

target_include_directories(encoder_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_compile_definitions(encoder_common PRIVATE ENCODER_BUILD)

target_link_libraries(encoder_common PUBLIC OpenSSL::SSL OpenSSL::Crypto)
option(ENCODER_NATIVE_GOST "Enable native libakrypt GOST-MGM backend" OFF)
if(ENCODER_NATIVE_GOST)
  find_path(AKRYPT_INCLUDE_DIR libakrypt.h REQUIRED)
  find_library(AKRYPT_LIBRARY akrypt REQUIRED)
  target_sources(encoder_common PRIVATE src/gost_native.cpp)
  target_include_directories(encoder_common PRIVATE ${AKRYPT_INCLUDE_DIR})
  target_link_libraries(encoder_common PUBLIC ${AKRYPT_LIBRARY} Threads::Threads)
  target_compile_definitions(encoder_common PRIVATE ENCODER_NATIVE_GOST=1)
endif()

if(WIN32)
  target_link_libraries(encoder_common PUBLIC ws2_32)
endif()
if(BUILD_UI_TESTS)
  add_executable(encoder-unicode-file-tests tests/unicode_file_checks.cpp)
  target_link_libraries(encoder-unicode-file-tests PRIVATE encoder_common)
  add_test(NAME unicode-file-io COMMAND encoder-unicode-file-tests)
  if(ENCODER_NATIVE_GOST)
    add_executable(encoder-gost-native-tests tests/gost_native_checks.cpp)
    target_link_libraries(encoder-gost-native-tests PRIVATE encoder_common)
    add_test(NAME gost-native COMMAND encoder-gost-native-tests)
    set_tests_properties(gost-native PROPERTIES TIMEOUT 60)
  endif()
  add_executable(encoder-wifi-rpc-tests tests/wifi_rpc_checks.cpp)
  target_link_libraries(encoder-wifi-rpc-tests PRIVATE encoder_common Threads::Threads)
  add_test(NAME wifi-rpc COMMAND encoder-wifi-rpc-tests)
  add_executable(encoder-wifi-session-tests tests/wifi_session_checks.cpp)
  target_link_libraries(encoder-wifi-session-tests PRIVATE encoder_common Threads::Threads)
  add_test(NAME wifi-session COMMAND encoder-wifi-session-tests)
  add_executable(encoder-wifi-link-tests tests/wifi_link_checks.cpp)
  target_link_libraries(encoder-wifi-link-tests PRIVATE encoder_common)
  add_test(NAME wifi-link-readiness COMMAND encoder-wifi-link-tests)
  add_executable(encoder-wifi-config-tests tests/wifi_config_checks.cpp)
  target_link_libraries(encoder-wifi-config-tests PRIVATE encoder_common)
  add_test(NAME wifi-config COMMAND encoder-wifi-config-tests)
  add_executable(encoder-wifi-change-tests tests/wifi_change_checks.cpp)
  target_link_libraries(encoder-wifi-change-tests PRIVATE encoder_common)
  add_test(NAME wifi-change COMMAND encoder-wifi-change-tests)
  add_executable(encoder-net-tests tests/net_checks.cpp)
  target_link_libraries(encoder-net-tests PRIVATE encoder_common Threads::Threads)
  add_test(NAME net-loopback COMMAND encoder-net-tests)
  set_tests_properties(net-loopback PROPERTIES TIMEOUT 15)
endif()
