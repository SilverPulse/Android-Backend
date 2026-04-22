#pragma once
#include <string>
#include <mutex>
#include <nlohmann/json.hpp>
#include <libpq-fe.h>
#include "AppState.h"

class Database {
public:
    Database(const std::string& connection_string);
    ~Database();

    bool IsConnected() const;
    std::string GetErrorMessage() const;

    void InsertTelemetry(const nlohmann::json& j);
    void ImportJsonToDB(const std::string& filename, AppState& state);
    void LoadTrackFromDB(int step, AppState& state);

private:
    std::string BuildInsertSQL(const nlohmann::json& j);

    template <typename T>
    std::string GetVal(const nlohmann::json& j, const std::string& key);

    PGconn* conn = nullptr;
    std::mutex db_mutex;
};