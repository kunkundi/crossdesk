/*
 * @Author: DI JUNKUN
 * @Date: 2026-11-03
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _AUTOMATION_PROTOCOL_H_
#define _AUTOMATION_PROTOCOL_H_

#include <frame_image.h>
#include <initializer_list>
#include <string>
#include <vector>

namespace crossdesk::cli::automation {
using nlohmann::json;
void Fields(const json &, std::initializer_list<const char *> names);
int Number(const json &, const char *key, int low, int high, int fallback);
uint64_t Sequence(const json &, const char *key, uint64_t fallback = 0);
bool Flag(const json &, const char *key, bool fallback = false);
std::string String(const json &, const char *key, size_t limit = 128);

struct InputGeometry {
  int width, height, source_width, source_height;
  ImageRegion region;
};
enum class StepKind { Pointer, Key, Text };
struct Step {
  StepKind kind = StepKind::Pointer;
  int delay = 0, key = 0, flag = 0, wheel = 0;
  float x = 0, y = 0;
  bool down = false;
  std::string text;
};

// Pure validation and translation: no session access or input side effects.
std::vector<Step> BuildInputSteps(const json &action,
                                  const InputGeometry &geometry);
std::vector<std::vector<Step>> BuildInputSequence(const json &action,
                                                const InputGeometry &geometry);
std::vector<Step> BuildUnlockSteps(const std::string &password, bool macos);
} // namespace crossdesk::cli::automation

#endif
