#pragma once

#include <cstdlib>
#include <string>

namespace eac {

struct AppConfig {
    // Database
    std::string dbType = "postgres";
    std::string dbHost = "postgres";
    int dbPort = 5432;
    std::string dbName = "localpkd";
    std::string dbUser = "pkd";
    std::string dbPassword;

    // Oracle
    std::string oracleHost = "oracle";
    int oraclePort = 1521;
    std::string oracleServiceName = "XEPDB1";
    std::string oracleUser = "pkd_user";
    std::string oraclePassword;

    // Server
    int serverPort = 8086;
    int threadNum = 8;

    static AppConfig fromEnv() {
        AppConfig cfg;
        auto getEnv = [](const char* name, const char* def) -> std::string {
            const char* val = std::getenv(name);
            return val ? val : def;
        };
        auto getEnvInt = [&getEnv](const char* name, int def) -> int {
            try { return std::stoi(getEnv(name, std::to_string(def).c_str())); }
            catch (...) { return def; }
        };

        cfg.dbType = getEnv("DB_TYPE", "postgres");
        cfg.dbHost = getEnv("DB_HOST", "postgres");
        cfg.dbPort = getEnvInt("DB_PORT", 5432);
        cfg.dbName = getEnv("DB_NAME", "localpkd");
        cfg.dbUser = getEnv("DB_USER", "pkd");
        cfg.dbPassword = getEnv("DB_PASSWORD", "");

        cfg.oracleHost = getEnv("ORACLE_HOST", "oracle");
        cfg.oraclePort = getEnvInt("ORACLE_PORT", 1521);
        cfg.oracleServiceName = getEnv("ORACLE_SERVICE_NAME", "XEPDB1");
        cfg.oracleUser = getEnv("ORACLE_USER", "pkd_user");
        cfg.oraclePassword = getEnv("ORACLE_PASSWORD", "");

        cfg.serverPort = getEnvInt("SERVER_PORT", 8086);
        cfg.threadNum = std::max(1, std::min(256, getEnvInt("THREAD_NUM", 8)));

        return cfg;
    }
};

} // namespace eac
