// FramebufferStream.cpp
//
// Minimal WebSocket server (RFC 6455) that streams engine framebuffer frames
// as JPEG to connected Theia / browser clients.
//
// Only compiled when MONOLITH_FRAMEBUFFER_STREAM=1 (CMake option).
// When the flag is off, stub no-op bodies are provided so the header can still
// be included without linker errors.

#include "FramebufferStream.h"

#ifndef MONOLITH_FRAMEBUFFER_STREAM

// ---- Stub implementations when streaming is disabled -----------------------
namespace Monolith {
struct FramebufferStream::Impl {};
FramebufferStream::FramebufferStream() : m_impl(std::make_unique<Impl>()) {}
FramebufferStream::~FramebufferStream() = default;
void FramebufferStream::Start(int) {}
void FramebufferStream::Stop() {}
bool FramebufferStream::IsRunning() const { return false; }
void FramebufferStream::BroadcastFrame(int, int) {}
}  // namespace Monolith

#else  // MONOLITH_FRAMEBUFFER_STREAM

#ifdef _MSC_VER
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#pragma warning(push)
#pragma warning(disable : 4996)
#endif

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBIW_ASSERT(x) ((void)(x))
#include <stb_image_write.h>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cstring>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

// OpenGL — included after platform sockets to avoid macro conflicts
#include <glad/glad.h>

namespace Monolith {
namespace {

// ---- Platform socket helpers ------------------------------------------------

#ifdef _WIN32
using Sock = SOCKET;
constexpr Sock kBadSock = INVALID_SOCKET;
void CloseSock(Sock s) { closesocket(s); }
void SockInit() {
  static std::once_flag once;
  std::call_once(once, [] { WSADATA d{}; WSAStartup(MAKEWORD(2, 2), &d); });
}
#else
using Sock = int;
constexpr Sock kBadSock = -1;
void CloseSock(Sock s) { close(s); }
void SockInit() {}
#endif

bool SendAllBytes(Sock s, const uint8_t* data, size_t len) {
  size_t sent = 0;
  while (sent < len) {
    const int rc = send(s, reinterpret_cast<const char*>(data + sent),
                        static_cast<int>(len - sent), 0);
    if (rc <= 0)
      return false;
    sent += static_cast<size_t>(rc);
  }
  return true;
}

bool RecvLine(Sock s, std::string& out) {
  out.clear();
  char c = 0;
  while (true) {
    const int rc = recv(s, &c, 1, 0);
    if (rc <= 0)
      return false;
    if (c == '\n')
      return true;
    if (c != '\r')
      out += c;
  }
}

// ---- SHA-1 (for WebSocket handshake, RFC 6455 §4.1) ------------------------
// Public-domain implementation.

struct Sha1State {
  uint32_t h[5]{0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
  uint64_t msgLen = 0;
  uint8_t buf[64]{};
  uint8_t bufLen = 0;

  static uint32_t RotL(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }

  void ProcessBlock(const uint8_t* block) {
    uint32_t w[80];
    for (int i = 0; i < 16; ++i) {
      w[i] = (static_cast<uint32_t>(block[i * 4]) << 24) |
              (static_cast<uint32_t>(block[i * 4 + 1]) << 16) |
              (static_cast<uint32_t>(block[i * 4 + 2]) << 8) |
              static_cast<uint32_t>(block[i * 4 + 3]);
    }
    for (int i = 16; i < 80; ++i)
      w[i] = RotL(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    auto Round = [&](int i, uint32_t f, uint32_t k) {
      const uint32_t tmp = RotL(a, 5) + f + e + k + w[i];
      e = d; d = c; c = RotL(b, 30); b = a; a = tmp;
    };
    for (int i = 0; i < 20; ++i) Round(i, (b & c) | (~b & d), 0x5A827999);
    for (int i = 20; i < 40; ++i) Round(i, b ^ c ^ d, 0x6ED9EBA1);
    for (int i = 40; i < 60; ++i) Round(i, (b & c) | (b & d) | (c & d), 0x8F1BBCDC);
    for (int i = 60; i < 80; ++i) Round(i, b ^ c ^ d, 0xCA62C1D6);
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
  }

  void Update(const uint8_t* data, size_t len) {
    msgLen += len * 8;
    while (len > 0) {
      const size_t space = 64 - bufLen;
      const size_t copy = std::min(space, len);
      std::memcpy(buf + bufLen, data, copy);
      bufLen += static_cast<uint8_t>(copy);
      data += copy;
      len -= copy;
      if (bufLen == 64) {
        ProcessBlock(buf);
        bufLen = 0;
      }
    }
  }

  std::array<uint8_t, 20> Digest() {
    uint8_t pad[64]{};
    pad[0] = 0x80;
    const size_t padLen = (bufLen < 56) ? (56 - bufLen) : (120 - bufLen);
    Update(pad, padLen);
    uint8_t lenBytes[8];
    for (int i = 7; i >= 0; --i) { lenBytes[i] = msgLen & 0xFF; msgLen >>= 8; }
    Update(lenBytes, 8);
    std::array<uint8_t, 20> out{};
    for (int i = 0; i < 5; ++i) {
      out[i * 4 + 0] = (h[i] >> 24) & 0xFF;
      out[i * 4 + 1] = (h[i] >> 16) & 0xFF;
      out[i * 4 + 2] = (h[i] >>  8) & 0xFF;
      out[i * 4 + 3] = (h[i]      ) & 0xFF;
    }
    return out;
  }
};

static const char* kBase64Chars =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string Base64Encode(const uint8_t* data, size_t len) {
  std::string out;
  out.reserve(((len + 2) / 3) * 4);
  for (size_t i = 0; i < len; i += 3) {
    const uint32_t b = (static_cast<uint32_t>(data[i]) << 16) |
                       (i + 1 < len ? static_cast<uint32_t>(data[i + 1]) << 8 : 0) |
                       (i + 2 < len ? static_cast<uint32_t>(data[i + 2]) : 0);
    out += kBase64Chars[(b >> 18) & 0x3F];
    out += kBase64Chars[(b >> 12) & 0x3F];
    out += (i + 1 < len) ? kBase64Chars[(b >> 6) & 0x3F] : '=';
    out += (i + 2 < len) ? kBase64Chars[(b >> 0) & 0x3F] : '=';
  }
  return out;
}

// Compute the Sec-WebSocket-Accept value per RFC 6455 §4.2.2
std::string WebSocketAccept(const std::string& clientKey) {
  const std::string combined = clientKey + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
  Sha1State sha;
  sha.Update(reinterpret_cast<const uint8_t*>(combined.data()), combined.size());
  const auto digest = sha.Digest();
  return Base64Encode(digest.data(), digest.size());
}

// ---- WebSocket frame builder ------------------------------------------------

std::vector<uint8_t> BuildBinaryFrame(const uint8_t* payload, size_t payloadLen) {
  std::vector<uint8_t> frame;
  frame.reserve(10 + payloadLen);
  frame.push_back(0x82);  // FIN=1, opcode=2 (binary)
  if (payloadLen <= 125) {
    frame.push_back(static_cast<uint8_t>(payloadLen));
  } else if (payloadLen <= 65535) {
    frame.push_back(126);
    frame.push_back(static_cast<uint8_t>((payloadLen >> 8) & 0xFF));
    frame.push_back(static_cast<uint8_t>(payloadLen & 0xFF));
  } else {
    frame.push_back(127);
    for (int i = 7; i >= 0; --i)
      frame.push_back(static_cast<uint8_t>((payloadLen >> (i * 8)) & 0xFF));
  }
  frame.insert(frame.end(), payload, payload + payloadLen);
  return frame;
}

// ---- JPEG encode callback for stb ------------------------------------------

struct JpegBuffer {
  std::vector<uint8_t> data;
};

void StbJpegWrite(void* ctx, void* data, int size) {
  auto* buf = static_cast<JpegBuffer*>(ctx);
  const auto* bytes = static_cast<const uint8_t*>(data);
  buf->data.insert(buf->data.end(), bytes, bytes + size);
}

}  // namespace

// ---- FramebufferStream::Impl ------------------------------------------------

struct FramebufferStream::Impl {
  std::atomic<bool> running{false};
  std::atomic<bool> stopRequested{false};

  Sock listenSock = kBadSock;
  std::thread acceptThread;

  std::mutex clientsMtx;
  std::vector<Sock> clients;

  std::vector<uint8_t> pixelBuf;
  int jpegQuality = 75;

  void AcceptLoop() {
    while (!stopRequested.load()) {
      fd_set readSet;
      FD_ZERO(&readSet);
      FD_SET(listenSock, &readSet);
      timeval tv{};
      tv.tv_sec = 0;
      tv.tv_usec = 200000;
      if (select(static_cast<int>(listenSock + 1), &readSet, nullptr, nullptr, &tv) <= 0)
        continue;

#ifdef _WIN32
      int addrLen = sizeof(sockaddr_in);
#else
      socklen_t addrLen = sizeof(sockaddr_in);
#endif
      sockaddr_in addr{};
      const Sock client = accept(listenSock, reinterpret_cast<sockaddr*>(&addr), &addrLen);
      if (client == kBadSock)
        continue;

      if (!DoHandshake(client)) {
        CloseSock(client);
        continue;
      }

      std::lock_guard<std::mutex> lock(clientsMtx);
      clients.push_back(client);
    }
  }

  bool DoHandshake(Sock client) {
    std::string wsKey;
    bool isUpgrade = false;

    for (;;) {
      std::string line;
      if (!RecvLine(client, line))
        return false;
      if (line.empty())
        break;
      if (line.find("Upgrade:") != std::string::npos)
        isUpgrade = true;
      const std::string prefix = "Sec-WebSocket-Key:";
      const auto pos = line.find(prefix);
      if (pos != std::string::npos) {
        wsKey = line.substr(pos + prefix.size());
        while (!wsKey.empty() && (wsKey.front() == ' ' || wsKey.front() == '\t'))
          wsKey.erase(wsKey.begin());
        while (!wsKey.empty() && (wsKey.back() == ' ' || wsKey.back() == '\t' || wsKey.back() == '\r'))
          wsKey.pop_back();
      }
    }

    if (!isUpgrade || wsKey.empty())
      return false;

    const std::string accept = WebSocketAccept(wsKey);
    std::ostringstream resp;
    resp << "HTTP/1.1 101 Switching Protocols\r\n"
         << "Upgrade: websocket\r\n"
         << "Connection: Upgrade\r\n"
         << "Sec-WebSocket-Accept: " << accept << "\r\n"
         << "\r\n";
    const std::string respStr = resp.str();
    return SendAllBytes(client,
                        reinterpret_cast<const uint8_t*>(respStr.data()),
                        respStr.size());
  }

  void BroadcastJpeg(const uint8_t* jpegData, size_t jpegSize) {
    const auto frame = BuildBinaryFrame(jpegData, jpegSize);
    std::lock_guard<std::mutex> lock(clientsMtx);
    std::vector<Sock> alive;
    for (Sock c : clients) {
      if (SendAllBytes(c, frame.data(), frame.size()))
        alive.push_back(c);
      else
        CloseSock(c);
    }
    clients = std::move(alive);
  }
};

// ---- FramebufferStream public API ------------------------------------------

FramebufferStream::FramebufferStream() : m_impl(std::make_unique<Impl>()) {}
FramebufferStream::~FramebufferStream() { Stop(); }

void FramebufferStream::Start(int port) {
  if (m_impl->running.load())
    return;

  SockInit();
  m_impl->stopRequested.store(false);
  m_impl->listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (m_impl->listenSock == kBadSock)
    return;

  int opt = 1;
#ifdef _WIN32
  setsockopt(m_impl->listenSock, SOL_SOCKET, SO_REUSEADDR,
             reinterpret_cast<const char*>(&opt), sizeof(opt));
#else
  setsockopt(m_impl->listenSock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<uint16_t>(port));
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  if (bind(m_impl->listenSock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 ||
      listen(m_impl->listenSock, 4) != 0) {
    CloseSock(m_impl->listenSock);
    m_impl->listenSock = kBadSock;
    return;
  }

  m_impl->running.store(true);
  m_impl->acceptThread = std::thread([this] { m_impl->AcceptLoop(); });
}

void FramebufferStream::Stop() {
  if (!m_impl->running.exchange(false))
    return;

  m_impl->stopRequested.store(true);
  if (m_impl->listenSock != kBadSock) {
    CloseSock(m_impl->listenSock);
    m_impl->listenSock = kBadSock;
  }
  if (m_impl->acceptThread.joinable())
    m_impl->acceptThread.join();

  std::lock_guard<std::mutex> lock(m_impl->clientsMtx);
  for (Sock c : m_impl->clients)
    CloseSock(c);
  m_impl->clients.clear();
}

bool FramebufferStream::IsRunning() const {
  return m_impl->running.load();
}

void FramebufferStream::BroadcastFrame(int width, int height) {
  if (!m_impl->running.load())
    return;

  {
    std::lock_guard<std::mutex> lock(m_impl->clientsMtx);
    if (m_impl->clients.empty())
      return;
  }

  if (width <= 0 || height <= 0)
    return;

  const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height) * 3;
  m_impl->pixelBuf.resize(pixelCount);

  glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, m_impl->pixelBuf.data());

  // Flip rows: OpenGL origin is bottom-left, JPEG expects top-left
  const size_t rowBytes = static_cast<size_t>(width) * 3;
  std::vector<uint8_t> rowTmp(rowBytes);
  for (int y = 0; y < height / 2; ++y) {
    uint8_t* top = m_impl->pixelBuf.data() + y * rowBytes;
    uint8_t* bot = m_impl->pixelBuf.data() + (height - 1 - y) * rowBytes;
    std::memcpy(rowTmp.data(), top, rowBytes);
    std::memcpy(top, bot, rowBytes);
    std::memcpy(bot, rowTmp.data(), rowBytes);
  }

  JpegBuffer jpegBuf;
  stbi_write_jpg_to_func(StbJpegWrite, &jpegBuf,
                         width, height, 3,
                         m_impl->pixelBuf.data(),
                         m_impl->jpegQuality);

  if (!jpegBuf.data.empty())
    m_impl->BroadcastJpeg(jpegBuf.data.data(), jpegBuf.data.size());
}

}  // namespace Monolith

#endif  // MONOLITH_FRAMEBUFFER_STREAM
