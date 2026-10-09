#pragma once
// Track-A network demo: seeds a throwaway store and mirrors it to the dev
// server over real HTTP on a background thread. Writes the outcome to
// Documents/net_result.txt and stderr. Temporary — replaced by the real
// lifecycle wiring once the memory DB lands.

void net_demo_start(void);
