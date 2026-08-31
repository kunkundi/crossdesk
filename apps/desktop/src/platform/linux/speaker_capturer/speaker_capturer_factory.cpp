#include "speaker_capturer_factory.h"

#include "speaker_capturer_linux.h"

namespace crossdesk {

SpeakerCapturer* SpeakerCapturerFactory::Create() {
  return new SpeakerCapturerLinux();
}

}  // namespace crossdesk
