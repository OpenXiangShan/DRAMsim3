#ifndef __MEMORY_SYSTEM__H
#define __MEMORY_SYSTEM__H

#include <fstream>
#include <vector>
#include <functional>
#include <list>
#include "common.h"
#include "configuration.h"
#include "timing.h"
#include "statistics.h"
#include "controller.h"

namespace dramcore {

class MemorySystem {
public:
    MemorySystem(const std::string &config_file, std::function<void(uint64_t)> callback);
    bool InsertReq(uint64_t req_id, uint64_t hex_addr, bool is_write);
    void ClockTick();
    void PrintStats();
    std::function<void(uint64_t req_id)> callback_;
    std::vector<Controller*> ctrls_;
    Config* ptr_config_;

    int numb_buffered_requests;
private:
    uint64_t clk_;
    uint64_t id_;
    Timing* ptr_timing_;
    Statistics* ptr_stats_;
    std::vector<Request*> buffer_q_;
};

}

#endif