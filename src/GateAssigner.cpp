// GateAssigner.cpp
#include "GateAssigner.h"

namespace GateAssigner
{
    GateAssigner::GateAssigner(
        PluginSDK::Aircraft::AircraftAPI &aircraftAPI,
        PluginSDK::Flightplan::FlightplanAPI &flightplanAPI,
        PluginSDK::Tag::TagAPI &tagAPI,
        PluginSDK::Logger::LoggerAPI &logger
    ) : aircraftAPI_(aircraftAPI),
        flightplanAPI_(flightplanAPI),
        tagAPI_(tagAPI),
        logger_(logger)
    {

        logger_.info("Initializing GateAssigner");
    
        // Initialize the tag item for gate assignment
        PluginSDK::Tag::TagItemDefinition tagDefinition;
        tagDefinition.name = GATE_ASSIGNER_TAG;
        tagDefinition.defaultValue = "--";
        tagDefinition.allowedActions = {};
        gateTagId_ = tagAPI_.getInterface()->RegisterTagItem(tagDefinition);
        
        logger_.info("GateAssigner initialized successfully");
    }

    void GateAssigner::startPoller(void)
    {
        if (!poolerRunning_)
        {
            poolerRunning_ = true;
            pollerThread_ = std::thread(&GateAssigner::pollerThread, this);
            logger_.info("GateAssigner poller started");
        }
    }

    void GateAssigner::stopPoller(void)
    {
        if (poolerRunning_)
        {
            poolerRunning_ = false;
            if (pollerThread_.joinable())
            {
                pollerThread_.join();
            }
            logger_.info("GateAssigner poller stopped");
        }
    }

    void GateAssigner::pollerThread(void)
    {
        int counter_sec = 0;

        // Get the list of supported airports
        supportedAirports_ = getSupportedAirport();
        if (supportedAirports_.empty())
        {
            logger_.warning("No supported airports found");
            return;
        }   
        logger_.info("Supported airports: " + std::accumulate(supportedAirports_.begin(), supportedAirports_.end(), std::string(),
            [](const std::string &a, const std::string &b) { return a + (a.length() > 0 ? ", " : "") + b; }));

        // Poller loop
        while (poolerRunning_)
        {
            if (counter_sec % POLLING_INTERVAL_SEC == 0)
            {
                std::vector<PluginSDK::Flightplan::Flightplan> flightplans = flightplanAPI_.getAll();
                for (const auto &flightplan : flightplans)
                {
                    if (std::find(supportedAirports_.begin(), supportedAirports_.end(), flightplan.destination) != supportedAirports_.end())
                    {
                        std::optional<PluginSDK::Aircraft::Aircraft> aircraft = aircraftAPI_.getByCallsign(flightplan.callsign);
                        if (aircraft.has_value())
                        {
                            if (aircraft.value().position.altitude < AIRCRAFT_MAX_ALTITUE)
                            {
#ifdef DEBUG
                                logger_.info("Requesting gate for " + flightplan.callsign + " from " + flightplan.origin + " to " + flightplan.destination + " with wake category " + flightplan.wakeCategory);
#endif
                                std::string assignedGate = assignGate(flightplan.callsign, flightplan.origin, flightplan.destination, flightplan.wakeCategory);
                                if (!assignedGate.empty())
                                {
#ifdef DEBUG
                                    logger_.info("Assigned gate " + assignedGate + " to " + flightplan.callsign);
#endif
                                    PluginSDK::Tag::TagContext context;
                                    context.callsign = flightplan.callsign;
                                    tagAPI_.getInterface()->UpdateTagValue(gateTagId_, assignedGate, context);
                                }
                            }
                        }
                    }
                }
            }
            
            std::this_thread::sleep_for(std::chrono::seconds(1));
            counter_sec++;
        }
    }

    std::vector<std::string> GateAssigner::getSupportedAirport(void)
    {
        httplib::Client cli(GATE_ASSIGNER_API_BASE.c_str());
        httplib::Result result;
        try {
            result = cli.Get(GATE_ASSIGNER_API_AIRPORTS.c_str());
        }
        catch (const std::exception &e)
        {
            logger_.error("Failed to connect to GateAssigner API: " + std::string(e.what()));
            return std::vector<std::string>();
        }
        
        if (result->status != httplib::StatusCode::OK_200) {
            logger_.error("GateAssigner API returned error: " + std::to_string(result->status));
            return std::vector<std::string>();
        }
        
        try {
            toml::value tomlResult = toml::parse_str(result->body);
            return toml::find<std::vector<std::string>>(tomlResult, "data", "icaos");
        }
        catch (const std::exception &err)
        {
            logger_.error("Failed to parse TOML: " + std::string(err.what()));
            return std::vector<std::string>();
        }        
        
        return std::vector<std::string>();
    }

    std::string GateAssigner::assignGate(std::string callsign, std::string origin, std::string destination, std::string wakeCategory)
    {
        httplib::Client cli(GATE_ASSIGNER_API_BASE.c_str());
        httplib::Params params;
        params.emplace("callsign", callsign);
        params.emplace("dep", origin);
        params.emplace("arr", destination);
        params.emplace("wtc", wakeCategory);
        httplib::Result result;

        try {
            result = cli.Post(GATE_ASSIGNER_API_GATES.c_str(), params);
        }
        catch (const std::exception &e)
        {
            logger_.error("Failed to connect to GateAssigner API: " + std::string(e.what()));
            return "";
        }
        
        if (result->status != httplib::StatusCode::OK_200) {
            logger_.error("GateAssigner API returned error: " + std::to_string(result->status));
            return "";
        }

        try {
            toml::value tomlResult = toml::parse_str(result->body);
            return toml::find<std::string>(tomlResult, "data", "stand");
        }
        catch (const std::exception &err)
        {
            logger_.error("Failed to parse TOML: " + std::string(err.what()));
            return "";
        }
        
        return "";
    }
}