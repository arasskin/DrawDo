#include "db_smoke.h"

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>

int smoke_main(const char* db_dir);       // aelmdb_smoke.cpp
int store_test_main(const char* db_dir);  // store_test.cpp
int sync_test_main(const char* base_dir); // sync/sync_test.cpp
int auth_test_main(const char* base_dir); // sync/auth_test.cpp
int engine_test_main(const char* unused);  // engine/engine_test.cpp
int glue_test_main(const char* dir);       // engine/glue_test.cpp

const char* db_smoke_run(void) {
    static std::string status;
    const char* home = getenv("HOME");  // app sandbox container on iOS
    if (!home) {
        status = "FAIL: no HOME";
        return status.c_str();
    }
    try {
        int rc = smoke_main((std::string(home) + "/tmp/aelmdb-smoke-db").c_str());
        if (rc == 0) rc = store_test_main((std::string(home) + "/tmp/store-test-db").c_str());
        if (rc == 0) rc = sync_test_main((std::string(home) + "/tmp/sync-test").c_str());
        if (rc == 0) rc = auth_test_main((std::string(home) + "/tmp/auth-test").c_str());
        if (rc == 0) rc = engine_test_main("");
        if (rc == 0) rc = glue_test_main((std::string(home) + "/tmp/glue-test").c_str());
        status = (rc == 0) ? "PASS" : "FAIL: rc=" + std::to_string(rc);
    } catch (const std::exception& e) {
        status = std::string("FAIL: ") + e.what();
    }
    std::string result_path = std::string(home) + "/Documents/db_smoke_result.txt";
    if (FILE* f = fopen(result_path.c_str(), "w")) {
        fputs(status.c_str(), f);
        fclose(f);
    }
    return status.c_str();
}
