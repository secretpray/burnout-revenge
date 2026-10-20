#include "ealobby/locker.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iterator>
#include <map>
#include <system_error>

namespace ealobby {
namespace {

// What a persona may keep: generous next to a clip's size, small next to a disk.
constexpr uint64_t kMaxBytes = 64ull << 20;
constexpr uint32_t kMaxFiles = 64;

using Params = std::map<std::string, std::string>;

std::string UrlDecode(const std::string& text) {
  std::string out;
  for (size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '+') {
      out += ' ';
    } else if (text[i] == '%' && i + 2 < text.size() && std::isxdigit(uint8_t(text[i + 1])) &&
               std::isxdigit(uint8_t(text[i + 2]))) {
      out += char(std::stoi(text.substr(i + 1, 2), nullptr, 16));
      i += 2;
    } else {
      out += text[i];
    }
  }
  return out;
}

Params ParseQuery(const std::string& query) {
  Params params;
  size_t start = 0;
  while (start <= query.size()) {
    size_t end = query.find('&', start);
    if (end == std::string::npos) {
      end = query.size();
    }
    const std::string pair = query.substr(start, end - start);
    const size_t eq = pair.find('=');
    if (!pair.empty()) {
      params[UrlDecode(pair.substr(0, eq))] =
          eq == std::string::npos ? std::string() : UrlDecode(pair.substr(eq + 1));
    }
    start = end + 1;
  }
  return params;
}

std::string XmlEscape(const std::string& text) {
  std::string out;
  for (const char c : text) {
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      default: out += c;
    }
  }
  return out;
}

// A name safe to keep as a file: what a persona or a clip may not hold is dropped.
std::string Safe(const std::string& name) {
  std::string out;
  for (const char c : name) {
    if (std::isalnum(uint8_t(c)) || c == '_' || c == '-' || c == '.' || c == ' ') {
      out += c;
    }
  }
  while (!out.empty() && out.front() == '.') {
    out.erase(out.begin());
  }
  return out.substr(0, 64);
}

std::vector<uint8_t> Response(int status, const std::string& type, const std::string& body) {
  const char* reason = status == 200 ? "OK" : status == 404 ? "Not Found" : "Bad Request";
  std::string head = "HTTP/1.1 " + std::to_string(status) + " " + reason +
                     "\r\nContent-Type: " + type + "\r\nContent-Length: " +
                     std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n";
  std::vector<uint8_t> out(head.begin(), head.end());
  out.insert(out.end(), body.begin(), body.end());
  return out;
}

// A failed request: the LOCKER element with a non-zero error attribute - what
// the client reads (sub_8240B358: the attribute, defaulting to 1, must be 0 for
// success), with the reason as text for the log.
std::vector<uint8_t> Error(const std::string& code) {
  return Response(200, "text/xml", "<LOCKER error=\"1\" reason=\"" + XmlEscape(code) + "\"/>\n");
}

std::map<std::string, std::string> ReadMeta(const std::filesystem::path& file) {
  std::map<std::string, std::string> meta;
  std::ifstream in(file);
  std::string line;
  while (std::getline(in, line)) {
    const size_t eq = line.find('=');
    if (eq != std::string::npos) {
      meta[line.substr(0, eq)] = line.substr(eq + 1);
    }
  }
  return meta;
}

std::string Today() {
  const std::time_t now = std::time(nullptr);
  std::tm tm{};
#ifdef _WIN32
  gmtime_s(&tm, &now);
#else
  gmtime_r(&now, &tm);
#endif
  char text[32];
  std::snprintf(text, sizeof(text), "%04d.%d.%d-%d:%02d:%02d", tm.tm_year + 1900, tm.tm_mon + 1,
                tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
  return text;
}

}  // namespace

Locker::Locker(std::filesystem::path root,
               std::function<std::string(const std::string&)> persona_for_key,
               std::function<void(const std::string&)> log)
    : root_(std::move(root)), persona_for_key_(std::move(persona_for_key)), log_(std::move(log)) {}

bool Locker::Complete(const std::vector<uint8_t>& request) {
  const std::string text(request.begin(), request.end());
  const size_t end = text.find("\r\n\r\n");
  if (end == std::string::npos) {
    return false;
  }
  std::string lower = text.substr(0, end);
  std::transform(lower.begin(), lower.end(), lower.begin(), [](char c) { return char(std::tolower(uint8_t(c))); });
  const size_t at = lower.find("content-length:");
  const size_t length = at == std::string::npos ? 0 : size_t(std::strtoull(lower.c_str() + at + 15, nullptr, 10));
  return request.size() >= end + 4 + length;
}

std::vector<uint8_t> Locker::Handle(const std::vector<uint8_t>& request) {
  const std::string text(request.begin(), request.end());
  const size_t head_end = text.find("\r\n\r\n");
  const std::string body = head_end == std::string::npos ? std::string() : text.substr(head_end + 4);
  const size_t line_end = text.find("\r\n");
  const std::string line = text.substr(0, line_end);
  // "GET /path?query HTTP/1.1"
  const size_t sp1 = line.find(' ');
  const size_t sp2 = line.find(' ', sp1 + 1);
  if (sp1 == std::string::npos || sp2 == std::string::npos) {
    return Response(400, "text/plain", "");
  }
  const std::string method = line.substr(0, sp1);
  const std::string target = line.substr(sp1 + 1, sp2 - sp1 - 1);
  const size_t q = target.find('?');
  Params params = ParseQuery(q == std::string::npos ? std::string() : target.substr(q + 1));
  // A put's fields may come in the body as a form instead of the query.
  if (method == "POST" && params.find("cmd") == params.end()) {
    for (auto& [key, value] : ParseQuery(body)) {
      params.emplace(key, value);
    }
  }
  const std::string cmd = params["cmd"];
  const std::string self = Safe(persona_for_key_(params["lkey"]));
  const std::string pers = Safe(params["pers"].empty() ? self : params["pers"]);
  log_("locker: " + method + " cmd=" + cmd + " pers=" + pers + " name=" + params["name"] +
       " (" + std::to_string(body.size()) + " bytes)" + (self.empty() ? " - unknown lkey" : ""));
  if (pers.empty()) {
    return Error("NOAUTH");
  }
  const std::filesystem::path dir = root_ / pers;
  std::error_code error;
  const auto list = [&]() {
    std::string files;
    uint64_t bytes = 0;
    uint32_t count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(dir, error)) {
      if (!entry.is_regular_file() || entry.path().extension() == ".meta") {
        continue;
      }
      const std::string stored = entry.path().filename().string();
      auto meta = ReadMeta(dir / (stored + ".meta"));
      const std::string name = meta.count("name") ? meta["name"] : stored;
      const uint64_t size = entry.file_size(error);
      bytes += size;
      ++count;
      files += "<FILE name=\"" + XmlEscape(name) + "\" size=\"" + std::to_string(size) +
               "\" type=\"" + XmlEscape(meta["type"]) + "\" game=\"" + XmlEscape(meta["game"]) +
               "\" attr=\"" + XmlEscape(meta["attr"]) + "\" desc=\"" + XmlEscape(meta["desc"]) +
               "\" date=\"" + XmlEscape(meta["date"]) + "\" pers=\"" + XmlEscape(pers) +
               "\" locs=\"" + XmlEscape(meta["locs"]) + "\"/>\n";
    }
    return "<LOCKER error=\"0\" pers=\"" + XmlEscape(pers) + "\" maxBytes=\"" + std::to_string(kMaxBytes) +
           "\" numBytes=\"" + std::to_string(bytes) + "\" maxFiles=\"" + std::to_string(kMaxFiles) +
           "\" numFiles=\"" + std::to_string(count) + "\">\n" + files + "</LOCKER>\n";
  };
  if (cmd == "dir") {
    return Response(200, "text/xml", list());
  }
  const std::string name = Safe(params["name"]);
  if (name.empty() || name.size() > 5 && name.compare(name.size() - 5, 5, ".meta") == 0) {
    return Error("BADNAME");
  }
  // A file is a name and a type: a clip is two, its body (b4replaybody) and
  // its header (b4replayhead2), under one name. Kept as <name>~<type>; a get
  // or del without a type takes the first of that name.
  std::string type = Safe(params["type"]);
  if (type.empty() && cmd != "put") {
    for (const auto& entry : std::filesystem::directory_iterator(dir, error)) {
      const std::string stored = entry.path().filename().string();
      if (stored.rfind(name + "~", 0) == 0 && entry.path().extension() != ".meta") {
        type = stored.substr(name.size() + 1);
        break;
      }
    }
  }
  const std::string stored = name + "~" + type;
  const std::filesystem::path file = dir / stored;
  if (cmd == "get") {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
      return Error("NOFILE");
    }
    // A clip fetched by someone else counts towards the top 20.
    if (pers != self && type == "b4replaybody") {
      auto meta = ReadMeta(dir / (stored + ".meta"));
      const uint64_t downloads = std::strtoull(meta["downloads"].c_str(), nullptr, 10) + 1;
      meta["downloads"] = std::to_string(downloads);
      std::ofstream out(dir / (stored + ".meta"), std::ios::trunc);
      for (const auto& [key, value] : meta) {
        out << key << '=' << value << '\n';
      }
    }
    std::string data((std::istreambuf_iterator<char>(in)), {});
    return Response(200, "application/octet-stream", data);
  }
  // Writing: only to one's own locker.
  if (pers != self) {
    return Error("NOAUTH");
  }
  if (cmd == "del") {
    std::filesystem::remove(file, error);
    std::filesystem::remove(dir / (stored + ".meta"), error);
    return Response(200, "text/xml", list());
  }
  if (cmd == "put") {
    std::filesystem::create_directories(dir, error);
    const bool exists = std::filesystem::exists(file, error);
    if (exists && params["over"] != "1" && params["over"] != "true") {
      return Error("EXISTS");
    }
    {
      std::ofstream out(file, std::ios::binary | std::ios::trunc);
      out.write(body.data(), std::streamsize(body.size()));
      if (!out) {
        return Error("WRITE");
      }
    }
    std::ofstream meta(dir / (stored + ".meta"), std::ios::trunc);
    meta << "name=" << name << '\n';
    for (const char* key : {"type", "game", "attr", "desc", "locs"}) {
      meta << key << '=' << params[key] << '\n';
    }
    meta << "date=" << Today() << '\n';
    return Response(200, "text/xml", list());
  }
  return Error("BADCMD");
}

std::vector<Locker::Clip> Locker::Clips() const {
  std::vector<Clip> clips;
  std::error_code error;
  for (const auto& persona : std::filesystem::directory_iterator(root_, error)) {
    if (!persona.is_directory()) {
      continue;
    }
    for (const auto& entry : std::filesystem::directory_iterator(persona.path(), error)) {
      const std::string stored = entry.path().filename().string();
      const std::string suffix = "~b4replaybody";
      if (stored.size() <= suffix.size() ||
          stored.compare(stored.size() - suffix.size(), suffix.size(), suffix) != 0) {
        continue;
      }
      auto meta = ReadMeta(persona.path() / (stored + ".meta"));
      Clip clip;
      clip.persona = persona.path().filename().string();
      clip.name = meta.count("name") ? meta["name"] : stored.substr(0, stored.size() - suffix.size());
      clip.downloads = std::strtoull(meta["downloads"].c_str(), nullptr, 10);
      clips.push_back(std::move(clip));
    }
  }
  std::sort(clips.begin(), clips.end(),
            [](const Clip& a, const Clip& b) { return a.downloads > b.downloads; });
  return clips;
}

}  // namespace ealobby
