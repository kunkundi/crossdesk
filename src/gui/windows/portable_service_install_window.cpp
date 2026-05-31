#include "render.h"

#if _WIN32 && CROSSDESK_PORTABLE

#include <shellapi.h>

#include <algorithm>
#include <vector>

#include "localization.h"
#include "rd_log.h"
#include "service_host.h"

namespace crossdesk {
namespace {

std::filesystem::path GetCurrentExecutablePath() {
  std::vector<wchar_t> buffer(MAX_PATH);
  while (true) {
    DWORD length = GetModuleFileNameW(nullptr, buffer.data(),
                                      static_cast<DWORD>(buffer.size()));
    if (length == 0) {
      return {};
    }
    if (length < buffer.size()) {
      return std::filesystem::path(buffer.data(), buffer.data() + length);
    }
    if (buffer.size() >= 32768) {
      return {};
    }
    buffer.resize(buffer.size() * 2);
  }
}

bool InstallServiceWithElevation() {
  const std::filesystem::path executable_path = GetCurrentExecutablePath();
  if (executable_path.empty()) {
    LOG_ERROR("Portable service install failed: current executable not found");
    return false;
  }

  const std::filesystem::path service_path =
      executable_path.parent_path() / L"crossdesk_service.exe";
  const std::filesystem::path helper_path =
      executable_path.parent_path() / L"crossdesk_session_helper.exe";
  if (!std::filesystem::exists(service_path) ||
      !std::filesystem::exists(helper_path)) {
    LOG_ERROR(
        "Portable service install failed: service binaries missing, "
        "service={}, "
        "helper={}",
        service_path.string(), helper_path.string());
    return false;
  }

  std::wstring executable = executable_path.wstring();
  std::wstring working_dir = executable_path.parent_path().wstring();
  std::wstring parameters = L"--service-install";

  SHELLEXECUTEINFOW execute_info{};
  execute_info.cbSize = sizeof(execute_info);
  execute_info.fMask = SEE_MASK_NOCLOSEPROCESS;
  execute_info.hwnd = nullptr;
  execute_info.lpVerb = L"runas";
  execute_info.lpFile = executable.c_str();
  execute_info.lpParameters = parameters.c_str();
  execute_info.lpDirectory = working_dir.c_str();
  execute_info.nShow = SW_HIDE;

  if (!ShellExecuteExW(&execute_info)) {
    LOG_ERROR("Portable service install failed: ShellExecuteExW error={}",
              GetLastError());
    return false;
  }

  DWORD wait_result = WaitForSingleObject(execute_info.hProcess, INFINITE);
  DWORD exit_code = 1;
  if (wait_result == WAIT_OBJECT_0) {
    GetExitCodeProcess(execute_info.hProcess, &exit_code);
  } else {
    LOG_ERROR("Portable service install wait failed, result={}", wait_result);
  }
  CloseHandle(execute_info.hProcess);

  if (exit_code != 0) {
    LOG_ERROR("Portable service install command failed, exit_code={}",
              exit_code);
    return false;
  }

  const bool started = StartCrossDeskService();
  if (!started) {
    LOG_WARN("Portable service installed but start failed");
  }
  return IsCrossDeskServiceInstalled() && started;
}

}  // namespace

void Render::CheckPortableWindowsService() {
  if (portable_service_prompt_checked_) {
    return;
  }
  portable_service_prompt_checked_ = true;

  if (IsCrossDeskServiceInstalled()) {
    return;
  }

  if (portable_service_prompt_suppressed_) {
    return;
  }

  portable_service_install_state_.store(PortableServiceInstallState::idle,
                                        std::memory_order_relaxed);
  show_portable_service_install_window_ = true;
}

void Render::StartPortableWindowsServiceInstall() {
  portable_service_do_not_remind_ = false;
  PortableServiceInstallState expected = PortableServiceInstallState::idle;
  if (!portable_service_install_state_.compare_exchange_strong(
          expected, PortableServiceInstallState::installing,
          std::memory_order_acq_rel)) {
    if (expected != PortableServiceInstallState::failed) {
      return;
    }
    portable_service_install_state_.store(
        PortableServiceInstallState::installing, std::memory_order_release);
  }

  JoinPortableWindowsServiceInstallThread();
  portable_service_install_thread_ = std::thread([this]() {
    const bool installed = InstallServiceWithElevation();
    portable_service_install_state_.store(
        installed ? PortableServiceInstallState::succeeded
                  : PortableServiceInstallState::failed,
        std::memory_order_release);
  });
}

void Render::JoinPortableWindowsServiceInstallThread() {
  if (portable_service_install_thread_.joinable()) {
    portable_service_install_thread_.join();
  }
}

int Render::PortableServiceInstallWindow() {
  if (!show_portable_service_install_window_ &&
      !show_portable_service_prompt_suppressed_window_) {
    return 0;
  }

  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  const float window_width =
      (std::min)(viewport->WorkSize.x * 0.6f, title_bar_button_width_ * 18.0f);
  const float window_height =
      (std::min)(viewport->WorkSize.y * 0.5f, title_bar_button_width_ * 8.0f);

  if (show_portable_service_prompt_suppressed_window_) {
    const float notice_width = window_width;
    const float notice_height = (std::min)(viewport->WorkSize.y * 0.35f,
                                           title_bar_button_width_ * 4.6f);
    ImGui::SetNextWindowPos(
        ImVec2(
            viewport->WorkPos.x + (viewport->WorkSize.x - notice_width) / 2.0f,
            viewport->WorkPos.y +
                (viewport->WorkSize.y - notice_height) / 2.0f),
        ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(ImVec2(notice_width, notice_height),
                             ImGuiCond_Always);

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, window_rounding_);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, window_rounding_ * 0.5f);

    ImGui::Begin(
        localization::notification[localization_language_index_].c_str(),
        nullptr,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoTitleBar);

    ImGui::Spacing();
    ImGui::SetWindowFontScale(0.55f);
    ImGui::SetCursorPosX(notice_width * 0.08f);
    ImGui::Text(
        "%s", localization::notification[localization_language_index_].c_str());

    ImGui::SetWindowFontScale(0.5f);
    ImGui::SetCursorPosX(notice_width * 0.06f);
    ImGui::SetCursorPosY(notice_height * 0.28f);
    ImGui::PushTextWrapPos(notice_width * 0.88f);
    ImGui::TextWrapped("%s",
                       localization::windows_service_prompt_suppressed_message
                           [localization_language_index_]
                               .c_str());
    ImGui::PopTextWrapPos();

    const std::string ok_label = localization::ok[localization_language_index_];
    const ImGuiStyle& style = ImGui::GetStyle();
    const float ok_width =
        ImGui::CalcTextSize(ok_label.c_str()).x + style.FramePadding.x * 2.0f;
    ImGui::SetCursorPosX((notice_width - ok_width) * 0.5f);
    ImGui::SetCursorPosY(notice_height * 0.75f);
    if (ImGui::Button(ok_label.c_str())) {
      show_portable_service_prompt_suppressed_window_ = false;
    }

    ImGui::SetWindowFontScale(1.0f);
    ImGui::End();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor();
  }

  if (!show_portable_service_install_window_) {
    return 0;
  }

  ImGui::SetNextWindowPos(
      ImVec2(
          viewport->WorkPos.x + (viewport->WorkSize.x - window_width) / 2.0f,
          viewport->WorkPos.y + (viewport->WorkSize.y - window_height) / 2.0f),
      ImGuiCond_Appearing);
  ImGui::SetNextWindowSize(ImVec2(window_width, window_height),
                           ImGuiCond_Always);

  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, window_rounding_);
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, window_rounding_ * 0.5f);

  ImGui::Begin(
      localization::windows_service_setup_title[localization_language_index_]
          .c_str(),
      nullptr,
      ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
          ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
          ImGuiWindowFlags_NoTitleBar);

  ImGui::Spacing();
  ImGui::SetWindowFontScale(0.55f);
  ImGui::SetCursorPosX(window_width * 0.08f);
  ImGui::Text(
      "%s",
      localization::windows_service_setup_title[localization_language_index_]
          .c_str());

  const PortableServiceInstallState state =
      portable_service_install_state_.load(std::memory_order_acquire);
  const char* status_text = nullptr;
  if (state == PortableServiceInstallState::installing ||
      state == PortableServiceInstallState::succeeded ||
      state == PortableServiceInstallState::failed) {
    status_text =
        localization::installing_windows_service[localization_language_index_]
            .c_str();
    if (state == PortableServiceInstallState::succeeded) {
      status_text = localization::windows_service_install_success
                        [localization_language_index_]
                            .c_str();
    } else if (state == PortableServiceInstallState::failed) {
      status_text = localization::windows_service_install_failed
                        [localization_language_index_]
                            .c_str();
    }
  }

  ImGui::SetWindowFontScale(0.45f);
  ImGui::SetCursorPosX(window_width * 0.04f);
  ImGui::SetCursorPosY(window_height * 0.22f);
  ImGui::BeginChild("PortableServiceInstallContent",
                    ImVec2(window_width * 0.92f, window_height * 0.45f),
                    ImGuiChildFlags_Borders, ImGuiWindowFlags_None);
  ImGui::SetWindowFontScale(0.5f);
  const float wrap_pos = ImGui::GetContentRegionAvail().x;
  ImGui::PushTextWrapPos(wrap_pos);
  ImGui::TextWrapped(
      "%s",
      localization::windows_service_setup_message[localization_language_index_]
          .c_str());
  if (status_text != nullptr) {
    ImGui::Spacing();
    ImGui::TextWrapped("%s", status_text);
  }
  ImGui::PopTextWrapPos();
  ImGui::EndChild();

  ImGui::SetWindowFontScale(0.5f);
  ImGui::SetCursorPosX(window_width * 0.08f);
  ImGui::SetCursorPosY(window_height * 0.71f);
  ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
  ImGui::Checkbox(
      localization::do_not_remind_again[localization_language_index_].c_str(),
      &portable_service_do_not_remind_);
  ImGui::PopStyleVar();

  const float button_y = window_height * 0.84f;
  const ImGuiStyle& style = ImGui::GetStyle();
  const auto default_button_width = [&style](const std::string& label) {
    return ImGui::CalcTextSize(label.c_str()).x + style.FramePadding.x * 2.0f;
  };
  const std::string install_label =
      localization::install_windows_service[localization_language_index_];
  const std::string cancel_label =
      localization::cancel[localization_language_index_];
  const std::string ok_label = localization::ok[localization_language_index_];
  const float buttons_width = state == PortableServiceInstallState::succeeded
                                  ? default_button_width(ok_label)
                                  : default_button_width(install_label) +
                                        style.ItemSpacing.x +
                                        default_button_width(cancel_label);
  ImGui::SetCursorPosX((window_width - buttons_width) * 0.5f);
  ImGui::SetCursorPosY(button_y);

  if (state == PortableServiceInstallState::succeeded) {
    if (ImGui::Button(ok_label.c_str())) {
      show_portable_service_install_window_ = false;
      JoinPortableWindowsServiceInstallThread();
    }
  } else {
    if (state == PortableServiceInstallState::installing) {
      ImGui::BeginDisabled();
    }
    if (ImGui::Button(install_label.c_str())) {
      StartPortableWindowsServiceInstall();
    }
    if (state == PortableServiceInstallState::installing) {
      ImGui::EndDisabled();
    }

    ImGui::SameLine();
    if (state == PortableServiceInstallState::installing) {
      ImGui::BeginDisabled();
    }
    if (ImGui::Button(cancel_label.c_str())) {
      if (portable_service_do_not_remind_) {
        portable_service_prompt_suppressed_ = true;
        config_center_->SetPortableServicePromptSuppressed(true);
        show_portable_service_prompt_suppressed_window_ = true;
      }
      show_portable_service_install_window_ = false;
    }
    if (state == PortableServiceInstallState::installing) {
      ImGui::EndDisabled();
    }
  }

  ImGui::SetWindowFontScale(1.0f);
  ImGui::End();
  ImGui::PopStyleVar(3);
  ImGui::PopStyleColor();

  return 0;
}

}  // namespace crossdesk

#endif
