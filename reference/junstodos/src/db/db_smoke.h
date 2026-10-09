#pragma once
// Phase 0 validation: runs the AELMDB + negentropy smoke test inside the app
// sandbox and reports the result. Temporary — replaced by the real store
// module once the sync schema lands.

// Runs the test, writes PASS/FAIL to Documents/db_smoke_result.txt in the
// app container, and returns a short status string (static storage).
const char* db_smoke_run(void);
