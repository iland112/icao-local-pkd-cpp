/**
 * @file logger.h
 * @brief Structured Logging Wrapper
 *
 * Provides consistent logging interface across all services
 * Wraps spdlog with standardized configuration
 *
 * @author SmartCore Inc.
 * @date 2026-02-04
 */

#pragma once

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <string>
#include <memory>

namespace common {

/**
 * @brief Logger initialization and configuration
 */
class Logger {
public:
    /**
     * @brief Initialize logger for service
     * @param serviceName Service name (e.g., "pkd-management")
     * @param logLevel Log level (trace, debug, info, warn, error, critical)
     * @param logToFile Enable file logging
     * @param logFile Log file path
     */
    static void initialize(
        const std::string& serviceName,
        const std::string& logLevel = "info",
        bool logToFile = false,
        const std::string& logFile = ""
    ) {
        try {
            std::vector<spdlog::sink_ptr> sinks;

            // Console sink (colored)
            auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            consoleSink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] %v");
            sinks.push_back(consoleSink);

            // File sink (if enabled)
            if (logToFile && !logFile.empty()) {
                auto fileSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                    logFile, 1024 * 1024 * 10, 3  // 10MB, 3 files
                );
                fileSink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%l] %v");
                sinks.push_back(fileSink);
            }

            // Create logger
            auto logger = std::make_shared<spdlog::logger>(serviceName, sinks.begin(), sinks.end());

            // Set log level
            if (logLevel == "trace") {
                logger->set_level(spdlog::level::trace);
            } else if (logLevel == "debug") {
                logger->set_level(spdlog::level::debug);
            } else if (logLevel == "info") {
                logger->set_level(spdlog::level::info);
            } else if (logLevel == "warn") {
                logger->set_level(spdlog::level::warn);
            } else if (logLevel == "error") {
                logger->set_level(spdlog::level::err);
            } else if (logLevel == "critical") {
                logger->set_level(spdlog::level::critical);
            } else {
                logger->set_level(spdlog::level::info);
            }

            // Set as default logger
            spdlog::set_default_logger(logger);
            spdlog::flush_on(spdlog::level::warn);

            spdlog::info("Logger initialized: service={}, level={}, file={}",
                        serviceName, logLevel, logToFile ? logFile : "none");

        } catch (const spdlog::spdlog_ex& ex) {
            std::cerr << "Logger initialization failed: " << ex.what() << std::endl;
        }
    }

    /**
     * @brief Set log level at runtime
     */
    static void setLevel(const std::string& level) {
        if (level == "trace") {
            spdlog::set_level(spdlog::level::trace);
        } else if (level == "debug") {
            spdlog::set_level(spdlog::level::debug);
        } else if (level == "info") {
            spdlog::set_level(spdlog::level::info);
        } else if (level == "warn") {
            spdlog::set_level(spdlog::level::warn);
        } else if (level == "error") {
            spdlog::set_level(spdlog::level::err);
        } else if (level == "critical") {
            spdlog::set_level(spdlog::level::critical);
        }

        spdlog::info("Log level changed to: {}", level);
    }

    /**
     * @brief Flush all loggers
     */
    static void flush() {
        spdlog::default_logger()->flush();
    }
};

} // namespace common
