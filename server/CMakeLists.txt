add_executable(encoder-server
  src/main.cpp
  src/audit.cpp
  src/monitor.cpp
  src/admin_server.cpp
  src/network_status.cpp
  src/scan_ipc.cpp
)

target_link_libraries(encoder-server PRIVATE encoder_common Threads::Threads)
target_include_directories(encoder-server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_executable(encoder-wifi-scan-helper src/scan_helper.cpp src/scan_ipc.cpp src/scan_limit.cpp src/scan_session.cpp)
  target_include_directories(encoder-wifi-scan-helper PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  add_executable(encoder-wifi-collector src/wifi_collector.cpp src/wifi_control.cpp src/snapshot_publish.cpp src/network_status.cpp src/scan_limit.cpp)
  target_include_directories(encoder-wifi-collector PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
endif()

if(BUILD_UI_TESTS AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_executable(encoder-scan-flow-tests tests/scan_flow_checks.cpp src/scan_ipc.cpp src/scan_session.cpp)
  target_include_directories(encoder-scan-flow-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(encoder-scan-flow-tests PRIVATE Threads::Threads)
  add_test(NAME scan-flow COMMAND encoder-scan-flow-tests)
  set_tests_properties(scan-flow PROPERTIES TIMEOUT 30)
  add_executable(encoder-wifi-tests tests/wifi_socket_checks.cpp src/wifi_control.cpp)
  target_include_directories(encoder-wifi-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(encoder-wifi-tests PRIVATE Threads::Threads)
  add_test(NAME wifi-socket COMMAND encoder-wifi-tests)
  add_executable(encoder-snapshot-tests tests/network_snapshot_checks.cpp src/network_status.cpp)
  target_include_directories(encoder-snapshot-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  add_test(NAME network-snapshot COMMAND encoder-snapshot-tests)
  add_executable(encoder-publish-tests tests/snapshot_publish_checks.cpp src/snapshot_publish.cpp src/network_status.cpp)
  target_include_directories(encoder-publish-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  add_test(NAME snapshot-publish COMMAND encoder-publish-tests)
  add_executable(encoder-scan-limit-tests tests/scan_limit_checks.cpp src/scan_limit.cpp)
  target_include_directories(encoder-scan-limit-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  add_test(NAME scan-cooldown COMMAND encoder-scan-limit-tests)
endif()

if(WIN32)
  target_link_libraries(encoder-server PRIVATE ws2_32)
endif()
