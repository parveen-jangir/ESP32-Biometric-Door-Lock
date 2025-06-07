// Initially connect system to Wi-Fi usign SoftAP method (TESTED)
void softAP_initialSetup()
{
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>

    const char *apSSID = "ESP32-Setup";  // Name of the ESP32's network
    const char *apPassword = "setup123"; // Password for security

    WebServer server(80);
    Preferences preferences;

    // Serve the WiFi setup page
    void handleRoot()
    {
        String html = "<form action='/save' method='POST'>"
                      "SSID: <input type='text' name='ssid'><br>"
                      "Password: <input type='password' name='password'><br>"
                      "<input type='submit' value='Save'>"
                      "</form>";
        server.send(200, "text/html", html);
    }

    // Handle submitted credentials
    void handleSave()
    {
        String ssid = server.arg("ssid");
        String password = server.arg("password");

        WiFi.begin(ssid.c_str(), password.c_str());
        if (WiFi.waitForConnectResult() == WL_CONNECTED)
        {
            server.send(200, "text/plain", "Connected! Rebooting...");
            preferences.putString("ssid", ssid);
            preferences.putString("password", password);
            delay(1000);
            ESP.restart();
        }
        else
        {
            server.send(200, "text/plain", "Failed to connect. Try again.");
        }
    }

    void setup()
    {
        Serial.begin(115200);

        // Check for stored credentials
        preferences.begin("wifi", false);
        String ssid = preferences.getString("ssid", "");
        String password = preferences.getString("password", "");

        if (ssid != "")
        {
            WiFi.begin(ssid.c_str(), password.c_str());
            if (WiFi.waitForConnectResult() == WL_CONNECTED)
            {
                Serial.println("Connected to WiFi!");
                return;
            }
        }

        // Start SoftAP if no credentials or connection fails
        WiFi.softAP(apSSID, apPassword);
        Serial.println("SoftAP started");

        // Set up web server routes
        server.on("/", handleRoot);
        server.on("/save", handleSave);
        server.begin();
    }

    void loop()
    {
        server.handleClient();
    }
}