/*
 * @Author: DI JUNKUN
 * @Date: 2026-10-21
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#include "about_catalog.h"

#include <nlohmann/json.hpp>

#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "about_resources.h"

namespace crossdesk::about {
namespace {

const nlohmann::json& Catalog() {
  static const auto catalog = nlohmann::json::parse(
      std::begin(kAboutResources), std::end(kAboutResources));
  return catalog;
}

slint::SharedString Text(const nlohmann::json& object, const char* key) {
  return slint::SharedString(object.value(key, std::string{}));
}

std::string_view TrimLayoutWhitespace(std::string_view line) {
  const auto first = line.find_first_not_of(" \t\r");
  if (first == std::string_view::npos) return {};
  return line.substr(first, line.find_last_not_of(" \t\r") - first + 1);
}

slint::SharedString DocumentText(const nlohmann::json& document) {
  const auto title = document.value("title", std::string{});
  const auto text = document.value("text", std::string{});
  if (title != "GNU GPL v3" && title != "GNU LGPL v3") {
    return slint::SharedString(text);
  }

  // GNU's plain-text copies use fixed-width wrapping and space-aligned headings.
  // Reflow prose for the reader, retaining paragraph boundaries, notice examples,
  // and every non-whitespace character. The embedded source stays untouched.
  std::string formatted;
  std::vector<std::string_view> paragraph;
  auto append_paragraph = [&] {
    if (paragraph.empty()) return;
    if (!formatted.empty()) formatted += "\n\n";
    const bool preserve_lines = paragraph.front().starts_with("GNU ") ||
                                paragraph.front().starts_with("<");
    for (size_t index = 0; index < paragraph.size(); ++index) {
      if (index > 0) {
        const bool after_copyright =
            paragraph[index - 1].starts_with("Copyright (C)");
        formatted += preserve_lines || after_copyright ? '\n' : ' ';
      }
      formatted += paragraph[index];
    }
    paragraph.clear();
  };
  std::string_view remaining(text);
  while (!remaining.empty()) {
    const auto end = remaining.find('\n');
    const auto line = TrimLayoutWhitespace(remaining.substr(0, end));
    if (line.empty()) {
      append_paragraph();
    } else {
      paragraph.push_back(line);
    }
    if (end == std::string_view::npos) break;
    remaining.remove_prefix(end + 1);
  }
  append_paragraph();
  return slint::SharedString(formatted);
}

}  // namespace

void SetInstalledReleaseNotes(const slint::ComponentHandle<ui::MainWindow>& window,
                              const std::string& language) {
  const auto& notes = Catalog().at("releaseNotes");
  const auto& lines = notes.contains(language) ? notes.at(language) : notes.at("en-US");
  std::vector<slint::SharedString> highlights;
  for (const auto& line : lines) {
    highlights.emplace_back(line.get<std::string>());
  }
  window->set_installed_release_notes(
      std::make_shared<slint::VectorModel<slint::SharedString>>(highlights));
}

void Populate(const slint::ComponentHandle<ui::MainWindow>& window) {
  static const auto model = [] {
    std::vector<ui::AboutComponent> components;
    for (const auto& item : Catalog().at("components")) {
      ui::AboutComponent component;
      component.id = Text(item, "id");
      component.name = Text(item, "name");
      component.version = Text(item, "version");
      component.license = Text(item, "license");
      component.source_url = Text(item, "sourceURL");
      std::vector<ui::AboutDocument> documents;
      for (const auto& document : item.at("documents")) {
        ui::AboutDocument entry;
        entry.title = Text(document, "title");
        entry.text = DocumentText(document);
        documents.push_back(std::move(entry));
      }
      component.documents =
          std::make_shared<slint::VectorModel<ui::AboutDocument>>(documents);
      components.push_back(std::move(component));
    }
    return std::make_shared<slint::VectorModel<ui::AboutComponent>>(components);
  }();
  window->set_about_components(model);
  for (size_t index = 0; index < model->row_count(); ++index) {
    if (model->row_data(index)->id == "openfec") {
      window->set_about_openfec_index(static_cast<int>(index));
      break;
    }
  }
  const auto& catalog = Catalog();
  window->set_about_revision(Text(catalog, "revision"));
  window->set_about_source_url(Text(catalog, "sourceURL"));
  window->set_about_build_url(Text(catalog, "buildURL"));
}

}  // namespace crossdesk::about
