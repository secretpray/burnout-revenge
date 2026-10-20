// EA's Locker, as Burnout Revenge's DirtySock (LockerApi) asks it: the store
// behind Burnout Clips (save, share, recommend, the friends' and the top 20).
//
// One HTTP request a command:
//   GET  /easo/editorial/common/2006/locker/locker.jsp?site=easo&cmd=<cmd>&lkey=<key>&...
// cmd dir lists a persona's files (pers=, else the caller's), get sends one
// (name=), put stores the request's body as one (name, type, game, attr, desc,
// over=1 to replace, locs), del removes one. The answer is XML: a LOCKER
// element (pers, maxBytes, numBytes, maxFiles, numFiles) holding a FILE per
// file (name, size, type, game, attr, desc, date, pers, locs). The client takes
// LOCKER's error attribute (default 1) for the result: 0, or the request
// failed; and reads no more than 0x7000 bytes of it. get answers the file's
// bytes.
//
// The files are kept under a directory of the server's, a directory per
// persona: <name> and <name>.meta (its fields, KEY=VALUE lines).
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace ealobby {

class Locker {
 public:
  // persona_for_key: the persona a login key (LKEY) was given to, or "".
  Locker(std::filesystem::path root, std::function<std::string(const std::string&)> persona_for_key,
         std::function<void(const std::string&)> log);

  // Whether request holds a whole HTTP request yet (headers and body).
  static bool Complete(const std::vector<uint8_t>& request);
  // The whole HTTP response to it.
  std::vector<uint8_t> Handle(const std::vector<uint8_t>& request);

  // The clips on the server - each persona's files of the clip body type
  // (b4replaybody) - and how often others have fetched them: the top 20 and
  // the recent players' clips are made of these.
  struct Clip {
    std::string persona;
    std::string name;
    uint64_t downloads = 0;
  };
  std::vector<Clip> Clips() const;

 private:
  std::filesystem::path root_;
  std::function<std::string(const std::string&)> persona_for_key_;
  std::function<void(const std::string&)> log_;
};

}  // namespace ealobby
