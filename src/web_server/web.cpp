#include "web_server/web.hpp"
#include "WiFi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h" // The current version of this program
#include <Preferences.h>   // For NVS storage
#include "ArduinoJson.h"   // For JSON parsing
#include "LittleFS.h"
#include <esp-fs-webserver.h> // https://github.com/cotestatnt/esp-fs-webserver

namespace fs
{
  static const unsigned long loginTimeout = 5 * 60 * 1000; // 5 minutes in milliseconds

  FSWebServer myWebServer(LittleFS, 80, "rami-jazz");

  void getFsInfo(fsInfo_t *fsInfo)
  {
    fsInfo->fsName = "LittleFS";
    fsInfo->totalBytes = LittleFS.totalBytes();
    fsInfo->usedBytes = LittleFS.usedBytes();
  }

  /* Helper function to generate success response page */
  String generateSuccessPage(const String &message)
  {
    return "<html><head><meta http-equiv='refresh' content='3;url=/serverIndex'>"
           "<style>body{text-align:center;font-family:sans-serif;margin-top:50px;}</style></head>"
           "<body><h1>" +
           message + "</h1>"
                     "<button onclick=\"window.location.href='/serverIndex'\">Return to Main Page</button>"
                     "</body></html>";
  }

  void fs_server_loop(void *arg)
  {
    while (1)
    {
      myWebServer.run();
      delay(10); // allow the cpu to switch to other tasks
    }
  }

  void fs_server_setup(void)
  {
    // FILESYSTEM INIT
    Serial.println("Initializing LittleFS...");
    if (!LittleFS.begin(false, "/littlefs", 10))
    {
      Serial.println("ERROR on mounting filesystem.");
      return;
    }
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
    myWebServer.setAuthentication(storedUser.c_str(), storedPwd.c_str());
    myWebServer.setAP("rami-jazz", "11112222");
    myWebServer.startWiFi(5000);
    myWebServer.printFileList(LittleFS, Serial, "/", 2);
    myWebServer.enableFsCodeEditor(getFsInfo);

    myWebServer.on("/car", HTTP_GET, [&]()
                   {
                  if(!myWebServer.authenticate_internal())
                  {
                      Serial.println("Authentication failed, redirecting to login page.");
                    return myWebServer.requestAuthentication();
                  }
                myWebServer.sendHeader("Connection", "close");
                myWebServer.send(200, "text/html", serverIndex); });

    /* Time and Date Page */
    myWebServer.on("/timeDate", HTTP_GET, [&]()
                   {
                  if(!myWebServer.authenticate_internal())
                  {
                      Serial.println("Authentication failed, redirecting to login page.");
                    return myWebServer.requestAuthentication();
                  }
              myWebServer.sendHeader("Connection", "close");
              myWebServer.send(200, "text/html", timeDatePage); });

    /* Set Time */
    myWebServer.on("/setTime", HTTP_POST, [&]()
                   {
                  if(!myWebServer.authenticate_internal())
                  {
                      Serial.println("Authentication failed, redirecting to login page.");
                    return myWebServer.requestAuthentication();
                  }
              if (myWebServer.hasArg("plain"))
              {
                String body = myWebServer.arg("plain");
                JsonDocument doc;
                deserializeJson(doc, body);
                String datetime = doc["datetime"];
                // Parse and set system time here
                Serial.printf("Setting time to: %s\n", datetime.c_str());
                myWebServer.send(200, "text/html", generateSuccessPage("Time set successfully!"));
                //set system time using datetime string without seconds 
                // Example datetime string: "2023-10-01T12:34"
                struct tm timeinfo;
                if (strptime(datetime.c_str(), "%Y-%m-%dT%H:%M", &timeinfo) != NULL)
                {
                  time_t t = mktime(&timeinfo);
                  struct timeval tv = {t, 0};
                  settimeofday(&tv, NULL);
                  Serial.println("Time set successfully.");
                  // print system time 
                  char buffer[26];
                  strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", localtime(&t));
                  Serial.printf("Current time: %s\n", buffer);
                  
                }
                else
                {
                  Serial.println("Failed to parse datetime string.");
                }
              }
              else
              {
                myWebServer.send(400, "text/html", generateSuccessPage("Failed to set time."));
              } });

    /* BLE iBeacon UUID Page */
    myWebServer.on("/bleUUID", HTTP_GET, [&]()
                   {
                  if(!myWebServer.authenticate_internal())
                  {
                      Serial.println("Authentication failed, redirecting to login page.");
                    return myWebServer.requestAuthentication();
                  }
              myWebServer.sendHeader("Connection", "close");
              myWebServer.send(200, "text/html", bleUUIDPage); });

    /* Set BLE iBeacon UUID */
    myWebServer.on("/setUUID", HTTP_POST, [&]()
                   {
                  if(!myWebServer.authenticate_internal())
                 {
                    Serial.println("Authentication failed, redirecting to login page.");
                   return myWebServer.requestAuthentication();
                  }
              if (myWebServer.hasArg("plain"))
              {
                String body = myWebServer.arg("plain");
                JsonDocument doc;
                deserializeJson(doc, body);
                String uuid = doc["uuid"];
                Preferences preferences;
                preferences.begin("ble-settings", false);
                preferences.putString("ibeacon_uuid", uuid);
                preferences.end();
                Serial.printf("Saved UUID: %s\n", uuid.c_str());
                myWebServer.send(200, "text/html", generateSuccessPage("UUID saved successfully!"));
              }
              else
              {
                myWebServer.send(400, "text/html", generateSuccessPage("Failed to save UUID."));
              } });

    /* Change Username and Password */
    myWebServer.on("/setCredentials", HTTP_POST, [&]()
                   {
    if(!myWebServer.authenticate_internal())
    {
        Serial.println("Authentication failed, redirecting to login page.");
      return myWebServer.requestAuthentication();
    }
    if (myWebServer.hasArg("plain"))
    {
      String body = myWebServer.arg("plain");
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
      //got new credentials
      Serial.printf("Old User: %s, Old Pwd: %s, New User: %s, New Pwd: %s\n", oldUser.c_str(), oldPwd.c_str(), newUser.c_str(), newPwd.c_str());

      if (oldUser == storedUser && oldPwd == storedPwd && newUser.length() > 2 && newPwd.length() >= 10)
      {
        // Update credentials
        preferences.begin("auth-settings", false);
        preferences.putString("username", newUser);
        preferences.putString("password", newPwd);
        preferences.end();

        // Update authentication
        myWebServer.setAuthentication(newUser.c_str(), newPwd.c_str());

        Serial.printf("Updated credentials: User=%s\n", newUser.c_str());
        myWebServer.send(200, "text/html", generateSuccessPage("Credentials updated successfully!"));
      }
      else
      {
        myWebServer.send(401, "text/html", generateSuccessPage("Unauthorized: Old credentials are incorrect. or short new creditntials."));
        Serial.println("Unauthorized: Old credentials are incorrect or short new credentials."); 
        //pring what is wrring witrh the credentials including current credentials
        Serial.printf("Current User: %s, Current Pwd: %s\n", storedUser.c_str(), storedPwd.c_str());
        Serial.printf("New User: %s, New Pwd: %s\n", newUser.c_str(), newPwd.c_str());
      }
    }
    else
    {
      myWebServer.send(400, "text/html", generateSuccessPage("Failed to update credentials."));
      Serial.println("Failed to update credentials, no args.");
    } });

    /* Change Credentials Page */
    myWebServer.on("/changeCredentials", HTTP_GET, [&]()
                   {
                if(!myWebServer.authenticate_internal())
                {
                    Serial.println("Authentication failed, redirecting to login page.");
                  return myWebServer.requestAuthentication();
                }
              myWebServer.sendHeader("Connection", "close");
              myWebServer.send(200, "text/html", changeCredentialsPage); });

    /* Get Current Time */
    myWebServer.on("/getCurrentTime", HTTP_GET, [&]()
                   {
                  if(!myWebServer.authenticate_internal())
                  {
                      Serial.println("Authentication failed, redirecting to login page.");
                    return myWebServer.requestAuthentication();
                  }
              time_t now = time(nullptr);
              struct tm *timeinfo = localtime(&now);
              char buffer[26];
              strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeinfo);
              String currentTime = String(buffer);
              String jsonResponse = "{\"currentTime\": \"" + currentTime + "\"}";
              myWebServer.send(200, "application/json", jsonResponse); });
    myWebServer.on("/getCurrentUUID", HTTP_GET, [&]()
                   {
                  if(!myWebServer.authenticate_internal())
                  {
                      Serial.println("Authentication failed, redirecting to login page.");
                    return myWebServer.requestAuthentication();
                  }
              Preferences preferences;
              preferences.begin("ble-settings", true);
              String uuid = preferences.getString("ibeacon_uuid", "00000000-0000-0000-0000-000000000000");
              preferences.end();
              String jsonResponse = "{\"uuid\": \"" + uuid + "\"}";
              myWebServer.send(200, "application/json", jsonResponse); });
    myWebServer.onNotFound([]()
                           { myWebServer.send(404, "text/plain", "404: Not Found"); });

    myWebServer.begin();

    xTaskCreate(fs_server_loop, "fs_server_loop", 8192, NULL, 1, NULL);
  }

  /* Helper function to check if the session is still valid */
}