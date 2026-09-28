#include "support/socket.hpp"
#include "support/test.hpp"
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace wt::test {
namespace {
#ifdef _WIN32
SOCKET native(Socket value) { return static_cast<SOCKET>(value); }
struct Network {
    Network() {
        WSADATA data{};
        require(WSAStartup(MAKEWORD(2, 2), &data) == 0);
    }
    ~Network() { WSACleanup(); }
};
#else
int native(Socket value) { return static_cast<int>(value); }
#endif
} // namespace
void close_socket(Socket socket) {
#ifdef _WIN32
    closesocket(native(socket));
#else
    close(native(socket));
#endif
}
Socket listen_loopback(uint16_t& port) {
#ifdef _WIN32
    static Network network;
    auto listener = WSASocketW(AF_INET, SOCK_STREAM, 0, nullptr, 0, WSA_FLAG_NO_HANDLE_INHERIT);
    require(listener != INVALID_SOCKET);
    using Length = int;
#else
    auto listener = socket(AF_INET, SOCK_STREAM, 0);
    require(listener >= 0);
    if (fcntl(listener, F_SETFD, FD_CLOEXEC) != 0) {
        close(listener);
        throw std::runtime_error("Cannot protect listener inheritance");
    }
    using Length = socklen_t;
#endif
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    Length size = sizeof(address);
    if (bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
        getsockname(listener, reinterpret_cast<sockaddr*>(&address), &size) != 0 ||
        listen(listener, 8) != 0) {
        close_socket(static_cast<Socket>(listener));
        throw std::runtime_error("Cannot bind TLS test listener");
    }
    port = ntohs(address.sin_port);
    return static_cast<Socket>(listener);
}
Socket accept_connection(Socket listener) {
#ifdef _WIN32
    WSAPOLLFD ready{native(listener), POLLRDNORM, 0};
    if (WSAPoll(&ready, 1, 100) <= 0)
        return invalid_socket;
    auto connection = accept(native(listener), nullptr, nullptr);
    if (connection == INVALID_SOCKET)
        return invalid_socket;
    DWORD timeout = 3000;
    setsockopt(connection, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout),
               sizeof(timeout));
    setsockopt(connection, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout),
               sizeof(timeout));
#else
    pollfd ready{native(listener), POLLIN, 0};
    if (poll(&ready, 1, 100) <= 0)
        return invalid_socket;
    auto connection = accept(native(listener), nullptr, nullptr);
    if (connection < 0)
        return invalid_socket;
    timeval timeout{3, 0};
    setsockopt(connection, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(connection, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
#endif
    return static_cast<Socket>(connection);
}
} // namespace wt::test
