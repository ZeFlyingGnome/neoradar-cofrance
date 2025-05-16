// OceanicClearance.cpp
#include "OceanicClearance.h"

namespace OceanicClearance
{
    OceanicClearance::OceanicClearance(
        PluginSDK::ControllerData::ControllerDataAPI &controllerDataAPI,
        PluginSDK::Flightplan::FlightplanAPI &flightplanAPI,
        PluginSDK::Tag::TagAPI &tagAPI,
        PluginSDK::Logger::LoggerAPI &logger
    ) : controllerDataAPI_(controllerDataAPI),
        flightplanAPI_(flightplanAPI),
        tagAPI_(tagAPI),
        logger_(logger)
    {

        logger_.info("Initializing OceanicClearance");
    
        // Initialize the tag item for oceanic flag
        PluginSDK::Tag::TagItemDefinition tagDefinition;
        tagDefinition.name = OCEANIC_FLAG_TAG;
        tagDefinition.defaultValue = "";
        tagDefinition.allowedActions = {};
        oceanicFlagId_ = tagAPI_.getInterface()->RegisterTagItem(tagDefinition);
        
        logger_.info("OceanicClearance initialized successfully");
    }

    void OceanicClearance::startPoller(void)
    {
        if (!poolerRunning_)
        {
            poolerRunning_ = true;
            pollerThread_ = std::thread(&OceanicClearance::pollerThread, this);
            logger_.info("OceanicClearance poller started");
        }
    }

    void OceanicClearance::stopPoller(void)
    {
        if (poolerRunning_)
        {
            poolerRunning_ = false;
            if (pollerThread_.joinable())
            {
                pollerThread_.join();
            }
            logger_.info("OceanicClearance poller stopped");
        }
    }

    void OceanicClearance::pollerThread(void)
    {
        int counter_sec = 0;
        std::string oceanicFlag;

        // Poller loop
        while (poolerRunning_)
        {
            if (counter_sec % POLLING_INTERVAL_SEC == 0)
            {
                nattrakData_ = getNattrakData();
                std::vector<PluginSDK::Flightplan::Flightplan> flightplans = flightplanAPI_.getAll();
                for (auto flightplan : flightplans)
                {
                    /*
                    if BREST_OCEANIC_POINTS is in the route
                        if has OCL
                            if OCL level is same as the cleared level or the flight plan level
                                set OCL
                            else
                                set LCHG + flight level
                        else if the destination is Americas (ICAOs starting with KCPSTMN, and SPEM)
                            if the exist of sector is withing 30 minutes
                                if exist of sector is within 15 minutes
                                    set OCL yellow
                                else
                                    set OCL blue
                    else
                        set empty
                    
                    */
                    if (flightplan.isValid && stringContainsValue(flightplan.route, BREST_OCEANIC_POINTS))
                    {
                        if (hasOceanicClearance(flightplan.callsign))
                        {
                            auto controllerData = controllerDataAPI_.getByCallsign(flightplan.callsign);
                            if (controllerData.has_value())
                            {
                                int oeanicFlightLevel = getOceanicFlightLevel(flightplan.callsign);
                                int clearedFlightLevel = controllerData->clearedFlightLevel;
                                if (clearedFlightLevel == 0)
                                {
                                    clearedFlightLevel = flightplan.plannedAltitude;
                                }
                                if (clearedFlightLevel == oeanicFlightLevel)
                                {
                                    oceanicFlag = "OCL";
                                    updateOceanicFlag(flightplan.callsign, oceanicFlag, COLOR_OCEANIC_CLEARANCE);
                                }
                                else
                                {
                                    oceanicFlag = "LCHG" + std::to_string(oeanicFlightLevel / 1000);
                                    updateOceanicFlag(flightplan.callsign, oceanicFlag, COLOR_OCEANIC_CLEARANCE);
                                }
                            }
                        }
                        else if (std::regex_match(flightplan.destination, OCEANIC_DESTINATION_REGEX))
                        {
                            oceanicFlag = "OCL";
                            updateOceanicFlag(flightplan.callsign, oceanicFlag, COLOR_NO_OCEANIC_CLEARANCE);
                        }
                        else
                        {   
                            oceanicFlag = "";
                            updateOceanicFlag(flightplan.callsign, oceanicFlag, COLOR_DEFAULT);
                        }
                    }
                    else 
                    {
                        oceanicFlag = "";
                        updateOceanicFlag(flightplan.callsign, oceanicFlag, COLOR_DEFAULT);
                    }
                }
            }
            std::this_thread::sleep_for(std::chrono::seconds(1));
            counter_sec++;
        }
    }

    void OceanicClearance::updateOceanicFlag(std::string callsign, std::string value, std::array<unsigned int, 3> colour)
    {
        PluginSDK::Tag::TagContext context;
        context.callsign = callsign;
        context.colour = colour;
        tagAPI_.getInterface()->UpdateTagValue(oceanicFlagId_, value, context);
#ifdef DEBUG
        logger_.info("Update Oceanic Flag for " + callsign + " to '" + value + "'");
#endif
    }

    nlohmann::json OceanicClearance::getNattrakData(void)
    {
        httplib::Client cli(NATTRAK_API_BASE);
        httplib::Result result;
        
        try {
            result = cli.Get(NATTRAK_API_CLEARANCE);
        }
        catch (const std::exception &e)
        {
            logger_.error("Failed to connect to Nattrak API: " + std::string(e.what()));
            return nlohmann::json();
        }
        
        if (result->status != httplib::StatusCode::OK_200) {
            logger_.error("Nattrak API returned error: " + std::to_string(result->status));
            return nlohmann::json();
        }
        
        return nlohmann::json::parse(result->body);
    }

    bool OceanicClearance::hasOceanicClearance(std::string callsign)
    {
        try
        {
            for (auto ocl : nattrakData_)
            {
                if (ocl.contains("callsign") && ocl.contains("status"))
                {
                    if (ocl["callsign"] == callsign && ocl["status"] == "CLEARED") {
                        return true;
                    }
                }
            }
        }
        catch (std::exception& exc) {}
        return false;
    }

    int OceanicClearance::getOceanicFlightLevel(std::string callsign)
    {
        try
        {
            for (auto ocl : nattrakData_)
            {
                if (ocl.contains("callsign") && ocl.contains("status") && ocl.contains("level"))
                {
                    if (ocl["callsign"] == callsign && ocl["status"] == "CLEARED") 
                    {
                        return std::stoi(ocl["level"].get<std::string>())*100;
                    }
                }
            }
        }
        catch (std::exception& exc) {}
        return 0;
    }

    bool OceanicClearance::stringContainsValue(const std::string &str, const std::vector<std::string> &values)
    {
        std::string strUpper = str;
        std::transform(strUpper.begin(), strUpper.end(), strUpper.begin(), ::toupper);
        for (const auto &value : values)
        {
            if (strUpper.find(value) != std::string::npos)
            {
                return true;
            }
        }
        return false;
    }

    bool OceanicClearance::stringStartsWith(const std::string &str, const std::string &prefix)
    {
        if (str.size() < prefix.size())
        {
            return false;
        }
        return std::equal(prefix.begin(), prefix.end(), str.begin());
    }
}