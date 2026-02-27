#ifndef _WGC_SESSION_IMPL_H_
#define _WGC_SESSION_IMPL_H_

#include <d3d11_4.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>

#include <mutex>
#include <thread>

#include "wgc_session.h"

namespace crossdesk {

class WgcSessionImpl : public WgcSession {
  struct __declspec(uuid("A9B3D012-3DF2-4EE3-B8D1-8695F457D3C1"))
  IDirect3DDxgiInterfaceAccess : ::IUnknown {
    virtual HRESULT __stdcall GetInterface(GUID const& id, void** object) = 0;
  };

  template <typename T>
  inline auto GetDXGIInterfaceFromObject(
      winrt::Windows::Foundation::IInspectable const& object) {
    auto access = object.as<IDirect3DDxgiInterfaceAccess>();
    winrt::com_ptr<T> result;
    winrt::check_hresult(
        access->GetInterface(winrt::guid_of<T>(), result.put_void()));
    return result;
  }

  struct {
    union {
      HWND hwnd;
      HMONITOR hmonitor;
    };
    bool is_window;
  } target_{0};

 public:
  WgcSessionImpl(int id);
  ~WgcSessionImpl() override;

 public:
  void Release() override;

  int Initialize(HWND hwnd) override;
  int Initialize(HMONITOR hmonitor) override;

  void RegisterObserver(wgc_session_observer* observer) override;

  int Start(bool show_cursor) override;
  int Stop() override;

  int Pause() override;
  int Resume() override;

 private:
  auto CreateD3D11Device();
  auto CreateCaptureItemForWindow(HWND hwnd);
  auto CreateCaptureItemForMonitor(HMONITOR hmonitor);

  HRESULT CreateMappedTexture(winrt::com_ptr<ID3D11Texture2D> src_texture,
                              unsigned int width = 0, unsigned int height = 0);
  void OnFrame(
      winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool const&
          sender,
      winrt::Windows::Foundation::IInspectable const& args);
  void OnClosed(winrt::Windows::Graphics::Capture::GraphicsCaptureItem const&,
                winrt::Windows::Foundation::IInspectable const&);

  int Initialize();
  void CleanUp();

  // void message_func();

 private:
  int id_ = -1;
  std::mutex lock_;
  bool is_initialized_ = false;
  bool is_running_ = false;
  bool is_paused_ = false;
  bool last_show_cursor_ = false;

  wgc_session_observer* observer_ = nullptr;

  // wgc
  winrt::Windows::Graphics::Capture::GraphicsCaptureItem capture_item_{nullptr};
  winrt::Windows::Graphics::Capture::GraphicsCaptureSession capture_session_{
      nullptr};
  winrt::Windows::Graphics::SizeInt32 capture_frame_size_;

  winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice
      d3d11_direct_device_{nullptr};
  winrt::com_ptr<ID3D11DeviceContext> d3d11_device_context_{nullptr};
  winrt::com_ptr<ID3D11Texture2D> d3d11_texture_mapped_{nullptr};

  std::atomic<bool> cleaned_ = false;
  winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool
      capture_framepool_{nullptr};
  winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool::
      FrameArrived_revoker capture_framepool_trigger_;
  winrt::Windows::Graphics::Capture::GraphicsCaptureItem::Closed_revoker
      capture_close_trigger_;

  // message loop
  std::thread loop_;
  HWND hwnd_ = nullptr;
};

// template <typename T>
// inline auto WgcSessionImpl::GetDXGIInterfaceFromObject(
//     winrt::Windows::Foundation::IInspectable const &object) {
//   auto access = object.as<IDirect3DDxgiInterfaceAccess>();
//   winrt::com_ptr<T> result;
//   winrt::check_hresult(
//       access->GetInterface(winrt::guid_of<T>(), result.put_void()));
//   return result;
// }
}  // namespace crossdesk
#endif
