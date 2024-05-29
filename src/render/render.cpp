#include "render.h"

int RenderMainWindow(int argc, char *argv[]) {
  LOG_INFO("Remote desk");

  last_ts = static_cast<uint32_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::high_resolution_clock::now().time_since_epoch())
          .count());

  cd_cache_file = fopen("cache.cd", "r+");
  if (cd_cache_file) {
    fseek(cd_cache_file, 0, SEEK_SET);
    fread(&cd_cache.password, sizeof(cd_cache.password), 1, cd_cache_file);
    fclose(cd_cache_file);
    strncpy(input_password, cd_cache.password, sizeof(cd_cache.password));
  }

  // Setup SDL
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER |
               SDL_INIT_GAMECONTROLLER) != 0) {
    printf("Error: %s\n", SDL_GetError());
    return -1;
  }

  // From 2.0.18: Enable native IME.
#ifdef SDL_HINT_IME_SHOW_UI
  SDL_SetHint(SDL_HINT_IME_SHOW_UI, "1");
#endif

  // Create window with SDL_Renderer graphics context
  SDL_WindowFlags window_flags =
      (SDL_WindowFlags)(SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
  window = SDL_CreateWindow("Remote Desk", SDL_WINDOWPOS_CENTERED,
                            SDL_WINDOWPOS_CENTERED, window_w, window_h,
                            window_flags);

  SDL_DisplayMode DM;
  SDL_GetCurrentDisplayMode(0, &DM);
  screen_w = DM.w;
  screen_h = DM.h;

  sdlRenderer = SDL_CreateRenderer(
      window, -1, SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED);
  if (sdlRenderer == nullptr) {
    SDL_Log("Error creating SDL_Renderer!");
    return 0;
  }

  Uint32 pixformat = 0;
  pixformat = SDL_PIXELFORMAT_NV12;

  sdlTexture = SDL_CreateTexture(sdlRenderer, pixformat,
                                 SDL_TEXTUREACCESS_STREAMING, pixel_w, pixel_h);

  // Setup Dear ImGui context
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();

  io.ConfigFlags |=
      ImGuiConfigFlags_NavEnableKeyboard;  // Enable Keyboard Controls
  io.ConfigFlags |=
      ImGuiConfigFlags_NavEnableGamepad;  // Enable Gamepad Controls

#if CHINESE_FONT
  // Load Fonts
#ifdef _WIN32
  std::string default_font_path = "c:/windows/fonts/simhei.ttf";
  std::ifstream font_path_f(default_font_path.c_str());
  std::string font_path =
      font_path_f.good() ? "c:/windows/fonts/simhei.ttf" : "";
  if (!font_path.empty()) {
    io.Fonts->AddFontFromFileTTF(font_path.c_str(), 13.0f, NULL,
                                 io.Fonts->GetGlyphRangesChineseFull());
  }
#elif __APPLE__
  std::string default_font_path = "/System/Library/Fonts/PingFang.ttc";
  std::ifstream font_path_f(default_font_path.c_str());
  std::string font_path =
      font_path_f.good() ? "/System/Library/Fonts/PingFang.ttc" : "";
  if (!font_path.empty()) {
    io.Fonts->AddFontFromFileTTF(font_path.c_str(), 13.0f, NULL,
                                 io.Fonts->GetGlyphRangesChineseFull());
  }
#elif __linux__
  io.Fonts->AddFontFromFileTTF("c:/windows/fonts/msyh.ttc", 13.0f, NULL,
                               io.Fonts->GetGlyphRangesChineseFull());
#endif
#endif

  // Setup Dear ImGui style
  // ImGui::StyleColorsDark();
  ImGui::StyleColorsLight();

  // Setup Platform/Renderer backends
  ImGui_ImplSDL2_InitForSDLRenderer(window, sdlRenderer);
  ImGui_ImplSDLRenderer2_Init(sdlRenderer);

  // Our state
  bool show_demo_window = true;
  bool show_another_window = false;
  ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

  std::string mac_addr_str = GetMac();

  // Main loop
  while (!done) {
    // Start the Dear ImGui frame
    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    if (joined && !menu_hovered) {
      ImGui::SetMouseCursor(ImGuiMouseCursor_None);
    }

    {
      static float f = 0.0f;
      static int counter = 0;

      const ImGuiViewport *main_viewport = ImGui::GetMainViewport();
      ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Once);

      // ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));

#if CHINESE_FONT
      ImGui::SetNextWindowSize(ImVec2(160, 210));
#else
      ImGui::SetNextWindowSize(ImVec2(180, 210));
#endif

#if CHINESE_FONT
      if (!joined) {
        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
        ImGui::Begin(u8"菜单", nullptr,
                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                         ImGuiWindowFlags_NoMove);
      } else {
        ImGui::SetNextWindowCollapsed(true, ImGuiCond_Once);
        ImGui::Begin(u8"菜单", nullptr, ImGuiWindowFlags_None);
      }
#else
      if (!joined) {
        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
        ImGui::Begin("Menu", nullptr,
                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                         ImGuiWindowFlags_NoMove);
      } else {
        ImGui::SetNextWindowCollapsed(true, ImGuiCond_Once);
        ImGui::Begin("Menu", nullptr, ImGuiWindowFlags_None);
      }
#endif

      {
        menu_hovered = ImGui::IsWindowHovered();
#if CHINESE_FONT
        ImGui::Text(u8"本机ID:");
#else
        ImGui::Text("LOCAL ID:");
#endif
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90);
#if CHINESE_FONT
        ImGui::SetCursorPosX(60.0f);
#else
        ImGui::SetCursorPosX(80.0f);
#endif
        ImGui::InputText(
            "##local_id", (char *)mac_addr_str.c_str(),
            mac_addr_str.length() + 1,
            ImGuiInputTextFlags_CharsUppercase | ImGuiInputTextFlags_ReadOnly);

#if CHINESE_FONT
        ImGui::Text(u8"密码:");
#else
        ImGui::Text("PASSWORD:");
#endif
        ImGui::SameLine();
        char input_password_tmp[7] = "";
        std::string input_password_str = "123456";
        strncpy(input_password_tmp, input_password, sizeof(input_password));
        ImGui::SetNextItemWidth(90);
#if CHINESE_FONT
        ImGui::SetCursorPosX(60.0f);
        ImGui::InputTextWithHint("##server_pwd", u8"最长6个字符",
                                 input_password, IM_ARRAYSIZE(input_password),
                                 ImGuiInputTextFlags_CharsNoBlank);
#else
        ImGui::SetCursorPosX(80.0f);
        ImGui::InputTextWithHint("##server_pwd", "max 6 chars", input_password,
                                 IM_ARRAYSIZE(input_password),
                                 ImGuiInputTextFlags_CharsNoBlank);
#endif
        if (strcmp(input_password_tmp, input_password)) {
          cd_cache_file = fopen("cache.cd", "w+");
          if (cd_cache_file) {
            fseek(cd_cache_file, 0, SEEK_SET);
            strncpy(cd_cache.password, input_password, sizeof(input_password));
            fwrite(&cd_cache.password, sizeof(cd_cache.password), 1,
                   cd_cache_file);
            fclose(cd_cache_file);
          }
        }

        ImGui::Spacing();

        ImGui::Separator();

        ImGui::Spacing();
        {
          {
            static char remote_id[20] = "";
#if CHINESE_FONT
            ImGui::Text(u8"远端ID:");
#else
            ImGui::Text("REMOTE ID:");
#endif
            ImGui::SameLine();
            ImGui::SetNextItemWidth(90);
#if CHINESE_FONT
            ImGui::SetCursorPosX(60.0f);
#else
            ImGui::SetCursorPosX(80.0f);
#endif
            ImGui::InputTextWithHint("##remote_id", mac_addr_str.c_str(),
                                     remote_id, IM_ARRAYSIZE(remote_id),
                                     ImGuiInputTextFlags_CharsUppercase |
                                         ImGuiInputTextFlags_CharsNoBlank);

            ImGui::Spacing();

#if CHINESE_FONT
            ImGui::Text(u8"密码:");
#else
            ImGui::Text("PASSWORD:");
#endif
            ImGui::SameLine();
            ImGui::SetNextItemWidth(90);
            static char client_password[20] = "";
#if CHINESE_FONT
            ImGui::SetCursorPosX(60.0f);
            ImGui::InputTextWithHint("##client_pwd", u8"最长6个字符",
                                     client_password,
                                     IM_ARRAYSIZE(client_password),
                                     ImGuiInputTextFlags_CharsNoBlank);
#else
            ImGui::SetCursorPosX(80.0f);
            ImGui::InputTextWithHint("##client_pwd", "max 6 chars",
                                     client_password,
                                     IM_ARRAYSIZE(client_password),
                                     ImGuiInputTextFlags_CharsNoBlank);
#endif

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            if (ImGui::Button(connect_label)) {
              int ret = -1;
              if ("ClientSignalConnected" == client_signal_status_str) {
#if CHINESE_FONT
                if (strcmp(connect_label, u8"连接") == 0 && !joined) {
#else
                if (strcmp(connect_label, "Connect") == 0 && !joined) {
#endif
                  std::string user_id = "C-" + mac_addr_str;

#if CHINESE_FONT
                } else if (strcmp(connect_label, u8"断开连接") == 0 && joined) {
#else
                } else if (strcmp(connect_label, "Disconnect") == 0 && joined) {
#endif

                  memset(audio_buffer, 0, 960);
                  if (0 == ret) {
                    joined = false;
                    received_frame = false;
                  }
                }

                if (0 == ret) {
                  connect_button_pressed = !connect_button_pressed;
#if CHINESE_FONT
                  connect_label =
                      connect_button_pressed ? u8"断开连接" : u8"连接";
#else
                  connect_label =
                      connect_button_pressed ? "Disconnect" : "Connect";
#endif
                }
              }
            }
          }
        }
      }

      ImGui::Spacing();

      ImGui::Separator();

      ImGui::Spacing();

      {
#if CHINESE_FONT
        if (ImGui::Button(u8"重置窗口")) {
#else
        if (ImGui::Button("Resize Window")) {
#endif
          SDL_GetWindowSize(window, &window_w, &window_h);

          if (window_h != window_w * 9 / 16) {
            window_w = window_h * 16 / 9;
          }

          SDL_SetWindowSize(window, window_w, window_h);
        }
      }

      ImGui::SameLine();

#if CHINESE_FONT
      if (ImGui::Button(fullscreen_label)) {
        if (strcmp(fullscreen_label, u8"全屏") == 0) {
#else
      if (ImGui::Button(fullscreen_label)) {
        if (strcmp(fullscreen_label, "FULLSCREEN") == 0) {
#endif
          SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
        } else {
          SDL_SetWindowFullscreen(window, SDL_FALSE);
        }
        fullscreen_button_pressed = !fullscreen_button_pressed;
#if CHINESE_FONT
        fullscreen_label = fullscreen_button_pressed ? u8"退出全屏" : u8"全屏";
#else
        fullscreen_label =
            fullscreen_button_pressed ? "EXIT FULLSCREEN" : "FULLSCREEN";
#endif
      }
      ImGui::End();
    }

    // Rendering
    ImGui::Render();
    SDL_RenderSetScale(sdlRenderer, io.DisplayFramebufferScale.x,
                       io.DisplayFramebufferScale.y);

    SDL_RenderClear(sdlRenderer);
    SDL_RenderCopy(sdlRenderer, sdlTexture, NULL, &sdlRect);

    if (!joined || !received_frame) {
      SDL_RenderClear(sdlRenderer);
      SDL_SetRenderDrawColor(
          sdlRenderer, (Uint8)(clear_color.x * 0), (Uint8)(clear_color.y * 0),
          (Uint8)(clear_color.z * 0), (Uint8)(clear_color.w * 0));
    }

    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData());
    SDL_RenderPresent(sdlRenderer);

    frame_count++;
    end_time = SDL_GetTicks();
    elapsed_time = end_time - start_time;
    if (elapsed_time >= 1000) {
      fps = frame_count / (elapsed_time / 1000);
      frame_count = 0;
      window_title = "Remote Desk Client FPS [" + std::to_string(fps) +
                     "] status [" + server_signal_status_str + "|" +
                     client_signal_status_str + "|" +
                     server_connection_status_str + "|" +
                     client_connection_status_str + "]";
      // For MacOS, UI frameworks can only be called from the main thread
      SDL_SetWindowTitle(window, window_title.c_str());
      start_time = end_time;
    }
  }

  // Cleanup

  SDL_CloseAudioDevice(output_dev);
  SDL_CloseAudioDevice(input_dev);

  ImGui_ImplSDLRenderer2_Shutdown();
  ImGui_ImplSDL2_Shutdown();
  ImGui::DestroyContext();

  SDL_DestroyRenderer(sdlRenderer);
  SDL_DestroyWindow(window);

  SDL_CloseAudio();
  SDL_Quit();

  return 0;
}