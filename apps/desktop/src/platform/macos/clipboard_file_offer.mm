#include "clipboard.h"

#include <AppKit/AppKit.h>
#include <CoreServices/CoreServices.h>
#include <copyfile.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <fstream>
#include <thread>

#include "filesystem_utf8.h"
#include "rd_log.h"

namespace crossdesk {
namespace {

// Finder asks for file URLs when validating menus, too. A small, private marker
// lets the filesystem confirm an actual paste before any network IO starts.
// Only copies containing this offer's unguessable token are ever consumed.
struct PasteState {
  std::shared_ptr<ClipboardFilePromise> offer;
  std::string directory, marker, token;
  dev_t device = 0;
  ino_t inode = 0;
  std::atomic<unsigned> tasks{0};
  ~PasteState() {
    std::error_code ec;
    std::filesystem::remove_all(directory, ec);
  }
};

struct EventContext {
  std::atomic<unsigned> refs{1};
  std::shared_ptr<PasteState> state;
  void Release() {
    if (--refs == 0) delete this;
  }
};

struct NativeOffer {
  std::shared_ptr<PasteState> state;
  FSEventStreamRef stream = nullptr;
  NSInteger change = 0;
  ~NativeOffer() {
    if (state) state->offer->Cancel();
    if (stream) {
      FSEventStreamStop(stream);
      FSEventStreamInvalidate(stream);
      FSEventStreamRelease(stream);
    }
  }
};
std::unique_ptr<NativeOffer> current;

bool WriteFile(int directory, const std::string& source, const std::string& token, size_t index) {
  const auto path = PathFromUtf8(source);
  const auto native_name = path.filename().u8string();
  const std::string name(native_name.begin(), native_name.end());
  const std::string staging = ".crossdesk-receiving-" + token + "-" + std::to_string(index);
  const int input = open(source.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
  if (input < 0) return false;
  const int output =
      openat(directory, staging.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
  if (output < 0) {
    close(input);
    return false;
  }
  const bool copied = fcopyfile(input, output, nullptr, COPYFILE_DATA) == 0;
  close(input);
  close(output);
  bool placed = false;
  if (copied) {
    // Never overwrite an existing destination. Reserve a distinct name with an
    // atomic rename, including when several pastes finish at the same time.
    for (unsigned attempt = 0; attempt < 10000; ++attempt) {
      std::string candidate = name;
      if (attempt) {
        const auto stem = path.stem().u8string(), extension = path.extension().u8string();
        candidate = std::string(stem.begin(), stem.end()) + " (" + std::to_string(attempt) + ")" +
                    std::string(extension.begin(), extension.end());
      }
      if (renameatx_np(directory, staging.c_str(), directory, candidate.c_str(), RENAME_EXCL) ==
          0) {
        placed = true;
        break;
      }
      if (errno != EEXIST) break;
    }
  }
  if (!placed) unlinkat(directory, staging.c_str(), 0);
  return placed;
}

void ObservePaste(const std::shared_ptr<PasteState>& state, const char* value) {
  const std::filesystem::path path(value);
  const auto name = path.filename().string();
  if (name.compare(0, state->marker.size(), state->marker) != 0 || !state->offer->valid()) return;
  const int directory = open(path.parent_path().c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (directory < 0) return;
  const int file = openat(directory, name.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
  struct stat info{};
  char token[128]{};
  const bool pasted =
      file >= 0 && fstat(file, &info) == 0 && S_ISREG(info.st_mode) && info.st_uid == geteuid() &&
      !(info.st_dev == state->device && info.st_ino == state->inode) &&
      info.st_size == static_cast<off_t>(state->token.size()) &&
      read(file, token, sizeof(token)) == static_cast<ssize_t>(state->token.size()) &&
      std::string_view(token, state->token.size()) == state->token;
  if (file >= 0) close(file);
  if (!pasted) {
    close(directory);
    return;
  }
  // The copied marker belongs to us. Remove it before downloading so duplicate
  // filesystem events cannot start the same paste twice.
  if (unlinkat(directory, name.c_str(), 0) != 0) {
    close(directory);
    return;
  }
  if (state->tasks.fetch_add(1) >= 16) {
    --state->tasks;
    close(directory);
    return;
  }
  const std::string task = NSUUID.UUID.UUIDString.UTF8String;
  std::thread([state, directory, task] {
    const auto files = state->offer->Wait();
    if (files) {
      for (size_t i = 0; i < files->paths.size(); ++i) {
        if (!WriteFile(directory, files->paths[i], task, i)) {
          state->offer->Fail();
          LOG_WARN("Could not finish Finder clipboard file paste");
          break;
        }
      }
    }
    close(directory);
    --state->tasks;
  }).detach();
}

}  // namespace

bool Clipboard::SetFileOffer(std::shared_ptr<ClipboardFilePromise> offer) {
  @autoreleasepool {
    if (!offer || offer->entries().empty()) return false;
    ClearFileOffer();
    auto native = std::make_unique<NativeOffer>();
    auto state = std::make_shared<PasteState>();
    state->offer = std::move(offer);
    state->token = NSUUID.UUID.UUIDString.UTF8String;
    state->marker = ".crossdesk-paste-" + state->token;
    std::string pattern = std::string(NSTemporaryDirectory().UTF8String) + "crossdesk-paste-XXXXXX";
    if (!mkdtemp(pattern.data())) return false;
    state->directory = pattern;
    const auto source = pattern + "/" + state->marker;
    {
      std::ofstream file(source, std::ios::binary);
      file << state->token;
      if (!file) return false;
    }
    struct stat info{};
    if (stat(source.c_str(), &info) != 0) return false;
    state->device = info.st_dev;
    state->inode = info.st_ino;
    native->state = state;
    FSEventStreamContext context{};
    auto* event_context = new EventContext;
    event_context->state = state;
    context.info = event_context;
    context.retain = [](const void* value) -> const void* {
      auto* context = const_cast<EventContext*>(static_cast<const EventContext*>(value));
      ++context->refs;
      return value;
    };
    context.release = [](const void* value) {
      const_cast<EventContext*>(static_cast<const EventContext*>(value))->Release();
    };
    native->stream = FSEventStreamCreate(
        kCFAllocatorDefault,
        [](ConstFSEventStreamRef, void* info, size_t count, void* paths,
           const FSEventStreamEventFlags flags[], const FSEventStreamEventId[]) {
          const auto state = static_cast<EventContext*>(info)->state;
          const auto names = static_cast<char**>(paths);
          for (size_t i = 0; i < count; ++i)
            if (flags[i] & kFSEventStreamEventFlagItemIsFile) ObservePaste(state, names[i]);
        },
        &context, (__bridge CFArrayRef) @[ @"/" ], kFSEventStreamEventIdSinceNow, 0.05,
        kFSEventStreamCreateFlagFileEvents | kFSEventStreamCreateFlagNoDefer);
    event_context->Release();
    if (!native->stream) return false;
    FSEventStreamSetDispatchQueue(native->stream, dispatch_get_main_queue());
    if (!FSEventStreamStart(native->stream)) return false;
    NSPasteboard* pasteboard = NSPasteboard.generalPasteboard;
    [pasteboard clearContents];
    if (![pasteboard
            writeObjects:@[ [NSURL
                             fileURLWithPath:[NSString stringWithUTF8String:source.c_str()]] ]])
      return false;
    native->change = pasteboard.changeCount;
    current = std::move(native);
    return true;
  }
}

bool Clipboard::OwnsFileOffer(const std::shared_ptr<ClipboardFilePromise>& offer) {
  @autoreleasepool {
    return current && current->state->offer == offer &&
           NSPasteboard.generalPasteboard.changeCount == current->change;
  }
}

void Clipboard::ClearFileOffer() {
  @autoreleasepool {
    if (current && NSPasteboard.generalPasteboard.changeCount == current->change)
      [NSPasteboard.generalPasteboard clearContents];
    current.reset();
  }
}

}  // namespace crossdesk
