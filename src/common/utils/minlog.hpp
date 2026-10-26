#pragma once
#include <fstream>
#include <filesystem>
#include <iostream>
#include <mutex>

class MinLog
{
private:
    std::ofstream OutputStream;
    bool _isOpen = false;
    std::mutex OutputMutex;

public:
    MinLog() { }

    bool Open(const std::filesystem::path& file_path, std::string* error_message = nullptr)
    {
        std::lock_guard<std::mutex> lock(OutputMutex);
        if (_isOpen) return true;
        std::error_code error;
        std::filesystem::create_directories(file_path.parent_path(), error);
        if (error)
        {
            if (error_message)
                *error_message = "Failed to create log directory: " + error.message();
            return false;
        }
        MinLog::OutputStream.open(file_path, std::ios::out | std::ios::trunc);
        _isOpen = OutputStream.is_open();
        if (!_isOpen)
        {
            if (error_message)
                *error_message = "Failed to open log file: " + file_path.string();
        }
        return _isOpen;
    }

    void Write(const char* value)
    {
        std::lock_guard<std::mutex> lock(OutputMutex);
        MinLog::OutputStream << value;
        std::cout << value;
    }

    void WriteLine(const char* value)
    {
        std::lock_guard<std::mutex> lock(OutputMutex);
        MinLog::OutputStream << value << std::endl;
        std::cout << value << std::endl;
    }

    void WriteDebug(const char* value)
    {
#ifdef DEBUG
        std::lock_guard<std::mutex> lock(OutputMutex);
        MinLog::OutputStream << value << std::endl;
        std::cout << value << std::endl;
#endif
    }
};
