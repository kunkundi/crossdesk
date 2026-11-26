// Guard against X11 <X11/X.h> defining '#define None 0L' before GUI headers.
#ifndef None
#define None 0L
#endif
#include "gallery_workspace.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace {

using crossdesk::gallery::Workspace;

bool Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
  }
  return condition;
}

bool TestRenamePreservesGroupAndWallOrder() {
  Workspace ws;
  if (!Check(ws.Add("111111111", "Office"), "add first device")) return false;
  if (!Check(ws.Add("222222222", "Lab"), "add second device")) return false;
  if (!Check(ws.Add("333333333", "Home"), "add third device")) return false;
  if (!Check(ws.SetGroups("222222222", {"Lab", "Home"}), "assign two groups"))
    return false;
  // History-only grouped device (not enrolled on the wall).
  if (!Check(ws.SetGroups("444444444", {"Lab"}), "assign history-only group"))
    return false;
  if (!Check(!ws.Contains("444444444"), "history-only device stays off wall"))
    return false;

  const auto original_wall_order = ws.Ids();
  ws.selected.insert("222222222");
  if (!Check(ws.SwapPositions("111111111", "333333333", original_wall_order),
             "arrange custom wall order"))
    return false;
  const auto custom_wall_order = ws.Ids();

  if (!Check(ws.RenameGroup("Lab", "  Studio  "),
             "rename group with surrounding spaces"))
    return false;

  const std::vector<std::string> expected_groups{"Office", "Studio", "Home"};
  if (!Check(ws.groups == expected_groups,
             "renamed group preserves its exact slot in groups"))
    return false;
  if (!Check(ws.Ids() == custom_wall_order,
             "renaming group preserves custom wall order"))
    return false;
  if (!Check(ws.Ids("Studio") == std::vector<std::string>{"222222222"},
             "Studio filter returns wall member"))
    return false;
  if (!Check(ws.Ids("Lab").empty(), "old group filter is empty")) return false;
  if (!Check(ws.MatchesGroup("222222222", std::string("Studio")) &&
                 ws.MatchesGroup("222222222", std::string("Home")) &&
                 !ws.MatchesGroup("222222222", std::string("Lab")),
             "multi-group device retains other groups and updates renamed group"))
    return false;
  if (!Check(ws.MatchesGroup("444444444", std::string("Studio")) &&
                 !ws.Contains("444444444"),
             "history-only device updates group without joining wall"))
    return false;
  if (!Check(ws.selected == std::set<std::string>{"222222222"},
             "selection is preserved across group rename"))
    return false;

  // Same-name rename (including whitespace) succeeds without mutating order.
  if (!Check(ws.RenameGroup("Studio", "Studio") &&
                 ws.RenameGroup("Studio", " Studio "),
             "same-name rename is accepted as a no-op"))
    return false;
  if (!Check(ws.groups == expected_groups, "no-op rename keeps group order"))
    return false;

  return true;
}

bool TestDefaultMemberOrderAndDeduplication() {
  Workspace ws;
  ws.Add("111111111", "Alpha");
  ws.Add("222222222", "Beta");
  ws.Add("333333333", "Gamma");
  ws.SetGroups("111111111", {"Alpha", "Gamma"});

  // Default wall order (with empty device_order_) follows first member slot.
  const std::vector<std::string> expected_ids{
      "111111111", "222222222", "333333333"};
  if (!Check(ws.Ids() == expected_ids, "initial default wall order"))
    return false;

  if (!Check(ws.RenameGroup("Alpha", "Delta"), "rename first group"))
    return false;
  if (!Check(ws.Ids() == expected_ids,
             "renaming first group keeps default wall order"))
    return false;

  if (!Check(ws.DeleteGroup("Delta"), "delete renamed first group"))
    return false;
  if (!Check(ws.Ids() == expected_ids,
             "deleting first group keeps default wall order"))
    return false;
  if (!Check(ws.MatchesGroup("111111111", std::string("Gamma")) &&
                 !ws.MatchesGroup("111111111", std::string("")),
             "device with remaining group does not become ungrouped"))
    return false;

  if (!Check(ws.DeleteGroup("Beta"), "delete sole group of second device"))
    return false;
  if (!Check(ws.Ids() == expected_ids,
             "deleting sole group keeps ungrouped device on wall in place"))
    return false;
  if (!Check(ws.MatchesGroup("222222222", std::string("")),
             "device whose sole group was deleted becomes ungrouped"))
    return false;

  return true;
}

bool TestValidationEdgeCases() {
  Workspace ws;
  ws.Add("111111111", "Office");
  ws.Add("222222222", "Lab");

  using Error = Workspace::GroupNameError;
  if (!Check(!ws.RenameGroup("", "NewName") &&
                 ws.ValidateRenameGroup("", "NewName") == Error::kNotFound,
             "reject empty source group"))
    return false;
  if (!Check(!ws.RenameGroup("Missing", "NewName") &&
                 ws.ValidateRenameGroup("Missing", "NewName") == Error::kNotFound,
             "reject missing source group"))
    return false;
  if (!Check(!ws.RenameGroup("Office", "") &&
                 !ws.RenameGroup("Office", "   \t\r\n ") &&
                 ws.ValidateRenameGroup("Office", "   ") == Error::kEmpty,
             "reject empty or whitespace-only target name"))
    return false;
  if (!Check(!ws.RenameGroup("Office", "Line1\nLine2") &&
                 ws.ValidateRenameGroup("Office", "Line1\nLine2") == Error::kEmpty,
             "reject control characters in group name"))
    return false;
  if (!Check(!ws.RenameGroup("Office", "Lab") &&
                 !ws.RenameGroup("Office", "  Lab ") &&
                 ws.ValidateRenameGroup("Office", "Lab") == Error::kDuplicate,
             "reject duplicate existing group name"))
    return false;

  for (const char* reserved :
       {"全部设备", "未分组", "All devices", "Ungrouped"}) {
    if (!Check(!ws.RenameGroup("Office", reserved) &&
                   ws.ValidateRenameGroup("Office", reserved) == Error::kReserved,
               "reject built-in reserved system group names"))
      return false;
  }
  if (!Check(ws.ValidateRenameGroup("Office", "Все устройства",
                                    {"Все устройства", "Без группы"}) ==
                 Error::kReserved,
             "reject localized reserved system group names"))
    return false;

  const std::string ascii_65(Workspace::kMaxGroupNameLength + 1, 'x');
  if (!Check(!ws.RenameGroup("Office", ascii_65) &&
                 ws.ValidateRenameGroup("Office", ascii_65) == Error::kTooLong,
             "reject ASCII group name exceeding max length"))
    return false;

  std::string cjk_64;
  for (size_t i = 0; i < Workspace::kMaxGroupNameLength; ++i) cjk_64 += "组";
  const std::string cjk_65 = cjk_64 + "组";
  if (!Check(!ws.RenameGroup("Office", cjk_65) &&
                 ws.ValidateRenameGroup("Office", cjk_65) == Error::kTooLong,
             "reject 65-codepoint UTF-8 group name"))
    return false;
  if (!Check(ws.RenameGroup("Office", cjk_64),
             "accept 64-codepoint UTF-8 group name (192 UTF-8 bytes)"))
    return false;

  // Verify failed renames never mutate existing groups or memberships.
  if (!Check(!ws.RenameGroup(cjk_64, "Lab"),
             "collision after UTF-8 rename still fails"))
    return false;
  if (!Check(ws.groups == std::vector<std::string>{cjk_64, "Lab"} &&
                 ws.MatchesGroup("111111111", cjk_64) &&
                 ws.MatchesGroup("222222222", std::string("Lab")),
             "failed rename leaves workspace state intact"))
    return false;

  return true;
}

bool TestSaveLoadAndFilterPreservation() {
  Workspace ws;
  ws.Add("111111111", "Office");
  ws.Add("222222222", "Lab");
  ws.Add("333333333", "Prod");

  std::optional<std::string> recent_filter = "Lab";
  int wall_group_index = 2;  // 0 = All, 1 = Office, 2 = Lab, 3 = Prod

  if (!Check(ws.RenameGroup("Lab", "Staging"), "rename active group"))
    return false;
  if (recent_filter == "Lab") recent_filter = "Staging";
  const std::string active_wall_group = ws.groups[wall_group_index - 1];

  if (!Check(active_wall_group == "Staging" &&
                 ws.MatchesGroup("222222222", recent_filter) &&
                 ws.Ids(active_wall_group) ==
                     std::vector<std::string>{"222222222"},
             "active filters in Recent Connections and DeskHub follow rename"))
    return false;

  const auto temp_dir = std::filesystem::temp_directory_path() /
                        ("crossdesk_ws_test_" +
                         std::to_string(std::chrono::steady_clock::now()
                                            .time_since_epoch()
                                            .count()));
  std::filesystem::create_directories(temp_dir);
  const auto file = temp_dir / "deskhub.json";
  const bool saved = ws.Save(file);
  Workspace reloaded;
  const bool loaded = saved && reloaded.Load(file);
  std::error_code ignored;
  std::filesystem::remove_all(temp_dir, ignored);

  if (!Check(saved && loaded, "workspace saves and reloads cleanly"))
    return false;
  if (!Check(reloaded.groups ==
                 std::vector<std::string>{"Office", "Staging", "Prod"},
             "reloaded groups retain renamed group order"))
    return false;
  if (!Check(reloaded.Ids("Staging") == std::vector<std::string>{"222222222"} &&
                 reloaded.Ids("Lab").empty(),
             "reloaded memberships reflect renamed group"))
    return false;

  return true;
}

}  // namespace

int main() {
  if (!TestRenamePreservesGroupAndWallOrder()) return 1;
  if (!TestDefaultMemberOrderAndDeduplication()) return 2;
  if (!TestValidationEdgeCases()) return 3;
  if (!TestSaveLoadAndFilterPreservation()) return 4;
  std::cout << "Gallery workspace group rename/delete tests passed.\n";
  return 0;
}
