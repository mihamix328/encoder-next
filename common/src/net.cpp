#include "encoder/net.h"

#include <cstring>
#include <chrono>
#include <cerrno>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/select.h>
#endif

namespace encoder {

NetInit::NetInit() {
#if defined(_WIN32)
  WSADATA wsa;
  ok_ = (WSAStartup(MAKEWORD(2, 2), &wsa) == 0);
#else
  ok_ = true;
#endif
}

NetInit::~NetInit() {
#if defined(_WIN32)
  if (ok_) {
    WSACleanup();
  }
#endif
}

Socket::Socket() : handle_(static_cast<Handle>(-1)) {}

Socket::Socket(Handle handle) : handle_(handle) {}

Socket::Socket(Socket&& other) noexcept {
  handle_ = other.handle_;
  other.handle_ = static_cast<Handle>(-1);
}

Socket& Socket::operator=(Socket&& other) noexcept {
  if (this == &other) return *this;
  close();
  handle_ = other.handle_;
  other.handle_ = static_cast<Handle>(-1);
  return *this;
}

Socket::~Socket() {
  close();
}

bool Socket::valid() const {
#if defined(_WIN32)
  return handle_ != INVALID_SOCKET;
#else
  return handle_ >= 0;
#endif
}

void Socket::close() {
  if (!valid()) return;
#if defined(_WIN32)
  closesocket(static_cast<SOCKET>(handle_));
  handle_ = INVALID_SOCKET;
#else
  ::close(handle_);
  handle_ = -1;
#endif
}

bool Socket::connect_to(const std::string& host, int port, std::string* err, int timeout_ms) {
  close();
  if (host.empty() || port < 1 || port > 65535 || timeout_ms <= 0) {
    if (err) *err = "Invalid address, port or timeout";
    return false;
  }

  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  addrinfo* result = nullptr;
  std::string port_str = std::to_string(port);
  if (getaddrinfo(host.c_str(), port_str.c_str(), &hints, &result) != 0) {
    if (err) *err = "getaddrinfo failed";
    return false;
  }

  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  for (addrinfo* rp = result; rp != nullptr; rp = rp->ai_next) {
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
    if (remaining <= 0) break;
    Handle sock = static_cast<Handle>(::socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol));
#if defined(_WIN32)
    if (sock == INVALID_SOCKET) continue;
#else
    if (sock < 0) continue;
#endif
#if defined(_WIN32)
    u_long mode = 1;
    if (ioctlsocket(sock, FIONBIO, &mode) != 0) { closesocket(sock); continue; }
    const int rc = ::connect(sock, rp->ai_addr, static_cast<int>(rp->ai_addrlen));
    const bool pending = rc != 0 && WSAGetLastError() == WSAEWOULDBLOCK;
#else
    const int flags = fcntl(sock, F_GETFL, 0);
    if (flags < 0 || sock >= FD_SETSIZE || fcntl(sock, F_SETFL, flags | O_NONBLOCK) != 0) { ::close(sock); continue; }
    const int rc = ::connect(sock, rp->ai_addr, static_cast<int>(rp->ai_addrlen));
    const bool pending = rc != 0 && errno == EINPROGRESS;
#endif
    bool connected = rc == 0;
    if (pending) {
      fd_set writable, failed;
      FD_ZERO(&writable); FD_ZERO(&failed);
      FD_SET(sock, &writable); FD_SET(sock, &failed);
      timeval wait{static_cast<long>(remaining / 1000), static_cast<long>((remaining % 1000) * 1000)};
#if defined(_WIN32)
      const int ready = select(0, nullptr, &writable, &failed, &wait);
      int length = sizeof(int);
#else
      const int ready = select(sock + 1, nullptr, &writable, &failed, &wait);
      socklen_t length = sizeof(int);
#endif
      int socket_error = 0;
      connected = ready > 0 && getsockopt(sock, SOL_SOCKET, SO_ERROR,
          reinterpret_cast<char*>(&socket_error), &length) == 0 && socket_error == 0;
    }
#if defined(_WIN32)
    mode = 0;
    connected = connected && ioctlsocket(sock, FIONBIO, &mode) == 0;
#else
    connected = connected && fcntl(sock, F_SETFL, flags) == 0;
#endif
    if (connected) {
      handle_ = sock;
      freeaddrinfo(result);
      return true;
    }
#if defined(_WIN32)
    closesocket(static_cast<SOCKET>(sock));
#else
    ::close(sock);
#endif
  }

  freeaddrinfo(result);
  if (err) *err = "Unable to connect";
  return false;
}

bool Socket::wait_readable(int timeout_ms) const {
  if (!valid() || timeout_ms < 0) return false;
#if !defined(_WIN32)
  if (handle_ >= FD_SETSIZE) return false;
#endif
  fd_set readable;
  FD_ZERO(&readable);
  FD_SET(handle_, &readable);
  timeval wait{timeout_ms / 1000, (timeout_ms % 1000) * 1000};
#if defined(_WIN32)
  return select(0, &readable, nullptr, nullptr, &wait) > 0;
#else
  return select(handle_ + 1, &readable, nullptr, nullptr, &wait) > 0;
#endif
}

Socket Socket::listen_on(const std::string& host, int port, std::string* err) {
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;

  addrinfo* result = nullptr;
  std::string port_str = std::to_string(port);
  if (getaddrinfo(host.c_str(), port_str.c_str(), &hints, &result) != 0) {
    if (err) *err = "getaddrinfo failed";
    return Socket();
  }

  Socket listen_sock;
  for (addrinfo* rp = result; rp != nullptr; rp = rp->ai_next) {
    Handle sock = static_cast<Handle>(::socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol));
#if defined(_WIN32)
    if (sock == INVALID_SOCKET) continue;
#else
    if (sock < 0) continue;
#endif

    int opt = 1;
#if defined(_WIN32)
    setsockopt(static_cast<SOCKET>(sock), SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&opt), sizeof(opt));
#else
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

#if defined(_WIN32)
    if (::bind(static_cast<SOCKET>(sock), rp->ai_addr, static_cast<int>(rp->ai_addrlen)) == 0 &&
        ::listen(static_cast<SOCKET>(sock), 16) == 0) {
#else
    if (::bind(sock, rp->ai_addr, static_cast<int>(rp->ai_addrlen)) == 0 &&
        ::listen(sock, 16) == 0) {
#endif
      listen_sock = Socket(sock);
      break;
    }
#if defined(_WIN32)
    closesocket(static_cast<SOCKET>(sock));
#else
    ::close(sock);
#endif
  }

  freeaddrinfo(result);
  if (!listen_sock.valid() && err) {
    *err = "Unable to bind";
  }
  return listen_sock;
}

Socket Socket::accept(std::string* err) const {
  if (!valid()) {
    if (err) *err = "Invalid listen socket";
    return Socket();
  }
#if defined(_WIN32)
  SOCKET client = ::accept(static_cast<SOCKET>(handle_), nullptr, nullptr);
  if (client == INVALID_SOCKET) {
    if (err) *err = "accept failed";
    return Socket();
  }
  return Socket(static_cast<Handle>(client));
#else
  int client = ::accept(handle_, nullptr, nullptr);
  if (client < 0) {
    if (err) *err = "accept failed";
    return Socket();
  }
  return Socket(client);
#endif
}

bool Socket::set_io_timeout(int timeout_ms) {
  if (!valid() || timeout_ms <= 0) return false;
#if defined(_WIN32)
  DWORD value = static_cast<DWORD>(timeout_ms);
  return setsockopt(static_cast<SOCKET>(handle_), SOL_SOCKET, SO_RCVTIMEO,
                   reinterpret_cast<const char*>(&value), sizeof(value)) == 0 &&
         setsockopt(static_cast<SOCKET>(handle_), SOL_SOCKET, SO_SNDTIMEO,
                   reinterpret_cast<const char*>(&value), sizeof(value)) == 0;
#else
  timeval value{timeout_ms / 1000, (timeout_ms % 1000) * 1000};
  return setsockopt(handle_, SOL_SOCKET, SO_RCVTIMEO, &value, sizeof(value)) == 0 &&
         setsockopt(handle_, SOL_SOCKET, SO_SNDTIMEO, &value, sizeof(value)) == 0;
#endif
}

int Socket::read(uint8_t* buf, size_t len) const {
  if (!valid()) return -1;
#if defined(_WIN32)
  return ::recv(static_cast<SOCKET>(handle_), reinterpret_cast<char*>(buf),
                static_cast<int>(len), 0);
#else
  return ::recv(handle_, buf, len, 0);
#endif
}

int Socket::write(const uint8_t* buf, size_t len) const {
  if (!valid()) return -1;
#if defined(_WIN32)
  return ::send(static_cast<SOCKET>(handle_), reinterpret_cast<const char*>(buf),
                static_cast<int>(len), 0);
#else
  return ::send(handle_, buf, len, 0);
#endif
}

} // namespace encoder
