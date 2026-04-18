#pragma once

#include <chrono>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <string>
#include <string_view>

namespace lob {

enum class LogLevel : int { DEBUG = 0, INFO = 1, WARN = 2, ERROR = 3 };

/// Thread-safe singleton logger that writes to stderr.
/// All public methods are safe to call concurrently from multiple threads.
class Logger {
public:
    static Logger& instance() {
        static Logger inst;
        return inst;
    }

    void set_level(LogLevel lvl) noexcept { level_ = lvl; }
    [[nodiscard]] LogLevel level() const noexcept { return level_; }

    void log(LogLevel lvl, std::string_view component, std::string_view msg) {
        if (lvl < level_) return;

        auto now   = std::chrono::system_clock::now();
        auto now_t = std::chrono::system_clock::to_time_t(now);
        auto ms    = std::chrono::duration_cast<std::chrono::milliseconds>(
                         now.time_since_epoch()) %
                     1000;

        char buf[32]{};
#ifdef _WIN32
        struct tm tm_buf{};
        localtime_s(&tm_buf, &now_t);
        std::strftime(buf, sizeof(buf), "%H:%M:%S", &tm_buf);
#else
        struct tm tm_buf{};
        localtime_r(&now_t, &tm_buf);
        std::strftime(buf, sizeof(buf), "%H:%M:%S", &tm_buf);
#endif

        const char* lvl_str = "INFO ";
        switch (lvl) {
            case LogLevel::DEBUG: lvl_str = "DEBUG"; break;
            case LogLevel::INFO:  lvl_str = "INFO "; break;
            case LogLevel::WARN:  lvl_str = "WARN "; break;
            case LogLevel::ERROR: lvl_str = "ERROR"; break;
        }

        std::lock_guard<std::mutex> lk(mu_);
        std::fprintf(stderr, "[%s.%03lld][%s][%.*s] %.*s\n",
                     buf,
                     static_cast<long long>(ms.count()),
                     lvl_str,
                     static_cast<int>(component.size()), component.data(),
                     static_cast<int>(msg.size()),       msg.data());
    }

    /// Redirect output to a file.  Pass nullptr to revert to stderr.
    /// NOT thread-safe; call before spawning any logging threads.
    void set_output(std::FILE* fp) noexcept {
        out_ = fp ? fp : stderr;
    }

private:
    Logger() : out_(stderr) {}

    LogLevel   level_{LogLevel::INFO};
    std::mutex mu_;
    std::FILE* out_;
};

// ---------------------------------------------------------------------------
// Convenience macros
// ---------------------------------------------------------------------------
#define LOB_LOG(lvl, comp, msg) \
    ::lob::Logger::instance().log(::lob::LogLevel::lvl, comp, msg)

#define LOB_INFO(comp, msg)  LOB_LOG(INFO,  comp, msg)
#define LOB_WARN(comp, msg)  LOB_LOG(WARN,  comp, msg)
#define LOB_ERROR(comp, msg) LOB_LOG(ERROR, comp, msg)
#define LOB_DEBUG(comp, msg) LOB_LOG(DEBUG, comp, msg)

} // namespace lob
