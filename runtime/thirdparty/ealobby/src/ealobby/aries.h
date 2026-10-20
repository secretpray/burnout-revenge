// EA's Aries lobby protocol, as Burnout Revenge's DirtySock speaks it.
//
// Every message is a 12-byte header - a four-character command, a four-
// character code (zero bytes, or an error code in a reply) and the message's
// total length, big-endian - followed by a text body of KEY=VALUE lines,
// NUL-terminated.
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace ealobby {

constexpr size_t kHeaderSize = 12;

struct Message {
  std::string command;  // four characters, e.g. "@dir"
  std::string code;     // four characters; all NUL in a request or a success
  std::string body;     // raw body, NUL included
};

using Fields = std::map<std::string, std::string>;

// Parses KEY=VALUE lines out of a body.
Fields ParseFields(const std::string& body);

// Formats fields as a body: KEY=VALUE lines and a terminating NUL.
std::string FormatFields(const Fields& fields);

// A whole message on the wire. The code is padded or cut to four bytes.
std::vector<uint8_t> Encode(const std::string& command, const std::string& body,
                            const std::string& code = std::string(4, '\0'));
inline std::vector<uint8_t> Encode(const std::string& command, const Fields& fields,
                                   const std::string& code = std::string(4, '\0')) {
  return Encode(command, FormatFields(fields), code);
}

// Takes one complete message off the front of a receive buffer, if there is
// one. Returns false (leaving the buffer alone) while it is incomplete.
bool TakeMessage(std::vector<uint8_t>* buffer, Message* message);

// A body for a log line: printable text, other bytes as \xNN, newlines as |.
std::string Printable(const std::string& body);

// The id a player goes by in a game: ConnApi's client id and the lobby's
// OPIDn, which the game matches against each other. A hash of the whole
// gamertag (FNV-1a), so names sharing four letters ("Shipa", "Shipa_2") no
// longer collide; positive and non-zero, as -1 and 0 mean "no player".
inline uint32_t PlayerId(const std::string& name) {
  uint32_t h = 2166136261u;
  for (unsigned char c : name) {
    h = (h ^ c) * 16777619u;
  }
  h &= 0x7FFFFFFFu;
  return h ? h : 1;
}

}  // namespace ealobby
