#include "web_server/web.hpp"
#include "WiFi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h" // The current version of this program
#include <Preferences.h>   // For NVS storage
#include "ArduinoJson.h"   // For JSON parsing
#include "LittleFS.h"
#include <Update.h>
#include "sdcard/sdcard.h"
#include "wifi/wifi.hpp"
#include "SD_MMC.h"

namespace fs
{
  static FSWebServer myWebServer(LittleFS, 80, "CarFsServer");
  static FSWebServer myWebServerSDMMC(SD_MMC, 80, "CarSDServer");
  static FServerSource FSsource = FServerSource::LittleFS;
  static void getSdcardInfo(fsInfo_t *fsInfo)
  {
    fsInfo->fsName = "SDCard";
    fsInfo->totalBytes = SD_MMC.totalBytes();
    fsInfo->usedBytes = SD_MMC.usedBytes();
  }

  static void getFsInfo(fsInfo_t *fsInfo)
  {
    fsInfo->fsName = "LittleFS";
    fsInfo->totalBytes = LittleFS.totalBytes();
    fsInfo->usedBytes = LittleFS.usedBytes();
  }
  FSWebServer &GetmyWebServer()
  {
    if (FSsource == FServerSource::SDcard)
    {
      return myWebServerSDMMC;
    }
    return myWebServer;
  }
  /* Helper function to generate success response page */
  static String generateSuccessPage(const String &message)
  {
    return "<html><head><meta http-equiv='refresh' content='3;url=/serverIndex'>"
           "<style>body{text-align:center;font-family:sans-serif;margin-top:50px;}</style></head>"
           "<body><h1>" +
           message + "</h1>"
                     "<button onclick=\"window.location.href='/serverIndex'\">Return to Main Page</button>"
                     "</body></html>";
  }

  static void handleCar()
  {
    if (!GetmyWebServer().authenticate_internal())
    {
      Serial.println("Authentication failed, redirecting to login page.");
      return GetmyWebServer().requestAuthentication();
    }
    GetmyWebServer().sendHeader("Connection", "close");
    GetmyWebServer().send(200, "text/html", CarserverIndex);
  }

  static void handleTimeDate()
  {
    if (!GetmyWebServer().authenticate_internal())
    {
      Serial.println("Authentication failed, redirecting to login page.");
      return GetmyWebServer().requestAuthentication();
    }
    GetmyWebServer().sendHeader("Connection", "close");
    GetmyWebServer().send(200, "text/html", timeDatePage);
  }

  static void handleSetTime()
  {
    if (!GetmyWebServer().authenticate_internal())
    {
      Serial.println("Authentication failed, redirecting to login page.");
      return GetmyWebServer().requestAuthentication();
    }
    if (GetmyWebServer().hasArg("plain"))
    {
      String body = GetmyWebServer().arg("plain");
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, body);

      if (error)
      {
        Serial.println("Failed to parse JSON");
        GetmyWebServer().send(400, "text/html", generateSuccessPage("Invalid JSON format"));
        return;
      }

      String datetime = doc["datetime"];
      String timezone = doc["timezone"] | "CET-1CEST,M3.5.0,M10.5.0/3";

      setenv("TZ", timezone.c_str(), 1);
      tzset();

      struct tm timeinfo = {0};
      if (strptime(datetime.c_str(), "%Y-%m-%dT%H:%M", &timeinfo) != NULL)
      {
        timeinfo.tm_sec = 0;

        time_t t = mktime(&timeinfo);
        struct timeval tv = {.tv_sec = t, .tv_usec = 0};

        if (settimeofday(&tv, NULL) == 0)
        {
          time_t now = time(nullptr);
          char buffer[26];
          strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S %Z", localtime(&now));
          Serial.printf("Time set successfully to time: %s\n", buffer);

          GetmyWebServer().send(200, "text/html", generateSuccessPage("Time set successfully to: " + String(buffer)));
        }
        else
        {
          Serial.println("Failed to set system time");
          GetmyWebServer().send(500, "text/html", generateSuccessPage("Failed to set system time"));
        }
      }
      else
      {
        Serial.println("Failed to parse datetime string");
        GetmyWebServer().send(400, "text/html", generateSuccessPage("Invalid datetime format"));
      }
    }
    else
    {
      GetmyWebServer().send(400, "text/html", generateSuccessPage("No data received"));
    }
  }

  static void handleBleUUID()
  {
    if (!GetmyWebServer().authenticate_internal())
    {
      Serial.println("Authentication failed, redirecting to login page.");
      return GetmyWebServer().requestAuthentication();
    }
    GetmyWebServer().sendHeader("Connection", "close");
    GetmyWebServer().send(200, "text/html", bleUUIDPage);
  }

  static void handleSetUUID()
  {
    if (!GetmyWebServer().authenticate_internal())
    {
      Serial.println("Authentication failed, redirecting to login page.");
      return GetmyWebServer().requestAuthentication();
    }
    if (GetmyWebServer().hasArg("plain"))
    {
      String body = GetmyWebServer().arg("plain");
      JsonDocument doc;
      deserializeJson(doc, body);
      String uuid = doc["uuid"];
      Preferences preferences;
      preferences.begin("ble-settings", false);
      preferences.putString("ibeacon_uuid", uuid);
      preferences.end();
      Serial.printf("Saved UUID: %s\n", uuid.c_str());
      GetmyWebServer().send(200, "text/html", generateSuccessPage("UUID saved successfully!"));
    }
    else
    {
      GetmyWebServer().send(400, "text/html", generateSuccessPage("Failed to save UUID."));
    }
  }

  static void handleSetCredentials()
  {
    if (!GetmyWebServer().authenticate_internal())
    {
      Serial.println("Authentication failed, redirecting to login page.");
      return GetmyWebServer().requestAuthentication();
    }
    if (GetmyWebServer().hasArg("plain"))
    {
      String body = GetmyWebServer().arg("plain");
      JsonDocument doc;
      deserializeJson(doc, body);
      String oldUser = doc["oldUser"];
      String oldPwd = doc["oldPwd"];
      String newUser = doc["newUser"];
      String newPwd = doc["newPwd"];
      Preferences preferences;
      preferences.begin("auth-settings", true);
      String storedUser = preferences.getString("username", "admin");
      String storedPwd = preferences.getString("password", "admin");
      preferences.end();

      if (oldUser == storedUser && oldPwd == storedPwd && newUser.length() > 2 && newPwd.length() >= 10)
      {
        preferences.begin("auth-settings", false);
        preferences.putString("username", newUser);
        preferences.putString("password", newPwd);
        preferences.end();

        GetmyWebServer().setAuthentication(newUser.c_str(), newPwd.c_str());

        Serial.printf("Updated credentials: User=%s\n", newUser.c_str());
        GetmyWebServer().send(200, "text/html", generateSuccessPage("Credentials updated successfully!"));
      }
      else
      {
        GetmyWebServer().send(401, "text/html", generateSuccessPage("Unauthorized: Old credentials are incorrect. or short new creditntials."));
        Serial.println("Unauthorized: Old credentials are incorrect or short new credentials.");
        Serial.printf("Current User: %s, Current Pwd: %s\n", storedUser.c_str(), storedPwd.c_str());
        Serial.printf("New User: %s, New Pwd: %s\n", newUser.c_str(), newPwd.c_str());
      }
    }
    else
    {
      GetmyWebServer().send(400, "text/html", generateSuccessPage("Failed to update credentials."));
      Serial.println("Failed to update credentials, no args.");
    }
  }

  static void handleChangeCredentials()
  {
    if (!GetmyWebServer().authenticate_internal())
    {
      Serial.println("Authentication failed, redirecting to login page.");
      return GetmyWebServer().requestAuthentication();
    }
    GetmyWebServer().sendHeader("Connection", "close");
    GetmyWebServer().send(200, "text/html", changeCredentialsPage);
  }

  static void handleGetCurrentTime()
  {
    if (!GetmyWebServer().authenticate_internal())
    {
      Serial.println("Authentication failed, redirecting to login page.");
      return GetmyWebServer().requestAuthentication();
    }
    time_t now = time(nullptr);
    struct tm *timeinfo = localtime(&now);
    char buffer[26];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeinfo);
    String currentTime = String(buffer);
    String jsonResponse = "{\"currentTime\": \"" + currentTime + "\"}";
    GetmyWebServer().send(200, "application/json", jsonResponse);
  }

  static void handleGetCurrentUUID()
  {
    if (!GetmyWebServer().authenticate_internal())
    {
      Serial.println("Authentication failed, redirecting to login page.");
      return GetmyWebServer().requestAuthentication();
    }
    Preferences preferences;
    preferences.begin("ble-settings", true);
    String uuid = preferences.getString("ibeacon_uuid", "00000000-0000-0000-0000-000000000000");
    preferences.end();
    String jsonResponse = "{\"uuid\": \"" + uuid + "\"}";
    GetmyWebServer().send(200, "application/json", jsonResponse);
  }

  static void handleNotFound()
  {
    GetmyWebServer().send(404, "text/plain", "404: Not Found");
  }
  static const unsigned long loginTimeout = 5 * 60 * 1000; // 5 minutes in milliseconds

  bool fs_server_setup(FServerSource source)
  {
    // FILESYSTEM INIT
    FSsource = source;
    Serial.println("Initializing FS...");
    if (FSsource == FServerSource::SDcard)
    {
      Serial.println("Using SD Card as filesystem for web server.");
      if (sdcard::setupSdcard() == false)
        return false;
    }
    else

    {
      if (!LittleFS.begin(false, "/littlefs", 10))
      {
        Serial.println("ERROR on mounting filesystem.");
        return false;
      }
    }
    auto &myServer = GetmyWebServer();
    myServer.enableFsCodeEditor(getFsInfo);
    Preferences preferences;
    preferences.begin("auth-settings", true);
    String storedUser = preferences.getString("username", "admin");
    String storedPwd = preferences.getString("password", "admin");
    preferences.end();
    if (storedUser == "admin" || storedPwd == "admin")
    {
      Serial.println("No credentials found, using default: admin/admin");
    }
    else
    {
      Serial.printf("Stored credentials: User=%s, Pwd=%s\n", storedUser.c_str(), storedPwd.c_str());
    }
    myServer.setAuthentication(storedUser.c_str(), storedPwd.c_str());

    myServer.printFileList(Serial, "/", 3);
    myServer.on("/car", HTTP_GET, handleCar);
    /* Time and Date Page */
    myServer.on("/timeDate", HTTP_GET, handleTimeDate);
    /* Set Time */
    myServer.on("/setTime", HTTP_POST, handleSetTime);
    /* BLE iBeacon UUID Page */
    myServer.on("/bleUUID", HTTP_GET, handleBleUUID);
    /* Set BLE iBeacon UUID */
    myServer.on("/setUUID", HTTP_POST, handleSetUUID);
    /* Change Username and Password */
    myServer.on("/setCredentials", HTTP_POST, handleSetCredentials);
    /* Change Credentials Page */
    myServer.on("/changeCredentials", HTTP_GET, handleChangeCredentials);
    /* Get Current Time */
    myServer.on("/getCurrentTime", HTTP_GET, handleGetCurrentTime);
    myServer.on("/getCurrentUUID", HTTP_GET, handleGetCurrentUUID);
    
    myServer.begin();
    return true;
  }
  /* Helper function to check if the session is still valid */
}

#ifdef PIO_CI
const char *ssid = "test";
const char *wifiPassword = "test";
const char *mqtt_server = "test";
const char *mqttTopic = "test";
const char *cmdTopic = "test";
const char *mqttUser = "test";
const char *mqttPass = "test";
uint32_t mqtt_port = 5000;
#endif