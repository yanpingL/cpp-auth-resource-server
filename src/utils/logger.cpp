#include "logger.h"
#include <iostream>
#include <ctime>

// Returns the shared logger instance.
Logger* Logger::get_instance() {
    static Logger instance;
    return &instance;
}

// Opens the log file in append mode.
void Logger::init(const std::string& filename) {
    log_file.open(filename, std::ios::app);
}

// Closes the log file if it is open.
Logger::~Logger() {
    if (log_file.is_open()) {
        log_file.close();
    }
}

// Formats the current local time for log entries.
std::string get_time() {
    time_t now = time(nullptr);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", localtime(&now));
    return std::string(buf);
}

// Converts a log level enum into display text.
std::string level_to_string(LogLevel level) {
    switch(level) {
        case INFO: return "INFO";
        case ERROR: return "ERROR";
        case DEBUG: return "DEBUG";
        default: return "UNKNOWN";
    }
}

// Writes one log line to the file and console.
void Logger::log(LogLevel level, const std::string& message) {
    std::lock_guard<std::mutex> lock(mtx);

    std::string log_msg =
        "[" + get_time() + "] [" +
        level_to_string(level) + "] " +
        message;

    if (log_file.is_open()) {
        log_file << log_msg << std::endl;
    }

    std::cout << log_msg << std::endl;
}
