# Установка сервера в новой Linux-системе

Для администратора Linux. Это ручная установка, не обновление существующей базы.
На системе с encoder сначала сделайте согласованную резервную копию и план переноса.
Не удаляйте существующие ключи. Полная проверка этой инструкции на новом носителе
ещё требуется; действующая плата была обновлена с переносом данных.

## Сначала сборка

Выполните BUILD_RELEASE.md на целевой архитектуре. Для ГОСТ обязательна libakrypt
и `ENCODER_NATIVE_GOST=ON`. Проверьте CTest и TLS-тесты. Не копируйте Windows EXE.
`ldd build/server/server/encoder-server` не должен показывать отсутствующих библиотек.
Системные библиотеки должны быть доступны службе вне /home и не быть ей доступны
на запись. Их лицензионные уведомления сохраняются при распространении.

## Размещение

Команды ниже запускаются из корня исходников, только для новой установки:

```sh
test ! -e /opt/encoder && test ! -e /etc/encoder && test ! -e /var/lib/encoder || exit 1
sudo useradd --system --user-group --home-dir /var/lib/encoder --no-create-home --shell /usr/sbin/nologin encoder
sudo install -d -m 0755 /opt/encoder/bin
sudo install -m 0755 build/server/server/encoder-server /opt/encoder/bin/encoder-server
sudo install -d -o root -g encoder -m 0750 /etc/encoder /etc/encoder/certs
sudo install -d -o encoder -g encoder -m 0700 /var/lib/encoder
sudo install -o root -g encoder -m 0640 deploy/server.conf.example /etc/encoder/server.conf
```

Если какая-либо команда завершилась ошибкой, остановитесь и проверьте причину.
Не запускайте следующие команды автоматически после ошибки.

## Сертификат и настройки

Создайте сертификат с SAN для реального адреса или DNS-имени устройства.
Например, для заранее настроенного Ethernet-адреса 172.10.0.2:

```sh
sudo openssl req -x509 -newkey rsa:3072 -nodes -days 365 -subj /CN=encoder -addext subjectAltName=IP:172.10.0.2 -keyout /etc/encoder/certs/server.key -out /etc/encoder/certs/server.crt
sudo chown root:encoder /etc/encoder/certs/server.key /etc/encoder/certs/server.crt
sudo chmod 0640 /etc/encoder/certs/server.key
sudo chmod 0644 /etc/encoder/certs/server.crt
sudoedit /etc/encoder/server.conf
```

Замените `CHANGE_ME` длинным случайным административным токеном. Не используйте
его как пароль пользователя. Укажите абсолютные cert_file/key_file/storage_dir,
порты 7443/7444; Wi-Fi-флаги для базовой установки оставьте false.
Только публичный server.crt передаётся клиентам по доверенному каналу.

## Первая учётная запись и запуск

Создайте начальную учётную запись в доверенном локальном сеансе:

```sh
sudo -u encoder /opt/encoder/bin/encoder-server --config /etc/encoder/server.conf --init-user admin INITIAL_PASSWORD
sudo install -m 0644 deploy/encoder-server.service /etc/systemd/system/encoder-server.service
sudo systemd-analyze verify /etc/systemd/system/encoder-server.service
sudo systemctl daemon-reload
sudo systemctl start encoder-server.service
```

Замените INITIAL_PASSWORD своим начальным паролем. Текущая CLI передаёт пароль
аргументом процесса: он может попасть в историю и быть виден локальным процессам.
Не используйте этот способ для повседневного управления пользователями; после
входа смените начальный пароль и управляйте аккаунтами через TLS-админ-панель.
Токен админ-панели не равен паролю аккаунта admin.

Проверьте вход, ограничения обычной учётной записи и полный цикл на копии файла.
Затем включите автозапуск и проверьте после перезагрузки:

```sh
sudo systemctl enable encoder-server.service
systemctl is-active encoder-server.service
```

Резервируйте всю `/var/lib/encoder`, `/etc/encoder` и используемые версии
бинарников/библиотек. Согласованную копию базы делайте при остановленной службе.
Архив содержит секреты: доступ только администратору. Без ключей файлы могут быть
невосстановимы. Автоматическое управление Wi-Fi не нужно для шифрования по Ethernet.
