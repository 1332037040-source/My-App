#pragma once
#include "../planner/TaskBuilder.h"
#include "../engine/Engine.h"
#include "RunRequest.h"

struct MappedRunConfig {
    BuildRequest buildReq;
    EngineRunConfig engineCfg;
};

// 声明
MappedRunConfig MapRunRequest(const RunRequest& in);
