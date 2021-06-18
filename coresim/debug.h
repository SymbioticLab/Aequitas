#ifndef CORESIM_DEBUG_H
#define CORESIM_DEBUG_H

#include <stdint.h>

bool debug_flow(uint32_t fid);
//bool debug_queue(uint32_t fid);
//bool debug_host(uint32_t fid);
//bool debug();
bool print_flow_result();
#endif  // CORESIM_DEBUG_H
