// Burnout Revenge port. GPL-3.0-or-later (see COPYING).
// Xerenge's lobby server (runtime/thirdparty/ealobby), as burnout-lobby runs
// it: the Aries codec, then a server on 127.0.0.1 taking a login the way the
// game's DirtySock makes it (@dir, then addr, skey, auth and pers on the
// lobby), a game made and found by two players, and no peer links from
// clients; then seamless multiplayer (the game's own lobby on a LAN): peers
// and announced games only from LAN addresses.
#include <ealobby/aries.h>
#include <ealobby/server.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cassert>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

using namespace ealobby;

namespace
{
    int Connect(uint16_t port)
    {
        const int fd = socket(AF_INET, SOCK_STREAM, 0);
        assert(fd >= 0);
        sockaddr_in to{};
        to.sin_family = AF_INET;
        to.sin_port = htons(port);
        to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        const bool connected = connect(fd, reinterpret_cast<sockaddr*>(&to), sizeof(to)) == 0;
        assert(connected);
        return fd;
    }

    struct Client
    {
        int fd;
        std::vector<uint8_t> in;

        void Send(const std::string& command, const Fields& fields)
        {
            const std::vector<uint8_t> bytes = Encode(command, fields);
            const bool sent = write(fd, bytes.data(), bytes.size()) == ssize_t(bytes.size());
            assert(sent);
        }

        // The next message with this command (others, such as pings, skipped).
        Message Wait(const std::string& command)
        {
            const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            for (;;)
            {
                Message message;
                while (TakeMessage(&in, &message))
                    if (message.command == command)
                        return message;
                const bool timedOut = std::chrono::steady_clock::now() > until;
                assert(!timedOut);
                pollfd p{ fd, POLLIN, 0 };
                if (poll(&p, 1, 100) > 0)
                {
                    uint8_t buffer[4096];
                    const ssize_t got = read(fd, buffer, sizeof(buffer));
                    assert(got > 0);
                    in.insert(in.end(), buffer, buffer + got);
                }
            }
        }

        Fields Ask(const std::string& command, const Fields& fields)
        {
            Send(command, fields);
            return ParseFields(Wait(command).body);
        }
    };

    // A player through the directory into the lobby, logged in as `name`.
    Client LogIn(const Options& options, const std::string& name, const std::string& address)
    {
        Client directory{ Connect(options.directory_port) };
        const Fields where = directory.Ask("@dir", { { "PROD", "BURNOUT-XBL2-2006" }, { "VERS", "XBL2/1.0" } });
        assert(where.at("ADDR") == "127.0.0.1");
        assert(where.at("PORT") == std::to_string(options.lobby_port));
        close(directory.fd);

        Client lobby{ Connect(uint16_t(std::stoi(where.at("PORT")))) };
        lobby.Ask("addr", { { "ADDR", address }, { "PORT", "3658" } });
        assert(lobby.Ask("skey", { { "SKEY", "$0" } }).at("SKEY").size() == 33);
        const Fields auth = lobby.Ask("auth", { { "GTAG", name }, { "XUID", "$b13ebabe80000001" }, { "PASS", "x" } });
        assert(auth.at("NAME") == name && auth.at("PERSONAS") == name);
        assert(auth.count("PASS") == 0 && !auth.at("LKEY").empty());
        assert(auth.at("ADDR") == address);
        assert(lobby.Ask("pers", { { "PERS", name } }).at("PERS") == name);
        // '+who', the player's own record: without it the login never ends.
        const Fields who = ParseFields(lobby.Wait("+who").body);
        assert(who.at("N") == name && who.at("A") == address && who.at("G") == "0");
        assert(std::stoi(who.at("I")) > 0);
        return lobby;
    }
}

int main()
{
    // The codec: a 12-byte header (command, code, total length, big-endian)
    // and KEY=VALUE lines ending in a NUL.
    const std::vector<uint8_t> bytes = Encode("auth", Fields{ { "NAME", "MacA" }, { "XUID", "$1" } });
    assert(bytes.size() == 12 + 18 + 1);
    assert(std::string(bytes.begin(), bytes.begin() + 4) == "auth");
    assert(bytes[4] == 0 && bytes[7] == 0 && bytes[8] == 0 && bytes[11] == bytes.size());
    std::vector<uint8_t> buffer(bytes.begin(), bytes.end() - 1);
    Message message;
    assert(!TakeMessage(&buffer, &message) && buffer.size() == bytes.size() - 1);  // incomplete: left alone
    buffer.push_back(bytes.back());
    buffer.insert(buffer.end(), bytes.begin(), bytes.end());
    assert(TakeMessage(&buffer, &message) && buffer.size() == bytes.size());
    assert(message.command == "auth" && message.code == std::string(4, '\0'));
    const Fields fields = ParseFields(message.body);
    assert(fields.size() == 2 && fields.at("NAME") == "MacA" && fields.at("XUID") == "$1");
    assert(ParseFields("A=1\r\nB=x=y\nnoequals\n").at("B") == "x=y");
    assert(Encode("news", std::string(1, '\0'), "new7")[7] == '7');
    // The player's id: the gamertag's FNV-1a, top bit cleared (user/online_identity.h agrees).
    assert(PlayerId("BRMacProbeA") == 2062879033u);

    // A server of its own on 127.0.0.1, its ports out of the way of a real one.
    Options options;
    options.host = "127.0.0.1";
    options.directory_port = uint16_t(42000 + getpid() % 1000 * 2);
    options.lobby_port = uint16_t(options.directory_port + 1);
    int lines = 0;
    Server server(options, [&](const std::string&) { lines++; });
    const bool started = server.Start();
    assert(started);

    Client a = LogIn(options, "MacA", "127.0.0.2");
    Client b = LogIn(options, "MacB", "127.0.0.3");

    // A game: MacA makes it and is sent it as its own ('+mgm', hosted by it);
    // MacB finds it.
    a.Send("gcre", { { "NAME", "MacA" }, { "MAXSIZE", "8" }, { "MINSIZE", "2" }, { "CUSTFLAGS", "0" },
                       { "SYSFLAGS", "0" }, { "PARAMS", "" } });
    a.Wait("gcre");
    const Fields mine = ParseFields(a.Wait("+mgm").body);
    assert(mine.at("HOST") == "MacA" && mine.at("OPPO0") == "MacA");
    b.Send("gsea", { { "START", "0" }, { "COUNT", "10" } });
    const Fields found = ParseFields(b.Wait("+gam").body);
    assert(found.at("IDENT") == mine.at("IDENT") && found.at("HOST") == "MacA");

    // MacB joins; it may set its own USERPARAMS but not change the game or
    // kick its host.
    b.Send("gjoi", { { "IDENT", mine.at("IDENT") }, { "USERPARAMS", "1" } });
    b.Wait("gjoi");
    const Fields set = b.Ask("gset", { { "MAXSIZE", "2" }, { "KICK", "MacA" }, { "USERPARAMS", "7" } });
    assert(set.at("MAXSIZE") == "8" && set.at("COUNT") == "2" && set.at("OPPARAM1") == "7");

    // Ranked and Player Match must remain separate even when both have
    // room; a locked or full ranked game must not fall back to a player game.
    Client c = LogIn(options, "MacC", "127.0.0.4");
    const Fields ranked = c.Ask("gcre", {{"SYSFLAGS", "262464"}, {"MAXSIZE", "4"}});
    const Fields rankedJoin = b.Ask("gqwk", {{"GS0", "50"}, {"GS1", "1"}});
    assert(rankedJoin.at("IDENT") == ranked.at("IDENT") && rankedJoin.at("COUNT") == "2");
    const Fields playerJoin = b.Ask("gqwk", {{"GS0", "50"}, {"GS1", "0"}});
    assert(playerJoin.at("IDENT") == mine.at("IDENT"));
    c.Ask("gset", {{"SYSFLAGS", "266560"}});  // ranked + locked
    b.Send("gqwk", {{"GS1", "1"}});
    assert(b.Wait("gqwk").code == "nfnd");
    c.Ask("gset", {{"SYSFLAGS", "262464"}, {"MAXSIZE", "1"}});
    b.Send("gqwk", {{"GS1", "1"}});
    assert(b.Wait("gqwk").code == "nfnd");
    close(c.fd);

    // A client can't make itself a server's peer link where there is no
    // seamless multiplayer (ours, README): xhlo is ignored, and the
    // connection stays a player's.
    b.Send("xhlo", { { "NODE", "127.0.0.9" } });
    assert(b.Ask("skey", { { "SKEY", "$0" } }).at("SKEY").size() == 33);

    close(a.fd);
    close(b.fd);
    server.Stop();
    assert(lines > 0);

    // Seamless multiplayer, as a game's own lobby runs it (user/online.cpp).
    Options lan = options;
    lan.self_address = "127.0.0.1";
    lan.beacon_port = uint16_t(options.directory_port + 2);
    std::mutex logMutex;
    std::vector<std::string> log;
    Server own(lan, [&](const std::string& line) {
        std::lock_guard lock(logMutex);
        log.push_back(line);
    });
    const bool ownStarted = own.Start();
    assert(ownStarted);
    const auto logged = [&](const std::string& text) {
        std::lock_guard lock(logMutex);
        for (const std::string& line : log)
            if (line.find(text) != std::string::npos)
                return true;
        return false;
    };

    // A beacon names the server hosting the game; one off the LAN is ignored
    // (it would send this lobby's players there), one on it is listed.
    const int udp = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in beacon{};
    beacon.sin_family = AF_INET;
    beacon.sin_port = htons(lan.beacon_port);
    beacon.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    const auto announce = [&](const std::string& server, int id) {
        const std::string record = "IDENT=" + std::to_string(id) + "\nNAME=Far\nHOST=Far\nCOUNT=1\nMAXSIZE=8\n";
        std::string hex;
        for (unsigned char c : record)
        {
            char pair[3];
            snprintf(pair, sizeof(pair), "%02X", c);
            hex += pair;
        }
        const std::string datagram = "XERENGE-GAME 1 " + server + " " + std::to_string(id) + " " + hex;
        sendto(udp, datagram.data(), datagram.size(), 0, reinterpret_cast<sockaddr*>(&beacon), sizeof(beacon));
    };
    announce("8.8.8.8", 501);
    announce("127.0.0.5", 502);
    usleep(300 * 1000);
    close(udp);
    assert(!logged("game 501") && logged("game 502 on the server 127.0.0.5"));

    // A peer link only from a LAN server naming itself.
    Client far{ Connect(lan.lobby_port) };
    far.Send("xhlo", { { "NODE", "8.8.8.8" } });
    pollfd p{ far.fd, POLLIN, 0 };
    uint8_t byte;
    const bool closed = poll(&p, 1, 2000) > 0 && read(far.fd, &byte, 1) == 0;
    assert(closed && logged("as 8.8.8.8 refused"));
    close(far.fd);
    Client near{ Connect(lan.lobby_port) };
    near.Send("xhlo", { { "NODE", "127.0.0.3" } });
    usleep(300 * 1000);
    assert(logged("link from the server 127.0.0.3"));
    close(near.fd);
    own.Stop();
    printf("ealobby_test: ok\n");
    return 0;
}
