set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 COMPONENTS Widgets REQUIRED)

add_executable(encoder-admin
  src/main.cpp
  src/admin_window.cpp
  src/admin_window.h
  src/admin_client.cpp
  src/admin_client.h
  src/network_dialog.cpp
  src/network_dialog.h
)

target_include_directories(encoder-admin PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(encoder-admin PRIVATE encoder_common Qt6::Widgets)

if(BUILD_UI_TESTS)
  add_executable(encoder-admin-ui-tests tests/network_dialog_checks.cpp src/network_dialog.cpp)
  target_include_directories(encoder-admin-ui-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(encoder-admin-ui-tests PRIVATE Qt6::Widgets Threads::Threads)
  add_test(NAME admin-network-ui COMMAND encoder-admin-ui-tests)
  set_tests_properties(admin-network-ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=minimal" TIMEOUT 15)
endif()
