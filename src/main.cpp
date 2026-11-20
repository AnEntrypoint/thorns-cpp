#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <map>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr std::size_t kMaxBytes = 2 * 1024 * 1024;
constexpr std::size_t kLongFn = 80;
constexpr auto npos = std::string_view::npos;

enum class Style : std::uint8_t { Unknown, Brace, Python, Ruby, Json };

struct LangInfo {
    std::string_view name{};
    Style style = Style::Unknown;
};

struct FnRecord {
    std::size_t line = 0;
    std::size_t length = 0;
    std::string name;
};

struct FileReport {
    std::string rel;
    std::string_view lang{};
    std::size_t lines = 0;
    std::size_t functions = 0;
    std::size_t complexity = 0, asyncs = 0, awaits = 0, tries = 0, throws = 0, promises = 0, thenCatch = 0, exports = 0;
    std::vector<FnRecord> fns;
    std::vector<std::string> imports, envs, urls;
    std::vector<std::string> localSpecs;
    std::vector<std::string> targets;
    std::size_t branches = 0;
    std::vector<std::string> exportedNames, importedNames;
    std::vector<std::pair<std::string, std::size_t>> shapes;
    struct Span {
        std::string name;
        std::size_t from = 0, to = 0;
    };
    std::vector<Span> spans;
    std::string code;
    std::size_t sql = 0;
};

struct LangTally {
    std::size_t files = 0;
    std::size_t lines = 0;
};

struct Totals {
    std::size_t files = 0, lines = 0, bytes = 0, skipped = 0;
    std::size_t functions = 0, complexity = 0, asyncs = 0, awaits = 0, tries = 0, throws = 0, promises = 0, thenCatch = 0, exports = 0;
    std::map<std::string_view, LangTally> langs;
    std::map<std::string, std::size_t> imports, envs, urls;
    std::vector<FileReport> reports;
    std::size_t edges = 0, unresolved = 0;
    std::map<std::string, std::size_t> inDegree;
    std::vector<std::string> unresolvedSample;
    std::vector<std::vector<std::string>> cycles;
    std::size_t branches = 0;
    std::set<std::string> allImported;
    std::set<std::string> calls;
    std::size_t sql = 0;

    void add(FileReport&& r, std::size_t fileBytes) {
        ++files;
        lines += r.lines;
        bytes += fileBytes;
        auto& lang = langs[r.lang];
        ++lang.files;
        lang.lines += r.lines;
        for (const auto& m : r.imports) ++imports[m];
        for (const auto& e : r.envs) ++envs[e];
        for (const auto& u : r.urls) ++urls[u];
        functions += r.functions;
        complexity += r.complexity;
        asyncs += r.asyncs;
        awaits += r.awaits;
        tries += r.tries;
        throws += r.throws;
        promises += r.promises;
        thenCatch += r.thenCatch;
        exports += r.exports;
        branches += r.branches;
        sql += r.sql;
        for (const auto& n : r.importedNames) allImported.insert(n);
        reports.push_back(std::move(r));
    }
};

struct Project {
    std::string name, version, description, main;
    std::vector<std::string> bins, deps;
    bool esm = false;
};

bool isIdentChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' || c == '$';
}

bool isSpace(char c) {
    return std::isspace(static_cast<unsigned char>(c)) != 0;
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && isSpace(s.front())) s.remove_prefix(1);
    while (!s.empty() && isSpace(s.back())) s.remove_suffix(1);
    return s;
}

std::vector<std::string_view> splitLines(std::string_view s) {
    std::vector<std::string_view> out;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= s.size(); ++i) {
        if (i == s.size() || s[i] == '\n') {
            out.push_back(s.substr(start, i - start));
            start = i + 1;
        }
    }
    return out;
}

std::size_t countSub(std::string_view s, std::string_view needle) {
    std::size_t count = 0;
    for (std::size_t p = s.find(needle); p != npos; p = s.find(needle, p + needle.size())) ++count;
    return count;
}

LangInfo langOf(std::string_view ext) {
    static const std::map<std::string_view, LangInfo> table = {
        {".js", {"javascript", Style::Brace}},   {".mjs", {"javascript", Style::Brace}}, {".cjs", {"javascript", Style::Brace}},
        {".jsx", {"javascript", Style::Brace}},  {".ts", {"typescript", Style::Brace}},  {".tsx", {"typescript", Style::Brace}},
        {".py", {"python", Style::Python}},      {".rb", {"ruby", Style::Ruby}},         {".rs", {"rust", Style::Brace}},
        {".go", {"go", Style::Brace}},           {".c", {"c", Style::Brace}},            {".h", {"c", Style::Brace}},
        {".cc", {"cpp", Style::Brace}},          {".cpp", {"cpp", Style::Brace}},        {".cxx", {"cpp", Style::Brace}},
        {".hpp", {"cpp", Style::Brace}},         {".hh", {"cpp", Style::Brace}},         {".java", {"java", Style::Brace}},
        {".cs", {"csharp", Style::Brace}},       {".php", {"php", Style::Brace}},        {".json", {"json", Style::Json}},
    };
    const auto it = table.find(ext);
    return it == table.end() ? LangInfo{} : it->second;
}

bool startsRegex(std::string_view src, std::size_t i) {
    if (i + 1 < src.size() && (src[i + 1] == '/' || src[i + 1] == '*')) return false;
    std::size_t j = i;
    while (j > 0 && isSpace(src[j - 1])) --j;
    if (j == 0) return true;
    return std::string_view("(,=:[!&|?{};+-*%<>~^").find(src[j - 1]) != npos;
}

std::string codeView(std::string_view src, Style style) {
    std::string out(src.size(), ' ');
    const bool hashComments = style == Style::Python || style == Style::Ruby;
    const bool slashComments = style == Style::Brace;
    const std::size_t n = src.size();
    std::size_t i = 0;
    while (i < n) {
        const char c = src[i];
        if (c == '\n') {
            out[i] = '\n';
            ++i;
            continue;
        }
        if (style == Style::Python && (c == '"' || c == '\'') && i + 2 < n && src[i + 1] == c && src[i + 2] == c) {
            i += 3;
            while (i < n && !(src[i] == c && i + 2 < n && src[i + 1] == c && src[i + 2] == c)) {
                if (src[i] == '\n') out[i] = '\n';
                ++i;
            }
            i = std::min(n, i + 3);
            continue;
        }
        if (slashComments && c == '/' && i + 1 < n && src[i + 1] == '/') {
            while (i < n && src[i] != '\n') ++i;
            continue;
        }
        if (slashComments && c == '/' && i + 1 < n && src[i + 1] == '*') {
            i += 2;
            while (i < n && !(src[i] == '*' && i + 1 < n && src[i + 1] == '/')) {
                if (src[i] == '\n') out[i] = '\n';
                ++i;
            }
            i += 2;
            continue;
        }
        if (slashComments && c == '/' && startsRegex(src, i)) {
            ++i;
            bool inClass = false;
            while (i < n && src[i] != '\n') {
                if (src[i] == '\\' && i + 1 < n) {
                    i += 2;
                    continue;
                }
                if (src[i] == '[') inClass = true;
                else if (src[i] == ']') inClass = false;
                else if (src[i] == '/' && !inClass) break;
                ++i;
            }
            if (i < n && src[i] == '/') ++i;
            continue;
        }
        if (hashComments && c == '#') {
            while (i < n && src[i] != '\n') ++i;
            continue;
        }
        if (c == '"' || c == '\'' || c == '`') {
            ++i;
            while (i < n && src[i] != c) {
                if (src[i] == '\n' && c != '`') break;
                if (src[i] == '\\' && i + 1 < n) {
                    if (src[i + 1] == '\n') out[i + 1] = '\n';
                    i += 2;
                    continue;
                }
                if (src[i] == '\n') out[i] = '\n';
                ++i;
            }
            if (i < n && src[i] == c) ++i;
            continue;
        }
        out[i] = c;
        ++i;
    }
    return out;
}

void countTokens(std::string_view code, FileReport& r) {
    std::size_t i = 0;
    while (i < code.size()) {
        if (!isIdentChar(code[i]) || std::isdigit(static_cast<unsigned char>(code[i])) != 0) {
            ++i;
            continue;
        }
        const std::size_t s = i;
        while (i < code.size() && isIdentChar(code[i])) ++i;
        const std::string_view t = code.substr(s, i - s);
        if (t == "if" || t == "elif" || t == "for" || t == "while" || t == "case" || t == "catch" || t == "except" || t == "match") {
            ++r.complexity;
            ++r.branches;
        }
        else if (t == "async") ++r.asyncs;
        else if (t == "await") ++r.awaits;
        else if (t == "try") ++r.tries;
        else if (t == "throw" || t == "raise") ++r.throws;
        else if (t == "Promise") ++r.promises;
        else if (t == "export") ++r.exports;
    }
    r.complexity += countSub(code, "&&") + countSub(code, "||");
    r.thenCatch = countSub(code, ".then(") + countSub(code, ".catch(");
}

bool isControlWord(std::string_view w) {
    static const std::set<std::string_view> words = {"if", "for", "while", "switch", "catch", "with", "return", "else", "do", "sizeof"};
    return words.contains(w);
}

void recordFn(FileReport& r, std::size_t line, std::string name, std::size_t length) {
    ++r.functions;
    if (length >= kLongFn) r.fns.push_back(FnRecord{line, length, std::move(name)});
}

std::string normalizeShape(std::string_view s) {
    static const std::set<std::string_view> keep = {
        "if", "else", "for", "while", "do", "return", "const", "let", "var", "function", "class", "new", "throw", "try",
        "catch", "finally", "await", "async", "import", "export", "from", "switch", "case", "break", "continue", "default",
        "yield", "typeof", "instanceof", "in", "of", "delete", "void", "this", "super", "extends", "static", "null", "true",
        "false",
    };
    std::string out;
    std::size_t i = 0;
    while (i < s.size()) {
        if (isSpace(s[i])) {
            ++i;
            continue;
        }
        if (!isIdentChar(s[i])) {
            out += s[i++];
            out += ' ';
            continue;
        }
        std::size_t e = i;
        while (e < s.size() && isIdentChar(s[e])) ++e;
        const std::string_view word = s.substr(i, e - i);
        if (std::isdigit(static_cast<unsigned char>(word.front())) != 0) out += 'N';
        else if (keep.contains(word)) out += word;
        else out += 'I';
        out += ' ';
        i = e;
    }
    return out;
}

void scanBraces(std::string_view code, FileReport& r) {
    struct Open {
        bool fn;
        std::size_t line;
        std::string name;
        std::size_t head = npos;
    };
    std::vector<Open> stack;
    std::size_t line = 1;
    for (std::size_t i = 0; i < code.size(); ++i) {
        const char c = code[i];
        if (c == '\n') {
            ++line;
        } else if (c == '{') {
            Open o{false, line, {}};
            std::size_t j = i;
            while (j > 0 && isSpace(code[j - 1])) --j;
            if (j > 0 && code[j - 1] == ')') {
                int depth = 0;
                std::size_t k = j - 1;
                bool found = false;
                while (true) {
                    if (code[k] == ')') {
                        ++depth;
                    } else if (code[k] == '(' && --depth == 0) {
                        found = true;
                        break;
                    }
                    if (k == 0) break;
                    --k;
                }
                if (found) {
                    std::size_t e = k;
                    while (e > 0 && isSpace(code[e - 1])) --e;
                    std::size_t s = e;
                    while (s > 0 && isIdentChar(code[s - 1])) --s;
                    const std::string_view name = code.substr(s, e - s);
                    if (!name.empty() && !isControlWord(name)) {
                        o.fn = true;
                        o.name = std::string(name);
                        o.head = k;
                    }
                }
            }
            stack.push_back(std::move(o));
        } else if (c == '}' && !stack.empty()) {
            Open o = std::move(stack.back());
            stack.pop_back();
            if (o.fn) {
                const std::size_t length = line - o.line + 1;
                if (length >= 5) {
                    const std::string shape = normalizeShape(code.substr(o.head, i - o.head + 1));
                    r.shapes.emplace_back(o.name, std::hash<std::string>{}(shape));
                }
                if (o.head != npos) r.spans.push_back(FileReport::Span{o.name, o.head, i});
                recordFn(r, o.line, std::move(o.name), length);
            }
        }
    }
}

void scanPython(std::string_view code, FileReport& r) {
    const auto lines = splitLines(code);
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const std::string_view ln = lines[i];
        const std::size_t indent = ln.find_first_not_of(' ');
        if (indent == npos) continue;
        const std::string_view body = ln.substr(indent);
        if (!body.starts_with("def ") && !body.starts_with("async def ")) continue;
        const std::size_t d = body.find("def ") + 4;
        const std::size_t paren = body.find('(', d);
        if (paren == npos) continue;
        std::size_t j = i + 1;
        while (j < lines.size()) {
            const std::size_t k = lines[j].find_first_not_of(' ');
            if (k != npos && k <= indent) break;
            ++j;
        }
        std::size_t end = j;
        while (end > i + 1 && lines[end - 1].find_first_not_of(' ') == npos) --end;
        std::string name(body.substr(d, paren - d));
        const std::size_t from = static_cast<std::size_t>(lines[i].data() - code.data());
        const std::string_view last = lines[end - 1];
        const std::size_t to = static_cast<std::size_t>(last.data() + last.size() - code.data()) - 1;
        r.spans.push_back(FileReport::Span{name, from, to});
        recordFn(r, i + 1, std::move(name), end - i);
    }
}

std::string_view quotedIn(std::string_view s) {
    const std::size_t a = s.find_first_of("\"'<");
    if (a == npos) return {};
    const char close = s[a] == '<' ? '>' : s[a];
    const std::size_t b = s.find(close, a + 1);
    if (b == npos) return {};
    return s.substr(a + 1, b - a - 1);
}

std::string_view headOf(std::string_view s) {
    return s.substr(0, s.find_first_of(" ;,(:{\t"));
}

std::string_view firstSegment(std::string_view s) {
    return s.substr(0, s.find_first_of(".:\\"));
}

void addImport(FileReport& r, std::string_view mod) {
    if (mod.empty()) return;
    if (mod[0] == '.' || mod[0] == '/') {
        r.localSpecs.emplace_back(mod);
        return;
    }
    r.imports.emplace_back(mod);
}

void addPython(FileReport& r, std::string_view mod) {
    if (!mod.empty() && mod[0] == '.') r.localSpecs.emplace_back(mod);
    else if (!mod.empty()) r.localSpecs.push_back("abs:" + std::string(mod));
}

void scanImports(std::string_view src, FileReport& r) {
    for (const std::string_view raw : splitLines(src)) {
        const std::string_view ln = trim(raw);
        if (ln.starts_with("#include")) {
            if (ln.find('"') == npos) addImport(r, quotedIn(ln));
            else r.localSpecs.emplace_back(quotedIn(ln));
        } else if (ln.find("require(") != npos || ln.starts_with("require ") || ln.starts_with("import '") ||
                   ln.starts_with("import \"") ||
                   ((ln.starts_with("import") || ln.starts_with("export")) && ln.find(" from ") != npos)) {
            addImport(r, quotedIn(ln));
        } else if (ln.starts_with("import ")) {
            addImport(r, firstSegment(headOf(trim(ln.substr(7)))));
        } else if (ln.starts_with("from ")) {
            addPython(r, headOf(trim(ln.substr(5))));
        } else if (ln.starts_with("use ")) {
            const std::string_view path = headOf(trim(ln.substr(4)));
            if (path.starts_with("crate::") || path.starts_with("self::") || path.starts_with("super::")) {
                r.localSpecs.emplace_back(path);
            } else {
                addImport(r, firstSegment(path));
            }
        } else if (ln.starts_with("mod ") && ln.ends_with(';')) {
            r.localSpecs.push_back("mod:" + std::string(trim(ln.substr(4, ln.size() - 5))));
        } else if (ln.starts_with("using ")) {
            if (ln.find('=') != npos || ln.starts_with("using namespace")) continue;
            const std::string_view m = headOf(trim(ln.substr(6)));
            if (m != "static") addImport(r, firstSegment(m));
        }
    }
}

struct EnvMarker {
    std::string_view text;
    bool quoted;
};

void scanEnv(std::string_view src, FileReport& r) {
    static constexpr EnvMarker kMarkers[] = {
        {"process.env.", false}, {"import.meta.env.", false}, {"os.environ[", true},     {"os.environ.get(", true},
        {"os.getenv(", true},    {"getenv(", true},           {"env::var(", true},      {"GetEnvironmentVariable(", true},
    };
    for (const auto& m : kMarkers) {
        for (std::size_t p = src.find(m.text); p != npos; p = src.find(m.text, p + m.text.size())) {
            if (p > 0 && (isIdentChar(src[p - 1]) || src[p - 1] == '.')) continue;
            std::size_t q = p + m.text.size();
            std::string name;
            if (m.quoted) {
                while (q < src.size() && src[q] == ' ') ++q;
                if (q >= src.size() || (src[q] != '"' && src[q] != '\'')) continue;
                const char quote = src[q++];
                while (q < src.size() && src[q] != quote && src[q] != '\n') name.push_back(src[q++]);
            } else {
                while (q < src.size() && isIdentChar(src[q])) name.push_back(src[q++]);
            }
            if (!name.empty()) r.envs.push_back(name);
        }
    }
}

void scanUrls(std::string_view src, FileReport& r) {
    for (const std::string_view scheme : {std::string_view("https://"), std::string_view("http://")}) {
        for (std::size_t p = src.find(scheme); p != npos; p = src.find(scheme, p + scheme.size())) {
            std::size_t e = src.find_first_of(" \t\r\n'\"` )<>,", p);
            if (e == npos) e = src.size();
            while (e > p && (src[e - 1] == '.' || src[e - 1] == ';')) --e;
            if (e - p > scheme.size() + 3) r.urls.emplace_back(src.substr(p, e - p));
        }
    }
}

std::size_t skipSpaces(std::string_view s, std::size_t i) {
    while (i < s.size() && isSpace(s[i])) ++i;
    return i;
}

std::size_t identEndAt(std::string_view s, std::size_t i) {
    while (i < s.size() && isIdentChar(s[i])) ++i;
    return i;
}

bool wordAt(std::string_view s, std::size_t i, std::string_view w) {
    if (i > s.size() || s.substr(i, w.size()) != w) return false;
    const bool before = i == 0 || !isIdentChar(s[i - 1]);
    const bool after = i + w.size() >= s.size() || !isIdentChar(s[i + w.size()]);
    return before && after;
}

std::size_t closingParen(std::string_view s, std::size_t open) {
    int depth = 0;
    for (std::size_t i = open; i < s.size(); ++i) {
        if (s[i] == '(') ++depth;
        else if (s[i] == ')' && --depth == 0) return i + 1;
    }
    return npos;
}

void countBoundFunctions(std::string_view code, FileReport& r) {
    for (const std::string_view kw : {std::string_view("const"), std::string_view("let"), std::string_view("var")}) {
        for (std::size_t p = code.find(kw); p != npos; p = code.find(kw, p + kw.size())) {
            if (!wordAt(code, p, kw)) continue;
            std::size_t i = skipSpaces(code, p + kw.size());
            const std::size_t nameEnd = identEndAt(code, i);
            if (nameEnd == i) continue;
            i = skipSpaces(code, nameEnd);
            if (i >= code.size() || code[i] != '=' || code.substr(i, 2) == "==" || code.substr(i, 2) == "=>") continue;
            i = skipSpaces(code, i + 1);
            if (wordAt(code, i, "async")) i = skipSpaces(code, i + 5);
            if (wordAt(code, i, "function")) {
                ++r.functions;
                continue;
            }
            if (i < code.size() && code[i] == '(') {
                i = closingParen(code, i);
                if (i == npos) continue;
            } else {
                const std::size_t e = identEndAt(code, i);
                if (e == i) continue;
                i = e;
            }
            i = skipSpaces(code, i);
            if (code.substr(i, 2) == "=>") ++r.functions;
        }
    }
}

void addNameList(std::string_view body, std::vector<std::string>& out, bool exportSide) {
    body = body.substr(0, body.find('}'));
    while (!body.empty()) {
        const std::size_t comma = body.find(',');
        std::string_view item = trim(body.substr(0, comma));
        const std::size_t as = item.find(" as ");
        if (as != npos) item = trim(exportSide ? item.substr(as + 4) : item.substr(0, as));
        if (!item.empty()) out.emplace_back(item);
        if (comma == npos) break;
        body.remove_prefix(comma + 1);
    }
}

void addDeclaredName(std::string_view decl, std::vector<std::string>& out) {
    if (decl.starts_with("async ")) decl = trim(decl.substr(6));
    for (const std::string_view kw : {std::string_view("function"), std::string_view("class"), std::string_view("const"), std::string_view("let"), std::string_view("var")}) {
        if (!decl.starts_with(kw) || decl.size() <= kw.size()) continue;
        const char next = decl[kw.size()];
        if (!isSpace(next) && next != '*') continue;
        std::string_view after = trim(decl.substr(kw.size()));
        if (after.starts_with('*')) after = trim(after.substr(1));
        const std::size_t e = identEndAt(after, 0);
        if (e > 0) out.emplace_back(after.substr(0, e));
        return;
    }
}

void scanNames(std::string_view src, FileReport& r) {
    for (const std::string_view raw : splitLines(src)) {
        const std::string_view ln = trim(raw);
        const std::size_t brace = ln.find('{');
        if (brace != npos && (ln.starts_with("import ") || ln.find("require(") != npos)) {
            addNameList(ln.substr(brace + 1), r.importedNames, false);
        } else if (ln.starts_with("export ")) {
            const std::string_view rest = trim(ln.substr(7));
            if (rest.starts_with('{')) addNameList(rest.substr(1), r.exportedNames, true);
            else addDeclaredName(rest, r.exportedNames);
        }
    }
}

std::size_t countWord(std::string_view code, std::string_view w) {
    std::size_t count = 0;
    for (std::size_t p = code.find(w); p != npos; p = code.find(w, p + w.size())) {
        if (wordAt(code, p, w)) ++count;
    }
    return count;
}

bool isBuiltin(std::string_view name) {
    static const std::set<std::string_view> names = {
        "assert", "async_hooks", "buffer", "child_process", "crypto", "dns", "events", "fs", "http", "https", "module",
        "net", "os", "path", "perf_hooks", "process", "querystring", "readline", "stream", "timers", "tty", "url", "util",
        "vm", "worker_threads", "zlib",
    };
    return name.starts_with("node:") || names.contains(name);
}

std::string blankPyTripleQuoted(std::string_view src) {
    std::string out(src);
    const std::size_t n = src.size();
    for (std::size_t i = 0; i + 2 < n;) {
        const char c = src[i];
        if ((c == '"' || c == '\'') && src[i + 1] == c && src[i + 2] == c) {
            std::size_t j = i + 3;
            while (j < n && !(src[j] == c && j + 2 < n && src[j + 1] == c && src[j + 2] == c)) {
                if (src[j] != '\n') out[j] = ' ';
                ++j;
            }
            i = j + 3;
            continue;
        }
        ++i;
    }
    return out;
}

FileReport analyzeFile(const fs::path& root, const fs::path& path, LangInfo info, const std::string& src) {
    FileReport r;
    std::error_code ec;
    r.rel = fs::relative(path, root, ec).generic_string();
    r.lang = info.name;
    r.lines = static_cast<std::size_t>(std::count(src.begin(), src.end(), '\n')) + (!src.empty() && src.back() != '\n' ? 1 : 0);
    const std::string code = codeView(src, info.style);
    const std::string text = info.style == Style::Python ? blankPyTripleQuoted(src) : src;
    countTokens(code, r);
    if (info.style == Style::Brace) countBoundFunctions(code, r);
    if (info.style == Style::Brace) scanBraces(code, r);
    else if (info.style == Style::Python) scanPython(code, r);
    scanImports(text, r);
    scanNames(text, r);
    std::erase_if(r.exportedNames, [&](const std::string& n) { return countWord(code, n) > 1; });
    scanEnv(text, r);
    scanUrls(text, r);
    r.code = code;
    r.sql = countSub(text, "SELECT ") + countSub(text, "INSERT INTO ") + countSub(text, "DELETE FROM ");
    return r;
}

std::string relTo(const fs::path& p, const fs::path& root) {
    return p.lexically_normal().lexically_relative(root).generic_string();
}

std::vector<std::string> splitSegments(std::string_view s) {
    std::vector<std::string> out;
    while (!s.empty()) {
        const std::size_t e = s.find('/');
        const std::string_view seg = s.substr(0, e);
        if (!seg.empty()) out.emplace_back(seg);
        if (e == npos) break;
        s.remove_prefix(e + 1);
    }
    return out;
}

bool matchSegment(std::string_view p, std::string_view s) {
    if (p.empty()) return s.empty();
    switch (p.front()) {
        case '*':
            for (std::size_t k = 0; k <= s.size(); ++k) {
                if (matchSegment(p.substr(1), s.substr(k))) return true;
            }
            return false;
        case '?':
            return !s.empty() && matchSegment(p.substr(1), s.substr(1));
        case '[': {
            const std::size_t close = p.find(']', 2);
            if (close == npos) return !s.empty() && s.front() == '[' && matchSegment(p.substr(1), s.substr(1));
            if (s.empty()) return false;
            const bool negated = p[1] == '!' || p[1] == '^';
            bool hit = false;
            for (std::size_t i = negated ? 2 : 1; i < close; ++i) {
                if (i + 2 < close && p[i + 1] == '-') {
                    hit = hit || (s.front() >= p[i] && s.front() <= p[i + 2]);
                    i += 2;
                } else {
                    hit = hit || s.front() == p[i];
                }
            }
            return hit != negated && matchSegment(p.substr(close + 1), s.substr(1));
        }
        case '\\':
            return p.size() > 1 && !s.empty() && s.front() == p[1] && matchSegment(p.substr(2), s.substr(1));
        default:
            return !s.empty() && s.front() == p.front() && matchSegment(p.substr(1), s.substr(1));
    }
}

bool matchSegments(const std::vector<std::string>& pat, std::size_t pi, const std::vector<std::string>& path, std::size_t si) {
    if (pi == pat.size()) return si == path.size();
    if (pat[pi] == "**") {
        for (std::size_t k = si; k <= path.size(); ++k) {
            if (matchSegments(pat, pi + 1, path, k)) return true;
        }
        return false;
    }
    return si < path.size() && matchSegment(pat[pi], path[si]) && matchSegments(pat, pi + 1, path, si + 1);
}

struct IgnoreRule {
    std::vector<std::string> baseSegs;
    std::vector<std::string> pattern;
    bool negate = false;
    bool dirOnly = false;
};

bool parseRule(std::string_view line, std::string_view base, IgnoreRule& rule) {
    std::string_view ln = trim(line);
    if (ln.empty() || ln.front() == '#') return false;
    IgnoreRule parsed;
    parsed.baseSegs = splitSegments(base);
    if (ln.front() == '!') {
        parsed.negate = true;
        ln.remove_prefix(1);
    }
    if (ln.ends_with('/')) {
        parsed.dirOnly = true;
        ln.remove_suffix(1);
    }
    const bool anchored = ln.find('/') != npos;
    parsed.pattern = splitSegments(ln.starts_with('/') ? ln.substr(1) : ln);
    if (parsed.pattern.empty()) return false;
    if (!anchored) parsed.pattern.insert(parsed.pattern.begin(), "**");
    rule = std::move(parsed);
    return true;
}

void loadRules(const fs::path& file, std::string_view base, std::vector<IgnoreRule>& rules) {
    std::ifstream in(file);
    std::string line;
    IgnoreRule rule;
    while (std::getline(in, line)) {
        if (parseRule(line, base, rule)) rules.push_back(rule);
    }
}

bool isIgnored(const std::vector<IgnoreRule>& rules, const std::string& rel, bool isDir) {
    const std::vector<std::string> path = splitSegments(rel);
    bool ignored = false;
    for (const auto& rule : rules) {
        if (rule.dirOnly && !isDir) continue;
        const std::size_t depth = rule.baseSegs.size();
        if (path.size() < depth || !std::equal(rule.baseSegs.begin(), rule.baseSegs.end(), path.begin())) continue;
        if (matchSegments(rule.pattern, 0, path, depth)) ignored = !rule.negate;
    }
    return ignored;
}

void walkDir(const fs::path& root, const fs::path& dir, const std::vector<IgnoreRule>& inherited, Totals& totals) {
    std::error_code marker;
    if (dir != root && (fs::exists(dir / ".git", marker) || fs::exists(dir / "pyvenv.cfg", marker))) return;
    std::vector<IgnoreRule> rules = inherited;
    const std::string baseRel = dir == root ? std::string() : relTo(dir, root);
    loadRules(dir / ".gitignore", baseRel, rules);
    loadRules(dir / ".thornsignore", baseRel, rules);

    std::error_code ec;
    const fs::directory_iterator end;
    for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec); it != end && !ec; it.increment(ec)) {
        const fs::directory_entry& de = *it;
        std::error_code entryEc;
        const bool isDir = de.is_directory(entryEc);
        if (isIgnored(rules, relTo(de.path(), root), isDir)) continue;
        if (isDir) {
            if (!de.is_symlink(entryEc)) walkDir(root, de.path(), rules, totals);
            continue;
        }
        if (!de.is_regular_file(entryEc)) continue;
        const LangInfo info = langOf(de.path().extension().string());
        if (info.style == Style::Unknown) continue;
        if (de.file_size(entryEc) > kMaxBytes) {
            ++totals.skipped;
            continue;
        }
        std::ifstream in(de.path(), std::ios::binary);
        const std::string src((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        totals.add(analyzeFile(root, de.path(), info, src), src.size());
    }
}

Totals walk(const fs::path& root) {
    static const std::vector<std::string> kDefaultIgnores = {
        ".git", ".svn", ".hg", "node_modules", "dist", "build", "out", "target", ".next", ".nuxt", "__pycache__",
        ".venv", "venv", "vendor", ".cache", "coverage", ".gm", ".idea", ".vscode", "obj", ".config",
        "tests/fixtures", "*.min.js", "/embeddings",
    };
    std::vector<IgnoreRule> rules;
    for (const auto& name : kDefaultIgnores) {
        IgnoreRule rule;
        if (parseRule(name, "", rule)) rules.push_back(std::move(rule));
    }
    Totals totals;
    walkDir(root, root, rules, totals);
    return totals;
}

std::string jsonField(const std::string& text, std::string_view key) {
    const std::string needle = "\"" + std::string(key) + "\"";
    std::size_t p = text.find(needle);
    if (p == std::string::npos) return {};
    p = text.find(':', p + needle.size());
    if (p == std::string::npos) return {};
    ++p;
    while (p < text.size() && isSpace(text[p])) ++p;
    if (p >= text.size() || text[p] != '"') return {};
    std::string out;
    for (std::size_t e = p + 1; e < text.size() && text[e] != '"'; ++e) {
        if (text[e] == '\\' && e + 1 < text.size()) ++e;
        out.push_back(text[e]);
    }
    return out;
}

std::string jsonStringAt(const std::string& text, std::size_t q, std::size_t& end) {
    end = text.find('"', q + 1);
    if (end == std::string::npos) return {};
    return text.substr(q + 1, end - q - 1);
}

std::vector<std::string> jsonObjectValues(const std::string& text, std::string_view key, bool keys) {
    std::vector<std::string> out;
    const std::size_t k = text.find("\"" + std::string(key) + "\"");
    if (k == std::string::npos) return out;
    std::size_t p = text.find(':', k);
    if (p == std::string::npos) return out;
    p = skipSpaces(text, p + 1);
    if (p >= text.size()) return out;
    std::size_t end = 0;
    if (text[p] == '"') {
        out.push_back(jsonStringAt(text, p, end));
        return out;
    }
    if (text[p] != '{') return out;
    const std::size_t close = text.find('}', p);
    std::size_t index = 0;
    for (std::size_t q = p + 1; q < close;) {
        if (text[q] != '"') {
            ++q;
            continue;
        }
        const std::string s = jsonStringAt(text, q, end);
        if ((index % 2 == 0) == keys) out.push_back(s);
        ++index;
        q = end + 1;
    }
    return out;
}

Project readProject(const fs::path& root) {
    Project p;
    std::ifstream in(root / "package.json");
    if (!in) return p;
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    p.name = jsonField(text, "name");
    p.version = jsonField(text, "version");
    p.description = jsonField(text, "description");
    p.main = jsonField(text, "main");
    p.bins = jsonObjectValues(text, "bin", false);
    p.deps = jsonObjectValues(text, "dependencies", true);
    p.esm = jsonField(text, "type") == "module";
    return p;
}

using KnownSet = std::set<std::string>;

std::vector<std::string> splitRustPath(std::string_view s) {
    std::vector<std::string> out;
    while (!s.empty()) {
        const std::size_t e = s.find("::");
        const std::string_view seg = s.substr(0, e);
        if (!seg.empty()) out.emplace_back(seg);
        if (e == npos) break;
        s.remove_prefix(e + 2);
    }
    return out;
}

std::string rustPath(const fs::path& base, std::vector<std::string> segs, const fs::path& root, const KnownSet& known) {
    while (!segs.empty()) {
        fs::path p = base;
        for (const auto& s : segs) p /= s;
        const std::string stem = p.lexically_normal().string();
        for (const std::string& cand : {stem + ".rs", stem + "/mod.rs"}) {
            const std::string rel = relTo(fs::path(cand), root);
            if (known.contains(rel)) return rel;
        }
        segs.pop_back();
    }
    return {};
}

std::string resolveOne(const FileReport& r, std::string_view spec, const fs::path& root, const KnownSet& known) {
    const fs::path file = root / r.rel;
    const fs::path dir = file.parent_path();
    const std::string lang(r.lang);
    std::vector<std::string> cands;
    if (lang == "javascript" || lang == "typescript") {
        const std::string base = relTo(dir / std::string(spec), root);
        cands.push_back(base);
        for (const char* ext : {".js", ".mjs", ".cjs", ".jsx", ".ts", ".tsx", ".json"}) cands.push_back(base + ext);
        if (base.ends_with(".js")) {
            const std::string stripped = base.substr(0, base.size() - 3);
            cands.push_back(stripped + ".ts");
            cands.push_back(stripped + ".tsx");
        }
        for (const char* idx : {"/index.js", "/index.ts", "/index.tsx", "/index.mjs"}) cands.push_back(base + idx);
    } else if (lang == "python" && spec.starts_with("abs:")) {
        std::string modPath;
        for (const char ch : spec.substr(4)) modPath.push_back(ch == '.' ? '/' : ch);
        cands.push_back(modPath + ".py");
        cands.push_back(modPath + "/__init__.py");
    } else if (lang == "python") {
        const std::size_t dots = spec.find_first_not_of('.');
        const std::size_t levels = dots == npos ? spec.size() : dots;
        const std::string_view rest = dots == npos ? std::string_view{} : spec.substr(dots);
        fs::path base = dir;
        for (std::size_t k = 1; k < levels; ++k) base = base.parent_path();
        std::string modPath;
        for (const char ch : rest) modPath.push_back(ch == '.' ? '/' : ch);
        std::string target = relTo(base / modPath, root);
        while (target.ends_with('/')) target.pop_back();
        if (!modPath.empty()) cands.push_back(target + ".py");
        cands.push_back(target + "/__init__.py");
    } else if (lang == "rust") {
        const std::string stem = file.stem().string();
        const fs::path modDir = (stem == "mod" || stem == "lib" || stem == "main") ? dir : dir / stem;
        if (spec.starts_with("mod:")) return rustPath(modDir, {std::string(spec.substr(4))}, root, known);
        fs::path base = modDir;
        std::string_view rest = spec;
        if (rest.starts_with("crate::")) {
            base = root / "src";
            rest.remove_prefix(7);
        } else {
            for (;;) {
                if (rest.starts_with("self::")) {
                    rest.remove_prefix(6);
                } else if (rest.starts_with("super::")) {
                    base = base.parent_path();
                    rest.remove_prefix(7);
                } else {
                    break;
                }
            }
        }
        return rustPath(base, splitRustPath(rest), root, known);
    } else if (lang == "c" || lang == "cpp") {
        cands.push_back(relTo(dir / std::string(spec), root));
        for (const char* incDir : {"", "include", "src", "third_party"}) {
            cands.push_back(relTo(root / incDir / std::string(spec), root));
        }
    }
    for (const auto& c : cands) {
        if (known.contains(c)) return c;
    }
    return {};
}

void buildGraph(Totals& t, const fs::path& root) {
    KnownSet known;
    for (const auto& r : t.reports) known.insert(r.rel);
    for (auto& r : t.reports) {
        std::set<std::string> targets;
        for (const auto& spec : r.localSpecs) {
            const std::string hit = resolveOne(r, spec, root, known);
            if (hit.empty() && spec.starts_with("abs:")) {
                ++t.imports[std::string(firstSegment(spec.substr(4)))];
            } else if (hit.empty()) {
                ++t.unresolved;
                if (t.unresolvedSample.size() < 8) t.unresolvedSample.push_back(r.rel + " -> " + spec);
            } else {
                targets.insert(hit);
            }
        }
        r.targets.assign(targets.begin(), targets.end());
        for (const auto& target : r.targets) {
            ++t.edges;
            ++t.inDegree[target];
        }
    }
}

struct SccState {
    std::vector<std::size_t> index, low, stack;
    std::vector<bool> onStack;
    std::size_t counter = 0;
    std::vector<std::vector<std::size_t>> components;
};

void strongConnect(std::size_t v, const std::vector<std::vector<std::size_t>>& adj, SccState& s) {
    s.index[v] = s.low[v] = s.counter++;
    s.stack.push_back(v);
    s.onStack[v] = true;
    for (const std::size_t w : adj[v]) {
        if (s.index[w] == npos) {
            strongConnect(w, adj, s);
            s.low[v] = std::min(s.low[v], s.low[w]);
        } else if (s.onStack[w]) {
            s.low[v] = std::min(s.low[v], s.index[w]);
        }
    }
    if (s.low[v] != s.index[v]) return;
    std::vector<std::size_t> component;
    std::size_t w = v;
    do {
        w = s.stack.back();
        s.stack.pop_back();
        s.onStack[w] = false;
        component.push_back(w);
    } while (w != v);
    const bool selfLoop = std::find(adj[v].begin(), adj[v].end(), v) != adj[v].end();
    if (component.size() > 1 || selfLoop) s.components.push_back(std::move(component));
}

void buildCalls(Totals& t) {
    std::set<std::string> defined;
    for (const auto& r : t.reports) {
        for (const auto& sp : r.spans) defined.insert(sp.name);
    }
    for (const auto& r : t.reports) {
        for (const auto& sp : r.spans) {
            const std::string_view body = std::string_view(r.code).substr(sp.from, sp.to - sp.from + 1);
            for (std::size_t i = 0; i < body.size();) {
                if (!isIdentChar(body[i]) || std::isdigit(static_cast<unsigned char>(body[i])) != 0) {
                    ++i;
                    continue;
                }
                const std::size_t s0 = i;
                while (i < body.size() && isIdentChar(body[i])) ++i;
                const std::string name(body.substr(s0, i - s0));
                const std::size_t after = skipSpaces(body, i);
                if (after < body.size() && body[after] == '(' && name != sp.name && defined.contains(name)) {
                    t.calls.insert(sp.name + " → " + name);
                }
            }
        }
    }
}

void findCycles(Totals& t) {
    std::map<std::string, std::size_t> indexOf;
    std::vector<std::string> names;
    for (const auto& r : t.reports) {
        indexOf.emplace(r.rel, names.size());
        names.push_back(r.rel);
    }
    std::vector<std::vector<std::size_t>> adj(names.size());
    for (const auto& r : t.reports) {
        for (const auto& target : r.targets) adj[indexOf.at(r.rel)].push_back(indexOf.at(target));
    }
    SccState s;
    s.index.assign(names.size(), npos);
    s.low.assign(names.size(), 0);
    s.onStack.assign(names.size(), false);
    for (std::size_t v = 0; v < names.size(); ++v) {
        if (s.index[v] == npos) strongConnect(v, adj, s);
    }
    for (auto& component : s.components) {
        std::vector<std::string> members;
        for (const std::size_t v : component) members.push_back(names[v]);
        std::sort(members.begin(), members.end());
        t.cycles.push_back(std::move(members));
    }
    std::sort(t.cycles.begin(), t.cycles.end(), [](const auto& a, const auto& b) {
        return a.size() != b.size() ? a.size() > b.size() : a.front() < b.front();
    });
}

std::string joinCounted(const std::map<std::string, std::size_t>& m, std::size_t limit) {
    std::vector<std::pair<std::string, std::size_t>> v(m.begin(), m.end());
    std::sort(v.begin(), v.end(), [](const auto& a, const auto& b) {
        return a.second != b.second ? a.second > b.second : a.first < b.first;
    });
    std::string out;
    for (std::size_t i = 0; i < v.size() && i < limit; ++i) {
        if (i) out += ", ";
        out += v[i].first;
        out += "×" + std::to_string(v[i].second);
    }
    if (v.size() > limit) out += " +" + std::to_string(v.size() - limit);
    return out;
}

void appendTree(const std::string& rel, const std::map<std::string, const FileReport*>& byRel, std::size_t depth,
                std::set<std::string>& seen, std::string& out) {
    const auto it = byRel.find(rel);
    std::string label = rel;
    if (it != byRel.end() && !it->second->exportedNames.empty()) {
        label += "(";
        for (std::size_t k = 0; k < it->second->exportedNames.size(); ++k) {
            label += (k ? ", " : "") + it->second->exportedNames[k];
        }
        label += ")";
    }
    out += std::string(depth * 2, ' ') + label;
    if (seen.contains(rel)) {
        out += " (seen)\n";
        return;
    }
    out += "\n";
    seen.insert(rel);
    if (depth >= 6 || it == byRel.end()) return;
    for (const auto& target : it->second->targets) appendTree(target, byRel, depth + 1, seen, out);
}

std::string archTree(const Totals& t, const std::vector<std::string>& entries) {
    std::map<std::string, const FileReport*> byRel;
    for (const auto& r : t.reports) byRel.emplace(r.rel, &r);
    std::vector<std::string> starts;
    for (const auto& entry : entries) {
        if (byRel.contains(entry)) starts.push_back(entry);
    }
    if (starts.empty()) {
        for (const auto& r : t.reports) {
            if (starts.size() >= 3) break;
            if (r.lang != "json" && !t.inDegree.contains(r.rel)) starts.push_back(r.rel);
        }
    }
    std::string out;
    std::set<std::string> seen;
    for (const auto& s : starts) appendTree(s, byRel, 0, seen, out);
    if (!out.empty()) out.pop_back();
    return out;
}

constexpr std::string_view kLegend = "legend: ## section, ×N occurrences, (n) lines or complexity\n";

std::size_t droppedSize(const std::vector<std::string>& dropped) {
    return dropped.empty() ? 0 : 32;
}

enum Sec : std::size_t { kArch, kIssues, kDead, kDeps, kIo, kCalls, kMetrics, kSecCount };

std::string render(const Totals& t, const Project& p, const std::string& root, std::size_t budgetChars) {
    static constexpr std::string_view kTitles[kSecCount] = {"Architecture", "Issues", "Dead code", "Dependencies", "I/O", "Calls", "Metrics"};
    std::vector<std::string> sections[kSecCount];
    std::vector<std::string> dropped;
    std::size_t used = 0;
    const auto add = [&](Sec s, const char* name, std::string text) {
        if (text.empty()) return;
        const std::size_t heading = sections[s].empty() ? kTitles[s].size() + 4 : 0;
        const std::size_t cost = text.size() + 1 + heading;
        if (budgetChars != 0 && used + cost + droppedSize(dropped) + 24 > budgetChars) {
            dropped.emplace_back(name);
            return;
        }
        used += cost;
        sections[s].push_back(std::move(text));
    };

    std::string head = "# " + (p.name.empty() ? fs::path(root).filename().string() : p.name);
    if (!p.version.empty()) head += " " + p.version;
    std::vector<std::string> tags;
    if (!p.bins.empty()) tags.emplace_back("cli");
    if (p.esm) tags.emplace_back("ESM");
    if (!tags.empty()) {
        head += " (";
        for (std::size_t i = 0; i < tags.size(); ++i) head += (i ? ", " : "") + tags[i];
        head += ")";
    }

    std::size_t codeLines = 0;
    for (const auto& [name, tally] : t.langs) codeLines += tally.lines;
    const auto share = [&](std::initializer_list<std::string_view> names) {
        std::size_t n = 0;
        for (const auto& nm : names) {
            if (const auto it = t.langs.find(nm); it != t.langs.end()) n += it->second.lines;
        }
        return codeLines == 0 ? std::size_t{0} : n * 100 / codeLines;
    };
    const std::string stats = std::to_string(t.files) + " files · " + std::to_string(t.lines) + " lines · " +
                              std::to_string(t.functions) + " fn · JS " + std::to_string(share({"javascript", "typescript"})) +
                              "% · Py " + std::to_string(share({"python"})) + "%";

    used = head.size() + stats.size() + kLegend.size() + 2;
    std::vector<std::string> entries;
    if (!p.main.empty()) entries.push_back(p.main);
    entries.insert(entries.end(), p.bins.begin(), p.bins.end());
    for (auto& e : entries) {
        if (e.starts_with("./")) e.erase(0, 2);
    }
    std::vector<std::string> unimported;
    for (const auto& r : t.reports) {
        if (r.lang == "json" || std::find(entries.begin(), entries.end(), r.rel) != entries.end() || t.inDegree.contains(r.rel)) continue;
        unimported.push_back(r.rel);
    }

    const std::string arch = archTree(t, entries);
    for (std::size_t pos = 0; pos < arch.size();) {
        std::size_t e = arch.find('\n', pos);
        if (e == npos) e = arch.size();
        add(kArch, "arch", arch.substr(pos, e - pos));
        pos = e + 1;
    }

    for (std::size_t i = 0; i < t.cycles.size() && i < 8; ++i) {
        std::string line = "cycle " + std::to_string(t.cycles[i].size()) + ":";
        for (std::size_t k = 0; k < t.cycles[i].size() && k < 6; ++k) line += " " + t.cycles[i][k];
        add(kIssues, "cycles", line);
    }
    if (!t.unresolvedSample.empty()) {
        std::string line = "unresolved:";
        for (const auto& u : t.unresolvedSample) line += " [" + u + "]";
        add(kIssues, "unresolved", line);
    }
    std::map<std::size_t, std::vector<std::pair<std::string, std::string>>> shapeGroups;
    for (const auto& r : t.reports) {
        for (const auto& [name, hash] : r.shapes) shapeGroups[hash].emplace_back(name, r.rel);
    }
    std::vector<const std::vector<std::pair<std::string, std::string>>*> duplicateGroups;
    for (const auto& [hash, group] : shapeGroups) {
        if (group.size() > 1) duplicateGroups.push_back(&group);
    }
    std::sort(duplicateGroups.begin(), duplicateGroups.end(), [](const auto* a, const auto* b) { return a->size() > b->size(); });
    if (!duplicateGroups.empty()) {
        std::string line = "duplicates:";
        for (std::size_t i = 0; i < duplicateGroups.size() && i < 2; ++i) {
            const auto& group = *duplicateGroups[i];
            std::set<std::string> files;
            for (const auto& member : group) files.insert(member.second);
            std::string fileList;
            for (const auto& f : files) fileList += (fileList.empty() ? "" : ", ") + f;
            line += (i ? " |" : "") + std::string(" ") + group.front().first + "×" + std::to_string(group.size()) + " (" + fileList + ")";
        }
        add(kIssues, "duplicates", line);
    }

    std::vector<std::string> unusedLines;
    for (const auto& r : t.reports) {
        if (std::find(entries.begin(), entries.end(), r.rel) != entries.end()) continue;
        std::string names;
        for (const auto& n : r.exportedNames) {
            if (t.allImported.contains(n)) continue;
            names += (names.empty() ? "" : ", ") + n;
        }
        if (!names.empty()) unusedLines.push_back(r.rel + "(" + names + ")");
    }
    if (!unusedLines.empty()) {
        std::string line = "unused exports:";
        for (std::size_t i = 0; i < unusedLines.size() && i < 10; ++i) line += " " + unusedLines[i];
        add(kDead, "unused", line);
    }
    if (!unimported.empty()) {
        std::string line = "orphans:";
        for (std::size_t i = 0; i < unimported.size() && i < 12; ++i) line += " " + unimported[i];
        add(kDead, "orphans", line);
    }

    std::map<std::string, std::size_t> builtins;
    std::map<std::string, std::size_t> externals;
    for (const auto& [name, count] : t.imports) (isBuiltin(name) ? builtins : externals).emplace(name, count);
    if (!builtins.empty()) add(kDeps, "builtins", "builtins: " + joinCounted(builtins, 40));
    if (!externals.empty()) add(kDeps, "external", "external: " + joinCounted(externals, 40));
    std::string unusedDeps;
    for (const auto& d : p.deps) {
        if (!t.imports.contains(d)) unusedDeps += (unusedDeps.empty() ? "" : ", ") + d;
    }
    if (!unusedDeps.empty()) add(kDeps, "unused deps", "unused deps: " + unusedDeps);

    if (!t.envs.empty()) add(kIo, "env", "env: " + joinCounted(t.envs, 40));
    if (!t.urls.empty()) add(kIo, "urls", "urls: " + joinCounted(t.urls, 15));
    if (t.sql > 0) add(kIo, "storage", "storage: sql×" + std::to_string(t.sql));
    for (const auto& c : t.calls) add(kCalls, "calls", c);

    std::vector<std::pair<std::string_view, LangTally>> langs(t.langs.begin(), t.langs.end());
    std::sort(langs.begin(), langs.end(), [](const auto& a, const auto& b) { return a.second.lines > b.second.lines; });
    std::string langLine = "langs:";
    for (const auto& [name, tally] : langs) langLine += " " + std::string(name) + " " + std::to_string(tally.files) + "f/" + std::to_string(tally.lines) + "L,";
    if (!langs.empty()) langLine.pop_back();
    add(kMetrics, "langs", langLine);

    std::vector<const FileReport*> byLines;
    for (const auto& r : t.reports) byLines.push_back(&r);
    std::sort(byLines.begin(), byLines.end(), [](const auto* a, const auto* b) { return a->lines > b->lines; });
    std::string big = "big:";
    for (std::size_t i = 0; i < byLines.size() && i < 8; ++i) big += " " + byLines[i]->rel + "(" + std::to_string(byLines[i]->lines) + ")";
    add(kMetrics, "big", big);

    std::vector<std::pair<const FileReport*, const FnRecord*>> longFns;
    for (const auto& r : t.reports)
        for (const auto& f : r.fns) longFns.emplace_back(&r, &f);
    std::sort(longFns.begin(), longFns.end(), [](const auto& a, const auto& b) { return a.second->length > b.second->length; });
    if (!longFns.empty()) {
        std::string longLine = "longfn(>=" + std::to_string(kLongFn) + "):";
        for (std::size_t i = 0; i < longFns.size() && i < 12; ++i) {
            const auto& [r, f] = longFns[i];
            longLine += " " + r->rel + ":" + std::to_string(f->line) + " " + f->name + "(" + std::to_string(f->length) + ")";
        }
        add(kMetrics, "longfn", longLine);
    }

    std::vector<const FileReport*> branchy = byLines;
    std::sort(branchy.begin(), branchy.end(), [](const auto* a, const auto* b) { return a->complexity > b->complexity; });
    std::string branchLine = "branchy:";
    for (std::size_t i = 0; i < branchy.size() && i < 8 && branchy[i]->complexity > 0; ++i) {
        branchLine += " " + branchy[i]->rel + "(" + std::to_string(branchy[i]->complexity) + ")";
    }
    add(kMetrics, "branchy", branchLine);
    add(kMetrics, "functions", "functions: " + std::to_string(t.functions));
    add(kMetrics, "graph", "graph: edges=" + std::to_string(t.edges) + " unresolved=" + std::to_string(t.unresolved) +
                              " unimported=" + std::to_string(unimported.size()) + " cycles=" + std::to_string(t.cycles.size()));
    if (t.functions > 0) {
        const std::size_t tenths = (t.branches * 10 + t.functions / 2) / t.functions;
        add(kMetrics, "cx", "cx: " + std::to_string(tenths / 10) + "." + std::to_string(tenths % 10) + " (branches " + std::to_string(t.branches) +
                                " / functions " + std::to_string(t.functions) + ")");
    }
    add(kMetrics, "patterns", "patterns: async=" + std::to_string(t.asyncs) + " await=" + std::to_string(t.awaits) +
                                  " promise=" + std::to_string(t.promises) + " then/catch=" + std::to_string(t.thenCatch) +
                                  " try=" + std::to_string(t.tries) + " throw=" + std::to_string(t.throws) +
                                  " complexity=" + std::to_string(t.complexity) + " exports=" + std::to_string(t.exports));

    std::string out = head + "\n" + stats + "\n";
    for (std::size_t s = 0; s < kSecCount; ++s) {
        if (sections[s].empty()) continue;
        out += "## " + std::string(kTitles[s]) + "\n";
        for (const auto& line : sections[s]) out += line + "\n";
    }
    if (!dropped.empty()) {
        out += "dropped (budget): " + std::to_string(dropped.size()) + " sections\n";
    }
    out += kLegend;
    return out;
}

}

std::optional<std::size_t> parsePositive(std::string_view s) {
    if (s.empty()) return std::nullopt;
    std::size_t v = 0;
    for (const char c : s) {
        if (c < '0' || c > '9') return std::nullopt;
        const auto digit = static_cast<std::size_t>(c - '0');
        if (v > (std::numeric_limits<std::size_t>::max() - digit) / 10) return std::nullopt;
        v = v * 10 + digit;
    }
    if (v == 0) return std::nullopt;
    return v;
}

int main(int argc, char** argv) {
    std::string target = ".";
    std::size_t budgetChars = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--budget") {
            if (i + 1 >= argc) {
                std::cerr << "thorns: --budget needs a value\n";
                return 2;
            }
            const auto parsed = parsePositive(argv[++i]);
            if (!parsed) {
                std::cerr << "thorns: --budget must be a positive integer (characters)\n";
                return 2;
            }
            budgetChars = static_cast<std::size_t>(*parsed);
        } else if (arg == "-h" || arg == "--help") {
            std::cout << "usage: thorns [path] [--budget chars]\n";
            return 0;
        } else {
            target = arg;
        }
    }
    std::error_code ec;
    const fs::path root = fs::absolute(target, ec).lexically_normal();
    if (ec || !fs::is_directory(root, ec)) {
        std::cerr << "thorns: not a directory: " << target << "\n";
        return 1;
    }
    Totals totals = walk(root);
    buildGraph(totals, root);
    buildCalls(totals);
    findCycles(totals);
    const Project project = readProject(root);
    std::cout << render(totals, project, root.string(), budgetChars);
    return 0;
}
