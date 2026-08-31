#include "shared_cursor_state.h"

#include <remote_action.h>

#include <mutex>

namespace crossdesk {
namespace {

std::mutex g_cursor_state_mutex;
SharedCursorState g_cursor_state;
bool g_cursor_state_available = false;

}  // namespace

void PublishSharedCursorState(bool visible, RemoteCursorShape shape) {
  std::lock_guard<std::mutex> lock(g_cursor_state_mutex);
  ++g_cursor_state.generation;
  g_cursor_state.visible = visible;
  g_cursor_state.shape = visible ? shape : RemoteCursorShape::none;
  g_cursor_state_available = true;
}

bool GetSharedCursorState(SharedCursorState* state) {
  if (!state) {
    return false;
  }
  std::lock_guard<std::mutex> lock(g_cursor_state_mutex);
  if (!g_cursor_state_available) {
    return false;
  }
  *state = g_cursor_state;
  return true;
}

void ClearSharedCursorState() {
  std::lock_guard<std::mutex> lock(g_cursor_state_mutex);
  ++g_cursor_state.generation;
  g_cursor_state.visible = false;
  g_cursor_state.shape = RemoteCursorShape::none;
  g_cursor_state_available = false;
}

}  // namespace crossdesk
