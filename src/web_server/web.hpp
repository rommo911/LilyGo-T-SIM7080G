#ifndef WEB_H
#define WEB_H

/* Style */
#include "Arduino.h"
#include <WiFiClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <esp-fs-webserver.h> // https://github.com/cotestatnt/esp-fs-webserver

namespace fs
{
    enum class FServerSource
    {
        LittleFS,
        SDcard
    };
    extern FSWebServer* myWebServer;
    bool fs_server_setup(FServerSource source = FServerSource::LittleFS);

    static const String style =
        "<style>#file-input,input{width:100%;height:44px;border-radius:4px;margin:10px auto;font-size:15px}"
        "input{background:#f1f1f1;border:0;padding:0 15px}body{background:#3498db;font-family:sans-serif;font-size:14px;color:#777}"
        "#file-input{padding:0;border:1px solid #ddd;line-height:44px;text-align:left;display:block;cursor:pointer}"
        "#bar,#prgbar{background-color:#f1f1f1;border-radius:10px}#bar{background-color:#3498db;width:0%;height:10px}"
        "form{background:#fff;max-width:258px;margin:75px auto;padding:30px;border-radius:5px;text-align:center}"
        ".btn{background:#3498db;color:#fff;cursor:pointer}</style>";

    /* Change Username and Password Page */
    static const String changeCredentialsPage =
        "<form name=changeForm>"
        "<h1>Change Credentials</h1>"
        "<input name=oldUser placeholder='Old User ID'> "
        "<input name=oldPwd placeholder='Old Password' type=Password> "
        "<input name=newUser placeholder='New User ID'> "
        "<input name=newPwd placeholder='New Password' type=Password> "
        "<input type=button class=btn value='Save' onclick='saveCredentials()'>"
        "<script>"
        "function saveCredentials() {"
        "  const oldUser = document.changeForm.oldUser.value;"
        "  const oldPwd = document.changeForm.oldPwd.value;"
        "  const newUser = document.changeForm.newUser.value;"
        "  const newPwd = document.changeForm.newPwd.value;"
        "  if (oldUser && oldPwd && newUser && newPwd) {"
        "    fetch('/setCredentials', {"
        "      method: 'POST',"
        "      headers: { 'Content-Type': 'application/json' },"
        "      body: JSON.stringify({ oldUser, oldPwd, newUser, newPwd })"
        "    }).then(response => {"
        "      if (response.ok) alert('Credentials updated successfully!');"
        "      else alert('Failed to update credentials.');"
        "    });"
        "  } else {"
        "    alert('Please fill in all fields.');"
        "  }"
        "}"
        "</script>" +
        style;

    /* Server Index Page */
    static const String serverIndex =
        "<script src='https://ajax.googleapis.com/ajax/libs/jquery/3.2.1/jquery.min.js'></script>"
        "<br><br>"
        "<div id='prg'></div>"
        "<br><div id='prgbar'><div id='bar'></div></div><br></form>"
        "<button class=btn onclick=\"window.location.href='/timeDate'\">Set Time and Date</button>"
        "<br><br>"
        "<button class=btn onclick=\"window.location.href='/bleUUID'\">Set BLE iBeacon UUID</button>"
        "<br><br>"
        "<button class=btn onclick=\"window.location.href='/changeCredentials'\">Change Credentials</button>"
        "<script>"
        "function sub(obj){"
        "var fileName = obj.value.split('\\\\');"
        "document.getElementById('file-input').innerHTML = '   '+ fileName[fileName.length-1];"
        "};"
        "$('form').submit(function(e){"
        "e.preventDefault();"
        "var form = $('#upload_form')[0];"
        "var data = new FormData(form);"
        "$.ajax({"
        "url: '/update',"
        "type: 'POST',"
        "data: data,"
        "contentType: false,"
        "processData:false,"
        "xhr: function() {"
        "var xhr = new window.XMLHttpRequest();"
        "xhr.upload.addEventListener('progress', function(evt) {"
        "if (evt.lengthComputable) {"
        "var per = evt.loaded / evt.total;"
        "$('#prg').html('progress: ' + Math.round(per*100) + '%');"
        "$('#bar').css('width',Math.round(per*100) + '%');"
        "}"
        "}, false);"
        "return xhr;"
        "},"
        "success:function(d, s) {"
        "console.log('success!') "
        "},"
        "error: function (a, b, c) {"
        "}"
        "});"
        "});"
        "</script>" +
        style;

    /* Time and Date Page */
    static const String timeDatePage =
        "<form name=timeForm>"
        "<h1>Set Time and Date</h1>"
        "<p>Current ESP32 Time: <span id='currentTime'></span></p>"
        "<input type='datetime-local' id='datetime' name='datetime'>"
        "<input type='button' class=btn value='Set Time' onclick='sendTime()'>"
        "<script>"
        "function updateCurrentTime() {"
        "  fetch('/getCurrentTime')"
        "    .then(response => response.json())"
        "    .then(data => {"
        "      document.getElementById('currentTime').innerText = data.currentTime;"
        "    });"
        "}"
        "function sendTime() {"
        "  const datetime = document.getElementById('datetime').value;"
        "  if (datetime) {"
        "    fetch('/setTime', {"
        "      method: 'POST',"
        "      headers: { 'Content-Type': 'application/json' },"
        "      body: JSON.stringify({ datetime })"
        "    }).then(response => {"
        "      if (response.ok) alert('Time set successfully!');"
        "      else alert('Failed to set time.');"
        "    });"
        "  } else {"
        "    alert('Please select a valid date and time.');"
        "  }"
        "}"
        "document.addEventListener('DOMContentLoaded', () => {"
        "  updateCurrentTime();"
        "  const now = new Date();"
        "  const localDatetime = now.toISOString().slice(0, 16);"
        "  document.getElementById('datetime').value = localDatetime;"
        "});"
        "</script>" +
        style;

    /* BLE iBeacon UUID Page */
    static const String bleUUIDPage =
        "<form name=uuidForm>"
        "<h1>Set BLE iBeacon UUID</h1>"
        "<p>Current UUID: <span id='currentUUID'></span></p>"
        "<input name=uuid placeholder='Enter UUID'>"
        "<input type='button' class=btn value='Save UUID' onclick='saveUUID()'>"
        "<script>"
        "function saveUUID() {"
        "  const uuid = document.uuidForm.uuid.value;"
        "  if (uuid) {"
        "    fetch('/setUUID', {"
        "      method: 'POST',"
        "      headers: { 'Content-Type': 'application/json' },"
        "      body: JSON.stringify({ uuid })"
        "    }).then(response => {"
        "      if (response.ok) alert('UUID saved successfully!');"
        "      else alert('Failed to save UUID.');"
        "    });"
        "  } else {"
        "    alert('Please enter a valid UUID.');"
        "  }"
        "}"
        "function updateCurrentUUID() {"
        "  fetch('/getCurrentUUID')"
        "    .then(response => response.json())"
        "    .then(data => {"
        "      document.getElementById('currentUUID').innerText = data.uuid;"
        "    });"
        "}"
        "document.addEventListener('DOMContentLoaded', () => {"
        "  updateCurrentUUID();"
        "  const uuid = document.uuidForm.uuid.value;"
        "  if (uuid) {"
        "    document.getElementById('uuid').value = uuid;"
        "  }"
        "});"
        "</script>" +
        style;

}
#endif