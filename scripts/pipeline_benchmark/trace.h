#pragma once

#include <stdint.h>

namespace pipeline_benchmark {

enum Stage { Download, Headers, Body, Progress, Clear, Decode, Overlay, Refresh, Settle, StageCount };

struct Sample {
    uint64_t started = 0, total = 0, elapsed[StageCount] = {};
    uint32_t bytes = 0, attempts = 0, clearCalls = 0, partialCalls = 0;
    int httpStatus = 0;
    const char *outcome = "incomplete";
    bool complete = false;
};

extern Sample sample;

class Run {
public:
    Run();
    ~Run();
};

class Scope {
public:
    explicit Scope(Stage stage);
    ~Scope();
    Scope(const Scope &) = delete;
    Scope &operator=(const Scope &) = delete;
private:
    Stage stage;
    bool enabled;
    uint64_t started, clearBefore;
};

void configure();
void startTask();
void finish(bool rendered);
void notModified();
void attempt();
void headers(uint64_t started, int status);
void body(int bytes);
void clearDisplay();
void partialUpdate(bool forced);
void refresh();

}
