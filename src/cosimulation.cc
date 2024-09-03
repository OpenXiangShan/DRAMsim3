#include "cosimulation.h"
#include "memory_system.h"

#include "INIReader.h"

dramsim3::MemorySystem *memory = NULL;

ComplexCoDRAMsim3::ComplexCoDRAMsim3(const std::string &config_file, const std::string &output_dir, uint64_t padding_time) {
    if (memory) {
        std::cout << "should only init one memory currently" << std::endl;
        abort();
    }
    dram_clock = 0;
    auto reader = new INIReader(config_file);
    if (reader->ParseError() < 0) {
        std::cout << "Can't load " << config_file << std::endl;
        abort();
    }

    const auto protocol = reader->Get("dram_structure", "protocol", "DDR3");
    if (protocol == "DDR4" or protocol == "DDR5" or protocol == "DDR3") {
        long cpu_freq_i = reader->GetInteger("other", "cpu_freq", 0);
        long dram_freq_i = reader->GetInteger("other", "dram_freq", 0);
        if (cpu_freq_i == 0 || dram_freq_i == 0) {
            std::cout << "For cosim with xs, must specify CPU_FREQ and DRAM_FREQ in [other] section in the config file."
                << std::endl;
            abort();
        } else {
            CPU_FREQ = cpu_freq_i;
            DRAM_FREQ = dram_freq_i;
            CPU_CLOCK_CYCLE = 1.0 / CPU_FREQ;
            DRAM_CLOCK_CYCLE = 1.0 / DRAM_FREQ;
            std::cout << "CPU_FREQ: " << CPU_FREQ << " DRAM_FREQ: " << DRAM_FREQ << std::endl;
            std::cout << "CPU_CLOCK_CYCLE: " << CPU_CLOCK_CYCLE
                << " DRAM_CLOCK_CYCLE: " << DRAM_CLOCK_CYCLE << std::endl;
        }
    } else {
        std::cout << "Unsupported DRAM protocol for xs cosim: " << protocol << std::endl;
        abort();
    }

    memory = new dramsim3::MemorySystem(config_file, output_dir,
        std::bind(&ComplexCoDRAMsim3::callback, this, std::placeholders::_1, false),
        std::bind(&ComplexCoDRAMsim3::callback, this, std::placeholders::_1, true));
    padding = padding_time;
    std::cout << "DRAMsim3 memory system initialized." << std::endl;

    delete reader;
}

ComplexCoDRAMsim3::~ComplexCoDRAMsim3() {
    memory->PrintStats();
    delete memory;
    memory = NULL;
}

void ComplexCoDRAMsim3::tick() {
    cpu_time += CPU_CLOCK_CYCLE;
    dram_clock++;
    while (cpu_time >= next_dram_cycle_time) {
        next_dram_cycle_time += DRAM_CLOCK_CYCLE;
        memory->ClockTick();
    }
}

bool ComplexCoDRAMsim3::will_accept(uint64_t address, bool is_write) {
    return memory->WillAcceptTransaction(address, is_write);
}

bool ComplexCoDRAMsim3::add_request(const CoDRAMRequest *request) {
#ifdef COSIM_DOUBLE_CHECK_ACCEPT
    if (memory->WillAcceptTransaction(request->address, request->is_write)) {
#else
    if (true) {
#endif
        // if (request->is_write) {
        //     std::cout << "send write request with addr 0x" << std::hex << request->address << " to DRAMsim3" << std::endl;
        // }
        // else {
        //     std::cout << "send read request with addr 0x" << std::hex << request->address << " to DRAMsim3" << std::endl;
        // }
        std::lock_guard<std::mutex> guard(mtx);
        memory->AddTransaction(request->address, request->is_write);
        req_list.push_back(new CoDRAMResponse(request, get_clock_ticks()));
        return true;
    }
    return false;
}

CoDRAMResponse *ComplexCoDRAMsim3::check_read_response() {
    return check_response(resp_read_queue);
}

CoDRAMResponse *ComplexCoDRAMsim3::check_write_response() {
    return check_response(resp_write_queue);
}

CoDRAMResponse *ComplexCoDRAMsim3::check_response(std::queue<CoDRAMResponse*> &resp_queue) {
    std::lock_guard<std::mutex> guard(mtx);
    if (resp_queue.empty())
        return NULL;
    auto resp = resp_queue.front();
    auto now = get_clock_ticks();
    if (resp->finish_time <= now) {
        resp->resp_time = now;
        resp_queue.pop();
        return resp;
    }
    return NULL;
}

void ComplexCoDRAMsim3::callback(uint64_t addr, bool is_write) {
    // std::cout << "cycle " << std::dec << get_clock_ticks() << " callback "
    //           << "is_write " << std::dec << is_write << " addr " << std::hex << addr << std::endl;
    // search for the first matched request
    std::lock_guard<std::mutex> guard(mtx);
    auto iter = req_list.begin();
    while (iter != req_list.end()) {
        auto resp = *iter;
        if (resp->req->address == addr && resp->req->is_write == is_write) {
            req_list.erase(iter);
            resp->finish_time = resp->req_time + (get_clock_ticks() - resp->req_time) + padding;
            auto &queue = (resp->req->is_write) ? resp_write_queue : resp_read_queue;
            queue.push(resp);
            return;
        }
        iter++;
    }
    std ::cout << "INTERNAL ERROR: Do not find matched request for this response "
               << "(0x" << std::hex << addr << ", " << is_write << ")." << std::endl;
    abort();
}

SimpleCoDRAMsim3::SimpleCoDRAMsim3(int latency) : latency(latency) {
    dram_clock = 0;
    std::cout << "Simple memory system with " << latency << "-cycle latency initialized." << std::endl;
}

void SimpleCoDRAMsim3::tick() {
    dram_clock++;
}

bool SimpleCoDRAMsim3::will_accept(uint64_t address, bool is_write) {
    return true;
}

bool SimpleCoDRAMsim3::add_request(const CoDRAMRequest *request) {
#ifdef COSIM_DOUBLE_CHECK_ACCEPT
    if (this->will_accept(request->address, request->is_write)) {
#else
    if (true) {
#endif
        // if (request->is_write) {
        //     std::cout << "send write request with addr 0x" << std::hex << request->address << " to DRAMsim3" << std::endl;
        // }
        // else {
        //     std::cout << "send read request with addr 0x" << std::hex << request->address << " to DRAMsim3" << std::endl;
        // }
        auto now = get_clock_ticks();
        auto resp = new CoDRAMResponse(request, now);
        resp->finish_time = now + this->latency;
        resp_list.push_back(resp);
        return true;
    }
    return false;
}

CoDRAMResponse *SimpleCoDRAMsim3::check_read_response() {
    return check_response(false);
}

CoDRAMResponse *SimpleCoDRAMsim3::check_write_response() {
    return check_response(true);
}

CoDRAMResponse *SimpleCoDRAMsim3::check_response(bool is_write) {
    if (resp_list.empty())
        return NULL;
    auto now = get_clock_ticks();
    auto iter = resp_list.begin();
    while (iter != resp_list.end()) {
        auto resp = *iter;
        if (resp->req->is_write == is_write && resp->finish_time <= now) {
            resp_list.erase(iter);
            resp->resp_time = now;
            return resp;
        }
        iter++;
    }
    return NULL;
}
