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
)

target_sources(encoder_common PRIVATE ${COMMON_HEADERS} ${COMMON_SOURCES})

target_include_directories(encoder_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_compile_definitions(encoder_common PRIVATE ENCODER_BUILD)

target_link_libraries(encoder_common PUBLIC OpenSSL::SSL OpenSSL::Crypto)

if(WIN32)
  target_link_libraries(encoder_common PUBLIC ws2_32)
endif()
