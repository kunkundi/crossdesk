#include "speaker_capturer_factory.h"

#include "speaker_capturer_macosx.h"

namespace crossdesk {

SpeakerCapturer* SpeakerCapturerFactory::Create() {
  return new SpeakerCapturerMacosx();
}

}  // namespace crossdesk
