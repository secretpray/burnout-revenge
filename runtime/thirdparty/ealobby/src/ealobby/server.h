// A stand-in for EA's Burnout Revenge lobby servers, which are long gone.
//
// The title first asks a directory server where the lobby is (@dir, on the
// port it is built with, 31860), then connects to the lobby itself. This
// serves both, on its own thread. It is built up from what the title actually
// sends: a command it does not handle yet is logged and answered with an empty
// success, so every run shows the next thing the title wants.
//
// It runs standalone (tools/ealobby, for a server of your own) or inside the
// game, for two machines on a LAN that have no server between them.
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "ealobby/aries.h"
#include "ealobby/locker.h"

namespace ealobby {

// A socket descriptor, wide enough for both a POSIX fd (int) and a Windows
// SOCKET (an unsigned, pointer-sized handle); -1 means none/closed on both.
using SocketHandle = std::intptr_t;

struct Options {
  std::string host = "0.0.0.0";
  uint16_t directory_port = 31860;
  uint16_t lobby_port = 31861;
  // Where @dir sends clients. Empty: the address they reached the directory at.
  std::string advertise;
  // Seamless multiplayer: the address this server is reached at by the other servers on the LAN. Non-empty
  // turns it on - the games it hosts are announced on UDP beacon_port, and it can join its clients to games
  // hosted by the others. Empty (the standalone server): off.
  std::string self_address;
  uint16_t beacon_port = 31859;
  // EA's Locker (Burnout Clips, locker.h) over HTTP on this port, its files
  // under locker_dir. 0: no Locker - a game's own lobby has none; the clips
  // live on a self-hosted server only.
  uint16_t locker_port = 0;
  std::string locker_dir = "locker";
};

class Server {
 public:
  using LogFunction = std::function<void(const std::string&)>;

  explicit Server(Options options, LogFunction log);
  ~Server();

  // Opens both ports and starts serving. False (with the reason logged) when
  // a port cannot be opened - most often because another server already has it.
  bool Start();
  void Stop();

 private:
  // kPeer: a link between two servers (messages xhlo, xjoi, xfwd, xlea). kRemote: a player of another server,
  // joined to a game hosted here; what is sent to it goes through its link.
  enum class Role { kDirectory, kLobby, kPeer, kRemote, kHttp };

  struct Connection {
    SocketHandle fd = -1;
    Role role = Role::kLobby;
    std::string peer;
    std::string local_address;  // the address the client reached us at
    std::string user;           // the persona it logged in as, once it has
    std::string address;        // the address the client gives for itself (addr)
    uint32_t id = 0;            // its user id in this lobby, from the login on
    std::string maddr;          // its Xbox Live address (MADDR), as it sent it
    std::string xuid;           // its XUID ("$" and hex), as it sent it
    std::string user_params;    // its player parameters (USERPARAMS) in its game
    uint32_t game = 0;          // the game it is in (IDENT), 0 for none
    // A client of this server that joined a game hosted by another server: the link to it and the game.
    SocketHandle remote_link = -1;
    uint32_t remote_game = 0;
    // kRemote: the link its messages come through and its id at the server it comes from.
    SocketHandle link = -1;
    uint32_t remote_id = 0;
    std::string node;  // kPeer: the other server's address
    // When it was last sent anything: a quiet lobby connection is pinged.
    std::chrono::steady_clock::time_point last_sent = std::chrono::steady_clock::now();
    std::vector<uint8_t> in;
    std::vector<uint8_t> out;
    bool close_when_sent = false;  // kHttp: one request, one answer
  };

  SocketHandle Listen(uint16_t port);
  void Run();
  void Accept(SocketHandle listener, Role role);
  void Receive(Connection& connection);
  void Flush(Connection& connection);
  void Handle(Connection& connection, const Message& message);
  void HandleDirectory(Connection& connection, const Message& message);
  void HandleLobby(Connection& connection, const Message& message);
  void Send(Connection& connection, const std::vector<uint8_t>& bytes);
  void Log(const std::string& line) const;

  // Seamless multiplayer (see Options::self_address).
  struct RemoteGame {
    std::string server;  // the address of the server that hosts it
    Fields record;       // its +gam record: the host as its only player
    std::chrono::steady_clock::time_point seen;
  };
  bool OpenBeacon();
  void Announce();
  void ReceiveBeacon();
  void HandlePeer(Connection& link, const Message& message);
  Connection* LinkTo(const std::string& server);
  void ForwardToHost(Connection& client, const Message& message);
  void DropRemote(Connection& client, bool tell_host);
  void OnLinkClosed(SocketHandle link);
  Connection* RemotePlayer(SocketHandle link, uint32_t id) const;
  std::map<uint32_t, RemoteGame> remote_games_;
  std::map<std::string, SocketHandle> links_;  // by the host server's address
  SocketHandle beacon_ = -1;
  SocketHandle next_virtual_ = -2;
  uint32_t game_tag_ = 0;  // the server's part of a game id, so ids differ between servers
  std::chrono::steady_clock::time_point next_announce_ = std::chrono::steady_clock::now();

  Options options_;
  LogFunction log_;
  SocketHandle directory_listener_ = -1;
  SocketHandle lobby_listener_ = -1;
  SocketHandle locker_listener_ = -1;
  std::unique_ptr<Locker> locker_;
  // Login keys (LKEY) handed out, and the persona each went to: the Locker's
  // requests carry the key.
  std::map<std::string, std::string> lkeys_;
  // A pipe (POSIX) or a loopback socket pair (Windows) to interrupt poll()/
  // WSAPoll() when stopping.
  SocketHandle wake_[2] = {-1, -1};
  std::map<SocketHandle, std::unique_ptr<Connection>> connections_;
  std::thread thread_;
  std::atomic<bool> running_{false};
  uint32_t next_session_ = 100000;
  uint32_t next_user_ = 1;
  uint32_t next_ping_ = 1;

  // Games (gcre/gjoi/glea/gsta/gset/gsea). The title reads a game as a
  // LobbyApi play record (LobbyApiExtractPlayRecord, 0x82411A58): the fields
  // below plus, per player, OPID/OPPO/ADDR/LADDR/MADDR/OPPART/OPPARAM/
  // OPFLAG/PRES with its index.
  struct Game {
    uint32_t id = 0;
    std::string name;
    std::string host;
    std::string params;
    std::string room;
    std::string custflags;
    std::string sysflags;
    std::string minsize;
    std::string maxsize;
    std::string session;  // SESS: the host's Xbox Live session, for joiners
    uint32_t seed = 0;
    std::vector<SocketHandle> players;  // the host first
  };
  std::map<uint32_t, Game> games_;
  uint32_t next_game_ = 1;
  Fields GameRecord(const Game& game, const Connection& to) const;
  // Sends the game's record to every player in it, as '+mgm' (their own game).
  void SendGameToPlayers(const Game& game);
  void LeaveGame(Connection& connection);
  Connection* ConnectionFor(SocketHandle fd) const;
  // '+who': the user's own record, sent at login and again whenever the game
  // it is in (G) changes.
  void SendWho(Connection& connection);
};

}  // namespace ealobby
