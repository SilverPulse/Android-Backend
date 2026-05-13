#pragma once
#include <vector>
#include <string>
#include <mutex>
#include <map>
#include <atomic>

struct GPSPoint {
    float lat, lon, alt;
    long long timestamp;
    double rsrp = 0.0;
    double rsrq = 0.0;
    double rssi = 0.0;
    double altitude = 0.0;
};

struct PciData {
    std::vector<double> time_data;
    std::vector<double> rsrp_data;
};

struct AppState {
    std::vector<GPSPoint> points;
    std::mutex points_mutex;

    std::atomic<bool> running{true};

    std::string status = "Waiting for connection...";
    std::mutex status_mutex;

    std::map<int, PciData> pci_data;
    std::vector<double> pci_history_time;
    std::vector<double> pci_history_value;

    int current_pci = 0;
    double current_rsrp = 0.0;

    std::vector<double> log_lons;
    std::vector<double> log_lats;
    long long start_time = 0;

    void SetStatus(const std::string& new_status) {
        std::lock_guard<std::mutex> lock(status_mutex);
        status = new_status;
    }

    std::string GetStatus() {
        std::lock_guard<std::mutex> lock(status_mutex);
        return status;
    }
};