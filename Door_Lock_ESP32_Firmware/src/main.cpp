#include "config.h"
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

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

// Function declarations
void connectToWiFi();
void startSoftAP();
void setupSoftAPServer();
void setupNormalMode();
void scanWiFiNetworks();
String getSetupPageHTML();
void setupMQTT();

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

void resetDevice(bool type)
{
    nvs.putBool("haveRegistered", false);
    nvs.putBool("haveWiFiCred", false);
    nvs.clear();
    ESP.restart();
}

void setup()
{
    Serial.begin(115200);
    delay(1000);

    Serial.println("Smart Bulb Starting...");

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

    if (Serial.available())
    {
        String Sdata = Serial.readStringUntil('\n');
        Sdata.trim();
        Serial.println("Device reset");
        if (Sdata == "reset")
        {
            resetDevice(1);
        }
    }
}