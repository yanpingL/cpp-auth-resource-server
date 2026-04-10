#include "logger.h"
#include <iostream>
#include <ctime>

Logger* Logger::get_instance() {
    static Logger instance;
    return &instance;
}

void Logger::init(const std::string& filename) {
    log_file.open(filename, std::ios::app);
}

Logger::~Logger() {
    if (log_file.is_open()) {
        log_file.close();
    }
}

std::string get_time() {
    time_t now = time(nullptr);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", localtime(&now));
    return std::string(buf);
}

std::string level_to_string(LogLevel level) {
    switch(level) {
        case INFO: return "INFO";
        case ERROR: return "ERROR";
        case DEBUG: return "DEBUG";
        default: return "UNKNOWN";
    }
}

void Logger::log(LogLevel level, const std::string& message) {
    std::lock_guard<std::mutex> lock(mtx);

    std::string log_msg =
        "[" + get_time() + "] [" +
        level_to_string(level) + "] " +
        message;

    // 写文件
    if (log_file.is_open()) {
        log_file << log_msg << std::endl;
    }

    // 控制台（可选）
    std::cout << log_msg << std::endl;
}