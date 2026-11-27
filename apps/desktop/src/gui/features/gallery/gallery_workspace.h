/*
 * @Author: DI JUNKUN
 * @Date: 2026-10-12
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _GALLERY_WORKSPACE_H_
#define _GALLERY_WORKSPACE_H_

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "gallery_storage.h"
#include "gallery_stream_settings.h"

namespace crossdesk::gallery {
struct Member {
  std::string id;
  std::string group;
};

// Layout contains no credentials and never reconnects devices when loaded.
struct Workspace {
  // Group assignments are shared by history and the wall. Wall enrollment is
  // independent, so assigning a group alone must never add a device to it.
  std::vector<Member> members;
  std::vector<std::string> groups;
  std::set<std::string> selected;
  int page = 0;
  int capacity = 6;
  StreamSettings stream_settings;

  static std::string NormalizeId(const std::string &input) {
    std::string result;
    for (size_t i = 0; i < input.size();) {
      const auto ch = static_cast<unsigned char>(input[i]);
      if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
        ++i;
        continue;
      }
      if (i + 2 < input.size() && ch == 0xEF &&
          static_cast<unsigned char>(input[i + 1]) == 0xBC &&
          static_cast<unsigned char>(input[i + 2]) >= 0x90 &&
          static_cast<unsigned char>(input[i + 2]) <= 0x99) {
        result += char('0' + static_cast<unsigned char>(input[i + 2]) - 0x90);
        i += 3;
      } else if (input.compare(i, 3, "\xe3\x80\x80") == 0)
        i += 3;
      else
        result += input[i++];
    }
    return result;
  }
  static bool ValidId(const std::string &id) {
    return id.size() == 9 &&
           std::all_of(id.begin(), id.end(),
                       [](unsigned char c) { return c >= '0' && c <= '9'; });
  }
  bool Contains(const std::string &id) const {
    return wall_ids_.count(id) != 0;
  }
  bool Add(const std::string &id, const std::string &group = {}) {
    if (!ValidId(id))
      return false;
    const bool joined = wall_ids_.insert(id).second;
    // A device grouped earlier in history joins at the end when explicitly
    // added, while retaining its existing group assignments.
    if (joined)
      std::stable_partition(members.begin(), members.end(),
                            [&](const auto &m) { return m.id != id; });
    if (group.empty() && HasAssignments(id))
      return joined;
    return AddAssignment(id, group) || joined;
  }
  std::vector<std::string> Ids(const std::string &group = {}) const {
    std::vector<std::string> ids;
    std::set<std::string> seen;
    for (const auto &m : members)
      if (wall_ids_.count(m.id) && (group.empty() || m.group == group) &&
          seen.insert(m.id).second)
        ids.push_back(m.id);
    return OrderIds(ids);
  }
  // Order is independent of membership, group assignments and live sessions.
  std::vector<std::string> OrderIds(const std::vector<std::string> &ids) const {
    if (device_order_.empty())
      return ids;
    std::set<std::string> remaining(ids.begin(), ids.end());
    std::vector<std::string> ordered;
    ordered.reserve(ids.size());
    for (const auto &id : device_order_)
      if (remaining.erase(id)) ordered.push_back(id);
    for (const auto &id : ids)
      if (remaining.erase(id)) ordered.push_back(id);
    return ordered;
  }
  bool SwapPositions(const std::string &source, const std::string &target,
                     const std::vector<std::string> &visible_ids) {
    if (source == target || !ValidId(source) || !ValidId(target) ||
        std::find(visible_ids.begin(), visible_ids.end(), source) == visible_ids.end() ||
        std::find(visible_ids.begin(), visible_ids.end(), target) == visible_ids.end())
      return false;
    auto ordered = device_order_;
    std::set<std::string> known(ordered.begin(), ordered.end());
    // Include hidden groups before applying a swap in a filtered view.
    for (const auto &id : Ids())
      if (known.insert(id).second) ordered.push_back(id);
    // Active-only devices can be arranged without enrolling them in the wall.
    for (const auto &id : visible_ids)
      if (ValidId(id) && known.insert(id).second) ordered.push_back(id);
    std::iter_swap(std::find(ordered.begin(), ordered.end(), source),
                   std::find(ordered.begin(), ordered.end(), target));
    device_order_ = std::move(ordered);
    return true;
  }
  // History filtering uses group assignments, independently of wall enrollment.
  // nullopt means all devices; an empty name means no named group.
  bool MatchesGroup(const std::string &id,
                    const std::optional<std::string> &group) const {
    if (!group)
      return true;
    if (group->empty())
      return std::none_of(members.begin(), members.end(), [&](const auto &m) {
        return m.id == id && !m.group.empty();
      });
    return std::any_of(members.begin(), members.end(), [&](const auto &m) {
      return m.id == id && m.group == *group;
    });
  }
  size_t AddMany(const std::vector<std::string> &ids,
                 const std::string &group = {}) {
    size_t added = 0;
    for (const auto &id : ids) {
      // Without a target group, an existing membership already puts the device
      // on the wall. An explicit group may reuse that same device/session.
      if (group.empty() && wall_ids_.count(id))
        continue;
      if (Add(id, group))
        ++added;
    }
    return added;
  }
  // Both editors replace the same group assignments without changing wall
  // enrollment. Keep the position of devices already on the wall untouched.
  bool SetGroups(const std::string &id, const std::vector<std::string> &names) {
    if (!ValidId(id))
      return false;
    std::vector<Member> replacements;
    std::set<std::string> seen;
    for (const auto &name : names)
      if (!name.empty() && seen.insert(name).second)
        replacements.push_back({id, name});
    if (replacements.empty())
      replacements.push_back({id, ""});
    std::vector<Member> updated;
    bool inserted = false;
    for (const auto &member : members) {
      if (member.id != id) {
        updated.push_back(member);
      } else if (!inserted) {
        updated.insert(updated.end(), replacements.begin(), replacements.end());
        inserted = true;
      }
    }
    if (!inserted)
      updated.insert(updated.end(), replacements.begin(), replacements.end());
    members = std::move(updated);
    for (const auto &member : replacements)
      if (!member.group.empty() &&
          std::find(groups.begin(), groups.end(), member.group) == groups.end())
        groups.push_back(member.group);
    PruneGroups();
    return true;
  }
  bool HasAssignments(const std::string &id) const {
    return std::any_of(members.begin(), members.end(),
                       [&](const auto &m) { return m.id == id; });
  }
  static constexpr size_t kMaxGroupNameLength = 64;

  enum class GroupNameError {
    kOk,
    kEmpty,
    kTooLong,
    kReserved,
    kNotFound,
    kDuplicate,
  };

  static std::string TrimGroupName(std::string_view input) {
    // UTF-8 encodings of the Unicode White_Space property, independent of locale.
    static constexpr auto spaces = std::to_array<std::string_view>({
        " ", "\t", "\n", "\v", "\f", "\r", "\u0085", "\u00a0", "\u1680",
        "\u2000", "\u2001", "\u2002", "\u2003", "\u2004", "\u2005", "\u2006",
        "\u2007", "\u2008", "\u2009", "\u200a", "\u2028", "\u2029", "\u202f",
        "\u205f", "\u3000"});
    while (!input.empty()) {
      const auto space = std::find_if(spaces.begin(), spaces.end(),
          [&](auto s) { return input.starts_with(s); });
      if (space == spaces.end()) break;
      input.remove_prefix(space->size());
    }
    while (!input.empty()) {
      const auto space = std::find_if(spaces.begin(), spaces.end(),
          [&](auto s) { return input.ends_with(s); });
      if (space == spaces.end()) break;
      input.remove_suffix(space->size());
    }
    return std::string(input);
  }
  static size_t GroupNameLength(std::string_view name) {
    size_t count = 0;
    for (unsigned char ch : name)
      if ((ch & 0xC0) != 0x80)
        ++count;
    return count;
  }
  static bool HasControlCharacters(std::string_view name) {
    return std::any_of(name.begin(), name.end(), [](unsigned char ch) {
      return ch < 0x20 || ch == 0x7F;
    });
  }
  static bool IsReservedGroupName(
      std::string_view name,
      const std::vector<std::string> &extra_reserved = {}) {
    if (name == "全部设备" || name == "未分组" ||
        name == "All devices" || name == "Ungrouped")
      return true;
    return std::any_of(extra_reserved.begin(), extra_reserved.end(),
                       [&](const auto &reserved) {
                         return !reserved.empty() && name == reserved;
                       });
  }
  GroupNameError ValidateGroupName(
      std::string_view candidate,
      const std::vector<std::string> &extra_reserved = {}) const {
    const auto trimmed = TrimGroupName(candidate);
    if (trimmed.empty() || HasControlCharacters(trimmed))
      return GroupNameError::kEmpty;
    if (GroupNameLength(trimmed) > kMaxGroupNameLength)
      return GroupNameError::kTooLong;
    if (IsReservedGroupName(trimmed, extra_reserved))
      return GroupNameError::kReserved;
    return GroupNameError::kOk;
  }
  bool HasGroup(std::string_view name) const {
    return std::find(groups.begin(), groups.end(), name) != groups.end();
  }
  GroupNameError ResolveAssignmentGroup(
      std::string &name, const std::vector<std::string> &extra_reserved) const {
    // Reuse persisted names verbatim, including names accepted by older clients.
    if (name.empty() || HasGroup(name)) return GroupNameError::kOk;
    name = TrimGroupName(name);
    if (HasGroup(name)) return GroupNameError::kOk;
    return ValidateGroupName(name, extra_reserved);
  }
  GroupNameError ValidateRenameGroup(
      const std::string &old_name, const std::string &new_name,
      const std::vector<std::string> &extra_reserved = {}) const {
    if (old_name.empty() || !HasGroup(old_name))
      return GroupNameError::kNotFound;
    const auto name_error = ValidateGroupName(new_name, extra_reserved);
    if (name_error != GroupNameError::kOk)
      return name_error;
    const auto trimmed = TrimGroupName(new_name);
    if (trimmed != old_name && HasGroup(trimmed))
      return GroupNameError::kDuplicate;
    return GroupNameError::kOk;
  }
  bool RenameGroup(std::string old_name, const std::string &new_name) {
    if (ValidateRenameGroup(old_name, new_name) != GroupNameError::kOk)
      return false;
    const auto trimmed = TrimGroupName(new_name);
    if (trimmed == old_name)
      return true;
    const auto group_it = std::find(groups.begin(), groups.end(), old_name);
    *group_it = trimmed;
    std::vector<Member> updated;
    updated.reserve(members.size());
    std::set<std::string> seen_renamed;
    for (auto &member : members) {
      if (member.group == old_name)
        member.group = trimmed;
      if (member.group == trimmed && !seen_renamed.insert(member.id).second)
        continue;
      updated.push_back(std::move(member));
    }
    members = std::move(updated);
    return true;
  }
  bool DeleteGroup(std::string name) {
    if (name.empty() || !std::erase(groups, name)) return false;
    // The first assignment determines the default wall order. Keep its slot
    // even if the device's remaining named assignments occur further down.
    std::vector<Member> remaining;
    std::set<std::string> seen, empty_assignments;
    for (auto &member : members) {
      const bool first = seen.insert(member.id).second;
      if (member.group == name) {
        if (!first) continue;
        member.group.clear();
      }
      if (member.group.empty() && !empty_assignments.insert(member.id).second) continue;
      remaining.push_back(std::move(member));
    }
    members = std::move(remaining);
    return true;
  }
  void Forget(const std::string& id) {
    Remove(id);
    std::erase_if(members, [&](const auto& member) { return member.id == id; });
    PruneGroups();
  }
  void Remove(const std::string &id, const std::string &group = {}) {
    if (group.empty()) {
      // Removing from the wall leaves history's group assignments intact.
      wall_ids_.erase(id);
      std::erase(device_order_, id);
    } else if (MatchesGroup(id, group)) {
      // Removing the last named group leaves an ungrouped wall member.
      // Reuse the editor path so enrollment and card order stay unchanged.
      std::vector<std::string> remaining;
      for (const auto &member : members)
        if (member.id == id && member.group != group)
          remaining.push_back(member.group);
      SetGroups(id, remaining);
    }
    selected.erase(id);
  }
  void ResizePage(int new_capacity, int count) {
    const int first = page * capacity;
    capacity = std::max(1, new_capacity);
    page = std::clamp(first / capacity, 0, std::max(0, (count - 1) / capacity));
  }
  // The caller supplies every ID in the current filter, across all pages.
  void ToggleSelectAll(const std::vector<std::string> &ids) {
    std::set<std::string> targets(ids.begin(), ids.end());
    if (selected == targets)
      selected.clear();
    else
      selected = std::move(targets);
  }
  bool Load(const std::filesystem::path &path) {
    try {
      if (!std::filesystem::exists(path))
        return true;
      std::ifstream file(path);
      const auto doc = nlohmann::json::parse(file);
      const int version = doc.at("version").get<int>();
      if ((version != 1 && version != 2) || !doc.at("members").is_array())
        return false;
      if (version == 2 && !doc.at("wall_ids").is_array())
        return false;
      Workspace loaded;
      if (doc.contains("stream_settings"))
        loaded.stream_settings = StreamSettings::FromJson(doc["stream_settings"]);
      for (const auto &member : doc.at("members")) {
        const auto id = member.at("id").get<std::string>();
        loaded.AddAssignment(id, member.value("group", ""));
        // Every saved member in the old format was already on the wall.
        if (version == 1 && ValidId(id))
          loaded.wall_ids_.insert(id);
      }
      if (version == 2)
        for (const auto &value : doc.at("wall_ids")) {
          const auto id = value.get<std::string>();
          if (ValidId(id) && loaded.HasAssignments(id))
            loaded.wall_ids_.insert(id);
        }
      if (doc.contains("device_order") && doc["device_order"].is_array()) {
        std::set<std::string> seen;
        for (const auto &value : doc["device_order"]) {
          if (!value.is_string()) continue;
          const auto id = value.get<std::string>();
          if (ValidId(id) && seen.insert(id).second)
            loaded.device_order_.push_back(id);
        }
      }
      members = std::move(loaded.members);
      groups = std::move(loaded.groups);
      wall_ids_ = std::move(loaded.wall_ids_);
      device_order_ = std::move(loaded.device_order_);
      stream_settings = loaded.stream_settings;
      return true;
    } catch (...) {
      return false;
    }
  }
  bool Save(const std::filesystem::path &path) const {
    try {
      nlohmann::json doc{{"version", 2},
                         {"members", nlohmann::json::array()},
                         {"wall_ids", wall_ids_},
                         {"device_order", device_order_},
                         {"stream_settings", stream_settings.ToJson()}};
      for (const auto &m : members)
        doc["members"].push_back({{"id", m.id}, {"group", m.group}});
      return WriteAtomically(path, doc.dump(2));
    } catch (...) {
      return false;
    }
  }

private:
  std::set<std::string> wall_ids_;
  std::vector<std::string> device_order_;

  bool AddAssignment(const std::string &id, const std::string &group) {
    if (!ValidId(id))
      return false;
    if (std::any_of(members.begin(), members.end(), [&](const auto &m) {
          return m.id == id && m.group == group;
        }))
      return false;
    if (!group.empty() &&
        std::find(groups.begin(), groups.end(), group) == groups.end())
      groups.push_back(group);
    members.push_back({id, group});
    return true;
  }
  void PruneGroups() {
    groups.erase(std::remove_if(groups.begin(), groups.end(),
                                [&](const auto &name) {
                                  return std::none_of(members.begin(),
                                                      members.end(),
                                                      [&](const auto &m) {
                                                        return m.group == name;
                                                      });
                                }),
                 groups.end());
  }
};
} // namespace crossdesk::gallery

#endif
