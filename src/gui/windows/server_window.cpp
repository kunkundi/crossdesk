#include <cmath>
#include <vector>

#include "rd_log.h"
#include "render.h"

namespace crossdesk {

static void SetServerWindowCircleShape(SDL_Window* window, int diameter) {
  if (!window || diameter <= 0) {
    return;
  }

  const int pitch = diameter * 4;
  std::vector<unsigned char> pixels((size_t)diameter * (size_t)diameter * 4, 0);

  // Sub-pixel centered circle helps symmetry on even diameters.
  const float r = (float)diameter * 0.5f;
  const float cx = r - 0.5f;
  const float cy = r - 0.5f;

  for (int y = 0; y < diameter; ++y) {
    for (int x = 0; x < diameter; ++x) {
      const float dx = (float)x - cx;
      const float dy = (float)y - cy;
      const float dist = std::sqrt(dx * dx + dy * dy);

      // 1px soft edge to reduce jaggies on small circles.
      float a = r + 0.5f - dist;
      if (a < 0.0f) a = 0.0f;
      if (a > 1.0f) a = 1.0f;
      const unsigned char alpha = (unsigned char)(a * 255.0f);
      const size_t idx = ((size_t)y * (size_t)diameter + (size_t)x) * 4;
      pixels[idx + 0] = 255;    // R
      pixels[idx + 1] = 255;    // G
      pixels[idx + 2] = 255;    // B
      pixels[idx + 3] = alpha;  // A
    }
  }

  SDL_Surface* shape = SDL_CreateSurfaceFrom(
      diameter, diameter, SDL_PIXELFORMAT_RGBA32, pixels.data(), pitch);
  if (!shape) {
    LOG_ERROR("SDL_CreateSurfaceFrom failed: {}", SDL_GetError());
    return;
  }

  if (!SDL_SetWindowShape(window, shape)) {
    LOG_ERROR("SDL_SetWindowShape failed: {}", SDL_GetError());
  }

  SDL_DestroySurface(shape);
}

int Render::ServerWindow() {
  ImGui::SetNextWindowSize(ImVec2(server_window_width_, server_window_height_),
                           ImGuiCond_Always);
  ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);

  if (server_window_compact_) {
    ImGui::SetNextWindowBgAlpha(0.0f);
  }

  ImGui::Begin("##server_window", nullptr,
               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                   ImGuiWindowFlags_NoScrollbar |
                   ImGuiWindowFlags_NoScrollWithMouse);

  // Compact mode: show a 50x50 clickable square that restores the window.
  if (server_window_compact_) {
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImGui::SetCursorPos(ImVec2(0.0f, 0.0f));
    if (ImGui::InvisibleButton(
            "##server_compact_restore",
            ImVec2(server_window_width_, server_window_height_))) {
      if (server_window_) {
        SDL_SetWindowShape(server_window_, nullptr);
      }
      if (server_window_ && server_window_bounds_saved_ &&
          server_window_width_before_compact_ > 0 &&
          server_window_height_before_compact_ > 0) {
        SDL_SetWindowSize(server_window_, server_window_width_before_compact_,
                          server_window_height_before_compact_);
        SDL_SetWindowPosition(server_window_, server_window_x_before_compact_,
                              server_window_y_before_compact_);

        server_window_width_ = (float)server_window_width_before_compact_;
        server_window_height_ = (float)server_window_height_before_compact_;
      }
      server_window_compact_ = false;
      server_window_bounds_saved_ = false;
    }

    // Draw a visible circular affordance.
    const float w = server_window_width_;
    const float h = server_window_height_;
    const float radius = (w < h ? w : h) * 0.5f - 1.0f;
    const ImVec2 center(w * 0.5f, h * 0.5f);
    draw_list->AddCircleFilled(center, radius, IM_COL32(255, 255, 255, 220),
                               32);
    draw_list->AddCircle(center, radius, IM_COL32(0, 0, 0, 255), 32, 2.0f);
    // A simple "restore" hint.
    draw_list->AddRect(
        ImVec2(center.x - radius * 0.35f, center.y - radius * 0.35f),
        ImVec2(center.x + radius * 0.35f, center.y + radius * 0.35f),
        IM_COL32(0, 0, 0, 255), 2.0f, 0, 2.0f);

    ImGui::End();
    return 0;
  }

  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
  ImGui::BeginChild(
      "ServerTitleBar",
      ImVec2(server_window_width_, server_window_title_bar_height_),
      ImGuiChildFlags_Border,
      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
          ImGuiWindowFlags_NoBringToFrontOnFocus);

  ImDrawList* draw_list = ImGui::GetWindowDrawList();

  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

  float server_title_bar_button_width = server_window_title_bar_height_;
  float server_title_bar_button_height = server_window_title_bar_height_;

  float minimize_button_pos_x =
      server_window_width_ - server_title_bar_button_width * 2;
  ImGui::SetCursorPos(ImVec2(minimize_button_pos_x, 0.0f));
  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0.1f));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));

  float minimize_pos_x =
      minimize_button_pos_x + server_title_bar_button_width * 0.33f;
  float minimize_pos_y = server_title_bar_button_height * 0.5f;
  std::string server_minimize_button = "##minimize";  // ICON_FA_MINUS;
  if (ImGui::Button(server_minimize_button.c_str(),
                    ImVec2(server_title_bar_button_width,
                           server_title_bar_button_height))) {
    if (server_window_) {
      int w = 0, h = 0;
      int x = 0, y = 0;
      SDL_GetWindowSize(server_window_, &w, &h);
      SDL_GetWindowPosition(server_window_, &x, &y);
      server_window_width_before_compact_ = w;
      server_window_height_before_compact_ = h;
      server_window_x_before_compact_ = x;
      server_window_y_before_compact_ = y;
      server_window_bounds_saved_ = true;

      constexpr int kCompactSize = 50;
      SDL_SetWindowSize(server_window_, kCompactSize, kCompactSize);

      // Move to bottom-right of the current display's usable bounds.
      SDL_Rect display_bounds;
      if (SDL_GetDisplayUsableBounds(SDL_GetDisplayForWindow(server_window_),
                                     &display_bounds)) {
        int compact_x = display_bounds.x + display_bounds.w - kCompactSize;
        int compact_y = display_bounds.y + display_bounds.h - kCompactSize;
        SDL_SetWindowPosition(server_window_, compact_x, compact_y);
      }

      // Use pixel size to match transparency buffer on HiDPI.
      int w_px = kCompactSize;
      int h_px = kCompactSize;
      SDL_GetWindowSizeInPixels(server_window_, &w_px, &h_px);
      const int diameter = (w_px < h_px ? w_px : h_px);
      SetServerWindowCircleShape(server_window_, diameter);

      server_window_compact_ = true;
    }
  }
  draw_list->AddLine(
      ImVec2(minimize_pos_x, minimize_pos_y),
      ImVec2(minimize_pos_x + server_title_bar_button_width * 0.33f,
             minimize_pos_y),
      IM_COL32(0, 0, 0, 255));
  ImGui::PopStyleColor(3);

  float xmark_button_pos_x =
      server_window_width_ - server_title_bar_button_width;
  ImGui::SetCursorPos(ImVec2(xmark_button_pos_x, 0.0f));
  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0, 0, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 0, 0, 0.5f));

  float xmark_pos_x = xmark_button_pos_x + server_title_bar_button_width * 0.5f;
  float xmark_pos_y = server_title_bar_button_height * 0.5f;
  float xmark_size = server_title_bar_button_width * 0.33f;
  std::string server_close_button = "##xmark";  // ICON_FA_XMARK;
  if (ImGui::Button(server_close_button.c_str(),
                    ImVec2(server_title_bar_button_width,
                           server_title_bar_button_height))) {
    LOG_ERROR("Close button clicked");
    LeaveConnection(peer_, self_hosted_id_);
  }

  draw_list->AddLine(ImVec2(xmark_pos_x - xmark_size / 2 - 0.25f,
                            xmark_pos_y - xmark_size / 2 + 0.75f),
                     ImVec2(xmark_pos_x + xmark_size / 2 - 1.5f,
                            xmark_pos_y + xmark_size / 2 - 0.5f),
                     IM_COL32(0, 0, 0, 255));
  draw_list->AddLine(
      ImVec2(xmark_pos_x + xmark_size / 2 - 1.75f,
             xmark_pos_y - xmark_size / 2 + 0.75f),
      ImVec2(xmark_pos_x - xmark_size / 2, xmark_pos_y + xmark_size / 2 - 1.0f),
      IM_COL32(0, 0, 0, 255));

  ImGui::PopStyleColor(3);
  ImGui::PopStyleVar();
  ImGui::PopStyleColor();

  ImGui::EndChild();
  ImGui::PopStyleVar();
  ImGui::PopStyleColor();

  ImGui::End();
  return 0;
}
}  // namespace crossdesk