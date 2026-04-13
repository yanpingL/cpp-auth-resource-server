#ifndef LOGGER_H
#define LOGGER_H

#include <string>
#include <fstream>
#include <mutex>

enum LogLevel {
    INFO,
    ERROR,
    DEBUG
};

class Logger {
public:
    static Logger* get_instance();

    void init(const std::string& filename);

    void log(LogLevel level, const std::string& message);

private:
    Logger() {}
    ~Logger();

    std::ofstream log_file;
    std::mutex mtx;
};

#endif