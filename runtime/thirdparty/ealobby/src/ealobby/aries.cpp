#include "ealobby/aries.h"

#include <cstdio>

namespace ealobby {

Fields ParseFields(const std::string& body) {
  Fields fields;
  size_t start = 0;
  while (start < body.size()) {
    size_t end = body.find('\n', start);
    if (end == std::string::npos) {
      end = body.size();
    }
    std::string line = body.substr(start, end - start);
    while (!line.empty() && (line.back() == '\0' || line.back() == '\r')) {
      line.pop_back();
    }
    const size_t equals = line.find('=');
    if (equals != std::string::npos) {
      fields[line.substr(0, equals)] = line.substr(equals + 1);
    }
    start = end + 1;
  }
  return fields;
}

std::string FormatFields(const Fields& fields) {
  std::string body;
  for (const auto& [key, value] : fields) {
    body += key;
    body += '=';
    body += value;
    body += '\n';
  }
  body += '\0';
  return body;
}

std::vector<uint8_t> Encode(const std::string& command, const std::string& body,
                            const std::string& code) {
  std::vector<uint8_t> out(kHeaderSize + body.size());
  for (size_t i = 0; i < 4; ++i) {
    out[i] = i < command.size() ? uint8_t(command[i]) : 0;
    out[4 + i] = i < code.size() ? uint8_t(code[i]) : 0;
  }
  const uint32_t length = uint32_t(out.size());
  out[8] = uint8_t(length >> 24);
  out[9] = uint8_t(length >> 16);
  out[10] = uint8_t(length >> 8);
  out[11] = uint8_t(length);
  for (size_t i = 0; i < body.size(); ++i) {
    out[kHeaderSize + i] = uint8_t(body[i]);
  }
  return out;
}

bool TakeMessage(std::vector<uint8_t>* buffer, Message* message) {
  if (buffer->size() < kHeaderSize) {
    return false;
  }
  const uint8_t* b = buffer->data();
  size_t length = (size_t(b[8]) << 24) | (size_t(b[9]) << 16) | (size_t(b[10]) << 8) | b[11];
  if (length < kHeaderSize) {
    length = kHeaderSize;  // a malformed length: take the header alone
  }
  if (buffer->size() < length) {
    return false;
  }
  message->command.assign(reinterpret_cast<const char*>(b), 4);
  message->code.assign(reinterpret_cast<const char*>(b + 4), 4);
  message->body.assign(reinterpret_cast<const char*>(b + kHeaderSize), length - kHeaderSize);
  buffer->erase(buffer->begin(), buffer->begin() + ptrdiff_t(length));
  return true;
}

std::string Printable(const std::string& body) {
  std::string out;
  for (unsigned char c : body) {
    if (c == '\n') {
      out += '|';
    } else if (c >= 0x20 && c < 0x7F) {
      out += char(c);
    } else {
      char hex[5];
      std::snprintf(hex, sizeof(hex), "\\x%02X", c);
      out += hex;
    }
  }
  return out;
}

}  // namespace ealobby
