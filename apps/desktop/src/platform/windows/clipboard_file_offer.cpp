#include <shlobj.h>
#include <shlwapi.h>
#include <windows.h>

#include <atomic>
#include <condition_variable>
#include <thread>

#include "clipboard.h"
#include "filesystem_utf8.h"

namespace crossdesk {
namespace {

// Keep the cache alive until Explorer releases its last file stream.
class CachedStream final : public IStream {
 public:
  CachedStream(std::shared_ptr<ClipboardFilePromise> offer, size_t index)
      : offer_(std::move(offer)), index_(index) {}
  CachedStream(IStream* stream, std::shared_ptr<ClipboardFilePromise::Files> files)
      : stream_(stream), files_(std::move(files)) {}
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
    if (!out) return E_POINTER;
    *out = nullptr;
    if (iid != IID_IUnknown && iid != IID_ISequentialStream && iid != IID_IStream)
      return E_NOINTERFACE;
    *out = static_cast<IStream*>(this);
    AddRef();
    return S_OK;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
  ULONG STDMETHODCALLTYPE Release() override {
    const auto n = --refs_;
    if (!n) delete this;
    return n;
  }
  HRESULT STDMETHODCALLTYPE Read(void* data, ULONG size, ULONG* read) override {
    if (read) *read = 0;
    const auto status = Load();
    return FAILED(status) ? status : stream_->Read(data, size, read);
  }
  HRESULT STDMETHODCALLTYPE Write(const void*, ULONG, ULONG*) override {
    return STG_E_ACCESSDENIED;
  }
  HRESULT STDMETHODCALLTYPE Seek(LARGE_INTEGER move, DWORD origin,
                                 ULARGE_INTEGER* result) override {
    const auto status = Load();
    return FAILED(status) ? status : stream_->Seek(move, origin, result);
  }
  HRESULT STDMETHODCALLTYPE SetSize(ULARGE_INTEGER) override { return STG_E_ACCESSDENIED; }
  HRESULT STDMETHODCALLTYPE CopyTo(IStream* target, ULARGE_INTEGER size, ULARGE_INTEGER* read,
                                   ULARGE_INTEGER* written) override {
    const auto status = Load();
    return FAILED(status) ? status : stream_->CopyTo(target, size, read, written);
  }
  HRESULT STDMETHODCALLTYPE Commit(DWORD) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE Revert() override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE LockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override {
    return STG_E_INVALIDFUNCTION;
  }
  HRESULT STDMETHODCALLTYPE UnlockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override {
    return STG_E_INVALIDFUNCTION;
  }
  HRESULT STDMETHODCALLTYPE Stat(STATSTG* stat, DWORD flags) override {
    if (!stat) return E_POINTER;
    if (stream_) return stream_->Stat(stat, flags);
    *stat = {};
    const auto& entry = offer_->entries()[index_];
    stat->type = STGTY_STREAM;
    stat->cbSize.QuadPart = entry.size;
    stat->grfMode = STGM_READ;
    if (!(flags & STATFLAG_NONAME)) {
      const auto name = PathFromUtf8(entry.name).native();
      const auto bytes = (name.size() + 1) * sizeof(wchar_t);
      stat->pwcsName = static_cast<LPOLESTR>(CoTaskMemAlloc(bytes));
      if (!stat->pwcsName) return E_OUTOFMEMORY;
      memcpy(stat->pwcsName, name.c_str(), bytes);
    }
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE Clone(IStream** result) override {
    if (!result) return E_POINTER;
    *result = nullptr;
    if (!stream_) {
      *result = new CachedStream(offer_, index_);
      return S_OK;
    }
    IStream* stream = nullptr;
    const auto status = stream_->Clone(&stream);
    if (SUCCEEDED(status)) *result = new CachedStream(stream, files_);
    return status;
  }

 private:
  HRESULT Load() {
    if (stream_) return S_OK;
    files_ = offer_->Wait();
    if (!files_ || index_ >= files_->paths.size()) return E_ABORT;
    return SHCreateStreamOnFileEx(PathFromUtf8(files_->paths[index_]).c_str(),
                                  STGM_READ | STGM_SHARE_DENY_WRITE, FILE_ATTRIBUTE_NORMAL, FALSE,
                                  nullptr, &stream_);
  }
  ~CachedStream() {
    if (stream_) stream_->Release();
  }
  std::atomic<ULONG> refs_{1};
  IStream* stream_ = nullptr;
  std::shared_ptr<ClipboardFilePromise> offer_;
  size_t index_ = 0;
  std::shared_ptr<ClipboardFilePromise::Files> files_;
};

class FileDataObject final : public IDataObject, public IDataObjectAsyncCapability {
 public:
  explicit FileDataObject(std::shared_ptr<ClipboardFilePromise> offer) : offer_(std::move(offer)) {
    descriptor_ = static_cast<CLIPFORMAT>(RegisterClipboardFormatW(L"FileGroupDescriptorW"));
    contents_ = static_cast<CLIPFORMAT>(RegisterClipboardFormatW(L"FileContents"));
    effect_ = static_cast<CLIPFORMAT>(RegisterClipboardFormatW(L"Preferred DropEffect"));
  }
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
    if (!out) return E_POINTER;
    *out = nullptr;
    if (iid == IID_IUnknown || iid == IID_IDataObject)
      *out = static_cast<IDataObject*>(this);
    else if (iid == __uuidof(IDataObjectAsyncCapability))
      *out = static_cast<IDataObjectAsyncCapability*>(this);
    else
      return E_NOINTERFACE;
    AddRef();
    return S_OK;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
  ULONG STDMETHODCALLTYPE Release() override {
    const auto n = --refs_;
    if (!n) delete this;
    return n;
  }
  HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC* format) override {
    if (!format) return E_POINTER;
    if (!offer_->valid()) return DV_E_FORMATETC;
    if (format->dwAspect != DVASPECT_CONTENT) return DV_E_DVASPECT;
    if (format->cfFormat == contents_)
      return (format->tymed & TYMED_ISTREAM) && format->lindex >= -1 &&
                     (format->lindex == -1 ||
                      static_cast<size_t>(format->lindex) < offer_->entries().size())
                 ? S_OK
                 : DV_E_FORMATETC;
    if ((format->cfFormat == descriptor_ || format->cfFormat == effect_) &&
        (format->tymed & TYMED_HGLOBAL) && format->lindex == -1)
      return S_OK;
    return DV_E_FORMATETC;
  }
  HRESULT STDMETHODCALLTYPE GetData(FORMATETC* format, STGMEDIUM* medium) override {
    if (!medium) return E_POINTER;
    *medium = {};
    const auto supported = QueryGetData(format);
    if (FAILED(supported)) return supported;
    if (format->cfFormat == contents_) {
      if (format->lindex < 0) return DV_E_LINDEX;
      // Only FileContents is a paste request. Format enumeration, menu checks
      // and file descriptors never fetch bytes. This runs on our OLE thread.
      if (!offer_->Request()) return E_ABORT;
      medium->tymed = TYMED_ISTREAM;
      // Return the stream immediately; rendering a large batch must not hold
      // the clipboard's GetData call until the download finishes.
      medium->pstm = new CachedStream(offer_, static_cast<size_t>(format->lindex));
      return S_OK;
    }
    const auto& entries = offer_->entries();
    const size_t bytes = format->cfFormat == effect_ ? sizeof(DWORD)
                                                     : FIELD_OFFSET(FILEGROUPDESCRIPTORW, fgd) +
                                                           entries.size() * sizeof(FILEDESCRIPTORW);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, bytes);
    if (!memory) return E_OUTOFMEMORY;
    void* data = GlobalLock(memory);
    if (!data) {
      GlobalFree(memory);
      return E_OUTOFMEMORY;
    }
    if (format->cfFormat == effect_)
      *static_cast<DWORD*>(data) = DROPEFFECT_COPY;
    else {
      auto* group = static_cast<FILEGROUPDESCRIPTORW*>(data);
      group->cItems = static_cast<UINT>(entries.size());
      for (size_t i = 0; i < entries.size(); ++i) {
        auto& file = group->fgd[i];
        file.dwFlags = FD_ATTRIBUTES | FD_FILESIZE | FD_UNICODE | FD_PROGRESSUI;
        file.dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
        file.nFileSizeHigh = static_cast<DWORD>(entries[i].size >> 32);
        file.nFileSizeLow = static_cast<DWORD>(entries[i].size);
        auto name = PathFromUtf8(entries[i].name).native();
        for (auto& ch : name)
          if (ch < 32 || std::wstring(L"\\/:*?\"<>|").find(ch) != std::wstring::npos) ch = L'_';
        while (!name.empty() && (name.back() == L'.' || name.back() == L' ')) name.pop_back();
        if (name.empty()) name = L"received_file";
        wcsncpy_s(file.cFileName, name.c_str(), _TRUNCATE);
      }
    }
    GlobalUnlock(memory);
    medium->tymed = TYMED_HGLOBAL;
    medium->hGlobal = memory;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC*, STGMEDIUM*) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC*, FORMATETC* out) override {
    if (!out) return E_POINTER;
    out->ptd = nullptr;
    return E_NOTIMPL;
  }
  HRESULT STDMETHODCALLTYPE SetData(FORMATETC*, STGMEDIUM*, BOOL) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD direction, IEnumFORMATETC** out) override {
    if (direction != DATADIR_GET) return E_NOTIMPL;
    FORMATETC formats[] = {{descriptor_, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL},
                           {contents_, nullptr, DVASPECT_CONTENT, -1, TYMED_ISTREAM},
                           {effect_, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL}};
    return SHCreateStdEnumFmtEtc(3, formats, out);
  }
  HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) override {
    return OLE_E_ADVISENOTSUPPORTED;
  }
  HRESULT STDMETHODCALLTYPE DUnadvise(DWORD) override { return OLE_E_ADVISENOTSUPPORTED; }
  HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA**) override {
    return OLE_E_ADVISENOTSUPPORTED;
  }
  HRESULT STDMETHODCALLTYPE SetAsyncMode(BOOL value) override {
    async_ = value;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GetAsyncMode(BOOL* value) override {
    if (!value) return E_POINTER;
    *value = async_;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE StartOperation(IBindCtx*) override {
    operation_ = TRUE;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE InOperation(BOOL* value) override {
    if (!value) return E_POINTER;
    *value = operation_;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE EndOperation(HRESULT, IBindCtx*, DWORD) override {
    operation_ = FALSE;
    return S_OK;
  }

 private:
  std::atomic<ULONG> refs_{1};
  std::shared_ptr<ClipboardFilePromise> offer_;
  CLIPFORMAT descriptor_, contents_, effect_;
  BOOL async_ = TRUE, operation_ = FALSE;
};

// OLE can synchronously render data on its owning STA. Give that STA a separate
// message loop so even a synchronous paste leaves the desktop/progress UI live.
class ClipboardApartment {
 public:
  ClipboardApartment() {
    worker_ = std::thread([this] {
      const auto initialized = OleInitialize(nullptr);
      HWND window = nullptr;
      if (SUCCEEDED(initialized)) {
        WNDCLASSW cls{};
        cls.lpfnWndProc = WindowProc;
        cls.hInstance = GetModuleHandleW(nullptr);
        cls.lpszClassName = L"CrossDeskFileClipboard";
        RegisterClassW(&cls);
        window = CreateWindowExW(0, cls.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                                 cls.hInstance, this);
      }
      {
        std::lock_guard lock(mutex_);
        window_ = window;
        ready_ = true;
      }
      wake_.notify_all();
      if (window) {
        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
          TranslateMessage(&message);
          DispatchMessageW(&message);
        }
        if (object_) {
          if (OleIsCurrentClipboard(object_) == S_OK) OleSetClipboard(nullptr);
          object_->Release();
        }
        DestroyWindow(window);
      }
      if (SUCCEEDED(initialized)) OleUninitialize();
    });
    std::unique_lock lock(mutex_);
    wake_.wait(lock, [this] { return ready_; });
  }
  ~ClipboardApartment() {
    Clear();
    if (window_) PostMessageW(window_, WM_CLOSE, 0, 0);
    if (worker_.joinable()) worker_.join();
  }
  bool Set(std::shared_ptr<ClipboardFilePromise> offer) {
    Clear();
    if (!window_) return false;
    if (!SendMessageW(window_, WM_APP, 0, reinterpret_cast<LPARAM>(&offer))) return false;
    offer_ = std::move(offer);
    owner_ = GetClipboardOwner();
    return true;
  }
  void Clear() {
    // Wake a rendering GetData before sending work to its STA.
    if (offer_) offer_->Cancel();
    if (window_) SendMessageW(window_, WM_APP + 1, 0, 0);
    offer_.reset();
  }
  bool Owns(const std::shared_ptr<ClipboardFilePromise>& offer) const {
    return offer_ == offer && GetClipboardOwner() == owner_;
  }

 private:
  static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* self = reinterpret_cast<ClipboardApartment*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
      self = static_cast<ClipboardApartment*>(
          reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
      SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (self && message == WM_APP) {
      auto* object =
          new FileDataObject(*reinterpret_cast<std::shared_ptr<ClipboardFilePromise>*>(lparam));
      if (FAILED(OleSetClipboard(object))) {
        object->Release();
        return FALSE;
      }
      self->object_ = object;
      return TRUE;
    }
    if (self && message == WM_APP + 1) {
      if (self->object_) {
        if (OleIsCurrentClipboard(self->object_) == S_OK) OleSetClipboard(nullptr);
        self->object_->Release();
        self->object_ = nullptr;
      }
      return 0;
    }
    if (message == WM_CLOSE) {
      PostQuitMessage(0);
      return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
  }
  HWND window_ = nullptr;
  FileDataObject* object_ = nullptr;  // OLE thread only.
  std::thread worker_;
  std::mutex mutex_;
  std::condition_variable wake_;
  bool ready_ = false;
  HWND owner_ = nullptr;
  std::shared_ptr<ClipboardFilePromise> offer_;  // UI thread only.
};
std::unique_ptr<ClipboardApartment> apartment;

}  // namespace

bool Clipboard::SetFileOffer(std::shared_ptr<ClipboardFilePromise> offer, FileOfferError* error) {
  if (error) *error = FileOfferError::PublishFailed;
  if (!offer || offer->entries().empty()) return false;
  if (!apartment) apartment = std::make_unique<ClipboardApartment>();
  if (!apartment->Set(std::move(offer))) return false;
  if (error) *error = FileOfferError::None;
  return true;
}
bool Clipboard::OwnsFileOffer(const std::shared_ptr<ClipboardFilePromise>& offer) {
  return apartment && apartment->Owns(offer);
}
void Clipboard::ClearFileOffer() {
  if (apartment) apartment->Clear();
}

}  // namespace crossdesk
