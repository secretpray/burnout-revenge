// Burnout switch bounds and pinned dispatch regression. GPL-3.0-or-later.
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

    // Pin an independently recorded Burnout dispatch, then prove a label
    // escaping its real function is rejected by the same bounds validator.
    const Table* first = TableAt(tables, 0x82090330);
    CHECK(first && first->r == 10 && first->labels == std::vector<uint32_t>({
        0x82090344, 0x8209034C, 0x82090354, 0x8209035C}), "Burnout dispatch 82090330");
    const Function* f = FunctionAt(functions, 0x82090330);
    CHECK(f && f->address == 0x82090300 && f->size == 0x78, "Burnout function bounds");
    if (first && f) {
        Table bad = *first; bad.labels[0] = f->address + f->size;
        CHECK(!Escapes({bad}, functions).empty(), "reject label one past function");
        bad = *first; bad.hasDefault = true; bad.defaultLabel = f->address - 4;
        CHECK(!Escapes({bad}, functions).empty(), "reject borrowed default before function");
    }

    if (g_failures)
    {
        printf("switch_tables_test: %d FAILED\n", g_failures);
        return 1;
    }
    printf("switch_tables_test: all passed\n");
    return 0;
}
