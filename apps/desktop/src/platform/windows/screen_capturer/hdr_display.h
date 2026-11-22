/*
 * @Author: DI JUNKUN
 * @Date: 2026-11-23
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _HDR_DISPLAY_H_
#define _HDR_DISPLAY_H_

#include <Windows.h>
#include <d3d11.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <cwchar>
#include <vector>

namespace crossdesk {

inline bool IsHdrOutput(IDXGIOutput* output) {
  Microsoft::WRL::ComPtr<IDXGIOutput6> advanced;
  DXGI_OUTPUT_DESC1 desc{};
  return output && SUCCEEDED(output->QueryInterface(IID_PPV_ARGS(&advanced))) &&
         SUCCEEDED(advanced->GetDesc1(&desc)) &&
         desc.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
}

inline bool IsHdrMonitor(HMONITOR monitor, IDXGIAdapter1** owner = nullptr) {
  Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
  if (!monitor || FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
    return false;
  }
  Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
  for (UINT a = 0; SUCCEEDED(factory->EnumAdapters1(a, adapter.ReleaseAndGetAddressOf())); ++a) {
    Microsoft::WRL::ComPtr<IDXGIOutput> output;
    for (UINT o = 0; SUCCEEDED(adapter->EnumOutputs(o, output.ReleaseAndGetAddressOf())); ++o) {
      DXGI_OUTPUT_DESC desc{};
      if (SUCCEEDED(output->GetDesc(&desc)) && desc.Monitor == monitor) {
        return IsHdrOutput(output.Get()) && (!owner || SUCCEEDED(adapter.CopyTo(owner)));
      }
    }
  }
  return false;
}

inline float SdrWhiteNits(HMONITOR monitor) {
  MONITORINFOEXW info{};
  info.cbSize = sizeof(info);
  UINT32 paths = 0, modes = 0;
  if (GetMonitorInfoW(monitor, &info) &&
      GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &paths, &modes) == ERROR_SUCCESS) {
    std::vector<DISPLAYCONFIG_PATH_INFO> path(paths);
    std::vector<DISPLAYCONFIG_MODE_INFO> mode(modes);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &paths, path.data(), &modes, mode.data(),
                           nullptr) == ERROR_SUCCESS) {
      for (UINT32 i = 0; i < paths; ++i) {
        const auto& source = path[i].sourceInfo;
        DISPLAYCONFIG_SOURCE_DEVICE_NAME name{};
        name.header = {DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME, sizeof(name), source.adapterId,
                       source.id};
        if (DisplayConfigGetDeviceInfo(&name.header) != ERROR_SUCCESS ||
            std::wcscmp(name.viewGdiDeviceName, info.szDevice) != 0) {
          continue;
        }
        const auto& target = path[i].targetInfo;
        DISPLAYCONFIG_SDR_WHITE_LEVEL white{};
        white.header = {DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL, sizeof(white),
                        target.adapterId, target.id};
        if (DisplayConfigGetDeviceInfo(&white.header) == ERROR_SUCCESS && white.SDRWhiteLevel) {
          return 80.f * white.SDRWhiteLevel / 1000.f;
        }
      }
    }
  }
  return 203.f;
}

inline HRESULT DuplicateDesktop(IDXGIOutput* output, ID3D11Device* device, bool hdr,
                                IDXGIOutputDuplication** duplication) {
  if (hdr) {
    Microsoft::WRL::ComPtr<IDXGIOutput5> output5;
    HRESULT hr = output->QueryInterface(IID_PPV_ARGS(&output5));
    if (FAILED(hr)) {
      return hr;
    }
    const DXGI_FORMAT format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    return output5->DuplicateOutput1(device, 0, 1, &format, duplication);
  }
  Microsoft::WRL::ComPtr<IDXGIOutput1> output1;
  HRESULT hr = output->QueryInterface(IID_PPV_ARGS(&output1));
  return FAILED(hr) ? hr : output1->DuplicateOutput(device, duplication);
}

}  // namespace crossdesk

#endif
