# Сборка кандидата на выпуск

Нужны CMake 3.20+, компилятор C++17, OpenSSL; для Windows GUI — Qt6 Widgets.
Готовому пользователю эти инструменты не нужны: зависимости включены в ZIP.

## Windows x64

Проверенная среда: Visual Studio 2026, Qt6/OpenSSL из vcpkg. Установите зависимости
`qtbase:x64-windows` и `openssl:x64-windows` в собственном vcpkg. Укажите его путь:

```powershell
cmake -S . -B build/windows-client -G "Visual Studio 18 2026" -A x64 -DCMAKE_TOOLCHAIN_FILE=C:/tools/vcpkg/scripts/buildsystems/vcpkg.cmake -DBUILD_SERVER=OFF -DBUILD_CLIENT=ON -DBUILD_ADMIN=ON -DBUILD_GUI=ON -DBUILD_UI_TESTS=ON
cmake --build build/windows-client --config Release --parallel 4
ctest --test-dir build/windows-client -C Release --output-on-failure
```

Для тестов Qt при необходимости задайте QT_PLUGIN_PATH на Qt6/plugins в вашем
каталоге установленного triplet. Для ZIP используйте `cmake/PackagePortable.ps1`:
параметры описаны в PORTABLE.md. Не смешивайте runtime разных архитектур.

## Linux ARM64: сервер и ГОСТ

Нужны CMake, g++, OpenSSL development headers, Python3/PyYAML для проверок сети.
Для ГОСТ требуется установленная libakrypt с заголовками и библиотекой;
на действующей плате проверена версия 0.9.18. Она не встроена в исходный экспорт.
Установку и лицензию libakrypt нужно обеспечить отдельно; без этой зависимости
полная сборка ниже намеренно завершается ошибкой, а не подменяет алгоритм.

```sh
cmake -S . -B build/server -DCMAKE_BUILD_TYPE=Release -DBUILD_CLIENT=OFF -DBUILD_ADMIN=OFF -DBUILD_GUI=OFF -DBUILD_SERVER=ON -DBUILD_UI_TESTS=ON -DENCODER_NATIVE_GOST=ON
cmake --build build/server --parallel 2
ctest --test-dir build/server --output-on-failure
python3 server/tests/admin_protocol.py --server build/server/server/encoder-server --openssl /usr/bin/openssl --native-gost
```

Создание установочного образа без изменения системы:

```sh
python3 deploy/stage_server.py --build build/server --output build/server-image
```

Образ не является автоматическим установщиком: аккаунт службы, зависимости,
сертификат с SAN, уникальный токен и первичная учётная запись настраиваются по
SERVER_DEPLOYMENT.md. Экспериментальные Wi-Fi-службы не включать автоматически.
Не переносите одноразовые сценарии миграции конкретной платы на другую систему.
