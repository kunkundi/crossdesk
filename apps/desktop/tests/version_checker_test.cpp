#include "version_checker.h"

#include <iostream>
#include <string>

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

  return ok ? 0 : 1;
}
