#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace spdlog {

namespace level {
enum level_enum { trace = 0, debug, info, warn, err, critical, off };
}

namespace detail {

inline std::atomic<level::level_enum>& global_level() {
    static std::atomic<level::level_enum> lvl{level::info};
    return lvl;
}

inline std::mutex& log_mutex() {
    static std::mutex mtx;
    return mtx;
}

inline std::string_view level_to_string(level::level_enum lvl) {
    switch (lvl) {
        case level::trace:
            return "TRACE";
        case level::debug:
            return "DEBUG";
        case level::info:
            return "INFO";
        case level::warn:
            return "WARN";
        case level::err:
            return "ERROR";
        case level::critical:
            return "CRITICAL";
        case level::off:
        default:
            return "OFF";
    }
}

inline std::string to_string(std::string_view value) { return std::string(value); }
inline std::string to_string(const std::string& value) { return value; }
inline std::string to_string(const char* value) { return value ? std::string(value) : std::string{}; }
inline std::string to_string(char* value) { return value ? std::string(value) : std::string{}; }

template <typename T>
std::string to_string(const T& value) {
    std::ostringstream oss;
    oss << value;
    return oss.str();
}

template <typename... Args>
std::string format(std::string_view fmt, Args&&... args) {
    std::array<std::string, sizeof...(Args)> formatted{to_string(std::forward<Args>(args))...};
    std::ostringstream out;
    size_t arg_index = 0;
    for (size_t i = 0; i < fmt.size(); ++i) {
        if (fmt[i] == '{' && i + 1 < fmt.size() && fmt[i + 1] == '}' && arg_index < formatted.size()) {
            out << formatted[arg_index++];
            ++i;
        } else {
            out << fmt[i];
        }
    }
    while (arg_index < formatted.size()) {
        out << ' ' << formatted[arg_index++];
    }
    return out.str();
}

inline std::ostream& stream_for_level(level::level_enum lvl) {
    if (lvl >= level::err) {
        return std::cerr;
    }
    return std::cout;
}

inline std::string timestamp() {
    using namespace std::chrono;
    const auto now = system_clock::now();
    const auto time = system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &time);
#else
    localtime_r(&time, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

template <typename... Args>
void log(level::level_enum lvl, std::string_view fmt, Args&&... args) {
    if (lvl < global_level().load()) {
        return;
    }

    const auto message = format(fmt, std::forward<Args>(args)...);
    std::ostringstream line;
    line << '[' << timestamp() << "] [" << level_to_string(lvl) << "] " << message;

    std::lock_guard<std::mutex> lock(log_mutex());
    stream_for_level(lvl) << line.str() << std::endl;
}

}  // namespace detail

inline void set_level(level::level_enum lvl) { detail::global_level().store(lvl); }

template <typename... Args>
void log(level::level_enum lvl, std::string_view fmt, Args&&... args) {
    detail::log(lvl, fmt, std::forward<Args>(args)...);
}

template <typename... Args>
void info(std::string_view fmt, Args&&... args) {
    log(level::info, fmt, std::forward<Args>(args)...);
}

template <typename... Args>
void debug(std::string_view fmt, Args&&... args) {
    log(level::debug, fmt, std::forward<Args>(args)...);
}

template <typename... Args>
void warn(std::string_view fmt, Args&&... args) {
    log(level::warn, fmt, std::forward<Args>(args)...);
}

template <typename... Args>
void error(std::string_view fmt, Args&&... args) {
    log(level::err, fmt, std::forward<Args>(args)...);
}

}  // namespace spdlog
