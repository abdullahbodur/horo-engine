#include "mcp/McpServer.h"

#include <array>
#include <cctype>
#include <cstring>
#include <mutex>
#include <sstream>
#include <thread>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace Horo::Mcp {
    namespace {
#ifdef _WIN32
        using SocketHandle = SOCKET;
        constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
#else
        using SocketHandle = int;
        constexpr SocketHandle kInvalidSocket = -1;
#endif

        void CloseSocket(SocketHandle socketHandle) {
            if (socketHandle == kInvalidSocket)
                return;
#ifdef _WIN32
            closesocket(socketHandle);
#else
            close(socketHandle);
#endif
        }

        std::string SocketErrorString() {
#ifdef _WIN32
            return std::to_string(WSAGetLastError());
#else
            return std::strerror(errno);
#endif
        }

        std::string Trim(std::string value) {
            while (!value.empty() &&
                   std::isspace(static_cast<unsigned char>(value.front())))
                value.erase(value.begin());
            while (!value.empty() &&
                   std::isspace(static_cast<unsigned char>(value.back())))
                value.pop_back();
            return value;
        }

        std::string ToLowerAscii(std::string value) {
            for (char &c: value)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return value;
        }

        bool SendAll(SocketHandle socketHandle, std::string_view bytes) {
            size_t sent = 0;
            while (sent < bytes.size()) {
                const auto rc = send(socketHandle, bytes.data() + sent,
                                     static_cast<int>(bytes.size() - sent), 0);
                if (rc <= 0)
                    return false;
                sent += static_cast<size_t>(rc);
            }
            return true;
        }

        size_t ParseContentLength(const std::string &headerSection) {
            std::istringstream stream(headerSection);
            std::string line;
            std::getline(stream, line); // skip request line
            while (std::getline(stream, line)) {
                const size_t colon = line.find(':');
                if (colon == std::string::npos)
                    continue;
                if (ToLowerAscii(Trim(line.substr(0, colon))) == "content-length")
                    return std::strtoull(Trim(line.substr(colon + 1)).c_str(), nullptr, 10);
            }
            return 0;
        }

        void ParseRequestHeaders(const std::string &headerSection,
                                 McpHttpRequest *outRequest) {
            std::istringstream stream(headerSection);
            std::string line;
            std::getline(stream, line); // skip request line (already parsed)
            while (std::getline(stream, line)) {
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                const size_t colon = line.find(':');
                if (colon == std::string::npos)
                    continue;
                outRequest->headers[ToLowerAscii(Trim(line.substr(0, colon)))] =
                        Trim(line.substr(colon + 1));
            }
        }

        bool ParseFirstRequestLine(const std::string &headerSection,
                                   McpHttpRequest *outRequest) {
            std::istringstream stream(headerSection);
            std::string requestLine;
            std::getline(stream, requestLine);
            if (!requestLine.empty() && requestLine.back() == '\r')
                requestLine.pop_back();
            std::istringstream lineStream(requestLine);
            lineStream >> outRequest->method >> outRequest->path;
            return !outRequest->method.empty();
        }

        bool ReceiveRequest(SocketHandle socketHandle, McpHttpRequest *outRequest) {
            if (!outRequest)
                return false;

            std::string raw;
            std::array<char, 4096> buffer{};
            size_t headersEnd = std::string::npos;
            size_t contentLength = 0;

            while (true) {
                const auto rc =
                        recv(socketHandle, buffer.data(), static_cast<int>(buffer.size()), 0);
                if (rc <= 0)
                    return false;
                raw.append(buffer.data(), static_cast<size_t>(rc));

                if (headersEnd == std::string::npos) {
                    headersEnd = raw.find("\r\n\r\n");
                    if (headersEnd != std::string::npos)
                        contentLength = ParseContentLength(raw.substr(0, headersEnd));
                }

                if (headersEnd != std::string::npos &&
                    raw.size() >= headersEnd + 4 + contentLength) {
                    break;
                }
            }

            const std::string headerSection = raw.substr(0, headersEnd);
            if (!ParseFirstRequestLine(headerSection, outRequest))
                return false;
            ParseRequestHeaders(headerSection, outRequest);
            outRequest->body = raw.substr(headersEnd + 4, contentLength);
            return true;
        }

        std::string BuildHttpResponseBytes(const McpHttpResponse &response) {
            std::ostringstream out;
            out << "HTTP/1.1 " << response.statusCode << ' ' << response.statusText
                    << "\r\n";
            out << "Content-Type: " << response.contentType << "\r\n";
            out << "Content-Length: " << response.body.size() << "\r\n";
            out << "Connection: close\r\n\r\n";
            out << response.body;
            return out.str();
        }

        void EnsureSocketsReady() {
#ifdef _WIN32
            static std::once_flag once;
            std::call_once(once, []() {
                WSADATA data{};
                WSAStartup(MAKEWORD(2, 2), &data);
            });
#endif
        }
    } // namespace

    struct McpHttpServer::Impl {
        SocketHandle listenSocket = kInvalidSocket;
        // NOSONAR: cpp:S6168 std::jthread is not available on Apple Clang
        std::thread worker;
        std::function<McpHttpResponse(const McpHttpRequest &)> handler;
        std::function<void()> onRequestBegin;
        std::function<void()> onRequestEnd;
        std::atomic<bool> stopRequested{false};
    };

    namespace {
        using McpHandler = std::function<McpHttpResponse(const McpHttpRequest &)>;
        using McpCallback = std::function<void()>;

        void ServeClient(SocketHandle client, const McpHandler &handler,
                         const McpCallback &onBegin, const McpCallback &onEnd) {
            if (onBegin)
                onBegin();

            McpHttpRequest request;
            McpHttpResponse response;
            if (ReceiveRequest(client, &request) && handler) {
                response = handler(request);
            } else {
                response.statusCode = 400;
                response.statusText = "Bad Request";
                response.contentType = "application/json";
                response.body = R"({"error":"Invalid HTTP request."})";
            }

            SendAll(client, BuildHttpResponseBytes(response));
            CloseSocket(client);

            if (onEnd)
                onEnd();
        }

        SocketHandle AcceptNextClient(SocketHandle listenSocket) {
            fd_set readSet;
            FD_ZERO(&readSet);
            FD_SET(listenSocket, &readSet);
            timeval timeout{};
            timeout.tv_sec = 0;
            timeout.tv_usec = 200000;
            if (const int ready = select(static_cast<int>(listenSocket) + 1, &readSet,
                                         nullptr, nullptr, &timeout);
                ready <= 0)
                return kInvalidSocket;

            sockaddr_in clientAddr{};
#ifdef _WIN32
            int clientLen = sizeof(clientAddr);
#else
            socklen_t clientLen = sizeof(clientAddr);
#endif
            return accept(listenSocket,
                          static_cast<sockaddr *>(static_cast<void *>(&clientAddr)),
                          &clientLen);
        }
    } // namespace

    McpHttpServer::McpHttpServer() { m_impl = std::make_unique<Impl>(); }

    McpHttpServer::~McpHttpServer() { Stop(); }

    McpServerStartResult McpHttpServer::Start(
        const std::string &host, int port,
        std::function<McpHttpResponse(const McpHttpRequest &)> handler,
        std::function<void()> onRequestBegin, std::function<void()> onRequestEnd) {
        Stop();
        EnsureSocketsReady();

        m_impl->handler = std::move(handler);
        m_impl->onRequestBegin = std::move(onRequestBegin);
        m_impl->onRequestEnd = std::move(onRequestEnd);
        m_impl->stopRequested.store(false);

        McpServerStartResult result;
        m_impl->listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (m_impl->listenSocket == kInvalidSocket) {
            result.error = SocketErrorString();
            return result;
        }

        int optValue = 1;
#ifdef _WIN32
        std::array<char, sizeof(optValue)> optValueBytes{};
        std::memcpy(optValueBytes.data(), &optValue, sizeof(optValue));
        setsockopt(m_impl->listenSocket, SOL_SOCKET, SO_REUSEADDR,
                   optValueBytes.data(), static_cast<int>(optValueBytes.size()));
#else
        setsockopt(m_impl->listenSocket, SOL_SOCKET, SO_REUSEADDR, &optValue,
                   sizeof(optValue));
#endif

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(static_cast<uint16_t>(port));
        if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
            result.error = "Invalid host address.";
            CloseSocket(m_impl->listenSocket);
            m_impl->listenSocket = kInvalidSocket;
            return result;
        }

        if (bind(m_impl->listenSocket,
                 static_cast<sockaddr *>(static_cast<void *>(&addr)),
                 sizeof(addr)) != 0) {
            result.error = SocketErrorString();
            CloseSocket(m_impl->listenSocket);
            m_impl->listenSocket = kInvalidSocket;
            return result;
        }

        if (listen(m_impl->listenSocket, 8) != 0) {
            result.error = SocketErrorString();
            CloseSocket(m_impl->listenSocket);
            m_impl->listenSocket = kInvalidSocket;
            return result;
        }

        m_running.store(true);
        // NOSONAR: cpp:S6168 std::jthread is not available on Apple Clang
        m_impl->worker = std::thread([this]() {
            while (!m_impl->stopRequested.load()) {
                const SocketHandle client = AcceptNextClient(m_impl->listenSocket);
                if (client == kInvalidSocket)
                    continue;
                ServeClient(client, m_impl->handler, m_impl->onRequestBegin,
                            m_impl->onRequestEnd);
            }
        });

        result.ok = true;
        return result;
    }

    void McpHttpServer::Stop() {
        if (!m_running.exchange(false))
            return;

        m_impl->stopRequested.store(true);
        CloseSocket(m_impl->listenSocket);
        m_impl->listenSocket = kInvalidSocket;
        if (m_impl->worker.joinable())
            m_impl->worker.join();
    }
} // namespace Horo::Mcp
