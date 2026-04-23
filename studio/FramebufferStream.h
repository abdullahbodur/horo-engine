#pragma once

#include <atomic>
#include <memory>

namespace Monolith {

// Streams the engine framebuffer to connected WebSocket clients as JPEG frames.
//
// Usage:
//   stream.Start(39282);                      // once, at startup
//   // after Renderer::EndFrame() each frame:
//   stream.BroadcastFrame(windowW, windowH);
//   stream.Stop();                            // at shutdown
//
// Clients connect to ws://localhost:39282 and receive binary JPEG frames.
// Only active when MONOLITH_FRAMEBUFFER_STREAM is defined (opt-in at build time).
class FramebufferStream {
 public:
  FramebufferStream();
  ~FramebufferStream();

  FramebufferStream(const FramebufferStream&) = delete;
  FramebufferStream& operator=(const FramebufferStream&) = delete;

  void Start(int port = 39282);
  void Stop();
  bool IsRunning() const;

  // Must be called from the render thread (active OpenGL context required).
  // Reads the current default framebuffer, encodes as JPEG, and broadcasts
  // to all connected WebSocket clients.
  void BroadcastFrame(int width, int height);

 private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};

}  // namespace Monolith
