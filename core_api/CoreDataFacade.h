#pragma once

#include "core_api/CoreFileInfo.h"
#include "core_api/CoreChannelListResult.h"
#include "core_api/CoreSignalData.h"
#include "core_api/CoreDataRequest.h"

class CoreDataFacade
{
public:
    CoreFileInfo probeFile(const std::string& filePath) const;
    CoreChannelListResult listChannels(const std::string& filePath) const;
    CoreSignalData readSignal(const CoreDataRequest& request) const;
};