#include "ealobby/server.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <random>

namespace ealobby {
namespace {

// The Windows Sockets API differs from BSD sockets in its types (SOCKET vs.
// int), close/error/blocking calls, and needs WSAStartup once per process;
// WSAPoll stands in for poll() and a loopback socket pair for the wake pipe,
// since Windows has no fd that is both a pipe and pollable as a socket.
#ifdef _WIN32
using NativeSocket = SOCKET;
constexpr NativeSocket kInvalidNativeSocket = INVALID_SOCKET;
using PollFd = WSAPOLLFD;
using SockLen = int;
#else
using NativeSocket = int;
constexpr NativeSocket kInvalidNativeSocket = -1;
using PollFd = pollfd;
using SockLen = socklen_t;
#endif

NativeSocket ToNative(SocketHandle handle) {
  return static_cast<NativeSocket>(handle);
}

SocketHandle FromNative(NativeSocket socket) {
  return static_cast<SocketHandle>(socket);
}

int LastSocketError() {
#ifdef _WIN32
  return WSAGetLastError();
#else
  return errno;
#endif
}

bool WouldBlock(int code) {
#ifdef _WIN32
  return code == WSAEWOULDBLOCK;
#else
  return code == EAGAIN || code == EWOULDBLOCK;
#endif
}

bool Interrupted(int code) {
#ifdef _WIN32
  return code == WSAEINTR;
#else
  return code == EINTR;
#endif
}

std::string SocketErrorString(int code) {
#ifdef _WIN32
  char text[256] = {};
  FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
                 static_cast<DWORD>(code), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), text,
                 sizeof(text), nullptr);
  std::string message(text);
  while (!message.empty() && (message.back() == '\n' || message.back() == '\r')) {
    message.pop_back();
  }
  return message;
#else
  return std::strerror(code);
#endif
}

void CloseNative(NativeSocket socket) {
#ifdef _WIN32
  closesocket(socket);
#else
  close(socket);
#endif
}

bool SetNonBlocking(NativeSocket socket) {
#ifdef _WIN32
  u_long mode = 1;
  return ioctlsocket(socket, FIONBIO, &mode) == 0;
#else
  return fcntl(socket, F_SETFL, fcntl(socket, F_GETFL, 0) | O_NONBLOCK) == 0;
#endif
}

int PollWait(std::vector<PollFd>& fds, int timeout_ms) {
#ifdef _WIN32
  return WSAPoll(fds.data(), static_cast<ULONG>(fds.size()), timeout_ms);
#else
  return poll(fds.data(), fds.size(), timeout_ms);
#endif
}

PollFd MakePollFd(SocketHandle handle, short events) {
  PollFd entry{};
  entry.fd = static_cast<decltype(PollFd::fd)>(handle);
  entry.events = events;
  entry.revents = 0;
  return entry;
}

#ifdef _WIN32
struct WinsockInit {
  WinsockInit() {
    WSADATA data;
    WSAStartup(MAKEWORD(2, 2), &data);
  }
  ~WinsockInit() { WSACleanup(); }
};

// Windows has no fd that is both a pipe and poll()-able as a socket, so the
// wake signal rides a loopback TCP connection instead: a listener on an
// ephemeral port, a writer that connects to it, and the accepted end as the
// reader.
bool MakeWakePair(SocketHandle wake[2]) {
  static WinsockInit winsock_init;
  const NativeSocket listener = socket(AF_INET, SOCK_STREAM, 0);
  if (listener == kInvalidNativeSocket) {
    return false;
  }
  sockaddr_in address = {};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = 0;
  SockLen length = sizeof(address);
  if (bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
      getsockname(listener, reinterpret_cast<sockaddr*>(&address), &length) != 0 ||
      listen(listener, 1) != 0) {
    CloseNative(listener);
    return false;
  }
  const NativeSocket writer = socket(AF_INET, SOCK_STREAM, 0);
  if (writer == kInvalidNativeSocket ||
      connect(writer, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
    CloseNative(listener);
    if (writer != kInvalidNativeSocket) {
      CloseNative(writer);
    }
    return false;
  }
  const NativeSocket reader = accept(listener, nullptr, nullptr);
  CloseNative(listener);
  if (reader == kInvalidNativeSocket) {
    CloseNative(writer);
    return false;
  }
  wake[0] = FromNative(reader);
  wake[1] = FromNative(writer);
  return true;
}
#endif

std::string AddressString(const sockaddr_in& address) {
  char text[INET_ADDRSTRLEN] = {};
  inet_ntop(AF_INET, &address.sin_addr, text, sizeof(text));
  return text;
}

std::string RandomHex(size_t bytes) {
  static std::mt19937_64 random{std::random_device{}()};
  static const char digits[] = "0123456789abcdef";
  std::string out;
  for (size_t i = 0; i < bytes; ++i) {
    const unsigned value = unsigned(random() & 0xFF);
    out += digits[value >> 4];
    out += digits[value & 0xF];
  }
  return out;
}

std::string Hex(const uint8_t* data, size_t size) {
  static const char digits[] = "0123456789abcdef";
  std::string out;
  out.reserve(size * 2);
  for (size_t i = 0; i < size; ++i) {
    out += digits[data[i] >> 4];
    out += digits[data[i] & 0xF];
  }
  return out;
}

std::string Hex(const std::vector<uint8_t>& bytes) {
  return Hex(bytes.data(), bytes.size());
}

std::string Hex(const std::string& text) {
  return Hex(reinterpret_cast<const uint8_t*>(text.data()), text.size());
}

std::vector<uint8_t> Unhex(const std::string& text) {
  const auto value = [](char c) { return c >= 'a' ? c - 'a' + 10 : c >= 'A' ? c - 'A' + 10 : c - '0'; };
  std::vector<uint8_t> out;
  for (size_t i = 0; i + 1 < text.size(); i += 2) {
    out.push_back(uint8_t(value(text[i]) << 4 | value(text[i + 1])));
  }
  return out;
}

// Seamless multiplayer: a server announces the games it hosts as "XERENGE-GAME 1 <server> <game id> <hex of
// its +gam record>" on UDP, to the broadcast address and to a multicast group (which also reaches the other
// copies on the same machine).
constexpr char kGameBeacon[] = "XERENGE-GAME 1 ";
constexpr uint32_t kBeaconGroup = 0xEFFF4D4Du;  // 239.255.77.77
constexpr auto kRemoteGameLifetime = std::chrono::seconds(4);

}  // namespace

Server::Server(Options options, LogFunction log) : options_(std::move(options)), log_(std::move(log)) {}

Server::~Server() {
  Stop();
}

void Server::Log(const std::string& line) const {
  if (log_) {
    log_(line);
  }
}

SocketHandle Server::Listen(uint16_t port) {
#ifdef _WIN32
  static WinsockInit winsock_init;
#endif
  const NativeSocket fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd == kInvalidNativeSocket) {
    Log("ealobby: socket: " + SocketErrorString(LastSocketError()));
    return -1;
  }
  const int yes = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));
  sockaddr_in address = {};
  address.sin_family = AF_INET;
  address.sin_port = htons(port);
  if (inet_pton(AF_INET, options_.host.c_str(), &address.sin_addr) != 1) {
    Log("ealobby: bad listen address " + options_.host);
    CloseNative(fd);
    return -1;
  }
  if (bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0 || listen(fd, 16) < 0) {
    Log("ealobby: cannot listen on " + options_.host + ":" + std::to_string(port) + ": " +
        SocketErrorString(LastSocketError()));
    CloseNative(fd);
    return -1;
  }
  SetNonBlocking(fd);
  return FromNative(fd);
}

bool Server::Start() {
  if (running_) {
    return true;
  }
  directory_listener_ = Listen(options_.directory_port);
  lobby_listener_ = Listen(options_.lobby_port);
#ifdef _WIN32
  const bool wake_ok = MakeWakePair(wake_);
#else
  int wake_fds[2] = {-1, -1};
  const bool wake_ok = pipe(wake_fds) == 0;
  wake_[0] = wake_fds[0];
  wake_[1] = wake_fds[1];
#endif
  if (directory_listener_ < 0 || lobby_listener_ < 0 || !wake_ok) {
    Stop();
    return false;
  }
  if (options_.locker_port != 0) {
    locker_listener_ = Listen(options_.locker_port);
    if (locker_listener_ >= 0) {
      locker_ = std::make_unique<Locker>(
          options_.locker_dir,
          [this](const std::string& key) {
            const auto it = lkeys_.find(key);
            return it != lkeys_.end() ? it->second : std::string();
          },
          [this](const std::string& line) { Log(line); });
      Log("ealobby: Locker (Burnout Clips) on port " + std::to_string(options_.locker_port) +
          ", files in " + options_.locker_dir);
    }
  }
  if (!options_.self_address.empty()) {
    unsigned a = 0, b = 0, c = 0, d = 0;
    if (std::sscanf(options_.self_address.c_str(), "%u.%u.%u.%u", &a, &b, &c, &d) == 4) {
      game_tag_ = (d & 0xFFu) << 16;
    }
    if (!OpenBeacon()) {
      Log("ealobby: no beacon socket; games on other servers will not be found");
    }
  }
  SetNonBlocking(ToNative(wake_[0]));
  running_ = true;
  thread_ = std::thread([this] { Run(); });
  Log("ealobby: directory on " + options_.host + ":" + std::to_string(options_.directory_port) +
      ", lobby on port " + std::to_string(options_.lobby_port));
  return true;
}

void Server::Stop() {
  if (running_.exchange(false) && wake_[1] >= 0) {
    const char byte = 0;
#ifdef _WIN32
    send(ToNative(wake_[1]), &byte, 1, 0);
#else
    [[maybe_unused]] ssize_t written = write(wake_[1], &byte, 1);
#endif
  }
  if (thread_.joinable()) {
    thread_.join();
  }
  for (auto& [fd, connection] : connections_) {
    if (fd >= 0) {
      CloseNative(ToNative(fd));
    }
  }
  connections_.clear();
  for (SocketHandle* fd : {&directory_listener_, &lobby_listener_, &locker_listener_, &beacon_, &wake_[0], &wake_[1]}) {
    if (*fd >= 0) {
      CloseNative(ToNative(*fd));
      *fd = -1;
    }
  }
}

void Server::Run() {
  while (running_) {
    std::vector<PollFd> fds;
    fds.push_back(MakePollFd(wake_[0], POLLIN));
    fds.push_back(MakePollFd(directory_listener_, POLLIN));
    fds.push_back(MakePollFd(lobby_listener_, POLLIN));
    fds.push_back(MakePollFd(beacon_, POLLIN));
    fds.push_back(MakePollFd(locker_listener_, POLLIN));
    for (auto& [fd, connection] : connections_) {
      if (connection->role == Role::kRemote) {
        continue;
      }
      fds.push_back(MakePollFd(fd, short(POLLIN | (connection->out.empty() ? 0 : POLLOUT))));
    }
    if (PollWait(fds, 1000) < 0) {
      const int code = LastSocketError();
      if (Interrupted(code)) {
        continue;
      }
      Log("ealobby: poll: " + SocketErrorString(code));
      break;
    }
    if (fds[1].revents & POLLIN) {
      Accept(directory_listener_, Role::kDirectory);
    }
    if (fds[2].revents & POLLIN) {
      Accept(lobby_listener_, Role::kLobby);
    }
    if (beacon_ >= 0 && (fds[3].revents & POLLIN)) {
      ReceiveBeacon();
    }
    if (locker_listener_ >= 0 && (fds[4].revents & POLLIN)) {
      Accept(locker_listener_, Role::kHttp);
    }
    std::vector<SocketHandle> closed;
    for (size_t i = 5; i < fds.size(); ++i) {
      auto found = connections_.find(static_cast<SocketHandle>(fds[i].fd));
      if (found == connections_.end()) {
        continue;
      }
      Connection& connection = *found->second;
      if (fds[i].revents & (POLLIN | POLLHUP | POLLERR)) {
        Receive(connection);
      }
      if (connection.fd >= 0 && (fds[i].revents & POLLOUT)) {
        Flush(connection);
      }
      if (connection.fd < 0) {
        closed.push_back(static_cast<SocketHandle>(fds[i].fd));
      }
    }
    for (SocketHandle fd : closed) {
      if (Connection* gone = ConnectionFor(fd)) {
        gone->fd = fd;  // Receive() cleared it; LeaveGame removes the player by fd
        if (gone->role == Role::kPeer) {
          OnLinkClosed(fd);
        } else {
          if (gone->remote_link >= 0) {
            DropRemote(*gone, true);
          }
          LeaveGame(*gone);
        }
        gone->fd = -1;
      }
      connections_.erase(fd);
    }
    if (std::chrono::steady_clock::now() >= next_announce_) {
      next_announce_ = std::chrono::steady_clock::now() + std::chrono::seconds(1);
      Announce();
    }
    // DirtySock drops a lobby connection it has heard nothing on for 60 s
    // (LobbyApi ref+0x34, renewed by every message). EA's server kept it
    // alive with '~png', which the client answers in kind.
    const auto now = std::chrono::steady_clock::now();
    for (auto& [fd, connection] : connections_) {
      if (connection->role == Role::kLobby && connection->id != 0 &&
          now - connection->last_sent >= std::chrono::seconds(20)) {
        Send(*connection, Encode("~png", Fields{{"REF", std::to_string(next_ping_++)}}));
      }
    }
  }
}

void Server::Accept(SocketHandle listener, Role role) {
  for (;;) {
    sockaddr_in peer = {};
    SockLen peer_length = sizeof(peer);
    const NativeSocket native =
        accept(ToNative(listener), reinterpret_cast<sockaddr*>(&peer), &peer_length);
    if (native == kInvalidNativeSocket) {
      return;
    }
    const SocketHandle fd = FromNative(native);
    SetNonBlocking(native);
    const int yes = 1;
    setsockopt(native, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&yes), sizeof(yes));
    auto connection = std::make_unique<Connection>();
    connection->fd = fd;
    connection->role = role;
    connection->peer = AddressString(peer) + ":" + std::to_string(ntohs(peer.sin_port));
    sockaddr_in local = {};
    SockLen local_length = sizeof(local);
    getsockname(native, reinterpret_cast<sockaddr*>(&local), &local_length);
    connection->local_address = AddressString(local);
    Log(std::string(role == Role::kDirectory ? "directory" : "lobby") + ": connection from " +
        connection->peer);
    connections_[fd] = std::move(connection);
  }
}

void Server::Receive(Connection& connection) {
  uint8_t chunk[4096];
  const NativeSocket native = ToNative(connection.fd);
  for (;;) {
    const auto received = recv(native, reinterpret_cast<char*>(chunk), sizeof(chunk), 0);
    if (received > 0) {
      connection.in.insert(connection.in.end(), chunk, chunk + received);
      continue;
    }
    if (received < 0 && WouldBlock(LastSocketError())) {
      break;
    }
    Log(std::string(connection.role == Role::kDirectory ? "directory" : "lobby") + ": " +
        connection.peer + " closed");
    CloseNative(native);
    connection.fd = -1;
    return;
  }
  // The Locker's: one HTTP request, its answer, closed once sent.
  if (connection.role == Role::kHttp) {
    if (locker_ && !connection.close_when_sent && Locker::Complete(connection.in)) {
      connection.close_when_sent = true;
      const std::vector<uint8_t> request = std::move(connection.in);
      connection.in.clear();
      Send(connection, locker_->Handle(request));
    }
    return;
  }
  Message message;
  while (connection.fd >= 0 && TakeMessage(&connection.in, &message)) {
    Handle(connection, message);
  }
}

void Server::Flush(Connection& connection) {
  const NativeSocket native = ToNative(connection.fd);
  while (!connection.out.empty()) {
#ifdef _WIN32
    const auto sent = send(native, reinterpret_cast<const char*>(connection.out.data()),
                            static_cast<int>(connection.out.size()), 0);
#else
    const auto sent =
        send(native, connection.out.data(), connection.out.size(), MSG_NOSIGNAL);
#endif
    if (sent <= 0) {
      if (sent < 0 && WouldBlock(LastSocketError())) {
        return;
      }
      CloseNative(native);
      connection.fd = -1;
      return;
    }
    connection.out.erase(connection.out.begin(), connection.out.begin() + sent);
  }
  if (connection.close_when_sent) {
    CloseNative(native);
    connection.fd = -1;
  }
}

void Server::Send(Connection& connection, const std::vector<uint8_t>& bytes) {
  if (connection.role == Role::kRemote) {
    if (Connection* link = ConnectionFor(connection.link)) {
      Fields forwarded;
      forwarded["U"] = std::to_string(connection.remote_id);
      forwarded["D"] = Hex(bytes);
      Send(*link, Encode("xfwd", forwarded));
    }
    return;
  }
  connection.last_sent = std::chrono::steady_clock::now();
  connection.out.insert(connection.out.end(), bytes.begin(), bytes.end());
  Flush(connection);
}

void Server::Handle(Connection& connection, const Message& message) {
  if (connection.role == Role::kLobby && message.command == "xhlo") {
    connection.role = Role::kPeer;
  }
  if (connection.role == Role::kPeer) {
    HandlePeer(connection, message);
    return;
  }
  if (message.command == "~png") {
    // The answer to our keepalive (or the client's own): nothing to say back,
    // and every 20 s is too often to log.
    return;
  }
  const char* who = connection.role == Role::kDirectory ? "directory" : "lobby";
  Log(std::string(who) + " <- " + Printable(message.command) + " [" + Printable(message.code) +
      "] " + Printable(message.body));
  if (connection.role == Role::kDirectory) {
    HandleDirectory(connection, message);
  } else {
    HandleLobby(connection, message);
  }
}

void Server::HandleDirectory(Connection& connection, const Message& message) {
  if (message.command == "@tic") {
    // The client offers to encrypt the session (RC4+MD5-V2). Not answering
    // keeps it in the clear.
    return;
  }
  if (message.command == "@dir") {
    Fields reply;
    reply["ADDR"] = options_.advertise.empty() ? connection.local_address : options_.advertise;
    reply["PORT"] = std::to_string(options_.lobby_port);
    reply["SESS"] = std::to_string(next_session_++);
    reply["MASK"] = RandomHex(16);
    Log("directory -> @dir " + Printable(FormatFields(reply)));
    Send(connection, Encode("@dir", reply));
    return;
  }
  Log("directory: " + Printable(message.command) + " not handled yet; answered empty");
  Send(connection, Encode(message.command, std::string(1, '\0')));
}

void Server::HandleLobby(Connection& connection, const Message& message) {
  // The client as its peers see it: what it said in 'addr', else where its
  // connection comes from.
  const auto PeerAddress = [](const Connection& c) {
    return !c.address.empty() ? c.address : c.peer.substr(0, c.peer.find(':'));
  };
  // A client joined to a game another server hosts: what belongs to the game goes to that server, which
  // answers as if the client were its own; anything else is this server's business as before.
  if (connection.remote_link >= 0) {
    if (message.command == "gset" || message.command == "gsta" || message.command == "mesg") {
      ForwardToHost(connection, message);
      return;
    }
    if (message.command == "glea" || message.command == "gdel") {
      ForwardToHost(connection, message);
      connection.remote_link = -1;
      connection.remote_game = 0;
      return;
    }
    if (message.command == "gjoi" || message.command == "gcre" || message.command == "gqwk") {
      DropRemote(connection, true);
    }
  }
  if (message.command == "addr") {
    // The client's own address and port, as it sees them. Kept: it is the
    // address its peers reach it at, which the connection's own source (on
    // one machine, 127.0.0.1) is not.
    const Fields asked = ParseFields(message.body);
    const auto addr = asked.find("ADDR");
    if (addr != asked.end() && !addr->second.empty()) {
      connection.address = addr->second;
    }
    Send(connection, Encode("addr", std::string(1, '\0')));
    return;
  }
  if (message.command == "skey") {
    // The session key exchange. The client reads SKEY (binary, $hex) and DP
    // from the reply, then counts itself connected.
    Fields reply;
    reply["SKEY"] = "$" + RandomHex(16);
    reply["DP"] = "XBL2/Burnout-Feb2006";
    Log("lobby -> skey " + Printable(FormatFields(reply)));
    Send(connection, Encode("skey", reply));
    return;
  }
  if (message.command == "news") {
    // NAME=7: the configuration the lobby code reads back later - where the
    // news and terms of service are, and the buddy server. There are none.
    Fields reply;
    reply["BUDDY_SERVER"] = connection.local_address;
    reply["BUDDY_PORT"] = "0";
    reply["BUDDY_URL"] = "";
    reply["TOSAC_URL"] = "";
    reply["NEWS_URL"] = "";
    reply["NEWS_DATE"] = "2006.2.10-0:00:00";
    // The client files the reply by its header code, 'new' and the NAME asked
    // for (sub_8240EAD8 checks for 'new7'); without it the configuration is
    // dropped and the login waits at the connect step for good.
    const Fields asked = ParseFields(message.body);
    const auto name = asked.find("NAME");
    const std::string code =
        "new" + (name != asked.end() && !name->second.empty() ? name->second.substr(0, 1)
                                                              : std::string("7"));
    Log("lobby -> news/" + code + " " + Printable(FormatFields(reply)));
    Send(connection, Encode("news", reply, code));
    return;
  }

  // The login, from what the client code reads back (retail strings near
  // 0x82052880 and 0x820469A0). Until each reply's exact shape is known from
  // a run, a reply repeats what was asked - the client finds its own fields
  // where it expects them - and adds the ones it reads.
  const Fields asked = ParseFields(message.body);
  Log("lobby <- " + message.command + " " + Printable(message.body));
  const auto reply_with = [&](Fields reply) {
    Log("lobby -> " + message.command + " " + Printable(FormatFields(reply)));
    Send(connection, Encode(message.command, reply));
  };
  const auto field = [&](const char* key, const std::string& fallback) {
    const auto it = asked.find(key);
    return it != asked.end() && !it->second.empty() ? it->second : fallback;
  };

  if (message.command == "sele") {
    // What the client subscribes to (INGAME, MESGS, USERS, GAMES, ROOMS, ...).
    // The dispatcher reads MORE, SLOTS and STATS back.
    Fields reply = asked;
    reply["MORE"] = "0";
    reply["SLOTS"] = "4";
    reply.emplace("STATS", "0");
    reply_with(reply);
    return;
  }
  if (message.command == "auth" || message.command == "acct") {
    // Logging in as the Xbox Live gamertag. PERSONAS lists the names the
    // account plays as; LKEY is the key later requests (and EA's web
    // services) carry.
    const std::string name = field("GTAG", field("NAME", field("PERS", "Player")));
    connection.user = name;
    connection.maddr = field("MADDR", "");
    connection.xuid = field("XUID", "");
    Fields reply;
    for (const auto& [key, value] : asked) {
      if (key != "PASS") {
        reply[key] = value;
      }
    }
    reply["NAME"] = name;
    reply["PERSONAS"] = name;
    reply["LKEY"] = RandomHex(16);
    lkeys_[reply["LKEY"]] = name;
    reply["TOS"] = "1";
    reply["SHARE"] = "1";
    reply["SPAM"] = "NN";
    reply["BORN"] = "19800101";
    reply["GEND"] = "M";
    reply["MAIL"] = "player@xerenge.local";
    reply["LAST"] = "2006.2.10-0:00:00";
    reply["ADDR"] = PeerAddress(connection);
    reply_with(reply);
    return;
  }
  if (message.command == "pers") {
    // Choosing the persona to play as.
    const std::string name = field("PERS", field("NAME", connection.user.empty() ? "Player"
                                                                                  : connection.user));
    connection.user = name;
    Fields reply = asked;
    reply["PERS"] = name;
    reply["NAME"] = name;
    reply["LKEY"] = RandomHex(16);
    lkeys_[reply["LKEY"]] = name;
    reply["LAST"] = "2006.2.10-0:00:00";
    reply["PLAST"] = "2006.2.10-0:00:00";
    reply["ADDR"] = PeerAddress(connection);
    reply_with(reply);

    // '+who': the user's own record, which the client files as 'self'
    // (handler 0x8241C4C8, LobbyApiExtractUserRecord). Until it arrives the
    // login's last request stays in flight - CGtLobbyDirtySock's rejoin
    // step (0x823719B8) waits for self's I, then reads G, the game the user
    // was in (0: none) - and the lobby reports itself busy for good.
    if (connection.id == 0) {
      connection.id = next_user_++;
    }
    SendWho(connection);
    return;
  }
  if (message.command == "cate") {
    // The ranking categories (DirtySock's LobbyRank, reply parsed at
    // 0x824066D0). CC, IC and VC (each defaulting to 1) size its tables;
    // then R, comma separated: per category "name,indices", per index
    // "name,variations", per variation "name,n,columns" and that many column
    // values. The category's and the index's counts drive the parser's loop
    // (0 underflows it). Only once R has been read to the end is the fetch
    // reported done - short of that it stays in flight and the lobby busy
    // for good. One empty ranking table.
    Fields reply;
    reply["CC"] = "1";
    reply["IC"] = "1";
    reply["VC"] = "1";
    reply["R"] = "RANKED,1,ALL,1,DEFAULT,0,0";
    reply_with(reply);
    return;
  }
  if (message.command == "onln" || message.command == "user") {
    // Who is online / a user's details: this lobby knows only who is
    // connected to it.
    Fields reply = asked;
    reply["NAME"] = field("PERS", field("NAME", connection.user));
    reply["ADDR"] = PeerAddress(connection);
    reply_with(reply);
    return;
  }
  // Games. A game is created (gcre), found (gsea), joined (gjoi), changed
  // (gset), started (gsta) and left (glea); every player in it is sent the
  // whole record again as '+mgm' whenever it changes, which is how the title
  // (CGtLobbyDirtySock's event callback, 0x8236C700, event 'game') learns who
  // is in its game and who hosts it - it finds itself among OPPO%d by name.
  // '+ses' (event 'play') starts the session.
  if (message.command == "gcre") {
    LeaveGame(connection);
    Game game;
    game.id = game_tag_ | next_game_++;
    game.host = connection.user.empty() ? "Player" : connection.user;
    game.name = field("NAME", game.host);
    game.params = field("PARAMS", "");
    game.room = field("ROOM", "0");
    game.custflags = field("CUSTFLAGS", "0");
    game.sysflags = field("SYSFLAGS", "0");
    game.minsize = field("MINSIZE", "2");
    game.maxsize = field("MAXSIZE", "2");
    game.seed = uint32_t(std::stoul(RandomHex(4), nullptr, 16));
    game.players.push_back(connection.fd);
    connection.game = game.id;
    connection.user_params = field("USERPARAMS", "");
    const Game& stored = games_[game.id] = std::move(game);
    reply_with(GameRecord(stored, connection));
    SendGameToPlayers(stored);
    SendWho(connection);
    return;
  }
  // SYSFLAGS 0x1000 is set by LockGame at the start of a race: such a game is not found and cannot be joined.
  const auto number = [](const std::string& text) { return std::strtoul(text.c_str(), nullptr, 10); };
  const auto locked = [&](const Game& g) { return (number(g.sysflags) & 0x1000) != 0; };
  // Joins a game another server hosts: that server takes the player (xjoi), running the client's own request
  // (a gjoi, or a gqwk) as if the client were connected to it, and answers to it through this one.
  const auto join_remote = [&](uint32_t remote_id, const RemoteGame& remote) {
    Connection* link = LinkTo(remote.server);
    if (!link) {
      Log("lobby: cannot reach the server " + remote.server + " of game " + std::to_string(remote_id));
      return false;
    }
    DropRemote(connection, true);
    LeaveGame(connection);
    connection.remote_link = link->fd;
    connection.remote_game = remote_id;
    connection.user_params = field("USERPARAMS", connection.user_params);
    Fields join;
    join["U"] = std::to_string(connection.id);
    join["NAME"] = connection.user;
    join["ADDR"] = PeerAddress(connection);
    join["MADDR"] = connection.maddr;
    join["XUID"] = connection.xuid;
    join["PEER"] = connection.peer;
    join["USERPARAMS"] = connection.user_params;
    join["D"] = Hex(Encode(message.command, message.body, message.code));
    Log("lobby: " + connection.user + " joins game " + std::to_string(remote_id) + " on the server " + remote.server);
    Send(*link, Encode("xjoi", join));
    return true;
  };
  if (message.command == "gjoi") {
    // By IDENT, or by the host's name (NAME/USER).
    Game* game = nullptr;
    const std::string ident = field("IDENT", "");
    const std::string by_name = field("NAME", field("USER", ""));
    for (auto& [id, g] : games_) {
      if ((!ident.empty() && std::to_string(id) == ident) ||
          (ident.empty() && !by_name.empty() && (g.name == by_name || g.host == by_name))) {
        game = &g;
        break;
      }
    }
    if (!game) {
      // Not one of ours: a game another server announced.
      for (const auto& [remote_id, remote] : remote_games_) {
        const auto name = remote.record.find("NAME");
        const auto host = remote.record.find("HOST");
        const bool named = ident.empty() && !by_name.empty() &&
                           ((name != remote.record.end() && name->second == by_name) ||
                            (host != remote.record.end() && host->second == by_name));
        if (!((!ident.empty() && std::to_string(remote_id) == ident) || named)) {
          continue;
        }
        if (join_remote(remote_id, remote)) {
          return;
        }
        break;
      }
    }
    if (!game) {
      Log("lobby: gjoi - no such game");
      Send(connection, Encode(message.command, Fields{}, "gjnf"));
      return;
    }
    if (locked(*game) && connection.game != game->id) {
      Log("lobby: gjoi - game " + std::to_string(game->id) + " is locked (race in progress)");
      Send(connection, Encode(message.command, Fields{}, "gjnf"));
      return;
    }
    if (connection.game != game->id) {
      LeaveGame(connection);
      game->players.push_back(connection.fd);
      connection.game = game->id;
    }
    connection.user_params = field("USERPARAMS", connection.user_params);
    reply_with(GameRecord(*game, connection));
    SendGameToPlayers(*game);
    SendWho(connection);
    return;
  }
  if (message.command == "gqwk") {
    // Quick match: the first open game with room, or none.
    Game* game = nullptr;
    for (auto& [id, g] : games_) {
      if (id != connection.game && !locked(g) && std::stoi(g.maxsize.empty() ? "0" : g.maxsize) >
                                       static_cast<int>(g.players.size())) {
        game = &g;
        break;
      }
    }
    if (!game) {
      // None here: one that another server announced, open and with room.
      const auto now = std::chrono::steady_clock::now();
      for (const auto& [remote_id, remote] : remote_games_) {
        if (now - remote.seen > kRemoteGameLifetime) {
          continue;
        }
        const auto field_of = [&](const char* key) {
          const auto it = remote.record.find(key);
          return it != remote.record.end() ? it->second : std::string();
        };
        if ((number(field_of("SYSFLAGS")) & 0x1000) == 0 &&
            number(field_of("COUNT")) < number(field_of("MAXSIZE")) && join_remote(remote_id, remote)) {
          return;
        }
      }
    }
    if (!game) {
      Log("lobby: gqwk - no open game");
      Send(connection, Encode(message.command, Fields{}, "gjnf"));
      return;
    }
    LeaveGame(connection);
    game->players.push_back(connection.fd);
    connection.game = game->id;
    connection.user_params = field("USERPARAMS", connection.user_params);
    reply_with(GameRecord(*game, connection));
    SendGameToPlayers(*game);
    SendWho(connection);
    return;
  }
  if (message.command == "glea" || message.command == "gdel") {
    reply_with(Fields{});
    // The host deleting its game ends it for everyone in it. Taken as the
    // host leaving, the game went on with the next player as its host, alone
    // in a game nobody else was in: a third player sat in "Returning from
    // game" for good.
    if (auto it = games_.find(connection.game);
        message.command == "gdel" && it != games_.end() && it->second.host == connection.user) {
      const std::vector<SocketHandle> others = it->second.players;
      for (SocketHandle fd : others) {
        Connection* player = ConnectionFor(fd);
        if (!player || player == &connection) {
          continue;
        }
        player->game = 0;
        Send(*player, Encode("+mgm", Fields{{"IDENT", "0"}, {"NAME", ""}, {"HOST", ""}, {"COUNT", "0"}}));
        SendWho(*player);
      }
      Log("lobby: game " + std::to_string(it->first) + " deleted by its host");
      it->second.players.assign(1, connection.fd);
    }
    LeaveGame(connection);
    Send(connection, Encode("+mgm", Fields{{"IDENT", "0"}, {"NAME", ""}, {"HOST", ""}, {"COUNT", "0"}}));
    SendWho(connection);
    return;
  }
  if (message.command == "gset") {
    auto it = games_.find(connection.game);
    if (it == games_.end()) {
      reply_with(Fields{});
      return;
    }
    Game& game = it->second;
    // The host's "kick player": KICK names the player. The game finds it was kicked when its own name is
    // missing from the game's player list (CGtLobbyDirtySock::CheckForPlayerKicked), and shows "kicked" instead
    // of "removed" if a message with flag bit 29 ("2") came first.
    const std::string kick = field("KICK", "");
    if (!kick.empty()) {
      Connection* target = nullptr;
      for (SocketHandle fd : game.players) {
        Connection* player = ConnectionFor(fd);
        if (player && player != &connection && player->user == kick) {
          target = player;
          break;
        }
      }
      if (target) {
        Fields push;
        push["N"] = connection.user;
        push["T"] = target->user;
        push["F"] = "2";
        Log("lobby: kick " + target->user + " out of game " + std::to_string(game.id));
        Send(*target, Encode("+msg", push));
        LeaveGame(*target);
        Send(*target, Encode("+mgm", Fields{{"IDENT", "0"}, {"NAME", ""}, {"HOST", ""}, {"COUNT", "0"}}));
        SendWho(*target);
      } else {
        Log("lobby: kick " + kick + " - not in the game");
      }
      reply_with(GameRecord(game, connection));
      return;
    }
    game.name = field("NAME", game.name);
    game.params = field("PARAMS", game.params);
    game.custflags = field("CUSTFLAGS", game.custflags);
    game.sysflags = field("SYSFLAGS", game.sysflags);
    game.minsize = field("MINSIZE", game.minsize);
    game.maxsize = field("MAXSIZE", game.maxsize);
    game.session = field("SESS", game.session);
    connection.user_params = field("USERPARAMS", connection.user_params);
    reply_with(GameRecord(game, connection));
    SendGameToPlayers(game);
    return;
  }
  if (message.command == "gsta") {
    auto it = games_.find(connection.game);
    reply_with(it != games_.end() ? GameRecord(it->second, connection) : Fields{});
    if (it != games_.end()) {
      for (SocketHandle fd : it->second.players) {
        if (Connection* player = ConnectionFor(fd)) {
          const Fields record = GameRecord(it->second, *player);
          Log("lobby -> +ses to " + player->user + " " + Printable(FormatFields(record)));
          Send(*player, Encode("+ses", record));
        }
      }
    }
    return;
  }
  if (message.command == "gsea") {
    // Every game that fits the search masks, one '+gam' each, after the reply says how many.
    const unsigned long sys_mask = number(field("SYSMASK", "0")), sys_want = number(field("SYSFLAGS", "0"));
    const unsigned long cust_mask = number(field("CUSTMASK", "0")), cust_want = number(field("CUSTFLAGS", "0"));
    const auto fits = [&](unsigned long sys, unsigned long cust) {
      return (sys & sys_mask) == (sys_want & sys_mask) && (cust & cust_mask) == (cust_want & cust_mask);
    };
    std::vector<Fields> found;
    for (const auto& [id, game] : games_) {
      if (fits(number(game.sysflags), number(game.custflags))) {
        found.push_back(GameRecord(game, connection));
      }
    }
    // The games other servers announced (a server not heard from lately is gone with its games).
    const auto now = std::chrono::steady_clock::now();
    for (const auto& [id, remote] : remote_games_) {
      if (now - remote.seen > kRemoteGameLifetime) {
        continue;
      }
      const auto field_of = [&](const char* key) {
        const auto it = remote.record.find(key);
        return it != remote.record.end() ? it->second : std::string();
      };
      const unsigned long players = number(field_of("COUNT")), room = number(field_of("MAXSIZE"));
      if (fits(number(field_of("SYSFLAGS")), number(field_of("CUSTFLAGS"))) && players < room) {
        Fields record = remote.record;
        record["SELF"] = connection.user;
        found.push_back(std::move(record));
      }
    }
    Fields reply;
    reply["COUNT"] = std::to_string(found.size());
    reply_with(reply);
    for (const Fields& record : found) {
      Log("lobby -> +gam " + Printable(FormatFields(record)));
      Send(connection, Encode("+gam", record));
    }
    return;
  }
  if (message.command == "mesg") {
    // A message to the other players of the sender's game (the host's start-of-race launch goes this way).
    reply_with(Fields{});
    auto it = games_.find(connection.game);
    if (it != games_.end()) {
      Fields push;
      // The client reads single-letter fields: N sender, T text, F flag letters ("0" = bit 27, a game message).
      push["N"] = connection.user;
      push["T"] = field("TEXT", "");
      push["F"] = field("ATTR", "0");  // the sender's flag letters go through as they are ("GN0" game message, the kick letters)
      const std::string private_to = field("PRIV", "");
      for (SocketHandle fd : it->second.players) {
        Connection* player = ConnectionFor(fd);
        if (player && player != &connection && (private_to.empty() || player->user == private_to)) {
          Log("lobby -> +msg to " + player->user + " " + Printable(FormatFields(push)));
          Send(*player, Encode("+msg", push));
        }
      }
    }
    return;
  }
  if (message.command == "sviw") {
    // The statistics view the next rankings are of (VIEW=PS2 before the
    // recent players' clips): its columns, read at 0x82410020 - N of them,
    // then NAMES, DESCS, TYPES, PARAMS and SYMS, a comma-separated value per
    // column. N=0 leaves the client allocating nothing, which it takes for a
    // failure (and waits on "Downloading" for good): one column, the downloads.
    reply_with(Fields{{"VIEW", field("VIEW", "")},
                      {"N", "1"},
                      {"NAMES", "DOWNLOADS"},
                      {"DESCS", "Downloads"},
                      {"TYPES", "0"},
                      {"PARAMS", "0"},
                      {"SYMS", "D"}});
    return;
  }
  if (message.command == "sdta") {
    // A persona's statistics in the view sviw chose (read at 0x82410460): SLOT
    // as asked (another is refused), STATS its columns' values, comma-separated
    // - the one column, the downloads of its clips.
    uint64_t downloads = 0;
    if (locker_) {
      for (const Locker::Clip& clip : locker_->Clips()) {
        if (clip.persona == field("PERS", "")) {
          downloads += clip.downloads;
        }
      }
    }
    reply_with(Fields{{"SLOT", field("SLOT", "0")}, {"STATS", std::to_string(downloads)}});
    return;
  }
  if (message.command == "snap") {
    // A rankings snapshot (LobbyRank): the reply heads it - CHAN and SEQN as
    // asked, RANGE the rows, DESC, CC columns, each CN/CD/CP/CW/CT (read at
    // 0x82406CC8) - and a +snp a row follows: R its rank, P its points, O, N
    // the persona, S its columns' values (0x82411F58). The top 20 clips and
    // the recent players' are the server's clips, the most fetched first.
    const std::vector<Locker::Clip> clips = locker_ ? locker_->Clips() : std::vector<Locker::Clip>{};
    const size_t start = std::min<size_t>(number(field("START", "0")), clips.size());
    const size_t rows = std::min<size_t>(clips.size() - start, 20);
    Fields reply;
    reply["CHAN"] = field("CHAN", "0");
    reply["SEQN"] = field("SEQN", "0");
    reply["RANGE"] = std::to_string(rows);
    reply["DESC"] = "Clips";
    reply["CC"] = "1";
    reply["CN0"] = "DOWNLOADS";
    reply["CD0"] = "Downloads";
    reply["CP0"] = "";
    reply["CW0"] = "8";
    reply["CT0"] = "0";
    reply_with(reply);
    for (size_t i = 0; i < rows; ++i) {
      const Locker::Clip& clip = clips[start + i];
      Fields row;
      row["R"] = std::to_string(start + i + 1);
      // Points of 0 the client drops (the top 20 came up empty with every
      // clip at 0 downloads): a shared clip ranks with 1 at least.
      row["P"] = std::to_string(std::max<uint64_t>(clip.downloads, 1));
      row["O"] = "0";
      row["N"] = clip.persona;
      // The column's value is the locker the client opens for the clip
      // (it asked dir pers=1 when this held the downloads): the owner.
      row["S"] = clip.persona;
      Log("lobby -> +snp " + Printable(FormatFields(row)) + " (clip " + clip.name + ")");
      Send(connection, Encode("+snp", row));
    }
    return;
  }
  // Not handled yet: an empty success, to see what comes next.
  Log("lobby: " + Printable(message.command) + " not handled yet; answered empty");
  Send(connection, Encode(message.command, std::string(1, '\0')));
}


Server::Connection* Server::ConnectionFor(SocketHandle fd) const {
  auto it = connections_.find(fd);
  return it != connections_.end() ? it->second.get() : nullptr;
}

Fields Server::GameRecord(const Game& game, const Connection& to) const {
  const auto address = [](const Connection& c) {
    return !c.address.empty() ? c.address : c.peer.substr(0, c.peer.find(':'));
  };
  Fields record;
  record["IDENT"] = std::to_string(game.id);
  record["NAME"] = game.name;
  record["HOST"] = game.host;
  record["SELF"] = to.user;
  record["PARAMS"] = game.params;
  record["PLATPARAMS"] = "";
  record["ROOM"] = game.room;
  record["CUSTFLAGS"] = game.custflags;
  record["SYSFLAGS"] = game.sysflags;
  record["PRIV"] = "0";
  record["MINSIZE"] = game.minsize;
  record["MAXSIZE"] = game.maxsize;
  record["SEED"] = std::to_string(game.seed);
  record["WHEN"] = "2006.2.10-0:00:00";
  record["AUTH"] = "";
  if (!game.session.empty()) {
    record["SESS"] = game.session;
  }
  uint32_t count = 0;
  for (SocketHandle fd : game.players) {
    const Connection* player = ConnectionFor(fd);
    if (!player) {
      continue;
    }
    const std::string n = std::to_string(count++);
    // The game finds a remote player in its manager by the id ConnApi gave it.
    record["OPID" + n] = std::to_string(PlayerId(player->user));
    record["OPPO" + n] = player->user;
    record["ADDR" + n] = address(*player);
    record["LADDR" + n] = address(*player);
    record["MADDR" + n] = player->maddr;
    record["OPPART" + n] = "0";
    record["OPPARAM" + n] = player->user_params;
    record["OPFLAG" + n] = "0";
    record["PRES" + n] = "0";
  }
  record["COUNT"] = std::to_string(count);
  // NUMPART is not the players: it counts the record's other list, read
  // into 40-byte slots from game record+448 (LobbyApi, 0x82411A58), and a third
  // slot lands on COUNT (+524). NUMPART=3 zeroed the count, the host no longer
  // found itself in its own game and ended it: everyone left as the third
  // player joined.
  record["NUMPART"] = "0";
  return record;
}

void Server::SendGameToPlayers(const Game& game) {
  for (SocketHandle fd : game.players) {
    if (Connection* player = ConnectionFor(fd)) {
      const Fields record = GameRecord(game, *player);
      Log("lobby -> +mgm to " + player->user + " " + Printable(FormatFields(record)));
      Send(*player, Encode("+mgm", record));
    }
  }
}

void Server::LeaveGame(Connection& connection) {
  auto it = games_.find(connection.game);
  connection.game = 0;
  if (it == games_.end()) {
    return;
  }
  Game& game = it->second;
  game.players.erase(std::remove(game.players.begin(), game.players.end(), connection.fd), game.players.end());
  if (game.players.empty()) {
    Log("lobby: game " + std::to_string(game.id) + " closed");
    games_.erase(it);
    return;
  }
  // The host gone, the next player hosts.
  if (game.host == connection.user) {
    if (const Connection* next = ConnectionFor(game.players.front())) {
      game.host = next->user;
    }
  }
  SendGameToPlayers(game);
}

void Server::SendWho(Connection& connection) {
  const std::string address =
      !connection.address.empty() ? connection.address
                                  : connection.peer.substr(0, connection.peer.find(':'));
  Fields who;
  who["I"] = std::to_string(connection.id);
  who["N"] = connection.user;
  who["M"] = connection.user;
  who["F"] = "0";
  who["A"] = address;
  who["LA"] = address;
  who["P"] = "0";
  who["S"] = "";
  // X: the XUID, "$" and hex, as the client sent it.
  who["X"] = connection.xuid;
  // MA: its Xbox Live address as it gave it in MADDR ("$" XUID "^" XNADDR). The
  // session code takes the console's own XUID from it (ConnApiOnline
  // 0x82587D00, self+0x1E0) and compares it with the host's MADDR to tell
  // that it hosts a game it created; empty, it never did, and left the game.
  who["MA"] = connection.maddr;
  // G: the game the user is in. Once in one, the title checks it every tick
  // (0x8236D2D8) and after 200 with G still 0 gives the game up -
  // "$NetworkingNoGame", "the game you were in no longer exists".
  who["G"] = std::to_string(connection.game);
  who["AT"] = "";
  who["CL"] = "0";
  who["LV"] = "0";
  who["MD"] = "0";
  who["HW"] = "0";
  who["RP"] = "0";
  Log("lobby -> +who " + Printable(FormatFields(who)));
  Send(connection, Encode("+who", who));
}

// ---- Seamless multiplayer -------------------------------------------------------------------------------
//
// Every server hosts the games of its own clients and announces them on the LAN. A client that joins a game
// of another server stays connected to its own: this server opens a link to the host's ('xhlo'), asks it to
// take the client as one of its players ('xjoi') and from then on carries what the client sends for the game
// ('xfwd') to it and what it answers back. The host sees the player as a kRemote connection.

bool Server::OpenBeacon() {
  const NativeSocket s = socket(AF_INET, SOCK_DGRAM, 0);
  if (s == kInvalidNativeSocket) {
    return false;
  }
  const int yes = 1;
  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));
#ifdef SO_REUSEPORT
  // Every copy on one machine listens on the same port.
  setsockopt(s, SOL_SOCKET, SO_REUSEPORT, reinterpret_cast<const char*>(&yes), sizeof(yes));
#endif
  setsockopt(s, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&yes), sizeof(yes));
  sockaddr_in at = {};
  at.sin_family = AF_INET;
  at.sin_port = htons(options_.beacon_port);
  at.sin_addr.s_addr = htonl(INADDR_ANY);
  if (bind(s, reinterpret_cast<sockaddr*>(&at), sizeof(at)) != 0) {
    CloseNative(s);
    return false;
  }
  ip_mreq group = {};
  group.imr_multiaddr.s_addr = htonl(kBeaconGroup);
  group.imr_interface.s_addr = htonl(INADDR_ANY);
  setsockopt(s, IPPROTO_IP, IP_ADD_MEMBERSHIP, reinterpret_cast<const char*>(&group), sizeof(group));
  const int loop = 1;
  setsockopt(s, IPPROTO_IP, IP_MULTICAST_LOOP, reinterpret_cast<const char*>(&loop), sizeof(loop));
  SetNonBlocking(s);
  beacon_ = FromNative(s);
  return true;
}

void Server::Announce() {
  const auto now = std::chrono::steady_clock::now();
  for (auto it = remote_games_.begin(); it != remote_games_.end();) {
    it = now - it->second.seen > kRemoteGameLifetime ? remote_games_.erase(it) : std::next(it);
  }
  if (beacon_ < 0) {
    return;
  }
  for (const auto& [id, game] : games_) {
    if (game.players.empty()) {
      continue;
    }
    // Only the games a client of this server hosts; one that has moved to a remote player is not announced.
    const Connection* host = ConnectionFor(game.players.front());
    if (!host || host->role != Role::kLobby) {
      continue;
    }
    Game shown = game;
    shown.players = {game.players.front()};
    Fields record = GameRecord(shown, *host);
    record["COUNT"] = std::to_string(game.players.size());
    record["NUMPART"] = "0";  // not the players (GameRecord)
    record.erase("SELF");
    const std::string datagram = std::string(kGameBeacon) + options_.self_address + " " +
                                 std::to_string(id) + " " + Hex(FormatFields(record));
    for (const uint32_t to_address : {htonl(INADDR_BROADCAST), htonl(kBeaconGroup)}) {
      sockaddr_in to = {};
      to.sin_family = AF_INET;
      to.sin_port = htons(options_.beacon_port);
      to.sin_addr.s_addr = to_address;
      sendto(ToNative(beacon_), datagram.data(), static_cast<int>(datagram.size()), 0,
             reinterpret_cast<sockaddr*>(&to), sizeof(to));
    }
  }
}

void Server::ReceiveBeacon() {
  for (;;) {
    char in[2048];
    sockaddr_in from = {};
    SockLen from_length = sizeof(from);
    const auto got = recvfrom(ToNative(beacon_), in, sizeof(in) - 1, 0,
                              reinterpret_cast<sockaddr*>(&from), &from_length);
    if (got <= 0) {
      return;
    }
    in[got] = '\0';
    const size_t prefix = sizeof(kGameBeacon) - 1;
    if (static_cast<size_t>(got) <= prefix || std::memcmp(in, kGameBeacon, prefix) != 0) {
      continue;
    }
    char server[64] = {};
    unsigned long id = 0;
    char blob[2000] = {};
    if (std::sscanf(in + prefix, "%63s %lu %1999s", server, &id, blob) != 3) {
      continue;
    }
    if (options_.self_address == server) {
      continue;
    }
    const std::vector<uint8_t> bytes = Unhex(blob);
    RemoteGame remote;
    remote.server = server;
    remote.record = ParseFields(std::string(bytes.begin(), bytes.end()));
    remote.seen = std::chrono::steady_clock::now();
    const bool fresh = remote_games_.find(static_cast<uint32_t>(id)) == remote_games_.end();
    remote_games_[static_cast<uint32_t>(id)] = std::move(remote);
    if (fresh) {
      Log("federation: game " + std::to_string(id) + " on the server " + server);
    }
  }
}

Server::Connection* Server::LinkTo(const std::string& server) {
  if (auto found = links_.find(server); found != links_.end()) {
    if (Connection* link = ConnectionFor(found->second); link && link->fd >= 0) {
      return link;
    }
    links_.erase(found);
  }
  const NativeSocket s = socket(AF_INET, SOCK_STREAM, 0);
  if (s == kInvalidNativeSocket) {
    return nullptr;
  }
  sockaddr_in to = {};
  to.sin_family = AF_INET;
  to.sin_port = htons(options_.lobby_port);
  if (inet_pton(AF_INET, server.c_str(), &to.sin_addr) != 1) {
    CloseNative(s);
    return nullptr;
  }
  SetNonBlocking(s);
  if (connect(s, reinterpret_cast<sockaddr*>(&to), sizeof(to)) != 0) {
    const int code = LastSocketError();
#ifdef _WIN32
    const bool in_progress = WouldBlock(code);
#else
    const bool in_progress = code == EINPROGRESS || WouldBlock(code);
#endif
    if (!in_progress) {
      CloseNative(s);
      return nullptr;
    }
    // A LAN connect is over in a moment; an unreachable server holds the lobby up for a second at most.
    std::vector<PollFd> wait{MakePollFd(FromNative(s), POLLOUT)};
    int error = 0;
    SockLen error_length = sizeof(error);
    if (PollWait(wait, 1000) <= 0 ||
        getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &error_length) != 0 ||
        error != 0) {
      CloseNative(s);
      return nullptr;
    }
  }
  const int yes = 1;
  setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&yes), sizeof(yes));
  auto link = std::make_unique<Connection>();
  link->fd = FromNative(s);
  link->role = Role::kPeer;
  link->node = server;
  link->peer = server + ":" + std::to_string(options_.lobby_port);
  link->local_address = options_.self_address;
  Connection& stored = *link;
  connections_[stored.fd] = std::move(link);
  links_[server] = stored.fd;
  Log("federation: link to the server " + server);
  Send(stored, Encode("xhlo", Fields{{"NODE", options_.self_address}}));
  return &stored;
}

Server::Connection* Server::RemotePlayer(SocketHandle link, uint32_t id) const {
  for (const auto& [fd, connection] : connections_) {
    if (connection->role == Role::kRemote && connection->link == link && connection->remote_id == id) {
      return connection.get();
    }
  }
  return nullptr;
}

void Server::ForwardToHost(Connection& client, const Message& message) {
  Connection* link = ConnectionFor(client.remote_link);
  if (!link) {
    DropRemote(client, false);
    return;
  }
  Fields forwarded;
  forwarded["U"] = std::to_string(client.id);
  forwarded["D"] = Hex(Encode(message.command, message.body, message.code));
  Send(*link, Encode("xfwd", forwarded));
}

void Server::DropRemote(Connection& client, bool tell_host) {
  if (tell_host) {
    if (Connection* link = ConnectionFor(client.remote_link)) {
      Send(*link, Encode("xlea", Fields{{"U", std::to_string(client.id)}}));
    }
  }
  client.remote_link = -1;
  client.remote_game = 0;
}

void Server::OnLinkClosed(SocketHandle link) {
  for (auto it = links_.begin(); it != links_.end();) {
    it = it->second == link ? links_.erase(it) : std::next(it);
  }
  // Players that came through this link are gone from the games hosted here.
  std::vector<SocketHandle> gone;
  for (const auto& [fd, connection] : connections_) {
    if (connection->role == Role::kRemote && connection->link == link) {
      gone.push_back(fd);
    }
  }
  for (SocketHandle fd : gone) {
    if (Connection* player = ConnectionFor(fd)) {
      player->link = -1;
      LeaveGame(*player);
    }
    connections_.erase(fd);
  }
  // Clients of this server whose game was hosted at the other end: the game is gone.
  for (const auto& [fd, connection] : connections_) {
    if (connection->role == Role::kLobby && connection->remote_link == link) {
      Log("federation: the server of " + connection->user + "'s game is gone");
      connection->remote_link = -1;
      connection->remote_game = 0;
      connection->game = 0;
      Send(*connection, Encode("+mgm", Fields{{"IDENT", "0"}, {"NAME", ""}, {"HOST", ""}, {"COUNT", "0"}}));
      SendWho(*connection);
    }
  }
}

void Server::HandlePeer(Connection& link, const Message& message) {
  const Fields fields = ParseFields(message.body);
  const auto field = [&](const char* key) {
    const auto it = fields.find(key);
    return it != fields.end() ? it->second : std::string();
  };
  if (message.command == "xhlo") {
    link.node = field("NODE");
    Log("federation: link from the server " + link.node);
    return;
  }
  const uint32_t id = static_cast<uint32_t>(std::strtoul(field("U").c_str(), nullptr, 10));
  const std::vector<uint8_t> bytes = Unhex(field("D"));

  // The host end: the client's own messages, run as if it were connected here.
  const auto run = [&](Connection& player) {
    std::vector<uint8_t> buffer = bytes;
    Message inner;
    while (TakeMessage(&buffer, &inner)) {
      Log("lobby <- (" + player.user + " via " + link.node + ") " + Printable(inner.command) + " " +
          Printable(inner.body));
      HandleLobby(player, inner);
      if (inner.command == "glea" || inner.command == "gdel") {
        const SocketHandle left = player.fd;
        connections_.erase(left);
        return;
      }
    }
  };
  if (message.command == "xjoi") {
    if (const Connection* old = RemotePlayer(link.fd, id)) {
      const SocketHandle old_fd = old->fd;
      LeaveGame(*connections_[old_fd]);
      connections_.erase(old_fd);
    }
    auto player = std::make_unique<Connection>();
    player->fd = next_virtual_--;
    player->role = Role::kRemote;
    player->link = link.fd;
    player->remote_id = id;
    player->id = id;
    player->user = field("NAME");
    player->address = field("ADDR");
    player->maddr = field("MADDR");
    player->xuid = field("XUID");
    player->peer = field("PEER");
    player->user_params = field("USERPARAMS");
    player->local_address = link.local_address;
    Connection& stored = *player;
    connections_[stored.fd] = std::move(player);
    Log("federation: " + stored.user + " of the server " + link.node + " joins");
    run(stored);
    return;
  }
  if (message.command == "xlea") {
    if (Connection* player = RemotePlayer(link.fd, id)) {
      const SocketHandle fd = player->fd;
      LeaveGame(*player);
      connections_.erase(fd);
    }
    return;
  }
  if (message.command == "xfwd") {
    if (Connection* player = RemotePlayer(link.fd, id)) {
      run(*player);
      return;
    }
    // The client's end: what the host answers goes to the client as it is.
    for (const auto& [fd, connection] : connections_) {
      if (connection->role == Role::kLobby && connection->id == id) {
        Send(*connection, bytes);
        return;
      }
    }
  }
}

}  // namespace ealobby
