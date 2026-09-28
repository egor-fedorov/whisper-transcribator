#include "models/download.hpp"
#include "models/models.hpp"
#include "support/cancel.hpp"
#include "support/hash.hpp"
#include "support/io.hpp"
#include "support/socket.hpp"
#include "support/test.hpp"
#include <atomic>
#include <iostream>
#include <openssl/ssl.h>
#include <thread>

using namespace wt;
using namespace wt::test;
namespace {
const std::string payload = "abcdefghijklmnopqrstuvwxyz012345";
class Server {
    std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)> context{SSL_CTX_new(TLS_server_method()),
                                                              SSL_CTX_free};
    Socket listener = invalid_socket;
    std::atomic<bool> stopping{false};
    std::thread worker;
    bool cut_once = false;
    void serve(Socket fd) {
        std::unique_ptr<SSL, decltype(&SSL_free)> ssl(SSL_new(context.get()), SSL_free);
        SSL_set_fd(ssl.get(), static_cast<int>(fd));
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
            stop_signal = SIGINT;
        SSL_shutdown(ssl.get());
    }

  public:
    std::string url;
    std::atomic<int> ranges{0};
    Server(const fs::path& cert, const fs::path& key) {
        require(bool(context));
        require(SSL_CTX_use_certificate_file(context.get(), cert.u8string().c_str(),
                                             SSL_FILETYPE_PEM) == 1);
        require(SSL_CTX_use_PrivateKey_file(context.get(), key.u8string().c_str(),
                                            SSL_FILETYPE_PEM) == 1);
        uint16_t port = 0;
        listener = listen_loopback(port);
        url = "https://127.0.0.1:" + std::to_string(port);
        // shutdown() of a listening socket does not interrupt accept() on macOS, so the worker
        // waits for connections with a timeout and checks for the end of the test.
        worker = std::thread([&] {
            while (!stopping) {
                auto fd = accept_connection(listener);
                if (fd == invalid_socket)
                    continue;
                serve(fd);
                close_socket(fd);
            }
        });
    }
    ~Server() {
        stopping = true;
        worker.join();
        close_socket(listener);
    }
};
void prefix(const Model& model, const fs::path& root) {
    fs::create_directories(root);
    auto path = partial_model_path(model, root);
    auto fd = open_partial_model(path);
    require(fd.close() == 0);
    atomic_write(path, payload.substr(0, 8), true);
}
void transfers(const fs::path& root, const fs::path& cert, const fs::path& key) {
    Server server(cert, key);
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
    rejects([&] { ensure_cached(cut, root / "cut", false); }, "Model download failed");
    require(fs::file_size(partial_model_path(cut, root / "cut")) == 8);
    rejects([&] { ensure_cached(cut, root / "cut", true); }, "Model missing in offline mode");
    require(read_text(ensure_cached(cut, root / "cut", false).path) == payload);
    auto wrong = model("wrong");
    prefix(wrong, root / "wrong");
    // libcurl may reject the range before the application's response callback.
    rejects([&] { ensure_cached(wrong, root / "wrong", false); }, "range");
    require(read_text(partial_model_path(wrong, root / "wrong")) == payload.substr(0, 8));
    auto bad = model("bad");
    rejects([&] { ensure_cached(bad, root / "bad", false); },
            "Model size/SHA-256 verification failed");
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
    {
        ScopedEnv untrusted("SSL_CERT_FILE", std::nullopt);
        rejects([&] { ensure_cached(tls, root / "tls", false); }, "Model download failed");
    }
    require(!fs::exists(root / "tls/fixture.bin"));
    auto unsafe = model("unsafe");
    fs::create_directories(root / "unsafe");
    atomic_write(root / "victim", "preserve");
    fs::create_symlink(root / "victim", partial_model_path(unsafe, root / "unsafe"));
    rejects([&] { ensure_cached(unsafe, root / "unsafe", false); }, "Unsafe partial model file");
    require(read_text(root / "victim") == "preserve");
    fs::remove(partial_model_path(unsafe, root / "unsafe"));
    fs::create_hard_link(root / "victim", partial_model_path(unsafe, root / "unsafe"));
    rejects([&] { ensure_cached(unsafe, root / "unsafe", false); }, "Unsafe partial model file");
    require(read_text(root / "victim") == "preserve");
    require(server.ranges >= 5);
}
} // namespace
int main(int argc, char** argv) {
    if (argc != 3)
        return 2;
#ifndef _WIN32
    signal(SIGPIPE, SIG_IGN);
#endif
    install_signal_handlers();
    ScopedEnv cert("SSL_CERT_FILE", argv[1]);
    ScopedEnv proxy("NO_PROXY", "127.0.0.1");
    ScopedEnv lower_proxy("no_proxy", "127.0.0.1");
    return run_tests({{"HTTPS transfer, resume, TLS and partial-file safety",
                       [&](const fs::path& root) { transfers(root, argv[1], argv[2]); }}});
}
