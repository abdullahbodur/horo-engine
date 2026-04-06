#include "mcp/McpServer.h"

#include <array>
#include <cctype>
#include <cstring>
#include <mutex>
#include <sstream>

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

namespace Monolith {
namespace Mcp {

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
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
    value.erase(value.begin());
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
    value.pop_back();
  return value;
}

std::string ToLowerAscii(std::string value) {
  for (char& c : value)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return value;
}

bool SendAll(SocketHandle socketHandle, const std::string& bytes) {
  size_t sent = 0;
  while (sent < bytes.size()) {
    const int rc = send(socketHandle, bytes.data() + sent,
                        static_cast<int>(bytes.size() - sent), 0);
    if (rc <= 0)
      return false;
    sent += static_cast<size_t>(rc);
  }
  return true;
}

bool ReceiveRequest(SocketHandle socketHandle, McpHttpRequest* outRequest) {
  if (!outRequest)
    return false;

  std::string raw;
  std::array<char, 4096> buffer{};
  size_t headersEnd = std::string::npos;
  size_t contentLength = 0;

  while (true) {
    const int rc = recv(socketHandle, buffer.data(), static_cast<int>(buffer.size()), 0);
    if (rc <= 0)
      return false;
    raw.append(buffer.data(), static_cast<size_t>(rc));

    if (headersEnd == std::string::npos) {
      headersEnd = raw.find("\r\n\r\n");
      if (headersEnd != std::string::npos) {
        std::istringstream headStream(raw.substr(0, headersEnd));
        std::string line;
        std::getline(headStream, line);
        while (std::getline(headStream, line)) {
          const size_t colon = line.find(':');
          if (colon == std::string::npos)
            continue;
          const std::string key = ToLowerAscii(Trim(line.substr(0, colon)));
          const std::string value = Trim(line.substr(colon + 1));
          if (key == "content-length")
            contentLength = static_cast<size_t>(std::strtoull(value.c_str(), nullptr, 10));
        }
      }
    }

    if (headersEnd != std::string::npos &&
        raw.size() >= headersEnd + 4 + contentLength) {
      break;
    }
  }

  std::istringstream requestStream(raw.substr(0, headersEnd));
  std::string requestLine;
  std::getline(requestStream, requestLine);
  if (!requestLine.empty() && requestLine.back() == '\r')
    requestLine.pop_back();

  std::istringstream requestLineStream(requestLine);
  requestLineStream >> outRequest->method >> outRequest->path;

  std::string line;
  while (std::getline(requestStream, line)) {
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    const size_t colon = line.find(':');
    if (colon == std::string::npos)
      continue;
    outRequest->headers[ToLowerAscii(Trim(line.substr(0, colon)))] =
        Trim(line.substr(colon + 1));
  }

  outRequest->body = raw.substr(headersEnd + 4, contentLength);
  return !outRequest->method.empty();
}

std::string BuildHttpResponseBytes(const McpHttpResponse& response) {
  std::ostringstream out;
  out << "HTTP/1.1 " << response.statusCode << ' ' << response.statusText << "\r\n";
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

}  // namespace

struct McpHttpServer::Impl {
  SocketHandle listenSocket = kInvalidSocket;
  std::thread worker;
  std::function<McpHttpResponse(const McpHttpRequest&)> handler;
  std::function<void()> onRequestBegin;
  std::function<void()> onRequestEnd;
  std::atomic<bool> stopRequested{false};
};

McpHttpServer::McpHttpServer() : m_impl(std::make_unique<Impl>()) {}

McpHttpServer::~McpHttpServer() {
  Stop();
}

McpServerStartResult McpHttpServer::Start(const std::string& host,
                                          int port,
                                          std::function<McpHttpResponse(const McpHttpRequest&)> handler,
                                          std::function<void()> onRequestBegin,
                                          std::function<void()> onRequestEnd) {
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
  setsockopt(m_impl->listenSocket, SOL_SOCKET, SO_REUSEADDR,
             reinterpret_cast<const char*>(&optValue), sizeof(optValue));
#else
  setsockopt(m_impl->listenSocket, SOL_SOCKET, SO_REUSEADDR, &optValue, sizeof(optValue));
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

  if (bind(m_impl->listenSocket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
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
  m_impl->worker = std::thread([this]() {
    while (!m_impl->stopRequested.load()) {
      fd_set readSet;
      FD_ZERO(&readSet);
      FD_SET(m_impl->listenSocket, &readSet);
      timeval timeout{};
      timeout.tv_sec = 0;
      timeout.tv_usec = 200000;
      const int ready =
          select(static_cast<int>(m_impl->listenSocket + 1), &readSet, nullptr, nullptr, &timeout);
      if (ready <= 0)
        continue;

      sockaddr_in clientAddr{};
#ifdef _WIN32
      int clientLen = sizeof(clientAddr);
#else
      socklen_t clientLen = sizeof(clientAddr);
#endif
      const SocketHandle client =
          accept(m_impl->listenSocket, reinterpret_cast<sockaddr*>(&clientAddr), &clientLen);
      if (client == kInvalidSocket)
        continue;

      if (m_impl->onRequestBegin)
        m_impl->onRequestBegin();

      McpHttpRequest request;
      McpHttpResponse response;
      if (ReceiveRequest(client, &request) && m_impl->handler) {
        response = m_impl->handler(request);
      } else {
        response.statusCode = 400;
        response.statusText = "Bad Request";
        response.contentType = "application/json";
        response.body = "{\"error\":\"Invalid HTTP request.\"}";
      }

      const std::string bytes = BuildHttpResponseBytes(response);
      SendAll(client, bytes);
      CloseSocket(client);

      if (m_impl->onRequestEnd)
        m_impl->onRequestEnd();
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

}  // namespace Mcp
}  // namespace Monolith
