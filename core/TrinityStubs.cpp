// Minimal stub implementations for TrinityCore symbols that our simplified build references.
// These are placeholders to satisfy linker dependencies when using isolated portions of TrinityCore
// (Collision / Recast code) without pulling the entire TrinityCore common library.
// The implementations are NO-OP and should be replaced by real ones if full TrinityCore functionality is needed.

#include <string>
#include <string_view>
#include <vector>
#include <mutex>
#include <stdexcept>
#include <time.h>
#include <cstddef>
#include <fmt/core.h>

// ---------------- Trinity:: Utilities -----------------
namespace Trinity {

// Very simple tokenization helper (copy from StringUtilities)
inline std::vector<std::string_view> Tokenize(std::string_view str, char sep, bool keepEmpty)
{
    std::vector<std::string_view> out;
    size_t begin = 0;
    while (begin <= str.size())
    {
        size_t end = str.find(sep, begin);
        if (end == std::string_view::npos)
            end = str.size();

        if (end > begin || keepEmpty)
            out.push_back(str.substr(begin, end - begin));

        begin = end + 1;
    }
    return out;
}

inline bool StringEqualI(std::string_view s1, std::string_view s2) 
{
    return std::equal(s1.begin(), s1.end(), s2.begin(), s2.end(), [](char a, char b) {
        return tolower(a) == tolower(b);
    });
}

// (no simple Assert/Abort stubs here; see comprehensive overloads later)

} // namespace Trinity

// ---------------- Platform Specifics ------------------
#if defined(_MSC_VER)
bool WriteWinConsole(std::string_view, bool) { return true; }
#endif

// ---------------- Stub Log --------------------------
enum LogLevel { };
class Log
{
public:
    static Log* instance();
    bool ShouldLog(const std::string&, LogLevel) const;
    template<typename... Args>
    void OutMessage(std::string_view, LogLevel, const char*, Args&&...) {}
private:
    void OutMessageImpl(std::string_view, LogLevel, fmt::string_view, fmt::format_args);
};

// ----------- Out-of-class definitions (non-inline) -----------------
Log* Log::instance() {
    static Log s;
    return &s;
}

bool Log::ShouldLog(const std::string&, LogLevel) const {
    return false;
}

void Log::OutMessageImpl(std::string_view, LogLevel, fmt::string_view, fmt::format_args) {}

// ---------------- Stub Metric ---------------------------
class Metric
{
public:
    static Metric* instance();
    void LogEvent(std::string, std::string, std::string);
};

// Metric non-inline definitions
Metric* Metric::instance() {
    static Metric s;
    return &s;
}

void Metric::LogEvent(std::string, std::string, std::string) {}

// ---------------- Stub localtime_r ----------------------
#if defined(_MSC_VER)
struct tm* localtime_r(const time_t* time, struct tm* result)
{
    if (localtime_s(result, time) == 0)
        return result;
    
    // Clear the structure on error, although localtime_s should already do this.
    result->tm_sec = result->tm_min = result->tm_hour = 0;
    result->tm_mday = result->tm_mon = result->tm_year = 0;
    result->tm_wday = result->tm_yday = result->tm_isdst = 0;
    return nullptr;
}
#endif

// ---------------- Debug Info -------------------------
// (no simple GetDebugInfo stub here; see comprehensive overloads later)

// ---------------- Trinity Assert / Abort and DebugInfo implementations ----------

namespace Trinity {
void Assert(char const*, int, char const*, std::string, char const*) {}
void Abort(char const*, int, char const*) { std::terminate(); }
void Abort(char const*, int, char const*, char const*, ...) { std::terminate(); }
}

// Global GetDebugInfo returning std::string
std::string GetDebugInfo() { return {}; } 