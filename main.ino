#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Stepper.h>
#include <time.h>
#include <vector>
#include <algorithm>

// Stepper Motor Settings
const int stepsPerRevolution = 2048;
#define IN1 19
#define IN2 18
#define IN3 5
#define IN4 17

Stepper myStepper(stepsPerRevolution, IN1, IN3, IN2, IN4);

// Replace with your network credentials
const char* ssid = "ThiagoW";
const char* password = "thiago123";

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);

// Search for parameters in HTTP POST request
const char* PARAM_MANUAL_FEED = "steps";
const char* PARAM_SCHEDULE_FEED = "stepsAlarm";
const char* PARAM_SCHEDULE_HOUR = "hour";
const char* PARAM_SCHEDULE_MINUTE = "minute";
const char* PARAM_DELETE_ALARM = "deleteAlarm";

// Variables to save values from HTML form
String manualORschedule;
String steps;
String stepsAlarm;
String hour;
String minute;

// Alarm structure
struct Alarm {
  int hour;
  int minute;
  int steps;
  bool triggered;
};

std::vector<Alarm> alarms;
bool newRequest = false;

// HTML to build the web page
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>Stepper Motor</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <script>
    function updateTime() {
      var now = new Date();
      var hours = now.getHours().toString().padStart(2, '0');
      var minutes = now.getMinutes().toString().padStart(2, '0');
      var seconds = now.getSeconds().toString().padStart(2, '0');
      document.getElementById('clock').innerHTML = hours + ':' + minutes + ':' + seconds;
    }
    setInterval(updateTime, 1000);
  </script>
</head>
<body onload="updateTime()">
  <h1>PET FEEDER</h1>
  <div id="clock"></div>
  <form action="/" method="POST">
    <h3>Manual feeding</h3>
    <label for="steps">Quantity of food (g):</label>
    <input type="number" name="steps">
    <input type="submit" value="Feed">

    <h3>Schedule feeding</h3>
    <label for="stepsAlarm">Quantity of food (g):</label>
    <input type="number" name="stepsAlarm"><br>
    <label for="hour">Hour:</label>
    <input type="number" name="hour"><br>
    <label for="minute">Minute:</label>
    <input type="number" name="minute"><br>
    <input type="submit" value="Save"><br>
  </form>
  <br>
  <h3>Schedule</h3>
  <div id="meal-plan-msg">%MEAL_PLAN_MSG%</div>
</body>
</html>
)rawliteral";

// WiFi initialization
void initWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi ..");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print('.');
    delay(1000);
  }
  Serial.println(WiFi.localIP());
}

// Time initialization
void initTime() {
  configTime(-3 * 3600, 0, "pool.ntp.org", "time.nist.gov");
  Serial.print("Waiting for time");
  while (!time(nullptr)) {
    Serial.print(".");
    delay(1000);
  }
  Serial.println("");
  Serial.println("Time initialized!");
}

String padZero(int num) {
  if (num < 10) {
    return "0" + String(num);
  }
  return String(num);
}

String getMealPlanMsg() {
  String msg;
  std::sort(alarms.begin(), alarms.end(), [](const Alarm &a, const Alarm &b) {
    if (a.hour == b.hour) {
      return a.minute < b.minute;
    }
    return a.hour < b.hour;
  });

  for (size_t i = 0; i < alarms.size(); ++i) {
    int grams = (alarms[i].steps * 10) / 1024;
    msg += padZero(alarms[i].hour) + ":" + padZero(alarms[i].minute) + " - " +
           String(grams) + "g <form action='/delete' method='POST'><button type='submit' name='deleteAlarm' value='" +
           String(i) + "'>Delete</button></form><br>";
  }
  return msg;
}

int gramsToSteps(int grams) {
  return (grams * 1024) / 10;
}

void setup() {
  Serial.begin(115200);
  initWiFi();
  initTime();
  myStepper.setSpeed(10);

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    String html = String(index_html);
    html.replace("%MEAL_PLAN_MSG%", getMealPlanMsg());
    request->send(200, "text/html", html);
  });

  server.on("/", HTTP_POST, [](AsyncWebServerRequest *request) {
    int params = request->params();
    bool alarmExists = false;
    bool validTime = true;

    for (int i = 0; i < params; i++) {
      AsyncWebParameter* p = (AsyncWebParameter*) request->getParam(i);
      if (p->isPost()) {
        if (p->name() == PARAM_MANUAL_FEED) {
          steps = p->value().c_str();
          int stepsInt = gramsToSteps(steps.toInt());
          Serial.print("Quantity of food to feed: ");
          Serial.println(stepsInt);
          if (stepsInt > 0) {
            manualORschedule = "Manual";
            steps = String(stepsInt);
          }
        }
        if (p->name() == PARAM_SCHEDULE_FEED) {
          stepsAlarm = p->value().c_str();
          int stepsAlarmInt = gramsToSteps(stepsAlarm.toInt());
          Serial.print("Quantity of food in the alarm set to: ");
          Serial.println(stepsAlarmInt);
          if (stepsAlarmInt > 0) {
            manualORschedule = "Schedule";
            stepsAlarm = String(stepsAlarmInt);
          }
        }
        if (p->name() == PARAM_SCHEDULE_HOUR) {
          hour = p->value().c_str();
          int hourInt = hour.toInt();
          if (hourInt < 0 || hourInt > 23) {
            validTime = false;
          }
        }
        if (p->name() == PARAM_SCHEDULE_MINUTE) {
          minute = p->value().c_str();
          int minuteInt = minute.toInt();
          if (minuteInt < 0 || minuteInt > 59) {
            validTime = false;
          }
        }
      }
    }

    if (validTime && manualORschedule == "Schedule") {
      for (auto &alarm : alarms) {
        if (alarm.hour == hour.toInt() && alarm.minute == minute.toInt()) {
          alarmExists = true;
          break;
        }
      }
      if (!alarmExists) {
        alarms.push_back({hour.toInt(), minute.toInt(), stepsAlarm.toInt(), false});
      }
    }

    newRequest = true;
    String html = String(index_html);
    html.replace("%MEAL_PLAN_MSG%", getMealPlanMsg());
    request->send(200, "text/html", html);
  });

  server.on("/delete", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (request->hasParam(PARAM_DELETE_ALARM, true)) {
      int index = request->getParam(PARAM_DELETE_ALARM, true)->value().toInt();
      if (index >= 0 && index < alarms.size()) {
        alarms.erase(alarms.begin() + index);
      }
    }
    String html = String(index_html);
    html.replace("%MEAL_PLAN_MSG%", getMealPlanMsg());
    request->send(200, "text/html", html);
  });

  server.begin();
}

void manualFeeding(int steps) {
  myStepper.step(steps);
}

void scheduleFeeding() {
  time_t now = time(nullptr);
  struct tm *timeinfo = localtime(&now);
  int currentHour = timeinfo->tm_hour;
  int currentMinute = timeinfo->tm_min;

  for (auto &alarm : alarms) {
    if (alarm.hour == currentHour && alarm.minute == currentMinute && !alarm.triggered) {
      manualFeeding(alarm.steps);
      alarm.triggered = true;
      Serial.printf("Feeding scheduled at %02d:%02d with %d steps.\n", alarm.hour, alarm.minute, alarm.steps);
    }
  }

  if (currentHour == 0 && currentMinute == 0) {
    for (auto &alarm : alarms) {
      alarm.triggered = false;
    }
  }
}

void loop() {
  if (newRequest) {
    if (manualORschedule == "Manual" && steps.toInt() > 0) {
      manualFeeding(steps.toInt());
      steps = "";
      manualORschedule = "";
    }
    newRequest = false;
  }

  scheduleFeeding();
}
