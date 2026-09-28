#pragma once
#include <cstdint>
namespace wt::test {
using Socket = std::intptr_t;
constexpr Socket invalid_socket = -1;
Socket listen_loopback(uint16_t& port);
Socket accept_connection(Socket listener);
void close_socket(Socket socket);
} // namespace wt::test
