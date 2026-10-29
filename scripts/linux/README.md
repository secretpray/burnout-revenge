# Burnout Revenge for Linux and Steam Deck

Download the **Linux x86-64** archive from
[Releases](https://github.com/secretpray/burnout-revenge/releases/latest).
You need your own supported **USA Xbox 360 disc image**; game data is not included.

## Requirements

- An x86-64 CPU with **AVX2/FMA** (x86-64-v3).
- **glibc 2.38+** and **GLIBCXX_3.4.32**.
- A **Vulkan 1.2+** driver with dynamic rendering and push descriptors.
- A graphical Linux session. SteamOS on Deck supplies the required system libraries.

Ubuntu 22.04's default glibc is too old. The app uses your system's GPU driver.

## Install and play

1. Extract the archive and open a terminal in its **BurnoutRevenge** folder.
2. Run:

   ```sh
   ./install.sh
   ```

3. Open **Burnout Revenge** from your applications menu. On first launch,
   choose your own USA Xbox 360 disc image or extracted game folder.

Run the installer as your normal user, without `sudo`. It checks the download
and required libraries, installs the app and adds its name and icon to the menu.
You can delete the extracted download afterwards.

## Steam and Steam Deck

1. In Desktop Steam, choose **Games > Add a Non-Steam Game** and select the
   installed **Burnout Revenge** entry.
2. Leave forced Proton compatibility **off** and use a **Gamepad** template.
3. On Deck, return to **Gaming Mode**. Use **1280×800 / 60 Hz**, including on OLED.

If the app is not listed, use **Browse > All Files** and select
`~/.local/bin/burnout-revenge` (`/home/deck/.local/bin/burnout-revenge` on Deck).
Set the shortcut name to **Burnout Revenge** and choose the package's
`burnout-revenge.png` as its icon. For a library cover, use
`artwork/burnout-revenge-cover.jpg` with **Manage > Set custom artwork**.

The installer creates an applications-menu entry; add it to Steam using the
steps above. Library artwork is a separate Steam setting.

## Update or remove

**Update:** quit the game, extract the new archive and run its `./install.sh`.
Your installed game, saves and settings stay.

**Remove the app:** run `~/.local/bin/burnout-revenge --uninstall`.
Your game, saves and settings stay.

## Data and optional launch methods

Saves and settings are in `~/.local/share/burnout-revenge`; caches are in
`~/.cache/burnout-revenge`. Custom XDG paths are supported. The installed launcher
is `~/.local/bin/burnout-revenge`.

To run from the extracted folder without installing the app, use
`./burnout-revenge.sh`. Add that file to Steam and keep the extracted folder.
The executable inside `lib/` needs the launcher to set up its libraries.

`./burnout-revenge.sh --portable on` keeps data beside the extracted app in a
writable folder. Back up that data before replacing a portable installation.

## Troubleshooting

- **The game will not start:** run `./burnout-revenge.sh --check-system` from
  the extracted folder. Install any missing system libraries or GPU drivers
  reported there.
- **A button triggers two actions:** choose Steam's **Gamepad** template and
  check for keyboard bindings in Steam Input or another controller mapper.
- **FPS or graphical issues:** enable **Settings > Advanced > Performance
  Overlay**, then use **Create bug report** on the same page. Include your
  Linux version, GPU/driver, controller and a short description or video.
  Do not attach disc images or game files.

For developers: [build instructions](https://github.com/secretpray/burnout-revenge/blob/main/docs/BUILD.md#linux) and
[technical test results](https://github.com/secretpray/burnout-revenge/blob/main/docs/linux-validation.md).
