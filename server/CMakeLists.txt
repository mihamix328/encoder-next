add_executable(encoder-server
  src/main.cpp
  src/audit.cpp
  src/monitor.cpp
  src/admin_server.cpp
  src/network_status.cpp
)

target_link_libraries(encoder-server PRIVATE encoder_common Threads::Threads)
target_include_directories(encoder-server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

if(BUILD_UI_TESTS AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_executable(encoder-wifi-tests tests/wifi_socket_checks.cpp src/network_status.cpp)
  target_include_directories(encoder-wifi-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(encoder-wifi-tests PRIVATE Threads::Threads)
  add_test(NAME wifi-socket COMMAND encoder-wifi-tests)
endif()

if(WIN32)
  target_link_libraries(encoder-server PRIVATE ws2_32)
endif()
