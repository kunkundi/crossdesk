/*
 * @Author: DI JUNKUN
 * @Date: 2025-11-11
 * Copyright (c) 2025 by DI JUNKUN, All Rights Reserved.
 */

#include "version_checker.h"

#include <httplib.h>

#include "rd_log.h"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace crossdesk {

static std::string latest_release_date_ = "";
static bool latest_patch_available_ = false;
static int latest_patch_ = 0;

std::vector<int> SplitVersion(const std::string& ver);

namespace {

constexpr size_t kMaxInlinePatchDigits = 4;

struct ParsedVersion {
  std::vector<int> numbers;
  std::string date;
  bool has_patch = false;
  int patch = 0;
};

bool IsDigit(char c) {
  return std::isdigit(static_cast<unsigned char>(c)) != 0;
}

bool IsAlphaNumeric(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) != 0;
}

bool IsAllDigits(const std::string& value) {
  if (value.empty()) {
    return false;
  }

  for (char c : value) {
    if (!IsDigit(c)) {
      return false;
    }
  }
  return true;
}

bool TryParseNonNegativeInt(const std::string& value, int* result) {
  if (!IsAllDigits(value)) {
    return false;
  }

  try {
    const long long parsed = std::stoll(value);
    if (parsed > std::numeric_limits<int>::max()) {
      return false;
    }
    *result = static_cast<int>(parsed);
    return true;
  } catch (...) {
    return false;
  }
}

bool TryParseInlinePatch(const std::string& value, int* result) {
  if (value.size() > kMaxInlinePatchDigits) {
    return false;
  }

  return TryParseNonNegativeInt(value, result);
}

size_t FindNumericStart(const std::string& version) {
  size_t start = 0;
  while (start < version.size() && !IsDigit(version[start])) {
    start++;
  }
  return start;
}

size_t FindNumericEnd(const std::string& version, size_t start) {
  size_t end = start;
  while (end < version.size() &&
         (IsDigit(version[end]) || version[end] == '.')) {
    end++;
  }
  return end;
}

bool HasDigitBoundary(const std::string& value, size_t pos, size_t len) {
  const bool before_ok = pos == 0 || !IsDigit(value[pos - 1]);
  const size_t end = pos + len;
  const bool after_ok = end >= value.size() || !IsDigit(value[end]);
  return before_ok && after_ok;
}

bool IsCompactDateAt(const std::string& value, size_t pos) {
  if (pos + 8 > value.size() || !HasDigitBoundary(value, pos, 8)) {
    return false;
  }

  for (size_t i = 0; i < 8; ++i) {
    if (!IsDigit(value[pos + i])) {
      return false;
    }
  }
  return true;
}

std::string CompactDateToIso(const std::string& compact_date) {
  return compact_date.substr(0, 4) + "-" + compact_date.substr(4, 2) + "-" +
         compact_date.substr(6, 2);
}

bool ExtractDateFromText(const std::string& value,
                         std::string* date,
                         size_t* date_end) {
  for (size_t i = 0; i < value.size(); ++i) {
    if (IsCompactDateAt(value, i)) {
      *date = CompactDateToIso(value.substr(i, 8));
      *date_end = i + 8;
      return true;
    }
  }

  return false;
}

ParsedVersion ParseVersion(const std::string& version) {
  const size_t numeric_start = FindNumericStart(version);
  const size_t numeric_end = FindNumericEnd(version, numeric_start);

  ParsedVersion parsed;
  parsed.numbers = SplitVersion(version.substr(numeric_start,
                                               numeric_end - numeric_start));

  const std::string suffix = version.substr(numeric_end);
  size_t pos = 0;
  while (pos < suffix.size()) {
    while (pos < suffix.size() && !IsAlphaNumeric(suffix[pos])) {
      pos++;
    }

    const size_t token_start = pos;
    while (pos < suffix.size() && IsAlphaNumeric(suffix[pos])) {
      pos++;
    }

    if (token_start == pos) {
      continue;
    }

    const std::string token = suffix.substr(token_start, pos - token_start);
    if (parsed.date.empty() && IsCompactDateAt(token, 0)) {
      parsed.date = CompactDateToIso(token);
      continue;
    }

    int patch = 0;
    if (!parsed.has_patch && TryParseInlinePatch(token, &patch)) {
      parsed.has_patch = true;
      parsed.patch = patch;
    }
  }

  return parsed;
}

int CompareNumericVersion(const std::vector<int>& current,
                          const std::vector<int>& latest) {
  std::vector<int> current_parts = current;
  std::vector<int> latest_parts = latest;
  const size_t len = std::max(current_parts.size(), latest_parts.size());
  current_parts.resize(len, 0);
  latest_parts.resize(len, 0);

  for (size_t i = 0; i < len; ++i) {
    if (latest_parts[i] > current_parts[i]) {
      return 1;
    }
    if (latest_parts[i] < current_parts[i]) {
      return -1;
    }
  }

  return 0;
}

void ResetLatestMetadata() {
  latest_release_date_ = "";
  latest_patch_available_ = false;
  latest_patch_ = 0;
}

bool ReadPatchField(const nlohmann::json& json, int* patch) {
  if (!json.contains("patch")) {
    return false;
  }

  const auto& patch_value = json["patch"];
  if (patch_value.is_number_integer()) {
    const long long parsed = patch_value.get<long long>();
    if (parsed < 0 || parsed > std::numeric_limits<int>::max()) {
      return false;
    }
    *patch = static_cast<int>(parsed);
    return true;
  }

  if (patch_value.is_string()) {
    return TryParseNonNegativeInt(patch_value.get<std::string>(), patch);
  }

  return false;
}

void LogHttpError(const httplib::Result& result) {
  LOG_WARN("Failed to fetch version.json: error={}, message={}",
           static_cast<int>(result.error()), httplib::to_string(result.error()));
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
  LOG_WARN("version.json SSL error={}, OpenSSL error={}", result.ssl_error(),
           result.ssl_openssl_error());
#endif
}

}  // namespace

std::string ExtractNumericPart(const std::string& ver) {
  const size_t start = FindNumericStart(ver);
  const size_t end = FindNumericEnd(ver, start);
  return ver.substr(start, end - start);
}

std::vector<int> SplitVersion(const std::string& ver) {
  std::vector<int> nums;
  std::istringstream ss(ver);
  std::string token;
  while (std::getline(ss, token, '.')) {
    try {
      nums.push_back(std::stoi(token));
    } catch (...) {
      nums.push_back(0);
    }
  }
  return nums;
}

// extract date from version string (format: v1.2.3-20251113-abc
// or 1.2.3-20251113-abc)
std::string ExtractDateFromVersion(const std::string& version) {
  const size_t numeric_start = FindNumericStart(version);
  const size_t numeric_end = FindNumericEnd(version, numeric_start);
  const std::string suffix = version.substr(numeric_end);
  std::string date;
  size_t date_end = 0;
  if (ExtractDateFromText(suffix, &date, &date_end)) {
    return date;
  }
  return "";
}

// compare two dates in YYYY-MM-DD format
bool IsNewerDate(const std::string& date1, const std::string& date2) {
  if (date1.empty() || date2.empty()) return false;
  // simple string comparison works for ISO date format (YYYY-MM-DD)
  return date2 > date1;
}

bool IsNewerVersion(const std::string& current, const std::string& latest) {
  return IsNewerVersionWithMetadata(
      current, latest, latest_release_date_,
      latest_patch_available_ ? latest_patch_ : -1);
}

bool IsNewerVersionWithMetadata(const std::string& current,
                                const std::string& latest,
                                const std::string& latest_date,
                                int latest_patch) {
  (void)latest_date;

  const ParsedVersion current_version = ParseVersion(current);
  const ParsedVersion latest_version = ParseVersion(latest);

  const int numeric_compare =
      CompareNumericVersion(current_version.numbers, latest_version.numbers);
  if (numeric_compare > 0) {
    return true;
  }
  if (numeric_compare < 0) {
    return false;
  }

  const bool metadata_has_patch = latest_patch >= 0;
  const bool latest_has_patch = metadata_has_patch || latest_version.has_patch;
  if (latest_has_patch || current_version.has_patch) {
    const int resolved_latest_patch =
        metadata_has_patch ? latest_patch
                           : (latest_version.has_patch ? latest_version.patch
                                                       : 0);
    const int resolved_current_patch =
        current_version.has_patch ? current_version.patch : 0;
    return resolved_latest_patch > resolved_current_patch;
  }

  return false;
}

nlohmann::json CheckUpdate() {
  httplib::Client cli("https://version.crossdesk.cn");

  cli.set_connection_timeout(5);
  cli.set_read_timeout(5);
  cli.set_follow_location(true);

  auto res = cli.Get("/version.json");
  if (res) {
    if (res->status == 200) {
      try {
        auto j = nlohmann::json::parse(res->body);
        if (j.contains("releaseDate") && j["releaseDate"].is_string()) {
          latest_release_date_ = j["releaseDate"];
        } else {
          latest_release_date_ = "";
        }
        latest_patch_ = 0;
        latest_patch_available_ = ReadPatchField(j, &latest_patch_);
        LOG_INFO("Fetched version.json: latest_version={}, releaseDate={}, patch={}",
                 j.value("latest_version", j.value("version", "")),
                 j.value("releaseDate", ""),
                 latest_patch_available_ ? latest_patch_ : -1);
        return j;
      } catch (const std::exception& e) {
        LOG_WARN("Failed to parse version.json: {}", e.what());
        ResetLatestMetadata();
        return nlohmann::json{};
      }
    } else {
      LOG_WARN("Failed to fetch version.json: HTTP status={}", res->status);
      ResetLatestMetadata();
      return nlohmann::json{};
    }
  } else {
    LogHttpError(res);
    ResetLatestMetadata();
    return nlohmann::json{};
  }
}

}  // namespace crossdesk
