set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

set(CLIENT_SOURCES
  src/client_core.cpp
  src/client_core.h
)

if(BUILD_GUI)
  find_package(Qt6 COMPONENTS Widgets REQUIRED)
  add_executable(encoder-client
    src/main.cpp
    src/main_window.cpp
    src/main_window.h
    src/login_dialog.cpp
    src/login_dialog.h
    src/secure_guards.cpp
    src/secure_guards.h
    resources.qrc
    ${CLIENT_SOURCES}
  )
  target_include_directories(encoder-client PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(encoder-client PRIVATE encoder_common Qt6::Widgets)
  target_compile_definitions(encoder-client PRIVATE ENCODER_GUI)
  install(TARGETS encoder-client RUNTIME DESTINATION .)
  if(BUILD_UI_TESTS)
    add_executable(encoder-ui-tests
      tests/ui_checks.cpp src/main_window.cpp src/main_window.h
      src/login_dialog.cpp src/login_dialog.h src/secure_guards.cpp src/secure_guards.h
      ${CLIENT_SOURCES})
    target_include_directories(encoder-ui-tests PRIVATE src)
    target_link_libraries(encoder-ui-tests PRIVATE encoder_common Qt6::Widgets)
    foreach(scale IN ITEMS 1 1.25 1.5)
      add_test(NAME ui-${scale} COMMAND encoder-ui-tests)
      set_tests_properties(ui-${scale} PROPERTIES TIMEOUT 30
        ENVIRONMENT "QT_QPA_PLATFORM=minimal;QT_SCALE_FACTOR=${scale}")
    endforeach()
  endif()
else()
  add_executable(encoder-client
    src/cli_main.cpp
    ${CLIENT_SOURCES}
  )
  target_include_directories(encoder-client PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(encoder-client PRIVATE encoder_common)
  install(TARGETS encoder-client RUNTIME DESTINATION .)
endif()
