# Playing online

Burnout Revenge's Xbox Live play needs a lobby server in place of EA's,
which were shut down in 2012. The port speaks the game's own protocol to
the lobby server from [Xerenge](https://github.com/shipa-2/Xerenge) by
shipa-2 (GPL-3.0-or-later), so Mac players meet Xerenge's players on the
same server. Races (Player Match), Road Rage, Crash Party, Crash Tour,
Crash Battle and Burnout Clips work, with up to four players tested.

## Turning it on

Settings (F1, or Back + Start) > **Online**:

- **Online Play**: On. Applies after restarting the game.
- **Server**:
  - **Xerenge Community**: `94639.snk.wtf`, the server Xerenge's players
    use, run by its author, who agreed to it being offered here.
  - **My Server**: the address in **Server Address** (below).
  - **LAN, No Server**: every Mac on the local network runs a small lobby
    of its own and finds the others' games itself. No Burnout Clips there.
- **Gamertag**: your name online, up to 15 letters, digits, `-` and `_`.
  Every player needs a different one. Left empty, it is `Player` and four
  digits made from your Mac's account name (the name itself isn't sent).
- **Server Address**: your own server's host name or IPv4 address.
- **Check Server**: asks the chosen server whether it is up, as signing in
  does, and says whether it has Burnout Clips.

Then choose **Xbox Live** in the game's main menu.

After a failed sign-in the game doesn't ask the server again for a while
(5 seconds, then 10, 20, 40, at most 60): a server that is down isn't
pressed with retries. Signing in resets it.

## Ranked

Choose **Quick Match > Ranked** to find a game, or **Create Match** with
**Ranked: Yes** to host one. On the bundled server and LAN, if no matching
room is open, the game offers to create one. Ranked searches and Player
Match searches stay separate there. Other servers choose their own matches.

Ranked here is a way to play the game's Ranked matches. There is **no global
ranking, TrueSkill matchmaking or saved leaderboard**: World Rank remains
unavailable, and Scoreboards has no categories. Results within a match
still belong to that match. The lobby protocol stays compatible with
Xerenge; no new ranking service is required.

## Your own server

The app has the server inside it:

```bash
"/Applications/Burnout Revenge.app/Contents/MacOS/burnout-lobby" --host 0.0.0.0 --locker-dir ~/BurnoutLocker
```

`--advertise <address>`: the lobby's address its directory gives players
(by default the address they reached the directory at). Burnout Clips
players share are kept in the `--locker-dir` folder.

The server can't tell who a player is: anyone who reaches it can sign in
under any gamertag. Run one for people you know.

## Ports

| Port | | What |
|---|---|---|
| TCP 31860 | server | directory: where the lobby is |
| TCP 31861 | server | the lobby |
| TCP 31862 | server | Burnout Clips (EA's Locker, over HTTP) |
| UDP 31859, TCP 31861 | LAN | the LAN lobbies find each other's games and link up |
| UDP 31000, 8192, 6000 | every player | the game itself, player to player (31000 is the game's port 1000: macOS keeps ports below 1024 from games) |

The game goes player to player: everyone must be able to reach everyone
else directly. On one network that just works. Over the internet each
player needs those UDP ports forwarded to their Mac, or all of you on one
VPN (Tailscale, ZeroTier and the like give each Mac an address the others
reach). There is no NAT traversal.

## macOS firewall

With the firewall on (System Settings > Network > Firewall), macOS asks
whether Burnout Revenge may accept incoming connections the first time it
goes online: allow it, or the other players can't reach you. The app is
signed ad hoc, so macOS may ask again after an update.

## What doesn't work

- Global rankings, TrueSkill matchmaking and Scoreboards (see Ranked above).
- **Revenge Rivals**, the news ticker and voice chat.
- The Xbox Live friends list, messages, player reviews and gamer cards.
  Opening their buttons leaves the game running; no Xbox Live screen opens.
  Gamer pictures are unavailable too.
- Two players online from one Mac (split screen online).

## For testing

Environment variables override the settings for one run:
`BURNOUT_ONLINE=1`, `BURNOUT_GAMERTAG=<name>`,
`BURNOUT_LOBBY_SERVER=<host>|lan`, `BURNOUT_ONLINE_ADDRESS=<IPv4>` (this
Mac's address for the others, and the one its sockets use: `127.0.0.2`
and `127.0.0.3` let two copies play on one Mac after
`sudo ifconfig lo0 alias 127.0.0.2 up`), `BURNOUT_LOG_NET=1` (every
network call in the log).
