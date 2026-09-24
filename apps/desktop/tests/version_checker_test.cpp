#include "version_checker.h"

#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {

bool ExpectEqual(const std::string& name, bool actual, bool expected) {
  if (actual == expected) {
    return true;
  }

  std::cerr << name << " mismatch\n"
            << "  expected: " << expected << "\n"
            << "  actual:   " << actual << "\n";
  return false;
}

}  // namespace

int main() {
  bool ok = true;

  ok &= ExpectEqual("new patch-before-date is newer",
                    crossdesk::IsNewerVersionWithMetadata(
                        "v1.3.5-20260529", "v1.3.5-1-20260529", "", -1),
                    true);
  ok &= ExpectEqual("larger patch wins regardless of date",
                    crossdesk::IsNewerVersionWithMetadata(
                        "v1.3.5-2-20260530", "v1.3.5-3-20260529", "", -1),
                    true);
  ok &= ExpectEqual("smaller patch loses regardless of date",
                    crossdesk::IsNewerVersionWithMetadata(
                        "v1.3.5-3-20260529", "v1.3.5-2-20260530", "", -1),
                    false);
  ok &= ExpectEqual("old date-before-patch remains supported",
                    crossdesk::IsNewerVersionWithMetadata(
                        "v1.3.5-20260529-1", "v1.3.5-20260529-2", "", -1),
                    true);
  ok &= ExpectEqual("metadata patch overrides date",
                    crossdesk::IsNewerVersionWithMetadata(
                        "v1.3.5-9-20260530", "v1.3.5", "2026-05-31", 10),
                    true);
  ok &= ExpectEqual("date alone does not update same version",
                    crossdesk::IsNewerVersionWithMetadata(
                        "v1.3.5-20260529", "v1.3.5-20260530", "", -1),
                    false);
  ok &= ExpectEqual("numeric version still wins",
                    crossdesk::IsNewerVersionWithMetadata(
                        "v1.3.5-9-20260529", "v1.3.6-1-20260529", "", -1),
                    true);

  const auto info =
      crossdesk::ParseVersionInfo({{"latest_version", "v1.3.5-20260529"},
                                   {"patch", "10"},
                                   {"releaseName", "Release"},
                                   {"releaseNotes", "Changes"},
                                   {"releaseDate", "2026-05-29"}});
  ok &= ExpectEqual("valid metadata is normalized and retained",
                    info && info->version == "1.3.5-20260529" &&
                        info->patch == 10 && info->release_name == "Release" &&
                        info->release_notes == "Changes" &&
                        info->release_date == "2026-05-29",
                    true);
  const auto legacy = crossdesk::ParseVersionInfo({{"version", "1.3.6"}});
  ok &= ExpectEqual("legacy version field is supported",
                    legacy && legacy->version == "1.3.6" && legacy->patch == -1,
                    true);
  for (const auto& json :
       std::vector<nlohmann::json>{nullptr,
                                   nlohmann::json::array(),
                                   nlohmann::json::object(),
                                   {{"version", false}},
                                   {{"version", ""}},
                                   {{"version", "unknown"}},
                                   {{"version", "1..2"}},
                                   {{"version", ".1.2"}},
                                   {{"version", "1.2."}},
                                   {{"version", "1.2-"}},
                                   {{"version", "999999999999999999999.2"}}}) {
    ok &= ExpectEqual("invalid response is a failed check: " + json.dump(),
                      crossdesk::ParseVersionInfo(json).has_value(), false);
  }
  const auto malformed_optional = crossdesk::ParseVersionInfo(
      {{"version", "1.3.5"},
       {"patch", std::numeric_limits<uint64_t>::max()},
       {"releaseName", nullptr},
       {"releaseNotes", 123}});
  ok &= ExpectEqual("bad optional metadata does not leak into later checks",
                    malformed_optional && malformed_optional->patch == -1 &&
                        malformed_optional->release_name.empty() &&
                        malformed_optional->release_notes.empty(),
                    true);
  ok &= ExpectEqual("plain comparison has no hidden metadata from prior checks",
                    crossdesk::IsNewerVersion("1.3.5", "1.3.5"), false);

  return ok ? 0 : 1;
}
