#!/usr/bin/env bash
# Burnout Revenge. GPL-3.0-or-later (see COPYING).
set -euo pipefail

fail() { printf 'Burnout Revenge: %s\n' "$*" >&2; exit 1; }
[[ $(uname -s) == Linux ]] || fail 'This installer is for Linux.'
[[ ${EUID:-$(id -u)} != 0 ]] || fail 'Run this as your normal user, without sudo.'
[[ ${HOME:-} == /* ]] || fail 'HOME must be an absolute path.'
case ${1:-} in
    --help|-h)
        printf 'Usage: ./install.sh [--uninstall]\nInstalls for your user and adds an applications-menu entry. No sudo.\nUninstall removes only the app; game files, saves and settings stay.\n'
        exit 0 ;;
    ''|--uninstall) ;;
    *) fail 'Unknown option. Use --help.' ;;
esac
[[ $# -le 1 ]] || fail 'Use --help.'
SOURCE=$(cd "$(dirname "$(readlink -f "$0")")" && pwd)
DATA_HOME=${XDG_DATA_HOME:-$HOME/.local/share}
[[ $DATA_HOME == /* ]] || DATA_HOME=$HOME/.local/share
APP=$DATA_HOME/burnout-revenge/runtime
BIN=$HOME/.local/bin/burnout-revenge
DESKTOP=$DATA_HOME/applications/burnout-revenge.desktop
MARKER='Burnout Revenge user installation v1'
for value in "$APP" "$BIN" "$DESKTOP"; do
    [[ $value != *[[:cntrl:]]* && $value != *=* ]] || fail 'Installation paths cannot contain control characters or =.'
done
[[ ! -L $APP ]] || fail "Refusing a symlink as the app directory: $APP"
if [[ -e $APP ]]; then
    [[ -f $APP/.managed-by-burnout-revenge ]] &&
        [[ $(cat "$APP/.managed-by-burnout-revenge") == "$MARKER" ]] || fail "Not a managed app directory: $APP"
fi
if [[ -e $BIN || -L $BIN ]]; then
    [[ ! -L $BIN ]] && grep -qxF '# Burnout Revenge user launcher.' "$BIN" || fail "Another launcher already exists: $BIN"
fi
if [[ -e $DESKTOP || -L $DESKTOP ]]; then
    [[ ! -L $DESKTOP ]] && grep -qxF 'X-BurnoutRevenge-Managed=true' "$DESKTOP" || fail "Another desktop entry already exists: $DESKTOP"
fi

if [[ ${1:-} == --uninstall ]]; then
    [[ -d $APP ]] || { printf 'Burnout Revenge is not installed for this user.\n'; exit 0; }
    mkdir "$APP/.install-lock" 2>/dev/null || fail 'Another install is in progress. Retry when it finishes.'
    rm -f -- "$BIN" "$DESKTOP"
    # APP is the marked runtime subdirectory, never the game's data directory.
    rm -rf -- "$APP"
    command -v update-desktop-database >/dev/null && update-desktop-database "$(dirname "$DESKTOP")" >/dev/null 2>&1 || true
    printf 'App removed. Your game, saves and settings are still in %s/burnout-revenge.\n' "$DATA_HOME"
    exit 0
fi

[[ ! -e $SOURCE/portable.txt ]] || fail 'This is a portable setup. Launch it directly; disable portable mode before installing.'
[[ -f $SOURCE/SHA256SUMS && -f $SOURCE/bundle-info.json ]] || fail 'Run install.sh from the complete extracted Linux archive.'
(cd "$SOURCE" && sha256sum --check --strict SHA256SUMS >/dev/null) || fail 'Package checksum failed. Extract a fresh download.'
"$SOURCE/burnout-revenge.sh" --check-system
VERSION=$(sed -n 's/^[[:space:]]*"version": "\([^"]*\)".*/\1/p' "$SOURCE/bundle-info.json")
REVISION=$(sed -n 's/^[[:space:]]*"revision": "\([^"]*\)".*/\1/p' "$SOURCE/bundle-info.json")
[[ $VERSION =~ ^[0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z.-]+)?$ && $REVISION =~ ^[a-f0-9]{7,40}$ ]] || fail 'Invalid package identity.'
ID=$VERSION-$REVISION
mkdir -p -- "$APP/releases" "$(dirname "$BIN")" "$(dirname "$DESKTOP")"
printf '%s\n' "$MARKER" > "$APP/.managed-by-burnout-revenge"
mkdir "$APP/.install-lock" 2>/dev/null || fail 'Another install is in progress. Retry when it finishes.'
STAGE= BIN_TEMP= DESKTOP_TEMP= LINK_TEMP=
cleanup() {
    [[ -z $STAGE ]] || rm -rf -- "$STAGE"
    [[ -z $BIN_TEMP ]] || rm -f -- "$BIN_TEMP"
    [[ -z $DESKTOP_TEMP ]] || rm -f -- "$DESKTOP_TEMP"
    [[ -z $LINK_TEMP ]] || rm -f -- "$LINK_TEMP"
    rmdir "$APP/.install-lock" 2>/dev/null || true
}
trap cleanup EXIT
DEST=$APP/releases/$ID
if [[ -e $DEST ]]; then
    [[ ! -L $DEST ]] && cmp -s "$SOURCE/SHA256SUMS" "$DEST/SHA256SUMS" || fail 'An installation with this identity has different contents.'
    (cd "$DEST" && sha256sum --check --strict SHA256SUMS >/dev/null) || fail 'The installed copy is damaged. Remove the app and reinstall; user data will stay.'
else
    STAGE=$(mktemp -d "$APP/releases/.install.XXXXXX")
    # Only package files: never copy portable saves, caches or game assets.
    for item in lib licenses burnout-revenge.sh burnout-revenge.png README.md ARCHITECTURE MIN_GLIBC SHA256SUMS bundle-info.json install.sh uninstall.sh; do
        cp -a -- "$SOURCE/$item" "$STAGE/"
    done
    # Keep legacy packages' checksummed file when present. The applications-menu
    # entry below is always generated for the installed path, never copied from it.
    [[ ! -f $SOURCE/burnout-revenge.desktop ]] || cp -- "$SOURCE/burnout-revenge.desktop" "$STAGE/"
    [[ ! -f $SOURCE/VALIDATION.json ]] || cp -- "$SOURCE/VALIDATION.json" "$STAGE/"
    [[ ! -d $SOURCE/artwork ]] || cp -a -- "$SOURCE/artwork" "$STAGE/"
    (cd "$STAGE" && sha256sum --check --strict SHA256SUMS >/dev/null) || fail 'The copied package did not verify.'
    mv -- "$STAGE" "$DEST"
    STAGE=
fi

# A versioned tree and an atomic link keep an update from overwriting a running app.
LINK_TEMP=$APP/.current.$$
ln -s -- "releases/$ID" "$LINK_TEMP"
mv -Tf -- "$LINK_TEMP" "$APP/current"
LINK_TEMP=
BIN_TEMP=$(mktemp "$(dirname "$BIN")/.burnout-revenge.XXXXXX")
{
    printf '#!/usr/bin/env bash\n# Burnout Revenge user launcher.\n'
    printf 'APP=%q\n' "$APP/current"
    printf 'if [[ ${1:-} == --uninstall ]]; then exec "$APP/install.sh" "$@"; fi\n'
    printf 'exec "$APP/burnout-revenge.sh" "$@"\n'
} > "$BIN_TEMP"
chmod 755 "$BIN_TEMP"
mv -Tf -- "$BIN_TEMP" "$BIN"
BIN_TEMP=

# Exec has both desktop-value escaping and argument quoting; it is not a shell.
desktop_value() { local text=$1; printf '%s' "${text//\\/\\\\}"; }
desktop_exec() {
    local text=$1
    text=${text//\\/\\\\}
    text=${text//\"/\\\"}
    text=${text//\`/\\\`}
    text=${text//\$/\\\$}
    text=${text//%/%%}
    printf '"%s"' "$(desktop_value "$text")"
}
DESKTOP_TEMP=$(mktemp "$(dirname "$DESKTOP")/.burnout-revenge.XXXXXX")
{
    printf '[Desktop Entry]\nType=Application\nName=Burnout Revenge\nComment=Native Linux and Steam Deck port\n'
    # GIO checks argv[0] before expanding %% to %. Keep that first executable
    # fixed so a literal percent in the user's home is handled as an argument.
    printf 'Exec=/usr/bin/env -- %s\n' "$(desktop_exec "$BIN")"
    printf 'TryExec=%s\n' "$(desktop_value "$BIN")"
    printf 'Icon=%s\n' "$(desktop_value "$APP/current/burnout-revenge.png")"
    printf 'Terminal=false\nCategories=Game;\nX-BurnoutRevenge-Managed=true\n'
} > "$DESKTOP_TEMP"
chmod 644 "$DESKTOP_TEMP"
mv -Tf -- "$DESKTOP_TEMP" "$DESKTOP"
DESKTOP_TEMP=
command -v update-desktop-database >/dev/null && update-desktop-database "$(dirname "$DESKTOP")" >/dev/null 2>&1 || true
printf 'Installed. Open Burnout Revenge from your applications menu.\nOn first launch, select your own supported disc image.\nTerminal: %q\n' "$BIN"
printf 'Updates: extract the new archive and run its ./install.sh. Saves stay.\n'
