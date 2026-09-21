#include <jansson.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <ctime>
#include "config.h"
#include <unistd.h>
#include <pwd.h>
#include <grp.h>

using std::string;

string ssid = "*";
string password = "";
string mqttBrokerIP = "1.2.3.4";
string brokerusername;
string brokerpassword;
string publishTopic = "pylontech";
string responseTopic = "pyloncmd";
string locationTopic = "PylonMonitor";
string deviceTopic = "battery";
string serialPort = "/dev/ttyAMA0";
bool homeAssistantMode = false;
string homeAssistantPrefix = "battery";

bool mqtt_changed = false;
int pollInterval = 15;

#ifdef DJ0ABR
double batteryEnergy[16] = { 3374,  3374, 3374, 3374, 3374, 3374, 3374, 2280};
int batteryCapacity[16] = { 70, 70, 70, 70, 70, 70, 70, 47 };
#endif
#ifdef DL1EV
double batteryEnergy[16] = { 3374,  3374 };
int batteryCapacity[16] = { 70, 70 };
#endif

string getMQTTtopic() 
{
    static string cached_topic;
    static time_t last_update = 0;
    time_t now = time(nullptr);
    if (now - last_update >= 30) {
        readConfigFromJson();
        cached_topic = publishTopic + "/" + locationTopic + "/" + deviceTopic;
        last_update = now;
    }
    return cached_topic;
}
 
bool saveDefaultConfigToJson() 
{
    // Create a new JSON object
    json_t *root = json_object();
    
    // Insert data into the JSON object
    json_object_set_new(root, "ssid", json_string(ssid.c_str()));
    json_object_set_new(root, "password", json_string(password.c_str()));
    json_object_set_new(root, "mqttBrokerIP", json_string(mqttBrokerIP.c_str()));
    json_object_set_new(root, "brokerusername", json_string(brokerusername.c_str()));
    json_object_set_new(root, "brokerpassword", json_string(brokerpassword.c_str()));
    json_object_set_new(root, "publishTopic", json_string(publishTopic.c_str()));
    json_object_set_new(root, "responseTopic", json_string(responseTopic.c_str()));
    json_object_set_new(root, "locationTopic", json_string(locationTopic.c_str()));
    json_object_set_new(root, "deviceTopic", json_string(deviceTopic.c_str()));
    json_object_set_new(root, "serialPort", json_string(serialPort.c_str()));
    json_object_set_new(root, "homeAssistantMode", json_boolean(homeAssistantMode));
    json_object_set_new(root, "homeAssistantPrefix", json_string(homeAssistantPrefix.c_str()));
    json_object_set_new(root, "pollInterval", json_integer(pollInterval));
    
    // Dump the JSON object to a string
    char *jsonString = json_dumps(root, JSON_INDENT(4));
    if (!jsonString) {
        // Cleanup
        json_decref(root);
        return false; // Failed to dump JSON string
    }
    
    // Write the JSON string to a file
    // Check if the file already exists
    if (access("/var/www/html/wxdata/configData.json", F_OK) != -1) {
        std::cerr << "File already exists.\n";
        // Check if it has the HA fields, if not, update it
        std::ifstream checkFile("/var/www/html/wxdata/configData.json");
        if (checkFile.is_open()) {
            std::stringstream checkStream;
            checkStream << checkFile.rdbuf();
            std::string checkContent = checkStream.str();
            checkFile.close();
            
            if (checkContent.find("homeAssistantMode") == std::string::npos) {
                // Add the missing fields
                json_error_t error;
                json_t *existing = json_loads(checkContent.c_str(), 0, &error);
                if (existing) {
                    json_object_set_new(existing, "homeAssistantMode", json_boolean(homeAssistantMode));
                    json_object_set_new(existing, "homeAssistantPrefix", json_string(homeAssistantPrefix.c_str()));
                    char *updatedJson = json_dumps(existing, JSON_INDENT(4));
                    if (updatedJson) {
                        std::ofstream updateFile("/var/www/html/wxdata/configData.json");
                        if (updateFile.is_open()) {
                            updateFile << updatedJson;
                            updateFile.close();
                            free(updatedJson);
                        }
                    }
                    json_decref(existing);
                }
            }
        }
        // Cleanup
        json_decref(root);
        free(jsonString);
        return false; // Do not overwrite the file
    } else {
        // File does not exist, proceed to create it
        std::ofstream outFile("/var/www/html/wxdata/configData.json");
        if (!outFile.is_open()) {
            std::cerr << "Failed to open file for writing.\n";
            // Cleanup
            json_decref(root);
            free(jsonString);
            return false;
        }
        
        outFile << jsonString;
        outFile.close();
        
        // Get the UID and GID for www-data
        struct passwd *pwd = getpwnam("www-data");
        struct group *grp = getgrnam("www-data");
        if (!pwd || !grp) {
            std::cerr << "Failed to get UID or GID for www-data.\n";
            // Cleanup
            json_decref(root);
            free(jsonString);
            return false;
        }

        // Change file ownership to www-data:www-data
        if (chown("/var/www/html/wxdata/configData.json", pwd->pw_uid, grp->gr_gid) == -1) {
            std::cerr << "Failed to change file ownership.\n";
            // Cleanup, although not critical to return false here since the file was written successfully
        }
    }

    // Cleanup JSON resources
    json_decref(root);
    free(jsonString);

    return true; // Indicate success
}

bool readConfigFromJson() 
{
    // Open the JSON file
    std::ifstream inFile("/var/www/html/wxdata/configData.json");
    if (!inFile.is_open()) {
        std::cerr << "Failed to open file for reading.\n";
        return false; // Failed to open file
    }

    // Read file into a string
    std::stringstream strStream;
    strStream << inFile.rdbuf(); // Read the file
    std::string jsonString = strStream.str(); // str holds the content of the file
    inFile.close();

    // Parse the JSON string
    json_error_t error;
    json_t *root = json_loads(jsonString.c_str(), 0, &error);
    if (!root) {
        std::cerr << "Error parsing JSON: " << error.text << "\n";
        return false; // Failed to parse JSON
    }

    static string last_brokerIP;
    static string last_brokerusername;
    static string last_brokerpassword;

    // Extract values
    mqttBrokerIP = json_string_value(json_object_get(root, "mqttBrokerIP"));
    brokerusername = json_string_value(json_object_get(root, "brokerusername"));
    brokerpassword = json_string_value(json_object_get(root, "brokerpassword"));
    publishTopic = json_string_value(json_object_get(root, "publishTopic"));
    responseTopic = json_string_value(json_object_get(root, "responseTopic"));
    locationTopic = json_string_value(json_object_get(root, "locationTopic"));
    deviceTopic = json_string_value(json_object_get(root, "deviceTopic"));
    const char *serialPortStr = json_string_value(json_object_get(root, "serialPort"));
    if(serialPortStr) serialPort = serialPortStr;
    else serialPort = "/dev/ttyAMA0";
    const char *haPrefixStr = json_string_value(json_object_get(root, "homeAssistantPrefix"));
    if(haPrefixStr) homeAssistantPrefix = haPrefixStr;
    else homeAssistantPrefix = "No";
    homeAssistantMode = json_boolean_value(json_object_get(root, "homeAssistantMode"));
    json_t *poll_int = json_object_get(root, "pollInterval");
    if (poll_int && json_is_integer(poll_int)) {
        pollInterval = (int)json_integer_value(poll_int);
    }

    if( last_brokerIP != mqttBrokerIP ||
        last_brokerusername != brokerusername ||
        last_brokerpassword != brokerpassword) {

        printf("Broker Config has changed\n");        
        last_brokerIP = mqttBrokerIP;
        last_brokerusername = brokerusername;
        last_brokerpassword = brokerpassword;
        mqtt_changed = true;
    }

    // Cleanup
    json_decref(root);

    return true; // Success
}
