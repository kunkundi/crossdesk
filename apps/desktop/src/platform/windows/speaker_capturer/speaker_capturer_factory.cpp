#include "speaker_capturer_factory.h"

#include "speaker_capturer_wasapi.h"

namespace crossdesk {

SpeakerCapturer* SpeakerCapturerFactory::Create() {
  return new SpeakerCapturerWasapi();
}

}  // namespace crossdesk
