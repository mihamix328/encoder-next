add_executable(encoder-server
  src/main.cpp
  src/audit.cpp
  src/monitor.cpp
  src/admin_server.cpp
  src/network_status.cpp
)

target_link_libraries(encoder-server PRIVATE encoder_common Threads::Threads)
target_include_directories(encoder-server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

if(WIN32)
  target_link_libraries(encoder-server PRIVATE ws2_32)
endif()
