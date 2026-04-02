#include "core/EngineConfig.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include "core/Application.h"

namespace Monolith {
namespace {

std::string Trim(const std::string& s) {
  size_t start = 0;
  while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start])))
    ++start;

  size_t end = s.size();
  while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1])))
    --end;

  return s.substr(start, end - start);
}

std::string StripComment(const std::string& s) {
  bool inString = false;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '"')
      inString = !inString;
    if (!inString && s[i] == '#')
      return s.substr(0, i);
  }
  return s;
}

std::string Unquote(const std::string& s) {
  const std::string t = Trim(s);
  if (t.size() >= 2 && t.front() == '"' && t.back() == '"')
    return t.substr(1, t.size() - 2);
  return t;
}

bool ParseBool(const std::string& raw, bool fallback) {
  const std::string v = Trim(raw);
  if (v == "true")
    return true;
  if (v == "false")
    return false;
  return fallback;
}

int ParseInt(const std::string& raw, int fallback) {
  try {
    return std::stoi(Trim(raw));
  } catch (...) {
    return fallback;
  }
}

std::vector<std::string> ParseStringArray(const std::string& raw) {
  std::vector<std::string> out;
  const std::string t = Trim(raw);
  if (t.size() < 2 || t.front() != '[' || t.back() != ']')
    return out;

  std::string body = t.substr(1, t.size() - 2);
  std::string current;
  bool inString = false;

  for (char ch : body) {
    if (ch == '"') {
      inString = !inString;
      current.push_back(ch);
      continue;
    }

    if (!inString && ch == ',') {
      const std::string item = Unquote(current);
      if (!item.empty())
        out.push_back(item);
      current.clear();
      continue;
    }

    current.push_back(ch);
  }

  const std::string tail = Unquote(current);
  if (!tail.empty())
    out.push_back(tail);

  return out;
}

}  // namespace

EngineRuntimeConfig EngineRuntimeConfig::LoadFromFile(const std::string& path) {
  std::ifstream file(path);
  if (!file.is_open())
    throw std::runtime_error("EngineRuntimeConfig: cannot open '" + path + "'");

  EngineRuntimeConfig cfg;
  std::string section;
  std::string line;

  while (std::getline(file, line)) {
    line = Trim(StripComment(line));
    if (line.empty())
      continue;

    if (line.front() == '[' && line.back() == ']') {
      section = Trim(line.substr(1, line.size() - 2));
      continue;
    }

    const size_t eq = line.find('=');
    if (eq == std::string::npos)
      continue;

    const std::string key = Trim(line.substr(0, eq));
    const std::string value = Trim(line.substr(eq + 1));

    if (section == "window") {
      if (key == "width")
        cfg.windowWidth = ParseInt(value, cfg.windowWidth);
      else if (key == "height")
        cfg.windowHeight = ParseInt(value, cfg.windowHeight);
      else if (key == "vsync")
        cfg.vsync = ParseBool(value, cfg.vsync);
    } else if (section == "runtime") {
      if (key == "max_ram_mb")
        cfg.maxRamMb = ParseInt(value, cfg.maxRamMb);
      else if (key == "max_cpu_percent")
        cfg.maxCpuPercent = ParseInt(value, cfg.maxCpuPercent);
      else if (key == "default_scene")
        cfg.defaultScenePath = Unquote(value);
    } else if (section == "assets") {
      if (key == "directories")
        cfg.assetDirectories = ParseStringArray(value);
    }
  }

  return cfg;
}

EngineRuntimeConfig EngineRuntimeConfig::LoadOrDefault(const std::string& path) {
  try {
    return LoadFromFile(path);
  } catch (...) {
    return EngineRuntimeConfig{};
  }
}

void EngineRuntimeConfig::ApplyTo(AppSpec& spec) const {
  spec.width = windowWidth;
  spec.height = windowHeight;
  spec.vsync = vsync;
  spec.maxRamMb = maxRamMb;
  spec.maxCpuPercent = maxCpuPercent;
  spec.defaultScenePath = defaultScenePath;
  spec.assetDirectories = assetDirectories;
  spec.configPath = spec.configPath.empty() ? "engine.config.toml" : spec.configPath;
}

}  // namespace Monolith
