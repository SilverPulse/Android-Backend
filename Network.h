#pragma once
#include "AppState.h"
#include "Database.h"

class Network {
public:
    static void RunServer(AppState& state, Database& db);
};