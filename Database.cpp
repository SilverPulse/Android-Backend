#include "Database.h"
#include <fstream>
#include <iostream>

using json = nlohmann::json;

Database::Database(const std::string& connection_string) {
    conn = PQconnectdb(connection_string.c_str());
}

Database::~Database() {
    if (conn) {
        PQfinish(conn);
    }
}

bool Database::IsConnected() const {
    return conn && PQstatus(conn) == CONNECTION_OK;
}

std::string Database::GetErrorMessage() const {
    return conn ? PQerrorMessage(conn) : "No connection object";
}

template <typename T>
std::string Database::GetVal(const json& j, const std::string& key) {
    if (!j.contains(key) || j[key].is_null()) return "NULL";
    try {
        if constexpr (std::is_same_v<T, std::string>) {
            if (j[key].is_string()) return "'" + j[key].get<std::string>() + "'";
            if (j[key].is_number()) return "'" + std::to_string(j[key].get<int>()) + "'";
        } else {
            return std::to_string(j[key].get<T>());
        }
    } catch (...) {}
    return "NULL";
}

std::string Database::BuildInsertSQL(const json& j) {
    if (!j.contains("location")) return "";
    auto& loc = j["location"];
    long long time = j.value("timestamp", 0LL);
    if (time == 0) return "";

    std::string sql = "INSERT INTO full_telemetry (ts, lat, lon, alt, accuracy, "
                      "lte_pci, lte_earfcn, lte_ci, lte_tac, lte_band, lte_rsrp, lte_rsrq, lte_rssi, lte_rssnr, lte_cqi, lte_asu_level, lte_timing_advance, "
                      "gsm_ci, gsm_bsic, gsm_arfcn, gsm_lac, gsm_dbm, gsm_rssi, gsm_timing_advance, total_bytes_device) VALUES (";

    sql += std::to_string(time) + ", " + std::to_string(loc.value("lat", 0.0)) + ", " +
           std::to_string(loc.value("lon", 0.0)) + ", " + GetVal<double>(loc, "alt") + ", " + GetVal<double>(loc, "accuracy") + ", ";

    if (j.contains("telephony") && j["telephony"].contains("lte")) {
        auto& lte = j["telephony"]["lte"];
        sql += GetVal<int>(lte, "pci") + ", " + GetVal<int>(lte, "earfcn") + ", " + GetVal<long long>(lte, "ci") + ", " +
               GetVal<int>(lte, "tac") + ", " + GetVal<std::string>(lte, "band") + ", " + GetVal<int>(lte, "rsrp") + ", " +
               GetVal<int>(lte, "rsrq") + ", " + GetVal<int>(lte, "rssi") + ", " + GetVal<long long>(lte, "rssnr") + ", " +
               GetVal<long long>(lte, "cqi") + ", " + GetVal<int>(lte, "asu_level") + ", " + GetVal<long long>(lte, "timing_advance") + ", ";
    } else {
        sql += "NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, ";
    }

    if (j.contains("telephony") && j["telephony"].contains("gsm")) {
        auto& gsm = j["telephony"]["gsm"];
        sql += GetVal<long long>(gsm, "ci") + ", " + GetVal<int>(gsm, "bsic") + ", " + GetVal<int>(gsm, "arfcn") + ", " +
               GetVal<long long>(gsm, "lac") + ", " + GetVal<int>(gsm, "dbm") + ", " + GetVal<int>(gsm, "rssi") + ", " +
               GetVal<long long>(gsm, "timing_advance") + ", ";
    } else {
        sql += "NULL, NULL, NULL, NULL, NULL, NULL, NULL, ";
    }

    if (j.contains("network_stats")) {
        sql += GetVal<long long>(j["network_stats"], "total_bytes_device");
    } else {
        sql += "NULL";
    }

    sql += ") ON CONFLICT (ts) DO NOTHING;";
    return sql;
}

void Database::InsertTelemetry(const json& j) {
    if (!IsConnected()) return;
    std::string query = BuildInsertSQL(j);
    if (!query.empty()) {
        std::lock_guard<std::mutex> db_lock(db_mutex);
        PGresult* res_db = PQexec(conn, query.c_str());
        PQclear(res_db);
    }
}

void Database::ImportJsonToDB(const std::string& filename, AppState& state) {
    if (!IsConnected()) {
        state.SetStatus("Cannot import: No DB connection");
        return;
    }

    std::ifstream file(filename);
    if (!file.is_open()) return;

    std::string line;
    int imported_count = 0;
    state.SetStatus("Importing ALL data to DB... Please wait.");

    while (std::getline(file, line)) {
        if (line.empty()) continue;
        try {
            auto j = json::parse(line);
            if (j.contains("type") && j["type"] == "telemetry_update") {
                std::string query = BuildInsertSQL(j);
                if (query.empty()) continue;

                std::lock_guard<std::mutex> db_lock(db_mutex);
                PGresult* res_db = PQexec(conn, query.c_str());

                if (atof(PQcmdTuples(res_db)) > 0) {
                    imported_count++;
                }
                PQclear(res_db);
            }
        } catch (...) {}
    }
    state.SetStatus("Import finished! Inserted " + std::to_string(imported_count) + " FULL points.");
}

void Database::LoadTrackFromDB(int step, AppState& state) {
    if (!IsConnected()) {
        state.SetStatus("Error: No DB connection");
        return;
    }

    {
        std::lock_guard<std::mutex> lock(state.points_mutex);
        state.points.clear();
        state.pci_data.clear();
        state.pci_history_time.clear();
        state.pci_history_value.clear();
        state.log_lats.clear();
        state.log_lons.clear();
        state.start_time = 0;
    }

    std::lock_guard<std::mutex> db_lock(db_mutex);
    PGresult* res = PQexec(conn, "SELECT ts, lat, lon, lte_pci, lte_rsrp FROM full_telemetry ORDER BY ts ASC;");

    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        state.SetStatus("DB Select Error: " + std::string(PQerrorMessage(conn)));
        PQclear(res);
        return;
    }

    int rows = PQntuples(res);
    for (int i = 0; i < rows; i += step) {
        long long ts = atoll(PQgetvalue(res, i, 0));
        double lat = atof(PQgetvalue(res, i, 1));
        double lon = atof(PQgetvalue(res, i, 2));
        int pci = atoi(PQgetvalue(res, i, 3));
        int rsrp = atoi(PQgetvalue(res, i, 4));

        if (state.start_time == 0) state.start_time = ts;
        double time_sec = (ts - state.start_time) / 1000.0;

        std::lock_guard<std::mutex> lock(state.points_mutex);
        state.points.push_back({(float)lat, (float)lon, 0.0f, ts});
        state.log_lats.push_back(lat);
        state.log_lons.push_back(lon);

        if (pci != 0) {
            state.pci_data[pci].time_data.push_back(time_sec);
            state.pci_data[pci].rsrp_data.push_back((double)rsrp);
            state.pci_history_time.push_back(time_sec);
            state.pci_history_value.push_back((double)pci);
            state.current_pci = pci;
            state.current_rsrp = (double)rsrp;
        }
    }
    PQclear(res);
    state.SetStatus("Loaded " + std::to_string(rows) + " points with Telemetry from DB!");
}