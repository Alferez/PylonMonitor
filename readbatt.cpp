/*
reads battery data from the pylontech console connector

* check if we have a working connection by sending '\n' and waiting for a prompt '>'
* send command to get battery data: "bat 1\n" 2,3,4 and so on until there is no more battery
* read the response from the battery (until the prompt) and evaluate the data

sample string from Pylontech:
======================
bat 2
 @  
Battery Volt Curr Tempr Base State  Volt. State Curr. State Temp. State SOC  Coulomb   BAL   
0       3508 0    22100 Idle        Normal      Normal      Normal      100% 73555 mAH N  
1 3509 0 22100 Idle Normal Normal Normal 100% 73555 mAH N  
2 3511 0 22100 Idle Normal Normal Normal 100% 73555 mAH N  
3 3509 0 22100 Idle Normal Normal Normal 100% 73555 mAH N  
4 3486 0 22100 Idle Normal Normal Normal 100% 73555 mAH N  
5 3509 0 22300 Idle Normal Normal Normal 100% 73555 mAH N  
6 3511 0 22300 Idle Normal Normal Normal 100% 73555 mAH N  
7 3512 0 22300 Idle Normal Normal Normal 100% 73555 mAH N  
8 3511 0 22300 Idle Normal Normal Normal 100% 73555 mAH N  
9 3509 0 22300 Idle Normal Normal Normal 100% 73555 mAH N  
10 3508 0 22000 Idle Normal Normal Normal 100% 73555 mAH N  
11 3509 0 22000 Idle Normal Normal Normal 100% 73555 mAH N  
12 3510 0 22000 Idle Normal Normal Normal 100% 73555 mAH N  
13 3509 0 22000 Idle Normal Normal Normal 100% 73555 mAH N  
14 3500 0 22000 Idle Normal Normal Normal 100% 73555 mAH N 
 Command completed successfully 
 $$ 
 pylon>
*/

#include <termios.h>
#include "pylonmonitor.h"
#include "readbatt.h"
#include "serial.h"
#include <thread>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <fstream>
#include <iostream>
#include <ctime>
#include <jansson.h>
#include "fifo.h"
#include "config.h"
#include "helper.h"

extern double batteryEnergy[16];
extern int batteryCapacity[16];

READBATT::READBATT()
{
    // Initialize batteryInfo array
    for(int i = 0; i < MAXBATTNUMBER; i++) {
        batteryInfo[i].isNewModel = false;
        batteryInfo[i].cycleCount = 0;
        memset(batteryInfo[i].manufacturer, 0, sizeof(batteryInfo[i].manufacturer));
        memset(batteryInfo[i].model, 0, sizeof(batteryInfo[i].model));
        memset(batteryInfo[i].boardVersion, 0, sizeof(batteryInfo[i].boardVersion));
        memset(batteryInfo[i].board, 0, sizeof(batteryInfo[i].board));
        memset(batteryInfo[i].mainSoftVersion, 0, sizeof(batteryInfo[i].mainSoftVersion));
        memset(batteryInfo[i].softVersion, 0, sizeof(batteryInfo[i].softVersion));
        memset(batteryInfo[i].bootVersion, 0, sizeof(batteryInfo[i].bootVersion));
        memset(batteryInfo[i].commVersion, 0, sizeof(batteryInfo[i].commVersion));
        memset(batteryInfo[i].releaseDate, 0, sizeof(batteryInfo[i].releaseDate));
        memset(batteryInfo[i].serial, 0, sizeof(batteryInfo[i].serial));
        memset(batteryInfo[i].specification, 0, sizeof(batteryInfo[i].specification));
        memset(batteryInfo[i].cellNumber, 0, sizeof(batteryInfo[i].cellNumber));
        memset(batteryInfo[i].maxDischgCurr, 0, sizeof(batteryInfo[i].maxDischgCurr));
        memset(batteryInfo[i].maxChargeCurr, 0, sizeof(batteryInfo[i].maxChargeCurr));
        memset(batteryInfo[i].eponPortRate, 0, sizeof(batteryInfo[i].eponPortRate));
        memset(batteryInfo[i].consolePortRate, 0, sizeof(batteryInfo[i].consolePortRate));
    }

    // initialize and open the RPI's internal serial interface
    // this uses the primary serial interface on the RPI board
    // take care of the comments in pylonmonitor.cpp: remapping of serial port
    // the serial console must be switched OFF
    open_serial(B115200);

    // create a thread that handles reading from the battery
    std::thread myThread(&READBATT::batteryhandler_thread,this);

    // Detach the thread to allow it to run and exit independently.
    myThread.detach();
}

void READBATT::batteryhandler_thread()
{
    while(keeprunning) {
        // read byttery information of all connected batteries, one by one
        loop_pylon();
        usleep(50);
    }
}

// Function to write formatted text to the serial port.
void READBATT::serial_printf(const char *format, ...) 
{
    char buffer[1024]; // Defines the maximum size of the formatted string
    va_list args;

    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    for (int i = 0; buffer[i] != '\0'; ++i) {
        write_serial((uint8_t)buffer[i]);
    }
}

// read byttery information of all connected batteries, one by one, every second a battery
void READBATT::loop_pylon()
{
static int timeout = 0;
static int lasttime = 0;
static int acttime = 0;
static int cycleStart = 0;
static bool cycleInProgress = false;

    if(pylonState == PYLON_SEARCH) {
        // send LF, pylontech should respond with a prompt
        printf("search batt\n");
        timeout = 0;
        write_serial('\n');
        pylonState = PYLON_ACK;
        return;
    }

    if(pylonState == PYLON_ACK) {
        // wait for the prompt from the battery
        int data = read_serial();  // -1 ... no data
        if(data != -1) {
            // something was received from the battery
            uint8_t c = data;
            if (c == '>') {
                // prompt received
                printf("batt found\n");
                acttime = 0;
                lasttime = -1000; //make first request immediately
                pylonState = PYLON_PWR;
                return;
            }
        } else {
            // nothing received
            if(++timeout > 5000) {
                // nothing received within 5s
                printf("batt test timeout\n");
                pylonState = PYLON_SEARCH;
                return;
            }
            usleep(1000);
            return;
        }
    }

    if(pylonState == PYLON_PWR) {
        // send pwr command to get battery count
        acttime++;
        if((acttime - lasttime) < 20) {
            usleep(100000);
            return;
        }
        lasttime = acttime;
        printf("req pwr\n");
        serial_printf("pwr\n");
        timeout = 0;
        rxidx = 0;
        pylonState = PYLON_READ;
        return;
    }

    if(pylonState == PYLON_REQUEST) {
        // battery found, request the next batt data
        // wait for 1s before making a new request
        acttime++;
        // wait pollInterval seconds before starting a new cycle
        if(cycleInProgress && battnum == 1) {
            if(acttime - cycleStart < pollInterval * 10) {
                usleep(100000);
                return;
            }
            cycleInProgress = false;
        }
        if((acttime - lasttime) < 20) {
            // too early for next request
            usleep(100000);
            return;
        }
        lasttime = acttime;
        printf("req bat %d\n",battnum);
        // if the write buffer is full, restart the search to clear it
        if(write_serial_free() == -1) {
            printf("write buffer full, restarting search\n");
            acttime = 0;
            lasttime = 0;
            pylonState = PYLON_SEARCH;
            return;
        }
        serial_printf("bat %d\n", battnum);
        timeout = 0;
        rxidx = 0;
        pylonState = PYLON_READ;
        return;
    }

    if(pylonState == PYLON_INFO) {
        // Request info from new model batteries
        acttime++;
        if((acttime - lasttime) < 20) {
            usleep(100000);
            return;
        }
        lasttime = acttime;
        printf("req info %d\n", battnum);
        if(write_serial_free() == -1) {
            printf("write buffer full, restarting search\n");
            acttime = 0;
            lasttime = 0;
            pylonState = PYLON_SEARCH;
            return;
        }
        serial_printf("info %d\n", battnum);
        timeout = 0;
        rxidx = 0;
        pylonState = PYLON_READ;
        return;
    }

    if(pylonState == PYLON_STAT) {
        // Request stat from new model batteries to get cycle count
        acttime++;
        if((acttime - lasttime) < 20) {
            usleep(100000);
            return;
        }
        lasttime = acttime;
        printf("req stat %d\n", battnum);
        if(write_serial_free() == -1) {
            printf("write buffer full, restarting search\n");
            acttime = 0;
            lasttime = 0;
            pylonState = PYLON_SEARCH;
            return;
        }
        serial_printf("stat %d\n", battnum);
        timeout = 0;
        rxidx = 0;
        pylonState = PYLON_READ;
        return;
    }

    if(pylonState == PYLON_READ) {
        // read the response from the battery
        // read all bytes until the prompt is received
        int c = read_serial();
        if(c != -1) {
            while(c != -1) {
                // char received
                timeout = 0;
                // store received char
                pylon_rxbuf[rxidx++] = c;
                pylon_rxbuf[rxidx] = 0;
                if(rxidx >= MAXRXBUFLEN - 1) {
                    // overflow - process what we have and restart
                    printf("batt read pylon_rxbuf overflow\n");
                    pylon_rxbuf[MAXRXBUFLEN - 1] = 0;
                    processBatData();
                    rxidx = 0;
                }

                if(strstr(pylon_rxbuf,"Invalid")) {
                    // this bat num does not exist
                    // restart with bat 1                    
                    battnum = 1;
                    // empty the RX buffer
                    while(read_serial() != -1) usleep(100);
                    acttime = 0;
                    lasttime =0;
                    pylonState = PYLON_REQUEST;
                    return;
                }

                if (c == '>') {
                    // prompt received
                    pylon_rxbuf[rxidx] = 0;   // terminate string
                    bool wasInfoCommand = (strstr(pylon_rxbuf, "info") != NULL);
                    bool wasStatCommand = (strstr(pylon_rxbuf, "stat") != NULL);
                    processBatData();
                    // if we just read pwr data, start with battery 1
                    if(strstr(pylon_rxbuf, "pwr") != NULL) {
                        battnum = 1;
                    } else if(wasStatCommand) {
                        // Just read stat, go to next battery
                        if(++battnum > 64) battnum = 1;
                    } else if(wasInfoCommand) {
                        // Just read info, check if we need to request stat for this battery
                        // New models have isNewModel = true
                        if(battnum <= battnumber && batteryInfo[battnum-1].isNewModel) {
                            // Request stat for new model battery to get cycle count
                            pylonState = PYLON_STAT;
                            return;
                        } else {
                            // go to next battery
                            if(++battnum > 64) battnum = 1;
                        }
                    } else {
                        // Just read bat data, check if we need to request info for this battery
                        // New models have isNewModel = true
                        if(battnum <= battnumber && batteryInfo[battnum-1].isNewModel) {
                            // Request info for new model battery
                            pylonState = PYLON_INFO;
                            return;
                        } else {
                            // go to the next battery (never read more than 64 batteries)
                            if(++battnum > 64) battnum = 1;
                        }
                    }
                    acttime = 0;
                    lasttime =0;
                    // if this was the last battery in the cycle, wait pollInterval seconds
                    if(battnum > battnumber) {
                        battnum = 1;
                        cycleStart = acttime;
                        cycleInProgress = true;
                    }
                    pylonState = PYLON_REQUEST;
                    return;
                }
                
                c = read_serial();
            }
        } else {
            // nothing received
            if(++timeout > 2000) {
                // nothing received within 2s
                printf("batt read timeout\n");
                // repeat with the same battery
                acttime = 0;
                lasttime = 0;
                pylonState = PYLON_REQUEST;
                usleep(1000);
            }
        }
        return;
    }
}

// remove multiple SPCs
string READBATT::shrink(char *text) {
  string result = "";
  bool lastWasSpace = false;

  while (*text) {
    if (*text == ' ') {
      if (!lastWasSpace) {
        result += *text;
        lastWasSpace = true;
      }
    } else {
      result += *text;
      lastWasSpace = false;
    }
    text++;
  }

  return result;
}

void READBATT::processBatData()
{
    string s = shrink(pylon_rxbuf);
    const char *batteryString = s.c_str();



    // Check if this is a stat command response
    if(strstr(batteryString, "stat") != NULL) {
        // Parse stat response to get cycle count
        char *pos = (char *)batteryString;
        int battNum = 0;
        
        // Find battery number from the first line (after "stat ")
        char *statStr = strstr(pos, "stat ");
        if (statStr) {
            battNum = atoi(statStr + 5);
        } else if (pos[0] >= '1' && pos[0] <= '9') {
            battNum = atoi(pos);
        }
        
        // Helper function to extract value after colon and trim spaces/newlines
        auto extractValue = [](const char *data, const char *fieldName) -> std::string {
            const char *field = strstr(data, fieldName);
            if (!field) return "";
            
            // Find the colon after the field name
            const char *colon = strchr(field, ':');
            if (!colon) return "";
            
            // Skip the colon and spaces
            const char *value = colon + 1;
            while (*value == ' ' || *value == '\t') value++;
            
            // Extract until newline or end of string
            std::string result;
            while (*value && *value != '\n' && *value != '\r') {
                result += *value;
                value++;
            }
            
            // Trim trailing spaces
            while (!result.empty() && (result.back() == ' ' || result.back() == '\t')) {
                result.pop_back();
            }
            
            return result;
        };
        
        // Extract cycle count
        std::string cycleStr = extractValue(pos, "CYCLE Times");
        if (battNum > 0 && battNum <= 64) {
            batteryInfo[battNum-1].cycleCount = atoi(cycleStr.c_str());
        }
        
        return;
    }

    // Check if this is an info command response
    if(strstr(batteryString, "info") != NULL) {
        // Parse info response to get battery details
        // Format is: "Field name        : Value"
        char *pos = (char *)batteryString;
        int battNum = 0;
        
        // Find battery number from the first line (after "info ")
        char *infoStr = strstr(pos, "info ");
        if (infoStr) {
            battNum = atoi(infoStr + 5);
        } else if (pos[0] >= '1' && pos[0] <= '9') {
            battNum = atoi(pos);
        }
        
        // Helper function to extract value after colon and trim spaces/newlines
        auto extractValue = [](const char *data, const char *fieldName) -> std::string {
            const char *field = strstr(data, fieldName);
            if (!field) return "";
            
            // Find the colon after the field name
            const char *colon = strchr(field, ':');
            if (!colon) return "";
            
            // Skip the colon and spaces
            const char *value = colon + 1;
            while (*value == ' ' || *value == '\t') value++;
            
            // Extract until newline or end of string
            std::string result;
            while (*value && *value != '\n' && *value != '\r') {
                result += *value;
                value++;
            }
            
            // Trim trailing spaces
            while (!result.empty() && (result.back() == ' ' || result.back() == '\t')) {
                result.pop_back();
            }
            
            return result;
        };
        
        // Extract all fields
        std::string manufacturer = extractValue(pos, "Manufacturer");
        std::string model = extractValue(pos, "Device name");
        std::string boardVersion = extractValue(pos, "Board version");
        std::string board = extractValue(pos, "Board");
        std::string mainSoftVersion = extractValue(pos, "Main Soft version");
        std::string softVersion = extractValue(pos, "Soft version");
        std::string bootVersion = extractValue(pos, "Boot  version");
        std::string commVersion = extractValue(pos, "Comm version");
        std::string releaseDate = extractValue(pos, "Release Date");
        std::string serial = extractValue(pos, "Barcode");
        std::string specification = extractValue(pos, "Specification");
        std::string cellNumber = extractValue(pos, "Cell Number");
        std::string maxDischgCurr = extractValue(pos, "Max Dischg Curr");
        std::string maxChargeCurr = extractValue(pos, "Max Charge Curr");
        std::string eponPortRate = extractValue(pos, "EPONPort rate");
        std::string consolePortRate = extractValue(pos, "Console Port rate");
        
        // Store the fields
        if (battNum > 0 && battNum <= 64) {
            snprintf(batteryInfo[battNum-1].manufacturer, sizeof(batteryInfo[battNum-1].manufacturer), "%s", manufacturer.c_str());
            snprintf(batteryInfo[battNum-1].model, sizeof(batteryInfo[battNum-1].model), "%s", model.c_str());
            snprintf(batteryInfo[battNum-1].boardVersion, sizeof(batteryInfo[battNum-1].boardVersion), "%s", boardVersion.c_str());
            snprintf(batteryInfo[battNum-1].board, sizeof(batteryInfo[battNum-1].board), "%s", board.c_str());
            snprintf(batteryInfo[battNum-1].mainSoftVersion, sizeof(batteryInfo[battNum-1].mainSoftVersion), "%s", mainSoftVersion.c_str());
            snprintf(batteryInfo[battNum-1].softVersion, sizeof(batteryInfo[battNum-1].softVersion), "%s", softVersion.c_str());
            snprintf(batteryInfo[battNum-1].bootVersion, sizeof(batteryInfo[battNum-1].bootVersion), "%s", bootVersion.c_str());
            snprintf(batteryInfo[battNum-1].commVersion, sizeof(batteryInfo[battNum-1].commVersion), "%s", commVersion.c_str());
            snprintf(batteryInfo[battNum-1].releaseDate, sizeof(batteryInfo[battNum-1].releaseDate), "%s", releaseDate.c_str());
            snprintf(batteryInfo[battNum-1].serial, sizeof(batteryInfo[battNum-1].serial), "%s", serial.c_str());
            snprintf(batteryInfo[battNum-1].specification, sizeof(batteryInfo[battNum-1].specification), "%s", specification.c_str());
            snprintf(batteryInfo[battNum-1].cellNumber, sizeof(batteryInfo[battNum-1].cellNumber), "%s", cellNumber.c_str());
            snprintf(batteryInfo[battNum-1].maxDischgCurr, sizeof(batteryInfo[battNum-1].maxDischgCurr), "%s", maxDischgCurr.c_str());
            snprintf(batteryInfo[battNum-1].maxChargeCurr, sizeof(batteryInfo[battNum-1].maxChargeCurr), "%s", maxChargeCurr.c_str());
            snprintf(batteryInfo[battNum-1].eponPortRate, sizeof(batteryInfo[battNum-1].eponPortRate), "%s", eponPortRate.c_str());
            snprintf(batteryInfo[battNum-1].consolePortRate, sizeof(batteryInfo[battNum-1].consolePortRate), "%s", consolePortRate.c_str());
        }
        
        return;
    }

    // Check if this is a pwr command response
    if(strstr(batteryString, "pwr") != NULL) {
        // Parse pwr response to get battery count and detect new/old model
        // Count lines that have battery data (not "Absent")
        int count = 0;
        int lineNum = 0;
        char *pos = (char *)batteryString;
        
        while (*pos) {
            // Find end of line
            char *eol = strchr(pos, '\n');
            if (eol) {
                *eol = '\0'; // Temporarily null terminate
            }
            
            lineNum++;
            // Skip header lines (first 2 lines)
            if(lineNum > 2) {
                // Check if this line contains battery data (not "Absent")
                if(strstr(pos, "Absent") == NULL && strstr(pos, "Command") == NULL && strstr(pos, "pylon") == NULL) {
                    // Check if it starts with a number (battery number)
                    if(pos[0] >= '1' && pos[0] <= '9') {
                        int battNum = atoi(pos);
                        count++;
                        
                        // Check if the last field is "-" (old model) or a number/word (new model)
                        // Remove all spaces and newlines and check the last character
                        char *noSpaces = strdup(pos);
                        char *write = noSpaces;
                        for(char *read = noSpaces; *read; read++) {
                            if(*read != ' ' && *read != '\t' && *read != '\n' && *read != '\r') {
                                *write++ = *read;
                            }
                        }
                        *write = '\0';
                        
                        char lastChar = noSpaces[strlen(noSpaces) - 1];
                        
                        if (lastChar == '-') {
                            batteryInfo[battNum-1].isNewModel = false;
                        } else {
                            batteryInfo[battNum-1].isNewModel = true;
                        }
                        free(noSpaces);
                    }
                }
            }
            
            if (eol) {
                *eol = '\n'; // Restore newline
                pos = eol + 1;
            } else {
                break;
            }
        }
        
        if(count > 0) {
            battnumber = count;
            printf("Battery count from pwr: %d\n", battnumber);
        }
        return;
    }

    //printf("RXed:\n{%s}\n",batteryString);
    //printf("installed batts: %d\n",battnumber);
    // Split string by lines
    char *line = strtok((char *)batteryString, "\n");
    int cellCount = 0;
    int headerLinesCount = 0;
    int batNumber = 0;

    while (line) {
        if(headerLinesCount == 0) {
            sscanf(line, "bat %d", &batNumber);
            if(batNumber > 0) batNumber -= 1;
        } else if(headerLinesCount >= 3 && cellCount < 15 && sscanf(line, "%d %lf %lf %lf %s %s %s %s %d%% %lf mAH %c",
            &cells[batNumber][cellCount].cellNumber, &cells[batNumber][cellCount].voltage, &cells[batNumber][cellCount].current,
            &cells[batNumber][cellCount].temperature, cells[batNumber][cellCount].state, cells[batNumber][cellCount].voltState,
            cells[batNumber][cellCount].currState, cells[batNumber][cellCount].tempState, &cells[batNumber][cellCount].soc,
            &cells[batNumber][cellCount].coulomb, &cells[batNumber][cellCount].balance) == 11) {
            
            cells[batNumber][cellCount].cellNumber += 1;
            cells[batNumber][cellCount].voltage /= 1000;
            cells[batNumber][cellCount].current /= 1000;
            cells[batNumber][cellCount].temperature /= 1000;
            cells[batNumber][cellCount].coulomb /= 1000;
            cellCount++;
        }

        headerLinesCount++;
        line = strtok(NULL, "\n");
    }

    if(battnumber == (batNumber+1)) {
        publishBattdata();
    }
}

void READBATT::displayCells()
{
    for (int i = 0; i < battnumber; ++i) {
        //for (int j = 0; j < CELLNUMBER; ++j) 
        int j=0;
        {
            printf("Battery %d, Cell %d:", i + 1, j + 1);
            printf("  Cell Number: %d", cells[i][j].cellNumber);
            printf("  Voltage: %.2f", cells[i][j].voltage);
            printf("  Current: %.2f", cells[i][j].current);
            printf("  Temperature: %.2f", cells[i][j].temperature);
            printf("  State: %s", cells[i][j].state);
            printf("  Voltage State: %s", cells[i][j].voltState);
            printf("  Current State: %s", cells[i][j].currState);
            printf("  Temperature State: %s", cells[i][j].tempState);
            printf("  SOC: %d", cells[i][j].soc);
            printf("  Coulomb: %.2f", cells[i][j].coulomb);
            printf("  Balance: %c\n", cells[i][j].balance);
        }
    }
}

void READBATT::publishBattdata() 
{
    // measure the interval and build an average value
    static time_t last_execution_time;
    time_t current_time;
    double elapsed_time;
    double sum = 0.0;
    int num_measurements = 0;

    current_time = time(nullptr);
    elapsed_time = difftime(current_time, last_execution_time);
    last_execution_time = current_time;

    // build average
    num_measurements++;
    sum += elapsed_time;

    int intervall = (int)(sum / num_measurements);
    if (num_measurements >= 10) {
        sum = 0.0;
        num_measurements = 0;
    }

    // Publish status info (works even without batteries)
    json_t *root = json_object();
    json_object_set_new(root, "Name", json_string("Pylontech Battery Monitor"));
    json_object_set_new(root, "IP", json_string(myLocalIP.c_str()));
    json_object_set_new(root, "Interval", json_integer(intervall));
    json_object_set_new(root, "temperature", json_real(get_cpu_temperature()));
    json_object_set_new(root, "batteryCount", json_integer(battnumber));
    
    char *payload = json_dumps(root, JSON_ENCODE_ANY);
    if(payload) {
        std::string topic = getMQTTtopic() + "/values";
        send_to_mqtt(topic, string(payload));
        free(payload);
    }
    json_decref(root);

    // If no batteries, don't publish cell data
    if(battnumber == 0)
        return;

    // create json file for local web page
    string batstr_json = convertPylonDataToJson();
    std::ofstream outFile("/var/www/html/wxdata/batteryinfo.json");
    if (!outFile.is_open()) return;
    outFile << batstr_json;
    outFile.close();

    // publish battery current
    for(int battnum = 0; battnum < battnumber; battnum++) {
        json_t *root = json_object();
        json_object_set_new(root, "current", json_real(cells[battnum][0].current));

        // Convert JSON object to string
        char *payload = json_dumps(root, JSON_ENCODE_ANY);
        if(!payload) {
            printf("JSON serialization failed\n");
            json_decref(root);
            return;
        }
        std::string topic = getMQTTtopic() + "/current/" + std::to_string(battnum+1);
        send_to_mqtt(topic,string(payload));
        free(payload);
        json_decref(root);
    }

    // publish battery info
    for(int battnum = 0; battnum < battnumber; battnum++)
    {
        json_t *root = json_object();

        json_object_set_new(root, "energy",
            json_real(batteryEnergy[battnum]));

        json_object_set_new(root, "capacity",
            json_integer(batteryCapacity[battnum]));

        // Convert JSON object to string
        char *payload = json_dumps(root, JSON_ENCODE_ANY);

        if(!payload)
        {
            printf("JSON serialization failed\n");
            json_decref(root);
            return;
        }

        std::string topic =
            getMQTTtopic() + "/info/" +
            std::to_string(battnum + 1);

        send_to_mqtt(topic, std::string(payload));

        free(payload);
        json_decref(root);
    }

    // Assuming sendJson is similarly adapted to use Jansson
    sendJson(0, "voltage");
    sendJson(1, "temperature");
    sendJson(2, "SoC");
    sendJson(3, "charge");
    sendJson(4, "bal");
    sendJson(5, "status");
    sendJson(6, "vstatus");
    sendJson(7, "cstatus");
    sendJson(8, "tstatus");
    
    // Publish in Home Assistant format if enabled
    publishHomeAssistantData();
}

void READBATT::sendJson(int mode, char *name) 
{
    for(int battnum = 0; battnum < battnumber; battnum++) {
        json_t *root = json_object(); // Create a JSON object
        json_t *tarr = json_array(); // Create a nested JSON array

        for (int cellnum = 0; cellnum < 15; cellnum++) {
            switch (mode) {
                case 0: json_array_append_new(tarr, json_real(cells[battnum][cellnum].voltage)); break;
                case 1: json_array_append_new(tarr, json_real(cells[battnum][cellnum].temperature)); break;
                case 2: json_array_append_new(tarr, json_real(cells[battnum][cellnum].soc)); break;
                case 3: json_array_append_new(tarr, json_real(cells[battnum][cellnum].coulomb)); break;
                case 4: json_array_append_new(tarr, json_integer(cells[battnum][cellnum].balance == 'N' ? 0 : 1)); break;
                case 5: json_array_append_new(tarr, json_string(cells[battnum][cellnum].state)); break; // Directly use char array
                case 6: json_array_append_new(tarr, json_string(cells[battnum][cellnum].voltState)); break;
                case 7: json_array_append_new(tarr, json_string(cells[battnum][cellnum].currState)); break;
                case 8: json_array_append_new(tarr, json_string(cells[battnum][cellnum].tempState)); break;
            }
        }

        // Add the array to the root object with the given name
        json_object_set_new(root, name, tarr);

        // Convert JSON object to string for payload
        char *payload = json_dumps(root, JSON_ENCODE_ANY);
        if(!payload) {
            printf("JSON serialization failed\n");
            json_decref(root);
            return;
        }
        std::string topic = getMQTTtopic() + "/" + name + "/" + std::to_string(battnum+1);
        send_to_mqtt(topic,string(payload));
        free(payload);
        json_decref(root);
    }
}

string READBATT::convertPylonDataToJson() 
{
    json_t* root = json_array();

    // Add timestamp
    char timestamp[50];
    time_t now = time(nullptr);
    struct tm *tm_info = localtime(&now);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);
    
    json_t* meta = json_object();
    json_object_set_new(meta, "timestamp", json_string(timestamp));
    json_object_set_new(meta, "batteryCount", json_integer(battnumber));
    json_array_append_new(root, meta);

    for (int i = 0; i < battnumber; ++i) {
        json_t* pylon = json_object();

        json_object_set_new(pylon, "battery", json_integer(i));
        json_object_set_new(pylon, "current", json_real(cells[i][0].current));

        // Add battery info if available
        if(batteryInfo[i].isNewModel) {
            json_object_set_new(pylon, "isNewModel", json_boolean(true));
            json_object_set_new(pylon, "manufacturer", json_string(batteryInfo[i].manufacturer));
            json_object_set_new(pylon, "model", json_string(batteryInfo[i].model));
            json_object_set_new(pylon, "boardVersion", json_string(batteryInfo[i].boardVersion));
            json_object_set_new(pylon, "board", json_string(batteryInfo[i].board));
            json_object_set_new(pylon, "mainSoftVersion", json_string(batteryInfo[i].mainSoftVersion));
            json_object_set_new(pylon, "softVersion", json_string(batteryInfo[i].softVersion));
            json_object_set_new(pylon, "bootVersion", json_string(batteryInfo[i].bootVersion));
            json_object_set_new(pylon, "commVersion", json_string(batteryInfo[i].commVersion));
            json_object_set_new(pylon, "releaseDate", json_string(batteryInfo[i].releaseDate));
            json_object_set_new(pylon, "serial", json_string(batteryInfo[i].serial));
            json_object_set_new(pylon, "specification", json_string(batteryInfo[i].specification));
            json_object_set_new(pylon, "cellNumber", json_string(batteryInfo[i].cellNumber));
            json_object_set_new(pylon, "maxDischgCurr", json_string(batteryInfo[i].maxDischgCurr));
            json_object_set_new(pylon, "maxChargeCurr", json_string(batteryInfo[i].maxChargeCurr));
            json_object_set_new(pylon, "eponPortRate", json_string(batteryInfo[i].eponPortRate));
            json_object_set_new(pylon, "consolePortRate", json_string(batteryInfo[i].consolePortRate));
            json_object_set_new(pylon, "cycleCount", json_integer(batteryInfo[i].cycleCount));
        } else {
            json_object_set_new(pylon, "isNewModel", json_boolean(false));
        }

        json_t* voltageArray = json_array();
        json_t* temperatureArray = json_array();
        json_t* SoCArray = json_array();
        json_t* chargeArray = json_array();
        json_t* balArray = json_array();

        for (int j = 0; j < CELLNUMBER; ++j) {
            json_array_append_new(voltageArray, json_real(cells[i][j].voltage));
            json_array_append_new(temperatureArray, json_real(cells[i][j].temperature));
            json_array_append_new(SoCArray, json_integer(cells[i][j].soc));
            json_array_append_new(chargeArray, json_real(cells[i][j].coulomb));
            json_array_append_new(balArray, json_integer(cells[i][j].balance));
        }

        json_object_set_new(pylon, "voltage", voltageArray);
        json_object_set_new(pylon, "temperature", temperatureArray);
        json_object_set_new(pylon, "SoC", SoCArray);
        json_object_set_new(pylon, "charge", chargeArray);
        json_object_set_new(pylon, "bal", balArray);

        json_array_append_new(root, pylon);
    }

    char* jsonStr = json_dumps(root, JSON_COMPACT | JSON_PRESERVE_ORDER);
    string result(jsonStr);
    free(jsonStr);

    json_decref(root);

    return result;
}

void READBATT::publishHomeAssistantData()
{
    if(battnumber == 0) return;
    
    // Check if Home Assistant mode is enabled
    if(!homeAssistantMode) return;
    
    // Base topic for all publications
    std::string baseTopic = homeAssistantPrefix;
    
    for(int battnum = 0; battnum < battnumber; battnum++) {
        // Calculate pack voltage (sum of all cells)
        double packVoltage = 0;
        double maxVolt = -1;
        double minVolt = 1000;
        double avgSoc = 0;
        
        for(int cellnum = 0; cellnum < CELLNUMBER; cellnum++) {
            packVoltage += cells[battnum][cellnum].voltage;
            avgSoc += cells[battnum][cellnum].soc;
            if(cells[battnum][cellnum].voltage > maxVolt) maxVolt = cells[battnum][cellnum].voltage;
            if(cells[battnum][cellnum].voltage < minVolt) minVolt = cells[battnum][cellnum].voltage;
        }
        avgSoc /= CELLNUMBER;
        
        // Pack voltage
        char payload[50];
        snprintf(payload, sizeof(payload), "%.3f", packVoltage);
        std::string topic = baseTopic + "/pack_" + std::to_string(battnum+1) + "/pack_voltage";
        send_to_mqtt(topic, payload);
        
        // Pack current
        snprintf(payload, sizeof(payload), "%.3f", cells[battnum][0].current);
        topic = baseTopic + "/pack_" + std::to_string(battnum+1) + "/pack_current";
        send_to_mqtt(topic, payload);
        
        // State of charge
        snprintf(payload, sizeof(payload), "%.0f", avgSoc);
        topic = baseTopic + "/pack_" + std::to_string(battnum+1) + "/state_of_charge";
        send_to_mqtt(topic, payload);
        
        // Delta V in mV
        int deltaV = (int)((maxVolt - minVolt) * 1000);
        snprintf(payload, sizeof(payload), "%d", deltaV);
        topic = baseTopic + "/pack_" + std::to_string(battnum+1) + "/delta_v";
        send_to_mqtt(topic, payload);
        
        // Additional publications for Home Assistant
        // Highest cell voltage
        snprintf(payload, sizeof(payload), "%.3f", maxVolt);
        topic = baseTopic + "/pack_" + std::to_string(battnum+1) + "/highest_cell_voltage";
        send_to_mqtt(topic, payload);
        
        // Lowest cell voltage
        snprintf(payload, sizeof(payload), "%.3f", minVolt);
        topic = baseTopic + "/pack_" + std::to_string(battnum+1) + "/lowest_cell_voltage";
        send_to_mqtt(topic, payload);
        
        // Average temperature
        double avgTemp = 0;
        for(int cellnum = 0; cellnum < CELLNUMBER; cellnum++) {
            avgTemp += cells[battnum][cellnum].temperature;
        }
        avgTemp /= CELLNUMBER;
        snprintf(payload, sizeof(payload), "%.1f", avgTemp);
        topic = baseTopic + "/pack_" + std::to_string(battnum+1) + "/average_temperature";
        send_to_mqtt(topic, payload);
        
        // Remaining capacity (estimated based on SoC) - in Ah
        double remainingCapacity = batteryCapacity[battnum] * (avgSoc / 100.0);
        snprintf(payload, sizeof(payload), "%.1f", remainingCapacity);
        topic = baseTopic + "/pack_" + std::to_string(battnum+1) + "/remaining_capacity";
        send_to_mqtt(topic, payload);
        
        // Total capacity - in Ah
        snprintf(payload, sizeof(payload), "%d", batteryCapacity[battnum]);
        topic = baseTopic + "/pack_" + std::to_string(battnum+1) + "/total_capacity";
        send_to_mqtt(topic, payload);
        
        // Power (Voltage * Current)
        double power = packVoltage * cells[battnum][0].current;
        snprintf(payload, sizeof(payload), "%.3f", power);
        topic = baseTopic + "/pack_" + std::to_string(battnum+1) + "/power";
        send_to_mqtt(topic, payload);
        
        // Cycle count (only for new models)
        if(battnum < battnumber && batteryInfo[battnum].isNewModel) {
            snprintf(payload, sizeof(payload), "%d", batteryInfo[battnum].cycleCount);
            topic = baseTopic + "/pack_" + std::to_string(battnum+1) + "/cycle_count";
            send_to_mqtt(topic, payload);
        }
        
        // Temperature groups (average of cells 1-4, 5-8, 9-12, 13-15)
        double tempGroup1 = 0, tempGroup2 = 0, tempGroup3 = 0, tempGroup4 = 0;
        for(int i = 0; i < 4; i++) {
            tempGroup1 += cells[battnum][i].temperature;
        }
        for(int i = 4; i < 8; i++) {
            tempGroup2 += cells[battnum][i].temperature;
        }
        for(int i = 8; i < 12; i++) {
            tempGroup3 += cells[battnum][i].temperature;
        }
        for(int i = 12; i < 15; i++) {
            tempGroup4 += cells[battnum][i].temperature;
        }
        tempGroup1 /= 4;
        tempGroup2 /= 4;
        tempGroup3 /= 4;
        tempGroup4 /= 3;
        
        snprintf(payload, sizeof(payload), "%.1f", tempGroup1);
        topic = baseTopic + "/pack_" + std::to_string(battnum+1) + "/temp_cells_1_4";
        send_to_mqtt(topic, payload);
        
        snprintf(payload, sizeof(payload), "%.1f", tempGroup2);
        topic = baseTopic + "/pack_" + std::to_string(battnum+1) + "/temp_cells_5_8";
        send_to_mqtt(topic, payload);
        
        snprintf(payload, sizeof(payload), "%.1f", tempGroup3);
        topic = baseTopic + "/pack_" + std::to_string(battnum+1) + "/temp_cells_9_12";
        send_to_mqtt(topic, payload);
        
        snprintf(payload, sizeof(payload), "%.1f", tempGroup4);
        topic = baseTopic + "/pack_" + std::to_string(battnum+1) + "/temp_cells_13_15";
        send_to_mqtt(topic, payload);
        
        // Individual cell data
        for(int cellnum = 0; cellnum < CELLNUMBER; cellnum++) {
            // Cell voltage
            snprintf(payload, sizeof(payload), "%.3f", cells[battnum][cellnum].voltage);
            topic = baseTopic + "/pack_" + std::to_string(battnum+1) + "/cell_" + std::to_string(cellnum) + "/voltage";
            send_to_mqtt(topic, payload);
            
            // Cell temperature
            snprintf(payload, sizeof(payload), "%.1f", cells[battnum][cellnum].temperature);
            topic = baseTopic + "/pack_" + std::to_string(battnum+1) + "/cell_" + std::to_string(cellnum) + "/temperature";
            send_to_mqtt(topic, payload);
        }
    }
}
