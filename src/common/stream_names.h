#ifndef _STREAM_NAMES_H_
#define _STREAM_NAMES_H_

#include <cstddef>

namespace crossdesk::protocol {

inline constexpr char kAudioStream[] = "control_audio";
inline constexpr char kDataStream[] = "data";
inline constexpr char kMouseStream[] = "mouse";
inline constexpr char kKeyboardStream[] = "keyboard";
inline constexpr char kControlStream[] = "control_data";
inline constexpr char kFileStream[] = "file";
inline constexpr char kFileFeedbackStream[] = "file_feedback";
inline constexpr char kClipboardStream[] = "clipboard";

inline constexpr std::size_t kFileChunkSize = 64 * 1024;
inline constexpr std::size_t kMaxClipboardBytes = 128 * 1024;

}  // namespace crossdesk::protocol

#endif
