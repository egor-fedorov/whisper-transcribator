#include "app.hpp"
#include <arpa/inet.h>
#include <atomic>
#include <fcntl.h>
#include <iostream>
#include <openssl/ssl.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

using namespace wt;
namespace {
const std::string payload = "abcdefghijklmnopqrstuvwxyz012345";
void require(bool ok) {
    if (!ok)
        throw std::runtime_error("HTTPS download assertion failed");
}
template <class F> void rejects(F action) {
    bool failed = false;
    try {
        action();
    } catch (const std::exception&) {
        failed = true;
    }
    require(failed);
}
class Server {
    std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)> context{SSL_CTX_new(TLS_server_method()),
                                                              SSL_CTX_free};
    int listener = -1;
    std::atomic<bool> stopping{false};
    std::thread worker;
    bool cut_once = false;
    void serve(int fd) {
        timeval timeout{3, 0};
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
        std::unique_ptr<SSL, decltype(&SSL_free)> ssl(SSL_new(context.get()), SSL_free);
        SSL_set_fd(ssl.get(), fd);
        if (SSL_accept(ssl.get()) != 1)
            return;
        std::string request;
        char bytes[1024];
        while (request.find("\r\n\r\n") == std::string::npos && request.size() < 8192) {
            auto n = SSL_read(ssl.get(), bytes, sizeof(bytes));
            if (n <= 0)
                return;
            request.append(bytes, static_cast<size_t>(n));
        }
        std::string path = request.substr(4, request.find(' ', 4) - 4);
        auto range = request.find("Range: bytes=");
        size_t offset = range == std::string::npos ? 0 : std::stoul(request.substr(range + 13));
        bool partial = offset && path != "/ignore";
        if (offset)
            ++ranges;
        std::string data = path == "/bad" ? std::string(payload.size(), 'x') : payload;
        std::string response;
        if (offset && path == "/416")
            response = "HTTP/1.1 416 Range Not Satisfiable\r\nContent-Length: 0\r\nConnection: "
                       "close\r\n\r\n";
        else {
            if (partial) {
                response = "HTTP/1.1 206 Partial Content\r\nContent-Range: bytes " +
                           std::to_string(path == "/wrong" ? 0 : offset) + "-31/32\r\n";
                data = data.substr(offset);
            } else
                response = "HTTP/1.1 200 OK\r\n";
            response +=
                "Content-Length: " + std::to_string(data.size()) + "\r\nConnection: close\r\n\r\n";
            if ((path == "/cut" && !cut_once) || path == "/cancel") {
                cut_once = true;
                data.resize(8);
            }
            response += data;
        }
        size_t written = 0;
        while (written < response.size()) {
            auto n = SSL_write(ssl.get(), response.data() + written,
                               static_cast<int>(response.size() - written));
            if (n <= 0)
                break;
            written += static_cast<size_t>(n);
        }
        if (path == "/cancel")
            raise(SIGINT);
        SSL_shutdown(ssl.get());
    }

  public:
    std::string url;
    std::atomic<int> ranges{0};
    Server(const fs::path& cert, const fs::path& key) {
        require(bool(context));
        require(SSL_CTX_use_certificate_file(context.get(), cert.c_str(), SSL_FILETYPE_PEM) == 1);
        require(SSL_CTX_use_PrivateKey_file(context.get(), key.c_str(), SSL_FILETYPE_PEM) == 1);
        listener = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
        require(listener >= 0);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        require(bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
        socklen_t size = sizeof(address);
        require(getsockname(listener, reinterpret_cast<sockaddr*>(&address), &size) == 0);
        require(listen(listener, 8) == 0);
        url = "https://127.0.0.1:" + std::to_string(ntohs(address.sin_port));
        worker = std::thread([&] {
            while (!stopping) {
                auto fd = accept(listener, nullptr, nullptr);
                if (fd < 0)
                    continue;
                serve(fd);
                close(fd);
            }
        });
    }
    ~Server() {
        stopping = true;
        shutdown(listener, SHUT_RDWR);
        worker.join();
        close(listener);
    }
};
void prefix(const Model& model, const fs::path& root) {
    fs::create_directories(root);
    auto path = partial_model_path(model, root);
    int fd = open_partial_model(path);
    close(fd);
    atomic_write(path, payload.substr(0, 8), true);
}
} // namespace
int main(int argc, char** argv) {
    if (argc != 4)
        return 2;
    fs::path root = argv[3];
    signal(SIGPIPE, SIG_IGN);
    install_signal_handlers();
    setenv("SSL_CERT_FILE", argv[1], 1);
    setenv("NO_PROXY", "127.0.0.1", 1);
    setenv("no_proxy", "127.0.0.1", 1);
    try {
        Server server(argv[1], argv[2]);
        auto model = [&](const std::string& route) {
            return Model{route, "fixture.bin", server.url + "/" + route, sha256_text(payload),
                         payload.size()};
        };
        for (const auto& mode : {"resume", "ignore", "416"}) {
            auto m = model(mode);
            auto directory = root / mode;
            prefix(m, directory);
            require(read_text(ensure_cached(m, directory, false).path) == payload);
            require(!fs::exists(partial_model_path(m, directory)));
        }
        auto cut = model("cut");
        rejects([&] { ensure_cached(cut, root / "cut", false); });
        require(fs::file_size(partial_model_path(cut, root / "cut")) == 8);
        rejects([&] { ensure_cached(cut, root / "cut", true); });
        require(read_text(ensure_cached(cut, root / "cut", false).path) == payload);
        auto wrong = model("wrong");
        prefix(wrong, root / "wrong");
        rejects([&] { ensure_cached(wrong, root / "wrong", false); });
        require(read_text(partial_model_path(wrong, root / "wrong")) == payload.substr(0, 8));
        auto bad = model("bad");
        rejects([&] { ensure_cached(bad, root / "bad", false); });
        require(!fs::exists(partial_model_path(bad, root / "bad")) &&
                !fs::exists(root / "bad/fixture.bin"));
        auto cancelled = model("cancel");
        bool stopped = false;
        try {
            ensure_cached(cancelled, root / "cancel", false);
        } catch (const Cancelled&) {
            stopped = true;
        }
        stop_signal = 0;
        require(stopped && !fs::exists(root / "cancel/fixture.bin"));
        auto tls = model("tls");
        unsetenv("SSL_CERT_FILE");
        rejects([&] { ensure_cached(tls, root / "tls", false); });
        setenv("SSL_CERT_FILE", argv[1], 1);
        require(!fs::exists(root / "tls/fixture.bin"));
        auto unsafe = model("unsafe");
        fs::create_directories(root / "unsafe");
        atomic_write(root / "victim", "preserve");
        fs::create_symlink(root / "victim", partial_model_path(unsafe, root / "unsafe"));
        rejects([&] { ensure_cached(unsafe, root / "unsafe", false); });
        require(read_text(root / "victim") == "preserve");
        fs::remove(partial_model_path(unsafe, root / "unsafe"));
        fs::create_hard_link(root / "victim", partial_model_path(unsafe, root / "unsafe"));
        rejects([&] { ensure_cached(unsafe, root / "unsafe", false); });
        require(read_text(root / "victim") == "preserve");
        require(server.ranges >= 5);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout
        << "HTTPS ranges, interrupted/cancelled transfers, TLS and partial file safety passed\n";
}
