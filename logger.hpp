#pragma once

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>
#include <sstream>
#include <chrono>
#include <iomanip>
#include <windows.h>
#include <atomic>
#include <mutex>

#pragma comment(lib, "ws2_32.lib")

class Logger {
public:
    enum class Level {
        DEBUG,
        INFO,
        WARNING,
        ERROR
    };

    class LogStream {
    public:
        LogStream(Level level, Logger* logger)
            : level_(level), logger_(logger) {}

        ~LogStream() {
            if (!message_.str().empty()) {
                logger_->send(level_, message_.str());
            }
        }

        template<typename T>
        LogStream& operator<<(const T& value) {
            message_ << value;
            return *this;
        }

        // 스트림 조작자 지원 (std::endl 등)
        LogStream& operator<<(std::ostream& (*manipulator)(std::ostream&)) {
            manipulator(message_);
            return *this;
        }

    private:
        Level level_;
        Logger* logger_;
        std::ostringstream message_;
    };

public:
    static Logger& getInstance() {
        static Logger instance;
        return instance;
    }

    // 설정 (선택적, 호출하지 않으면 기본값 사용)
    void config(const std::string& appName = "Default",
                const std::string& serverIp = "127.0.0.1",
                int port = 9990) {
        std::lock_guard<std::mutex> lock(configMutex_);

        if (configured_) {
            // 이미 설정되어 있으면 무시 (또는 재설정)
            return;
        }

        appName_ = appName;
        serverIp_ = serverIp;
        port_ = port;
        configured_ = true;
    }

    // 로그 레벨별 스트림 생성
    LogStream debug() {
        ensureInitialized();
        return LogStream(Level::DEBUG, this);
    }

    LogStream info() {
        ensureInitialized();
        return LogStream(Level::INFO, this);
    }

    LogStream warning() {
        ensureInitialized();
        return LogStream(Level::WARNING, this);
    }

    LogStream error() {
        ensureInitialized();
        return LogStream(Level::ERROR, this);
    }

private:
    Logger()
        : configured_(false)
        , initialized_(false)
        , sock_(INVALID_SOCKET)
        , appName_("Default")
        , serverIp_("127.0.0.1")
        , port_(9990) {
        memset(&serverAddr_, 0, sizeof(serverAddr_));
    }

    ~Logger() {
        shutdown();
    }

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void ensureInitialized() {
        if (initialized_) return;

        std::lock_guard<std::mutex> lock(initMutex_);
        if (initialized_) return; // 더블 체크

        // Winsock 초기화
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            return; // 실패해도 조용히 처리
        }

        // UDP 소켓 생성
        sock_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sock_ == INVALID_SOCKET) {
            WSACleanup();
            return;
        }

        // 서버 주소 설정
        serverAddr_.sin_family = AF_INET;
        serverAddr_.sin_port = htons(port_);
        inet_pton(AF_INET, serverIp_.c_str(), &serverAddr_.sin_addr);

        initialized_ = true;
    }

    void shutdown() {
        if (!initialized_) return;

        if (sock_ != INVALID_SOCKET) {
            closesocket(sock_);
            sock_ = INVALID_SOCKET;
        }
        WSACleanup();
        initialized_ = false;
    }

    void send(Level level, const std::string& message) {
        if (!initialized_) return;

        std::string json = createJson(level, message);
        sendto(sock_, json.c_str(), (int)json.length(), 0,
               (sockaddr*)&serverAddr_, sizeof(serverAddr_));
    }

    std::string createJson(Level level, const std::string& msg) {
        std::string levelStr;
        switch (level) {
            case Level::DEBUG:   levelStr = "DEBUG"; break;
            case Level::INFO:    levelStr = "INFO"; break;
            case Level::WARNING: levelStr = "WARNING"; break;
            case Level::ERROR:   levelStr = "ERROR"; break;
        }

        // 현재 시간
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;

        std::tm tm;
        localtime_s(&tm, &time_t);

        std::ostringstream timestamp;
        timestamp << std::put_time(&tm, "%Y-%m-%d %H:%M:%S")
                 << '.' << std::setfill('0') << std::setw(3) << ms.count();

        // PID
        DWORD pid = GetCurrentProcessId();

        // JSON escape
        std::string escapedMsg = escapeJson(msg);

        // JSON 생성
        std::ostringstream json;
        json << "{"
             << "\"timestamp\":\"" << timestamp.str() << "\","
             << "\"app\":\"" << appName_ << "\","
             << "\"level\":\"" << levelStr << "\","
             << "\"message\":\"" << escapedMsg << "\","
             << "\"pid\":\"" << pid << "\","
             << "\"extra\":\"{}\""
             << "}";

        return json.str();
    }

    std::string escapeJson(const std::string& str) {
        std::ostringstream oss;
        for (char c : str) {
            switch (c) {
                case '\"': oss << "\\\""; break;
                case '\\': oss << "\\\\"; break;
                case '\b': oss << "\\b"; break;
                case '\f': oss << "\\f"; break;
                case '\n': oss << "\\n"; break;
                case '\r': oss << "\\r"; break;
                case '\t': oss << "\\t"; break;
                default:
                    if (c < 32) {
                        oss << "\\u" << std::hex << std::setw(4)
                            << std::setfill('0') << (int)c;
                    } else {
                        oss << c;
                    }
            }
        }
        return oss.str();
    }

    std::mutex configMutex_;
    std::mutex initMutex_;
    bool configured_;
    bool initialized_;
    std::string appName_;
    std::string serverIp_;
    int port_;
    SOCKET sock_;
    sockaddr_in serverAddr_;
};

// 편의를 위한 전역 접근자
namespace logger {
    // 설정 (선택적 - 호출하지 않으면 기본값 사용)
    inline void config(const std::string& appName = "Default",
                      const std::string& serverIp = "127.0.0.1",
                      int port = 9990) {
        Logger::getInstance().config(appName, serverIp, port);
    }

    inline Logger::LogStream debug() {
        return Logger::getInstance().debug();
    }

    inline Logger::LogStream info() {
        return Logger::getInstance().info();
    }

    inline Logger::LogStream warning() {
        return Logger::getInstance().warning();
    }

    inline Logger::LogStream error() {
        return Logger::getInstance().error();
    }
}
