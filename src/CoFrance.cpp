// CoFrance.cpp
#ifndef PLUGIN_VERSION
#define PLUGIN_VERSION "no_version"
#endif
#include "CoFrance.h"

CoFrancePlugin::CoFrancePlugin() = default;
CoFrancePlugin::~CoFrancePlugin() = default;

void CoFrancePlugin::Initialize(const PluginSDK::PluginMetadata &metadata, PluginSDK::CoreAPI *coreAPI, PluginSDK::ClientInformation info)
{
    metadata_ = metadata;
    clientInfo_ = info;
    coreAPI_ = coreAPI;
    logger_ = &coreAPI_->logger();

    logger_->info("Initializing CoFrance " + metadata.version);
    gateAssigner_ = std::make_unique<GateAssigner::GateAssigner>(
        coreAPI_->aircraft(),
        coreAPI_->flightplan(),
        coreAPI_->tag(),
        *logger_
    );
    oceanicClearance_ = std::make_unique<OceanicClearance::OceanicClearance>(
        coreAPI_->controllerData(),
        coreAPI_->flightplan(),
        coreAPI_->tag(),
        *logger_
    );

    if (isConnected())
    {
        gateAssigner_->startPoller();
        oceanicClearance_->startPoller();
    }

    logger_->info("CoFrance initialized successfully");
    initialized_ = true;
}

void CoFrancePlugin::Shutdown()
{
    if (initialized_)
    {
        gateAssigner_.get()->stopPoller();
        gateAssigner_.reset();
        oceanicClearance_.get()->stopPoller();
        oceanicClearance_.reset();
        initialized_ = false;
        logger_->info("CoFrance shutdown complete");
    }
}

PluginSDK::PluginMetadata CoFrancePlugin::GetMetadata() const
{
#ifdef DEBUG
    return {"CoFrance", "DEBUG" , "French VACC"};
#else
    return {"CoFrance", PLUGIN_VERSION, "French VACC"};
#endif
}

void CoFrancePlugin::OnFsdConnected(const PluginSDK::Fsd::FsdConnectedEvent* event)
{
    gateAssigner_->startPoller();
    oceanicClearance_->startPoller();
}

void CoFrancePlugin::OnFsdDisconnected(const PluginSDK::Fsd::FsdDisconnectedEvent* event)
{
    gateAssigner_->stopPoller();
    oceanicClearance_->stopPoller();        
}

bool CoFrancePlugin::isConnected() const
{
    auto connection = coreAPI_->fsd().getConnection();
    if (connection)
    {
        return connection->isConnected;
    }
    return false;
}

bool CoFrancePlugin::isConnectedAsController() const
{
    auto connection = coreAPI_->fsd().getConnection();
    if (connection)
    {
        return connection->isConnected && connection->facility != PluginSDK::Fsd::NetworkFacility::OBS;
    }
    return false;
}

bool CoFrancePlugin::isConnectedAsCTR() const
{
    auto connection = coreAPI_->fsd().getConnection();
    if (connection)
    {
        return connection->isConnected && connection->facility == PluginSDK::Fsd::NetworkFacility::CTR;
    }
    return false;
}
