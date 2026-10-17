// Tests for config/switch_tables.toml against config/burnout.toml's leaf
// function bounds: the jump tables XenonRecomp turns into C++ switches.
// XenonAnalyse takes a table's index register, label count and default from
// the bounds check before it (a cmplwi and a bgt/ble within 32
// instructions). A table with no bounds check of its own got the compare of
// the code before it, across a return: in v0.1.2 sub_822E7CE0 (the
// Performance Tuning row's label, GitHub issue #8) switched on r3 over one
// label instead of r4 over seven, so every row read "Steering"; two more
// leaf functions (sub_822C7720, sub_822C89F8) had the same fault.
// survey/fix_switch_tables.py now drops such tables and
// survey/find_missing_tables.py decodes them from the dispatch. Checked
// here, with no game files:
//   - every table inside a leaf function the config bounds keeps its labels
//     and its default (0 = a conditional return) inside that function: a
//     borrowed default lies before the function's start;
//   - the three tables as the guest code dispatches them;
//   - the v0.1.2 entries, planted, fail the first check.
// From the repo root:
//   clang++ -std=c++20 -O2 tests/switch_tables_test.cpp -o build/switch_tables_test
//   build/switch_tables_test config/switch_tables.toml config/burnout.toml
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

static int g_failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { g_failures++; printf("  FAIL %s:%d: %s: ", __FILE__, __LINE__, #cond); printf(__VA_ARGS__); printf("\n"); } } while (0)

struct Table
{
    uint32_t base = 0;
    uint32_t r = 99;
    bool hasDefault = false;
    uint32_t defaultLabel = 0;
    std::vector<uint32_t> labels;
};

struct Function
{
    uint32_t address = 0;
    uint32_t size = 0;
    bool Contains(uint32_t a) const { return a >= address && a < address + size; }
};

static std::string ReadFile(const char* path)
{
    std::ifstream f(path, std::ios::binary);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}

// The subset of TOML the generators write: [[switch]] blocks of
// `base = 0x..`, `r = N`, `default = 0x..` and a `labels = [ 0x.., ]` list.
static std::vector<Table> ParseTables(const std::string& text)
{
    std::vector<Table> tables;
    std::istringstream in(text);
    std::string line;
    bool inLabels = false;
    std::smatch m;
    static const std::regex kKey(R"(^\s*(base|r|default)\s*=\s*(0x[0-9A-Fa-f]+|\d+)\s*$)");
    static const std::regex kLabel(R"(^\s*(0x[0-9A-Fa-f]+)\s*,?\s*$)");
    while (std::getline(in, line))
    {
        if (line.rfind("[[switch]]", 0) == 0)
        {
            tables.emplace_back();
            inLabels = false;
            continue;
        }
        if (tables.empty() || line.empty() || line[0] == '#')
            continue;
        Table& t = tables.back();
        if (inLabels)
        {
            if (line.find(']') != std::string::npos)
                inLabels = false;
            else if (std::regex_match(line, m, kLabel))
                t.labels.push_back(uint32_t(std::stoul(m[1].str(), nullptr, 16)));
            continue;
        }
        if (line.rfind("labels", 0) == 0)
        {
            inLabels = true;
            continue;
        }
        if (std::regex_match(line, m, kKey))
        {
            uint32_t v = uint32_t(std::stoul(m[2].str(), nullptr, 0));
            if (m[1] == "base")
                t.base = v;
            else if (m[1] == "r")
                t.r = v;
            else
            {
                t.hasDefault = true;
                t.defaultLabel = v;
            }
        }
    }
    return tables;
}

static std::vector<Function> ParseFunctions(const std::string& text)
{
    std::vector<Function> functions;
    static const std::regex kFunction(R"(\{\s*address\s*=\s*(0x[0-9A-Fa-f]+)\s*,\s*size\s*=\s*(0x[0-9A-Fa-f]+)\s*\})");
    for (std::sregex_iterator it(text.begin(), text.end(), kFunction), end; it != end; ++it)
        functions.push_back({ uint32_t(std::stoul((*it)[1].str(), nullptr, 16)), uint32_t(std::stoul((*it)[2].str(), nullptr, 16)) });
    return functions;
}

// Tables inside a bounded leaf function with a label or a default outside
// it, as "base: what" lines.
static std::vector<std::string> Escapes(const std::vector<Table>& tables, const std::vector<Function>& functions)
{
    std::vector<std::string> out;
    char buf[160];
    for (const Table& t : tables)
        for (const Function& f : functions)
        {
            if (!f.Contains(t.base))
                continue;
            if (t.hasDefault && t.defaultLabel != 0 && !f.Contains(t.defaultLabel))
            {
                snprintf(buf, sizeof(buf), "%08X: default %08X outside %08X+%X", t.base, t.defaultLabel, f.address, f.size);
                out.push_back(buf);
            }
            for (uint32_t label : t.labels)
                if (!f.Contains(label))
                {
                    snprintf(buf, sizeof(buf), "%08X: label %08X outside %08X+%X", t.base, label, f.address, f.size);
                    out.push_back(buf);
                }
        }
    return out;
}

// The table XenonRecomp uses at a bctr: the last [[switch]] keyed at or
// before it within the dispatch (XenonAnalyse keys the lis 20 bytes before
// the bctr, find_missing_tables.py the rlwinm 12 bytes before).
static const Table* TableAt(const std::vector<Table>& tables, uint32_t bctr)
{
    const Table* best = nullptr;
    for (const Table& t : tables)
        if (t.base <= bctr && bctr - t.base <= 0x20 && (!best || t.base > best->base))
            best = &t;
    return best;
}

static const Function* FunctionAt(const std::vector<Function>& functions, uint32_t a)
{
    for (const Function& f : functions)
        if (f.Contains(a))
            return &f;
    return nullptr;
}

// The guest's dispatch: index register, table entries, last instruction.
struct Dispatch
{
    const char* what;
    uint32_t function;
    uint32_t bctr;
    uint32_t index;
    std::vector<uint32_t> labels;
    uint32_t end;   // one past the last case's blr
};

static const Dispatch kDispatches[] = {
    // Performance Tuning: the row's label string hash by slider (r4, 0-6:
    // Steering ... Nitrous, then Turbo or Supercharger by r3).
    { "tuning row label", 0x822E7CE0, 0x822E7CF4, 4,
      { 0x822E7D14, 0x822E7D20, 0x822E7D2C, 0x822E7D38, 0x822E7D44, 0x822E7D50, 0x822E7D5C }, 0x822E7D80 },
    // A count and its value from one of ten fields of the record in r3
    // (x2000, x450, x250, ...), stored to *r4 and *r5.
    { "count and value by r6", 0x822C7720, 0x822C7734, 6,
      { 0x822C7760, 0x822C7774, 0x822C7788, 0x822C779C, 0x822C77B0,
        0x822C77C4, 0x822C77D8, 0x822C77EC, 0x822C7800, 0x822C7814 }, 0x822C7828 },
    // 0-6 -> 0-6 (the identity, written as a table).
    { "enum by r3", 0x822C89F8, 0x822C8A0C, 3,
      { 0x822C8A2C, 0x822C8A34, 0x822C8A5C, 0x822C8A3C, 0x822C8A44, 0x822C8A4C, 0x822C8A54 }, 0x822C8A64 },
};

// v0.1.2's entries for those dispatches (XenonAnalyse's output) and their
// function bounds.
static const char kReleased[] = R"(
[[switch]]
base = 0x822C7720
r = 11
default = 0x822C76E4
labels = [
    0x822C7760,
    0x822C7774,
]

[[switch]]
base = 0x822C89F8
r = 10
default = 0x822C89D0
labels = [
    0x822C8A2C,
    0x822C8A34,
]

[[switch]]
base = 0x822E7CE0
r = 3
default = 0x822E7BC4
labels = [
    0x822E7D14,
]
)";
static const char kReleasedFunctions[] = R"(
    { address = 0x822C7720, size = 0x68 },
    { address = 0x822C89F8, size = 0x44 },
    { address = 0x822E7CE0, size = 0x40 },
)";

static bool Matches(const Table* t, const Function* f, const Dispatch& d)
{
    return t && f && t->r == d.index && t->labels == d.labels && f->address == d.function && f->address + f->size >= d.end;
}

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        fprintf(stderr, "usage: switch_tables_test <config/switch_tables.toml> <config/burnout.toml>\n");
        return 2;
    }
    std::vector<Table> tables = ParseTables(ReadFile(argv[1]));
    std::vector<Function> functions = ParseFunctions(ReadFile(argv[2]));
    printf("switch_tables_test: %zu tables, %zu bounded leaf functions\n", tables.size(), functions.size());
    CHECK(tables.size() > 500, "%zu tables: the file wasn't read", tables.size());
    CHECK(functions.size() > 100, "%zu functions: the file wasn't read", functions.size());
    for (const Table& t : tables)
        CHECK(t.base != 0 && t.r < 32 && !t.labels.empty(), "an entry at %08X didn't parse (r %u, %zu labels)", t.base, t.r, t.labels.size());

    for (const std::string& e : Escapes(tables, functions))
        CHECK(false, "%s", e.c_str());

    for (const Dispatch& d : kDispatches)
    {
        const Table* t = TableAt(tables, d.bctr);
        const Function* f = FunctionAt(functions, d.bctr);
        CHECK(t != nullptr, "%s: no table for the bctr at %08X", d.what, d.bctr);
        CHECK(f != nullptr, "%s: no bounded function holds %08X", d.what, d.bctr);
        if (!t || !f)
            continue;
        CHECK(t->r == d.index, "%s: switches on r%u, the guest indexes with r%u", d.what, t->r, d.index);
        CHECK(t->labels == d.labels, "%s: %zu labels, the guest's table has %zu", d.what, t->labels.size(), d.labels.size());
        CHECK(!t->hasDefault, "%s: a default label %08X, the guest has no bounds check", d.what, t->defaultLabel);
        CHECK(f->address == d.function && f->address + f->size >= d.end,
            "%s: function %08X+%X, the cases run %08X..%08X", d.what, f->address, f->size, d.function, d.end);
    }

    // Planted: the released entries must fail both checks.
    std::vector<Table> released = ParseTables(kReleased);
    std::vector<Function> releasedFunctions = ParseFunctions(kReleasedFunctions);
    std::vector<std::string> escapes = Escapes(released, releasedFunctions);
    CHECK(escapes.size() == 3, "the v0.1.2 entries escape their functions %zu times, expected 3 (one default each)", escapes.size());
    for (const Dispatch& d : kDispatches)
        CHECK(!Matches(TableAt(released, d.bctr), FunctionAt(releasedFunctions, d.bctr), d),
            "%s: the v0.1.2 entry passes as the guest's dispatch", d.what);

    if (g_failures)
    {
        printf("switch_tables_test: %d FAILED\n", g_failures);
        return 1;
    }
    printf("switch_tables_test: all passed\n");
    return 0;
}
