#include "config.h"
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <Adafruit_Fingerprint.h> // Example library
#include <HardwareSerial.h>
#include <Wire.h>
#include <RTCLib.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

// Create RTC object
RTC_DS3231 rtc;
// NTP Client setup
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", TIME_OFFSET, TIME_SYNC_DELAY); // UTC, update every 60 seconds

bool rtcInitialized = true;

HardwareSerial mySerial(2); // UART2 (TX2=17, RX2=16)
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&mySerial);

// Create objects
WebServer server(80);
Preferences nvs;

// Variables
String ssid = "";
String password = "";
bool wifiConnected = false;
String scannedNetworks = "";

static String incomingJSON;
static bool haveNewCommand = false;

WiFiClient mqttClient;
PubSubClient mqtt(mqttClient); // initialize MQTT client

// SoftAP credentials
const char *ap_ssid = "SmartBulb_Setup";
const char *ap_password = "12345678";

String companyID;
String branchID;
String deviceCode;
const char *defaultTopic = "unimanage/registerDevice";

int responseCode = 0;

// Function declarations
void connectToWiFi();
void startSoftAP();
void setupSoftAPServer();
void setupNormalMode();
void scanWiFiNetworks();
String getSetupPageHTML();
void setupMQTT();
void resetDevice(bool type);
bool addUser(const JsonObject &newMember);
void logAttendance(String user_id, String timestamp, String status);
void setupAttendanceDir();
void cleanupAttendance(int daysToKeep);
void setupFPSensor();
uint8_t saveFingerprint(uint16_t id);
uint16_t getNextAvailableID();
JsonDocument getSPIFFSStatus();
bool deleteUser(const String &userId);
void deviceInfo();
uint32_t dateStringToSeconds(String dateString);
uint32_t getCurrentTimestamp();
bool authenticateUser(JsonObject &obj);
void checkFingerprint();
bool authenticateUser(JsonObject &obj);
int getIndexByBioId(uint16_t bio_id, JsonDocument &doc);

void connectToWiFi()
{
    Serial.println("Connecting to WiFi: " + ssid);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), password.c_str());

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20)
    {
        delay(500);
        Serial.print(".");
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED)
    {
        wifiConnected = true;
        Serial.println("");
        Serial.println("WiFi Connected!");
        Serial.println("IP Address: " + WiFi.localIP().toString());

        // Save to nvs
        nvs.putString("ssid", ssid);
        nvs.putString("password", password);
        nvs.putBool("haveWiFiCred", true);

        // Stop any existing server and start in normal mode
        server.stop();
        delay(1000);
        setupNormalMode();
        setupMQTT();
    }
    else
    {
        Serial.println("");
        Serial.println("Failed to connect to WiFi. Starting SoftAP mode...");
        startSoftAP();
    }
}

void startSoftAP()
{
    Serial.println("Starting SoftAP mode...");
    WiFi.mode(WIFI_AP);
    WiFi.softAP(ap_ssid, ap_password);

    IPAddress IPAdd = WiFi.softAPIP();
    Serial.println("SoftAP IP address: " + IPAdd.toString());
    Serial.println("Connect to WiFi: " + String(ap_ssid));
    Serial.println("Password: " + String(ap_password));
    Serial.println("Open browser and go to: http://" + IPAdd.toString());

    // Scan for available networks
    scanWiFiNetworks();

    setupSoftAPServer();
}

void setupSoftAPServer()
{
    // Serve the WiFi setup page
    server.on("/", HTTP_GET, []()
              {
    String html = getSetupPageHTML();
    server.send(200, "text/html", html); });

    // Handle WiFi scan refresh
    server.on("/scan", HTTP_GET, []()
              {
    scanWiFiNetworks();
    String html = getSetupPageHTML();
    server.send(200, "text/html", html); });

    // Handle WiFi credentials submission
    server.on("/save", HTTP_POST, []()
              {
    if (server.hasArg("ssid") && server.hasArg("password")) {
      String newSSID = server.arg("ssid");
      String newPassword = server.arg("password");
      
      Serial.println("Received WiFi credentials:");
      Serial.println("SSID: " + newSSID);
      Serial.println("Password: " + newPassword);
      
      
      // Update global variables
      ssid = newSSID;
      password = newPassword;
      
      String response = "<!DOCTYPE html><html><head>";
      response += "<meta charset='UTF-8'>";
      response += "<title>Smart Bulb Setup</title>";
      response += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
      response += "<style>body{font-family:Arial;text-align:center;margin:50px;background:#f0f0f0;}</style></head>";
      response += "<body><h2>WiFi Credentials Saved!</h2>";
      response += "<p>Your smart bulb will now try to connect to: <strong>" + newSSID + "</strong></p>";
      response += "<p>Please wait while connecting...</p>";
      response += "<div id='status'>Connecting...</div>";
      response += "<script>";
      response += "setTimeout(function(){";
      response += "document.getElementById('status').innerHTML = 'Please check your device status';";
      response += "}, 10000);";
      response += "</script>";
      response += "</body></html>";
      
      server.send(200, "text/html", response);
      
      // Wait a moment then try to connect
      delay(2000);
      
      // Stop SoftAP server
      server.stop();
      
      // Try to connect to WiFi
      connectToWiFi();
    } else {
      server.send(400, "text/plain", "Missing SSID or Password");
    } });

    // Handle reset request
    server.on("/reset", HTTP_GET, []()
              {
    nvs.clear();
    String response = "<!DOCTYPE html><html><head><title>Smart Bulb</title>";
    response += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
    response += "<style>body{font-family:Arial;text-align:center;margin:50px;}</style></head>";
    response += "<body><h2>WiFi Settings Reset</h2>";
    response += "<p>WiFi credentials have been cleared.</p>";
    response += "<p>Device will restart in SoftAP mode...</p>";
    response += "</body></html>";
    server.send(200, "text/html", response);
    
    delay(2000);
    ssid = "";
    password = "";
    startSoftAP(); });

    server.begin();
    Serial.println("SoftAP HTTP server started");
}

void setupNormalMode()
{
    // Setup server routes for normal operation
    server.on("/", HTTP_GET, []()
              {
    String html = "<!DOCTYPE html><html><head>";
    html += "<meta charset='UTF-8'>";
    html += "<title>Smart Bulb</title>";
    html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
    html += "<style>";
    html += "body{font-family:Arial;text-align:center;margin:50px;background:#f0f0f0;}";
    html += ".container{max-width:400px;margin:0 auto;background:white;padding:30px;border-radius:10px;box-shadow:0 2px 10px rgba(0,0,0,0.1);}";
    html += ".status{color:green;font-weight:bold;font-size:18px;}";
    html += ".info{background:#e7f3ff;padding:15px;border-radius:5px;margin:20px 0;}";
    html += "button{background:#f44336;color:white;padding:10px 20px;border:none;border-radius:5px;cursor:pointer;margin-top:20px;}";
    html += "button:hover{background:#d32f2f;}";
    html += "</style></head>";
    html += "<body><div class='container'>";
    html += "<h2>Smart Bulb Online</h2>";
    html += "<div class='status'>Connected Successfully!</div>";
    html += "<div class='info'>";
    html += "<p><strong>WiFi Network:</strong> " + ssid + "</p>";
    html += "<p><strong>IP Address:</strong> " + WiFi.localIP().toString() + "</p>";
    html += "<p><strong>Signal Strength:</strong> " + String(WiFi.RSSI()) + " dBm</p>";
    html += "</div>";
    html += "<p>Your smart bulb is now connected to your WiFi network.</p>";
    html += "<button onclick='if(confirm(\"Reset WiFi settings?\")) window.location.href=\"/reset\"'>Reset WiFi Settings</button>";
    html += "</div></body></html>";
    server.send(200, "text/html", html); });

    server.on("/reset", HTTP_GET, []()
              {
    nvs.clear();
    String response = "<!DOCTYPE html><html><head><title>Smart Bulb</title>";
    response += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
    response += "<style>body{font-family:Arial;text-align:center;margin:50px;}</style></head>";
    response += "<body><h2>WiFi Settings Reset</h2>";
    response += "<p>WiFi credentials have been cleared.</p>";
    response += "<p>Device will restart in setup mode...</p>";
    response += "</body></html>";
    server.send(200, "text/html", response);
    
    delay(2000);
    ssid = "";
    password = "";
    wifiConnected = false;
    startSoftAP(); });

    server.begin();
    Serial.println("Normal mode HTTP server started");
}

void scanWiFiNetworks()
{
    Serial.println("Scanning for WiFi networks...");
    WiFi.mode(WIFI_AP_STA); // Enable both AP and STA mode for scanning

    int networkCount = WiFi.scanNetworks();
    scannedNetworks = "";

    if (networkCount == 0)
    {
        Serial.println("No networks found");
        scannedNetworks = "<option value=''>No networks found</option>";
    }
    else
    {
        Serial.printf("Found %d networks:\n", networkCount);

        // Build HTML options for dropdown
        for (int i = 0; i < networkCount; i++)
        {
            String networkSSID = WiFi.SSID(i);
            int networkRSSI = WiFi.RSSI(i);
            wifi_auth_mode_t encryptionType = WiFi.encryptionType(i);

            // Skip hidden networks
            if (networkSSID.length() == 0)
                continue;

            // Remove duplicates
            if (scannedNetworks.indexOf("value='" + networkSSID + "'") != -1)
                continue;

            String signalIcon = "";
            if (networkRSSI > -50)
                signalIcon = "Strong";
            else if (networkRSSI > -60)
                signalIcon = "Good";
            else if (networkRSSI > -70)
                signalIcon = "Fair";
            else
                signalIcon = "Weak";

            String lockIcon = (encryptionType == WIFI_AUTH_OPEN) ? "Open" : "Secured";

            scannedNetworks += "<option value='" + networkSSID + "'>";
            scannedNetworks += networkSSID + " (" + signalIcon + ", " + lockIcon;
            scannedNetworks += ", " + String(networkRSSI) + " dBm)</option>";

            Serial.printf("%d: %s (%d dBm) %s\n", i + 1, networkSSID.c_str(), networkRSSI,
                          (encryptionType == WIFI_AUTH_OPEN) ? "Open" : "Secured");
        }
    }

    WiFi.scanDelete(); // Free memory
    Serial.println("WiFi scan completed");
}

String getSetupPageHTML()
{
    String html = "<!DOCTYPE html><html><head>";
    html += "<meta charset='UTF-8'>";
    html += "<title>Smart Bulb WiFi Setup</title>";
    html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
    html += "<style>";
    html += "body{font-family:Arial,sans-serif;margin:0;padding:20px;background:#f0f0f0;}";
    html += ".container{max-width:400px;margin:0 auto;background:white;padding:30px;border-radius:10px;box-shadow:0 2px 10px rgba(0,0,0,0.1);}";
    html += "h2{color:#333;text-align:center;margin-bottom:30px;}";
    html += "label{display:block;margin:15px 0 5px;color:#555;font-weight:bold;}";
    html += "select,input[type='password']{width:100%;padding:12px;border:2px solid #ddd;border-radius:5px;font-size:16px;box-sizing:border-box;background:white;}";
    html += "select:focus,input[type='password']:focus{border-color:#4CAF50;outline:none;}";
    html += "button{width:100%;background:#4CAF50;color:white;padding:12px;border:none;border-radius:5px;font-size:18px;cursor:pointer;margin-top:20px;}";
    html += "button:hover{background:#45a049;}";
    html += ".refresh-btn{background:#2196F3;margin-bottom:10px;font-size:14px;padding:8px;}";
    html += ".refresh-btn:hover{background:#1976D2;}";
    html += ".info{background:#e7f3ff;padding:15px;border-radius:5px;margin-bottom:20px;font-size:14px;}";
    html += ".manual-entry{margin-top:20px;padding-top:20px;border-top:1px solid #ddd;}";
    html += ".manual-entry a{color:#666;text-decoration:none;font-size:14px;}";
    html += ".manual-entry a:hover{color:#333;}";
    html += "</style></head><body>";

    html += "<div class='container'>";
    html += "<h2>Smart Bulb Setup</h2>";
    html += "<div class='info'>Select your WiFi network from the list below</div>";

    html += "<form action='/save' method='POST'>";

    html += "<label for='ssid'>Available WiFi Networks:</label>";
    html += "<select id='ssid' name='ssid' required>";
    html += "<option value=''>-- Select WiFi Network --</option>";
    html += scannedNetworks;
    html += "</select>";

    html += "<button type='button' class='refresh-btn' onclick='window.location.href=\"/scan\"'>Refresh Networks</button>";

    html += "<label for='password'>WiFi Password:</label>";
    html += "<input type='password' id='password' name='password' required placeholder='Enter WiFi password'>";

    html += "<button type='submit'>Connect to WiFi</button>";
    html += "</form>";

    html += "<div class='manual-entry'>";
    html += "<a href='#' onclick='toggleManualEntry()'>Enter network manually</a>";
    html += "</div>";

    html += "</div>";

    html += "<script>";
    html += "function toggleManualEntry() {";
    html += "  var select = document.getElementById('ssid');";
    html += "  if (select.tagName === 'SELECT') {";
    html += "    var input = document.createElement('input');";
    html += "    input.type = 'text';";
    html += "    input.id = 'ssid';";
    html += "    input.name = 'ssid';";
    html += "    input.placeholder = 'Enter WiFi network name manually';";
    html += "    input.required = true;";
    html += "    input.style.cssText = select.style.cssText;";
    html += "    select.parentNode.replaceChild(input, select);";
    html += "  }";
    html += "}";
    html += "</script>";

    html += "</body></html>";
    return html;
}

void sendJsonResponse(const JsonDocument &doc)
{
    String jsonStr;
    serializeJsonPretty(doc, jsonStr);

    if (mqtt.connected())
    {
        String topic = "unimanage/" + companyID + "/" + branchID + "/" + deviceCode + "/callback";
        mqtt.publish(topic.c_str(), jsonStr.c_str());
    }
}

void mqttCallback(char *topic, byte *payload, unsigned int length)
{
    Serial.println("callback of mqtt");
    if (strcmp(topic, ("unimanage/" + companyID + "/" + branchID + "/" + deviceCode + "/command").c_str()) == 0 || strcmp(topic, defaultTopic) == 0)
    {
        char msg[length + 1];
        memcpy(msg, payload, length);
        msg[length] = '\0'; // Ensure null-termination
        incomingJSON = String(msg);
        haveNewCommand = true;
    }
    else
    {
        Serial.print("topic name: ");
        Serial.println(topic);
    }
}

//reconnectMqtt
/*
void reconnectMQTT()
{
    Serial.println("Connecting with MQTT server..");
    JsonDocument doc;

    doc["type"] = "mqtt_status";
    doc["message"] = mqtt.state();

    if (mqtt.connect("UNI_Manage"))
    {
        Serial.println("Connected With MQTT Server");

        if (nvs.getBool("haveRegistered"))
        {
            companyID = nvs.getString("companyID");
            branchID = nvs.getString("branchID");
            deviceCode = nvs.getString("deviceCode");
            mqtt.subscribe(("unimanage/" + companyID + "/" + branchID + "/" + deviceCode + "/command").c_str());
            Serial.println("topic name1 : " + ("unimanage/" + companyID + "/" + branchID + "/" + deviceCode + "/command"));
        }
        else
        {
            mqtt.subscribe(defaultTopic);
            Serial.println("topic name2 : " + String(defaultTopic));
        }

        doc["status"] = "success";
    }
    else
    {
        Serial.print("failed, rc=");
        Serial.print(mqtt.state());
        doc["status"] = "failed";
    }
    sendJsonResponse(doc);
}
*/

void reconnectMQTT()
{
    Serial.println("Connecting with MQTT server..");
    JsonDocument doc;

    doc["type"] = "mqtt_status";
    doc["message"] = mqtt.state();

    if (mqtt.connect("UNI_Manage"))
    {
        Serial.println("Connected With MQTT Server");

        if (nvs.getBool("haveRegistered"))
        {
            companyID = nvs.getString("companyID", "");
            branchID = nvs.getString("branchID", "");
            deviceCode = nvs.getString("deviceCode", "");
            
            // Check if we have valid IDs before subscribing
            if (companyID.length() > 0 && branchID.length() > 0 && deviceCode.length() > 0)
            {
                String topicName = "unimanage/" + companyID + "/" + branchID + "/" + deviceCode + "/command";
                mqtt.subscribe(topicName.c_str());
                Serial.println("Subscribed to topic: " + topicName);
            }
            else
            {
                Serial.println("Warning: Empty company/branch/device IDs");
                mqtt.subscribe(defaultTopic);
                Serial.println("Subscribed to default topic: " + String(defaultTopic));
            }
        }
        else
        {
            mqtt.subscribe(defaultTopic);
            Serial.println("Subscribed to default topic: " + String(defaultTopic));
        }

        doc["status"] = "success";
    }
    else
    {
        Serial.print("MQTT connection failed, rc=");
        Serial.print(mqtt.state());
        doc["status"] = "failed";
    }
    sendJsonResponse(doc);
}

void setupMQTT()
{
    mqtt.setServer(IP, 1883);
    mqtt.setCallback(mqttCallback);
    mqtt.setBufferSize(MQTT_MAX_BUFFER_SIZE);
}

// Function to handle incoming BLE data
void receiveFromMobile(String data)
{
    Serial.print("📥 Received from mobile: ");
    Serial.println(data);

    if (data.length() > 0)
    {
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, data);
        if (error)
        {
            if (error == DeserializationError::NoMemory)
                Serial.println("no memory in heap");
            else
                Serial.printf("JSON parse error: %s\n", error.c_str());
            return;
        }
        String commandType = doc["type"].as<String>();
        if (commandType == "registerDevice" && !nvs.getBool("haveRegistered"))
        {
            deviceCode = doc["deviceCode"].as<String>();
            branchID = doc["branchID"].as<String>();
            companyID = doc["companyID"].as<String>();
            nvs.putString("deviceCode", deviceCode);
            nvs.putString("branchID", branchID);
            nvs.putString("companyID", companyID);
            nvs.putBool("haveRegistered", true);
            mqtt.subscribe(("unimanage/" + companyID + "/" + branchID + "/" + deviceCode + "/command").c_str());
            mqtt.unsubscribe(defaultTopic);
        }
        else if (commandType == "enrollUser")
        {
            addUser(doc.as<JsonObject>()) ? doc["status"] = 1 : doc["status"] = 0;

            doc["type"] = "enrollUser";
            doc["message"] = responseCode;
            sendJsonResponse(doc);
        }
        else if (commandType == "spiffsStatus")
        {
            sendJsonResponse(getSPIFFSStatus());
        }
        else if (commandType == "deleteUser")
        {
            deleteUser(doc["userId"]) ? doc["status"] = 1 : doc["status"] = 0;
            sendJsonResponse(doc);
        }
        else if (commandType == "deviceInfo")
        {
            deviceInfo();
        }
        else
        {
            doc.clear();
            doc["type"] = "error";
            doc["status"] = "failed";
            doc["message"] = "Invalid command";
            sendJsonResponse(doc);
        }
    }
}

void deviceInfo()
{
    File file = SPIFFS.open("/members.json", "r");
    if (!file)
    {
        Serial.println("Failed to open members file");
        return;
    }

    String fileContent = file.readString();
    file.close();

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, fileContent);

    if (error)
    {
        Serial.println("Failed to parse JSON");
        return;
    }
    doc["timeStamp"] = getCurrentTimestamp();

    sendJsonResponse(doc);
}

void resetDevice(bool type)
{
    nvs.putBool("haveRegistered", false);
    nvs.putBool("haveWiFiCred", false);
    nvs.clear();
    SPIFFS.format();

    finger.emptyDatabase();

    JsonDocument initialDoc;
    JsonArray array = initialDoc.to<JsonArray>();

    File writeFile = SPIFFS.open("/members.json", "w");
    serializeJson(initialDoc, writeFile);
    writeFile.close();

    ESP.restart();
}

// Function to load JSON from file into document
bool loadJsonFromFile(JsonDocument &doc, const char *filename)
{
    File file = SPIFFS.open(filename, "r");
    if (!file)
    {
        Serial.println("Failed to open file for reading");
        return false;
    }

    String fileContent = file.readString();
    file.close();

    DeserializationError error = deserializeJson(doc, fileContent);
    if (error)
    {
        Serial.print("JSON parsing failed: ");
        Serial.println(error.c_str());
        return false;
    }

    Serial.println("JSON loaded from file successfully");
    return true;
}

// Function to save JSON document to file
bool saveJsonToFile(JsonDocument &doc, const char *filename)
{
    File file = SPIFFS.open(filename, "w");
    if (!file)
    {
        Serial.println("Failed to open file for writing");
        return false;
    }

    if (serializeJson(doc, file) == 0)
    {
        Serial.println("Failed to write to file");
        file.close();
        return false;
    }

    file.close();
    Serial.println("JSON saved to file successfully");
    return true;
}

// Function to add a new user.
/*
bool addUser(const JsonObject &newMember)
{
    uint16_t id = getNextAvailableID();

    Serial.println("id: " + id);

    if (id == 0)
        return 0;

    if (saveFingerprint(id) != true)
        return 0;

    newMember.remove("type");
    newMember["punchingId1"] = id;
    newMember["subsEndInSec"] = dateStringToSeconds(newMember["subscriptionEnd"]);

    JsonDocument members;

    if (!loadJsonFromFile(members, "/members.json"))
    {
        return 0;
    }

    JsonArray membersArray = members.as<JsonArray>();

    JsonObject addedMember = membersArray.createNestedObject();

    for (JsonPair kv : newMember)
    {
        addedMember[kv.key()] = kv.value();
    }

    saveJsonToFile(members, "/members.json");

    nvs.putUShort("lastUsedID", (id));

    return true;
}

*/

bool addUser(const JsonObject &newMember)
{
    uint16_t id = getNextAvailableID();

    Serial.println("Assigned ID: " + String(id));

    if (id == 0)
    {
        Serial.println("Failed to get available ID");
        return false;
    }

    if (saveFingerprint(id) != true)
    {
        Serial.println("Failed to save fingerprint");
        return false;
    }

    // Create a copy of the member object to modify
    JsonDocument tempDoc;
    JsonObject modifiableMember = tempDoc.to<JsonObject>();
    
    // Copy all data except "type"
    for (JsonPair kv : newMember)
    {
        if (strcmp(kv.key().c_str(), "type") != 0)
        {
            modifiableMember[kv.key()] = kv.value();
        }
    }
    
    modifiableMember["punchingId1"] = id;
    modifiableMember["subsEndInSec"] = dateStringToSeconds(modifiableMember["subscriptionEnd"]);

    JsonDocument members;

    if (!loadJsonFromFile(members, "/members.json"))
    {
        Serial.println("Failed to load members file");
        return false;
    }

    JsonArray membersArray = members.as<JsonArray>();

    JsonObject addedMember = membersArray.createNestedObject();

    for (JsonPair kv : modifiableMember)
    {
        addedMember[kv.key()] = kv.value();
    }

    if (!saveJsonToFile(members, "/members.json"))
    {
        Serial.println("Failed to save members file");
        return false;
    }

    nvs.putUShort("lastUsedID", id);
    Serial.println("User added successfully with ID: " + String(id));

    return true;
}

// Function to delete user
bool deleteUser(const String &userId)
{
    // Parse JSON
    JsonDocument doc; // Adjust size based on your needs
    loadJsonFromFile(doc, "/members.json");

    // Get the array of members
    JsonArray membersArray = doc.as<JsonArray>();
    bool userFound = false;
    int indexToRemove = -1;

    // Find the user by userId
    for (int i = 0; i < membersArray.size(); i++)
    {
        JsonObject member = membersArray[i];
        if (member["userId"].as<String>() == userId)
        {
            userFound = true;
            indexToRemove = i;

            // Print user details before deletion
            Serial.println("Found user to delete:");
            Serial.print("  User ID: ");
            Serial.println(member["userId"].as<String>());
            Serial.print("  Name: ");
            Serial.println(member["name"].as<String>());
            Serial.print("  User Type: ");
            Serial.println(member["userType"].as<int>());
            Serial.print("  Subscription End: ");
            Serial.println(member["subscriptionEnd"].as<String>());
            Serial.print("  Punching ID: ");
            Serial.println(member["punchingId"].as<int>());

            // Delete fingerPrint data from sensor
            finger.deleteModel(member["punchingId"].as<int>());

            break;
        }
    }

    if (!userFound)
    {
        Serial.print("User with ID '");
        Serial.print(userId);
        Serial.println("' not found");
        return false;
    }

    // Remove the user from array
    membersArray.remove(indexToRemove);

    saveJsonToFile(doc, "/members.json");

    Serial.print("Successfully deleted user: ");
    Serial.println(userId);
    return true;
}

void logAttendance(String user_id, String timestamp, String status)
{
    String date = timestamp.substring(0, 10); // Extract YYYY-MM-DD
    String filePath = "/attendance/" + date + ".json";

    File file = SPIFFS.open(filePath, FILE_READ);
    DynamicJsonDocument doc(2048);
    JsonArray records;

    if (file)
    {
        DeserializationError error = deserializeJson(doc, file);
        file.close();
        if (error)
        {
            Serial.println("Failed to parse attendance file: " + String(error.c_str()));
            doc.clear();
        }
        records = doc.as<JsonArray>();
    }
    else
    {
        records = doc.to<JsonArray>();
    }

    // Add new attendance record
    JsonObject record = records.createNestedObject();
    record["user_id"] = user_id;
    record["timestamp"] = timestamp;
    record["status"] = status;

    // Write back to file
    file = SPIFFS.open(filePath, FILE_WRITE);
    if (!file)
    {
        Serial.println("Failed to open attendance file for writing");
        return;
    }
    serializeJson(doc, file);
    file.close();
    Serial.println("Attendance logged for " + user_id);

    // Optionally send to MQTT server
    JsonDocument mqttDoc;
    mqttDoc["type"] = "attendance";
    mqttDoc["user_id"] = user_id;
    mqttDoc["timestamp"] = timestamp;
    mqttDoc["status"] = status;
    sendJsonResponse(mqttDoc);
}

// Function to create attendance directory if it doesn't exist
void setupAttendanceDir()
{
    if (!SPIFFS.exists("/attendance"))
    {
        SPIFFS.mkdir("/attendance");
        Serial.println("Created /attendance directory");
    }
}

JsonDocument getSPIFFSStatus()
{
    // Initialize SPIFFS if not already initialized
    if (!SPIFFS.begin(true))
    {
        JsonDocument errorDoc;
        errorDoc["error"] = "SPIFFS Mount Failed";
        return errorDoc;
    }

    // Count files first to properly size the JsonDocument
    int fileCount = 0;
    File root = SPIFFS.open("/");
    if (root && root.isDirectory())
    {
        File file = root.openNextFile();
        while (file)
        {
            if (!file.isDirectory())
            {
                fileCount++;
            }
            file = root.openNextFile();
        }
    }

    JsonDocument doc;

    // Get partition information
    size_t totalBytes = SPIFFS.totalBytes();
    size_t usedBytes = SPIFFS.usedBytes();
    size_t freeBytes = totalBytes - usedBytes;

    // Calculate values in KB with one decimal place
    float totalKB = totalBytes / 1024.0;
    float usedKB = usedBytes / 1024.0;
    float freeKB = freeBytes / 1024.0;

    // Add partition information to document
    JsonObject partition = doc.createNestedObject("partition");
    partition["total_kb"] = roundf(totalKB * 10) / 10.0; // Round to 1 decimal place
    partition["used_kb"] = roundf(usedKB * 10) / 10.0;
    partition["free_kb"] = roundf(freeKB * 10) / 10.0;
    partition["used_percent"] = roundf((usedBytes * 1000.0) / totalBytes) / 10.0;
    partition["free_percent"] = roundf((freeBytes * 1000.0) / totalBytes) / 10.0;

    // Reset and read root directory
    root = SPIFFS.open("/");
    if (!root || !root.isDirectory())
    {
        doc["error"] = "Failed to open directory";
        return doc;
    }

    // Add files information
    JsonArray files = doc.createNestedArray("files");
    size_t totalFileSize = 0;
    fileCount = 0;

    // Iterate through all files
    File file = root.openNextFile();
    while (file)
    {
        if (!file.isDirectory())
        {
            fileCount++;
            size_t fileSize = file.size();
            totalFileSize += fileSize;

            // Add file to array
            JsonObject fileObj = files.createNestedObject();
            fileObj["name"] = file.name();
            fileObj["size_kb"] = roundf((fileSize / 1024.0) * 10) / 10.0; // Round to 1 decimal
            fileObj["size_bytes"] = fileSize;
        }
        file = root.openNextFile();
    }

    // Add summary information
    doc["file_count"] = fileCount;
    doc["total_files_kb"] = roundf((totalFileSize / 1024.0) * 10) / 10.0;
    doc["total_files_bytes"] = totalFileSize;

    return doc;
}

void cleanupAttendance(int daysToKeep)
{
    File root = SPIFFS.open("/attendance");
    File file = root.openNextFile();
    String currentDate = "2025-06-08"; // Replace with actual date
    while (file)
    {
        String fileName = file.name();
        String fileDate = fileName.substring(11, 21); // Extract YYYY-MM-DD
        // Compare fileDate with currentDate - daysToKeep
        // Delete if older (implement date comparison logic)
        file = root.openNextFile();
    }
}

void setupFPSensor()
{
    // set the data rate for the sensor serial port
    mySerial.begin(57600, SERIAL_8N1, 16, 17); // RX=16, TX=17 for ESP32 UART2
    finger.begin(57600);
    if (finger.verifyPassword())
    {
        Serial.println("Fingerprint sensor detected");
        finger.getParameters();
        MAX_CAPACITY = finger.capacity;
    }
    else
    {
        Serial.println("Fingerprint sensor not found");
        while (true)
            ; // Halt if sensor fails
    }
}

//Check fingerprint
/*
void checkFingerprint()
{
    int fingerprintID = -1;
    if (finger.getImage() == FINGERPRINT_OK)
    {
        if (finger.image2Tz() == FINGERPRINT_OK)
        {
            if (finger.fingerSearch() == FINGERPRINT_OK)
            {
                fingerprintID = finger.fingerID;
                JsonDocument doc;
                loadJsonFromFile(doc, "/members.json");

                int index = getIndexByBioId(fingerprintID, doc);

                JsonObject user = doc[index];

                serializeJsonPretty(user, Serial);

                if (authenticateUser(user))
                {
                    Serial.println("welcome");
                }
                else
                {
                    Serial.println("get out");
                }
            }
        }
    }
}
*/

void checkFingerprint()
{
    int fingerprintID = -1;
    if (finger.getImage() == FINGERPRINT_OK)
    {
        if (finger.image2Tz() == FINGERPRINT_OK)
        {
            if (finger.fingerSearch() == FINGERPRINT_OK)
            {
                fingerprintID = finger.fingerID;
                JsonDocument doc;
                
                if (!loadJsonFromFile(doc, "/members.json"))
                {
                    Serial.println("Failed to load members file");
                    return;
                }

                int index = getIndexByBioId(fingerprintID, doc);
                
                if (index == -1)
                {
                    Serial.println("User not found in database");
                    return;
                }

                JsonObject user = doc[index];
                
                // Check if user object is valid
                if (user.isNull())
                {
                    Serial.println("Invalid user object");
                    return;
                }

                serializeJsonPretty(user, Serial);

                if (authenticateUser(user))
                {
                    Serial.println("Access granted - welcome");
                    // Log attendance
                    String timestamp = String(getCurrentTimestamp());
                    Serial.println("current time stamp" + timestamp);
                    // logAttendance(user["userId"].as<String>(), timestamp, "IN");
                }
                else
                {
                    Serial.println("Access denied - subscription expired or invalid");
                }
            }
            else
            {
                Serial.println("Fingerprint not found in database");
            }
        }
        else
        {
            Serial.println("Failed to convert fingerprint image");
        }
    }
}

bool authenticateUser(JsonObject &obj)
{
    Serial.println("current time stamp: " + getCurrentTimestamp());
    Serial.println("user time stamp: " + obj["subsEndInSec"].as<uint32_t>());
    if (obj["userType"] == 1)
    {
        return true;
    }
    else if (obj["subsEndInSec"].as<uint32_t>() >= getCurrentTimestamp())
    {
        return true;
    }
    return false;
}

uint8_t saveFingerprint(uint16_t id)
{
    Serial.print("Waiting for valid finger to enroll as #");
    Serial.println(id);
    int p = -1;

    while (p != FINGERPRINT_OK)
    {
        p = finger.getImage();
        switch (p)
        {
        case FINGERPRINT_OK:
            Serial.println("Image taken");
            break;
        case FINGERPRINT_NOFINGER:
            Serial.print(".");
            break;
        case FINGERPRINT_PACKETRECIEVEERR:
            Serial.println("Communication error");
            break;
        case FINGERPRINT_IMAGEFAIL:
            Serial.println("Imaging error");
            break;
        default:
            Serial.println("Unknown error");
            break;
        }
    }

    // OK success!

    p = finger.image2Tz(1);
    switch (p)
    {
    case FINGERPRINT_OK:
        Serial.println("Image converted");
        break;
    case FINGERPRINT_IMAGEMESS:
        Serial.println("Image too messy");
        return p;
    case FINGERPRINT_PACKETRECIEVEERR:
        Serial.println("Communication error");
        return p;
    case FINGERPRINT_FEATUREFAIL:
        Serial.println("Could not find fingerprint features");
        return p;
    case FINGERPRINT_INVALIDIMAGE:
        Serial.println("Could not find fingerprint features");
        return p;
    default:
        Serial.println("Unknown error");
        return p;
    }

    Serial.println("Remove finger");
    delay(2000);
    p = 0;
    while (p != FINGERPRINT_NOFINGER)
    {
        p = finger.getImage();
    }
    Serial.print("ID ");
    Serial.println(id);
    p = -1;
    Serial.println("Place same finger again");
    while (p != FINGERPRINT_OK)
    {
        p = finger.getImage();
        switch (p)
        {
        case FINGERPRINT_OK:
            Serial.println("Image taken");
            break;
        case FINGERPRINT_NOFINGER:
            Serial.print(".");
            break;
        case FINGERPRINT_PACKETRECIEVEERR:
            Serial.println("Communication error");
            break;
        case FINGERPRINT_IMAGEFAIL:
            Serial.println("Imaging error");
            break;
        default:
            Serial.println("Unknown error");
            break;
        }
    }

    // OK success!

    p = finger.image2Tz(2);
    switch (p)
    {
    case FINGERPRINT_OK:
        Serial.println("Image converted");
        break;
    case FINGERPRINT_IMAGEMESS:
        Serial.println("Image too messy");
        return p;
    case FINGERPRINT_PACKETRECIEVEERR:
        Serial.println("Communication error");
        return p;
    case FINGERPRINT_FEATUREFAIL:
        Serial.println("Could not find fingerprint features");
        return p;
    case FINGERPRINT_INVALIDIMAGE:
        Serial.println("Could not find fingerprint features");
        return p;
    default:
        Serial.println("Unknown error");
        return p;
    }

    // OK converted!
    Serial.print("Creating model for #");
    Serial.println(id);

    p = finger.createModel();
    if (p == FINGERPRINT_OK)
    {
        Serial.println("Prints matched!");
    }
    else if (p == FINGERPRINT_PACKETRECIEVEERR)
    {
        Serial.println("Communication error");
        return p;
    }
    else if (p == FINGERPRINT_ENROLLMISMATCH)
    {
        Serial.println("Fingerprints did not match");
        return p;
    }
    else
    {
        Serial.println("Unknown error");
        return p;
    }

    Serial.print("ID ");
    Serial.println(id);
    p = finger.storeModel(id);
    if (p == FINGERPRINT_OK)
    {
        Serial.println("Stored!");
    }
    else if (p == FINGERPRINT_PACKETRECIEVEERR)
    {
        Serial.println("Communication error");
        return p;
    }
    else if (p == FINGERPRINT_BADLOCATION)
    {
        Serial.println("Could not store in that location");
        return p;
    }
    else if (p == FINGERPRINT_FLASHERR)
    {
        Serial.println("Error writing to flash");
        return p;
    }
    else
    {
        Serial.println("Unknown error");
        return p;
    }

    return true;
}

uint16_t getNextAvailableID()
{
    // Get last used ID from Preferences, default to 0 if not set
    uint16_t lastUsedID = nvs.getUShort("lastUsedID", 0);

    // Get current template count
    if (finger.getTemplateCount() != FINGERPRINT_OK)
    {
        responseCode = F_SEN_COMMU;
        return 0; // Error: Cannot communicate with sensor
    }

    // Check if storage is full
    if (finger.templateCount >= MAX_CAPACITY)
    {
        responseCode = F_SEN_FULL;
        return 0; // Error: Sensor storage is full
    }

    if (lastUsedID < MAX_CAPACITY - 1)
    {
        // Update lastUsedID in Preferences
        return lastUsedID + 1;
    }

    // Step 1: Scan for gaps (unused IDs) from 1 to lastUsedID
    for (uint16_t id = 1; id <= lastUsedID; id++)
    {
        if (finger.loadModel(id) == FINGERPRINT_BADLOCATION)
        {
            // Found an unused ID
            return id;
        }
    }

    return 0;
}

// Placeholder for getting user_id from bio_id
int getIndexByBioId(uint16_t bio_id, JsonDocument &doc)
{
    JsonArray userArr = doc.as<JsonArray>();

    for (int i = 0; i < userArr.size(); i++)
    {
        JsonObject user = userArr[i];
        if (user["punchingId1"] == bio_id || user["punchingId2"] == bio_id)
        {
            return i;
        }
    }
    return -1;
}

// Placeholder for getting current timestamp
uint32_t getCurrentTimestamp()
{
    if (rtcInitialized)
    {
        DateTime now = rtc.now();
        return now.unixtime();
    }
    return 0;
}

uint32_t dateStringToSeconds(String dateString)
{
    // Remove 'T' and everything after it (time portion)
    int tIndex = dateString.indexOf('T');
    if (tIndex != -1)
    {
        dateString = dateString.substring(0, tIndex);
    }

    // Parse year, month, day
    int year = dateString.substring(0, 4).toInt();
    int month = dateString.substring(5, 7).toInt();
    int day = dateString.substring(8, 10).toInt();

    // Validate parsed values
    if (year < 1970 || year > 2100 || month < 1 || month > 12 || day < 1 || day > 31)
    {
        Serial.println("Invalid date format!");
        return 0;
    }

    // Create DateTime object with time set to 00:00:00
    DateTime dt(year, month, day, 0, 0, 0);

    // Return Unix timestamp
    return dt.unixtime();
}

void syncRTCWithNTP()
{
    if (!wifiConnected || !rtcInitialized)
    {
        return;
    }

    if (timeClient.update())
    {
        Serial.print("Syncing RTC with NTP... ");

        unsigned long epochTime = timeClient.getEpochTime();

        // Convert to DateTime object
        DateTime ntpTime = DateTime(epochTime);

        // Get current RTC time for comparison
        DateTime rtcTime = rtc.now();

        rtc.adjust(ntpTime);
    }
}


void printMemoryInfo()
{
    Serial.println("=== Memory Info ===");
    Serial.println("Free heap: " + String(ESP.getFreeHeap()) + " bytes");
    Serial.println("Heap size: " + String(ESP.getHeapSize()) + " bytes");
    Serial.println("Free PSRAM: " + String(ESP.getFreePsram()) + " bytes");
    Serial.println("Min free heap: " + String(ESP.getMinFreeHeap()) + " bytes");
    Serial.println("==================");
}

/*
void setup()
{
    Serial.begin(115200);
    delay(1000);

    Serial.println("Smart Bulb Starting...");

    // Initialize SPIFFS
    if (!SPIFFS.begin(true))
    { // true = format on failure
        Serial.println("Failed to mount SPIFFS");
        return;
    }

    // Initialize I2C
    Wire.begin(SDA_PIN, SCL_PIN);

    // Initialize RTC
    if (!rtc.begin())
    {
        Serial.println("ERROR: Couldn't find DS3231 RTC!");
        rtcInitialized = false;
    }

    Serial.println("SPIFFS mounted successfully");
    setupAttendanceDir();

    // Initialize fingerprint sensor
    setupFPSensor();
    // Initialize nvs
    nvs.begin("UniManage", false);

    if (nvs.getBool("haveWiFiCred", 0))
    {
        // Try to load saved WiFi credentials
        ssid = nvs.getString("ssid", "");
        password = nvs.getString("password", "");
        Serial.println("Found saved WiFi credentials");
        Serial.println("SSID: " + ssid);
        connectToWiFi();
        setupMQTT();
    }
    else
    {
        Serial.println("No saved WiFi credentials found");
        startSoftAP();
    }
}

void loop()
{
    server.handleClient();
    mqtt.loop();

    checkFingerprint();

    // Check WiFi connection status periodically
    if (wifiConnected && WiFi.status() != WL_CONNECTED)
    {
        Serial.println("WiFi connection lost! Starting SoftAP mode...");
        wifiConnected = false;
        startSoftAP();
    }

    if (haveNewCommand)
    {
        haveNewCommand = false;
        receiveFromMobile(incomingJSON);
    }

    if (WiFi.status() == WL_CONNECTED && !mqtt.connected())
    {
        reconnectMQTT();
    }

    syncRTCWithNTP();

    if (Serial.available())
    {
        String Sdata = Serial.readStringUntil('\n');
        Sdata.trim();
        Serial.println("Device reset");
        if (Sdata == "reset")
        {
            resetDevice(1);
        }
        else if (Sdata == "spiffs")
        {
            getSPIFFSStatus();
        }
        else if (Sdata == "enroll")
        {
        }
        else if (Sdata == "check")
        {
        }
    }
}
*/

void setup()
{
    Serial.begin(115200);
    delay(1000);

    Serial.println("Smart Bulb Starting...");
    printMemoryInfo(); // Check initial memory

    // Initialize SPIFFS
    if (!SPIFFS.begin(true))
    { 
        Serial.println("Failed to mount SPIFFS");
        return;
    }
    Serial.println("SPIFFS mounted successfully");

    // Initialize I2C
    Wire.begin(SDA_PIN, SCL_PIN);

    // Initialize RTC with better error handling
    if (!rtc.begin())
    {
        Serial.println("ERROR: Couldn't find DS3231 RTC!");
        rtcInitialized = false;
    }
    else
    {
        Serial.println("RTC initialized successfully");
        rtcInitialized = true;
    }

    setupAttendanceDir();

    // Initialize fingerprint sensor
    setupFPSensor();
    
    // Initialize nvs
    nvs.begin("UniManage", false);

    // Initialize time client
    timeClient.begin();

    if (nvs.getBool("haveWiFiCred", false))
    {
        // Try to load saved WiFi credentials
        ssid = nvs.getString("ssid", "");
        password = nvs.getString("password", "");
        Serial.println("Found saved WiFi credentials");
        Serial.println("SSID: " + ssid);
        connectToWiFi();
    }
    else
    {
        Serial.println("No saved WiFi credentials found");
        startSoftAP();
    }
    
    printMemoryInfo(); // Check memory after initialization
}

// 8. Enhanced loop with memory monitoring
void loop()
{
    static unsigned long lastMemoryCheck = 0;
    static unsigned long lastTimeSync = 0;
    
    server.handleClient();
    
    if (mqtt.connected())
    {
        mqtt.loop();
    }

    checkFingerprint();

    // Check WiFi connection status periodically
    if (wifiConnected && WiFi.status() != WL_CONNECTED)
    {
        Serial.println("WiFi connection lost! Starting SoftAP mode...");
        wifiConnected = false;
        startSoftAP();
    }

    if (haveNewCommand)
    {
        haveNewCommand = false;
        receiveFromMobile(incomingJSON);
    }

    if (WiFi.status() == WL_CONNECTED && !mqtt.connected())
    {
        reconnectMQTT();
    }

    // Sync time every 60 seconds
    if (millis() - lastTimeSync > 60000)
    {
        syncRTCWithNTP();
        lastTimeSync = millis();
    }

    // Check memory every 30 seconds
    if (millis() - lastMemoryCheck > 30000)
    {
        if (ESP.getFreeHeap() < 10000) // Less than 10KB free
        {
            Serial.println("WARNING: Low memory!");
            printMemoryInfo();
        }
        lastMemoryCheck = millis();
    }

    if (Serial.available())
    {
        String Sdata = Serial.readStringUntil('\n');
        Sdata.trim();
        
        if (Sdata == "reset")
        {
            Serial.println("Device reset command received");
            resetDevice(true);
        }
        else if (Sdata == "spiffs")
        {
            JsonDocument spiffsStatus = getSPIFFSStatus();
            serializeJsonPretty(spiffsStatus, Serial);
        }
        else if (Sdata == "memory")
        {
            printMemoryInfo();
        }
        else if (Sdata == "time")
        {
            Serial.println("Current timestamp: " + String(getCurrentTimestamp()));
        }
    }
}