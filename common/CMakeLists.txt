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
  src/wifi_config_draft.cpp
)

target_sources(encoder_common PRIVATE ${COMMON_HEADERS} ${COMMON_SOURCES})

target_include_directories(encoder_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_compile_definitions(encoder_common PRIVATE ENCODER_BUILD)

target_link_libraries(encoder_common PUBLIC OpenSSL::SSL OpenSSL::Crypto)

if(WIN32)
  target_link_libraries(encoder_common PUBLIC ws2_32)
endif()
if(BUILD_UI_TESTS)
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
