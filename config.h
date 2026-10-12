#include <string>

using std::string;

bool saveDefaultConfigToJson();
bool readConfigFromJson();
string getMQTTtopic();

extern string ssid;
extern string password;
extern string mqttBrokerIP;
extern string brokerusername;
extern string brokerpassword;
extern string publishTopic;
extern string responseTopic;
extern string locationTopic;
extern string deviceTopic;
extern string serialPort;
extern string serialPort2;
extern string serialPort3;
extern string serialPort4;
extern bool port2Enabled;
extern bool port3Enabled;
extern bool port4Enabled;
extern bool homeAssistantMode;
extern string homeAssistantPrefix;
extern bool mqtt_changed;
extern int pollInterval;
