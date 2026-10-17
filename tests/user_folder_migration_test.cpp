// User data migrations preserve all content, never merge profiles, and
// leave old absolute game-location paths usable by older builds.
#include <user/folder_migration.h>
#include <cassert>
#include <fstream>
#include <unistd.h>

int main()
{
    namespace fs = std::filesystem;
    char pattern[] = "/tmp/sb-folder-migration-XXXXXX";
    char* scratch = mkdtemp(pattern);
    assert(scratch);
    fs::path root = scratch;
    const char* active = "BurnoutRevenge";
    auto migrate = [&](const char* base) {
        return userpaths::MigrateFolder(root / base, "BurnoutRevenge", "BurnoutRevenge", active);
    };
    assert(migrate("absent").empty());
    fs::path old = root / "move/BurnoutRevenge";
    fs::path current = root / "move/BurnoutRevenge";
    fs::create_directories(old / "save");
    std::ofstream(old / "save/player") << "existing player";
    std::ofstream(old / "settings.toml") << "settings";
    fs::create_directories(old / "game");
    assert(!migrate("move").empty());
    assert(fs::is_symlink(old));
    assert(fs::equivalent(old / "save/player", current / "save/player"));
    assert(fs::file_size(current / "save/player") == 15);
    assert(fs::file_size(current / "settings.toml") == 8);
    assert(fs::is_directory(current / "game"));
    assert(migrate("move").empty()); // repeated startup is idempotent
    fs::create_directories(root / "both/BurnoutRevenge");
    fs::create_directories(root / "both/BurnoutRevenge");
    std::ofstream(root / "both/BurnoutRevenge/old") << "old";
    std::ofstream(root / "both/BurnoutRevenge/new") << "new";
    assert(!migrate("both").empty());
    assert(!fs::is_symlink(root / "both/BurnoutRevenge"));
    assert(!fs::exists(root / "both/BurnoutRevenge/old"));
    assert(fs::exists(root / "both/BurnoutRevenge/old"));
    assert(fs::exists(root / "both/BurnoutRevenge/new"));
    fs::create_directories(root / "first/BurnoutRevenge/save");
    std::ofstream(root / "first/BurnoutRevenge/save/player") << "first release";
    userpaths::MigrateFolder(root / "first", "BurnoutRevenge", "BurnoutRevenge", active, false);
    assert(!fs::exists(root / "first/BurnoutRevenge"));
    assert(fs::file_size(root / "first/BurnoutRevenge/save/player") == 13);
    fs::remove_all(root);
}
