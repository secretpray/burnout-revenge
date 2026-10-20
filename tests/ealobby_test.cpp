// Burnout Revenge port. GPL-3.0-or-later (see COPYING).
// Xerenge's lobby server (runtime/thirdparty/ealobby), as burnout-lobby runs
// it: the Aries codec, then a server on 127.0.0.1 taking a login the way the
// game's DirtySock makes it (@dir, then addr, skey, auth and pers on the
// lobby), a game made and found by two players, and no peer links from
// clients.
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

    // A client can't make itself a server's peer link where there is no
    // seamless multiplayer (ours, README): xhlo is ignored, and the
    // connection stays a player's.
    b.Send("xhlo", { { "NODE", "127.0.0.9" } });
    assert(b.Ask("skey", { { "SKEY", "$0" } }).at("SKEY").size() == 33);

    close(a.fd);
    close(b.fd);
    server.Stop();
    assert(lines > 0);
    printf("ealobby_test: ok\n");
    return 0;
}
