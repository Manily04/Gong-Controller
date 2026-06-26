#include <WiFiS3.h>
#include <DFRobotDFPlayerMini.h>
#include <EEPROM.h>
#include <RTC.h>

const char* ssid = "WIFI_SSID";
const char* password = "WIFI_PWD";

IPAddress local_IP(192, 168, 178, 141);
IPAddress gateway(192, 168, 178, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress dnsServer(192, 168, 178, 1);

#define RELAY_PIN 7
#define RELAY2_PIN 8
#define ENDSTUFEN_IN_PIN 11

DFRobotDFPlayerMini dfPlayer;

WiFiServer server(80);

bool isPlaying = false;
unsigned long relayStartTime = 0;
const int RELAY_DURATION = 250;

bool endstufenOn = false;
bool desiredEndstufen = false;
unsigned long relay2PulseStart = 0;
const unsigned long RELAY2_PULSE_DURATION = 100;
bool relay2PulseActive = false;

struct CustomButton {
  int id;
  char name[32];
  int track;
  bool enabled;
};
CustomButton customButtons[4] = {
  {7, "Button 7", 0, false},
  {8, "Button 8", 0, false},
  {9, "Button 9", 0, false},
  {10, "Button 10", 0, false}
};

#define EEPROM_MAGIC 0x474F4E47
#define EEPROM_START_ADDR 0

const char* systemPin = "1234";

String eventLogs[10];
int logIndex = 0;

void addLog(String msg);
void saveConfig();
void loadConfig();
void activateGong();
void playAnnouncement(int number);
void stopPlayback();
void resetTimerVars();
void setTimer(int minutes, int repeatCount, String eventTime = "");
void cancelTimer();
void checkTimer();
void handleRequest(WiFiClient &client, String request);
void sendTimerStatus(WiFiClient &client);
void sendMainPage(WiFiClient &client);
void sendAdminPage(WiFiClient &client);
void sendRedirect(WiFiClient &client);
void sendHTMLResponse(WiFiClient &client, String message);
void sendLogs(WiFiClient &client);

void addLog(String msg) {
  RTCTime currentTime;
  bool rtcOk = RTC.getTime(currentTime);
  
  char timeStr[10];
  if (rtcOk) {
    sprintf(timeStr, "[%02d:%02d] ", currentTime.getHour(), currentTime.getMinutes());
  } else {
    unsigned long allSeconds = millis() / 1000;
    int h = (allSeconds / 3600) % 24;
    int m = (allSeconds / 60) % 60;
    sprintf(timeStr, "[%02d:%02d] ", h, m);
  }
  
  eventLogs[logIndex] = String(timeStr) + msg;
  logIndex = (logIndex + 1) % 10;
  Serial.println("LOG: " + msg);
}

void saveConfig() {
  uint32_t magic = EEPROM_MAGIC;
  int addr = EEPROM_START_ADDR;
  EEPROM.put(addr, magic);
  addr += sizeof(magic);
  EEPROM.put(addr, customButtons);
  Serial.println("Konfiguration im EEPROM gespeichert.");
}

void loadConfig() {
  uint32_t magic;
  int addr = EEPROM_START_ADDR;
  EEPROM.get(addr, magic);
  if (magic == EEPROM_MAGIC) {
    addr += sizeof(magic);
    EEPROM.get(addr, customButtons);
    Serial.println("Konfiguration aus EEPROM geladen.");
  } else {
    Serial.println("Keine gültige Konfiguration im EEPROM gefunden (Initialisierung erforderlich).");
    saveConfig();
  }
}

bool timerActive = false;
unsigned long timerStartTime = 0;
unsigned long timerDuration = 0;
int timerRepeatCount = 0;
int timerRepeatsLeft = 0;
int timerInitialRepeats = 0;
String timerEventTime = "";
unsigned long lastRepeatTime = 0;
const unsigned long REPEAT_INTERVAL = 120000;

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);
  
  Serial.println("=== Gong und Durchsage System ===");
  
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);
  pinMode(RELAY2_PIN, OUTPUT);
  digitalWrite(RELAY2_PIN, LOW);

  pinMode(ENDSTUFEN_IN_PIN, INPUT_PULLUP);

  endstufenOn = (digitalRead(ENDSTUFEN_IN_PIN) == LOW);
  desiredEndstufen = endstufenOn;
  Serial.print("Endstufen initial: ");
  Serial.println(endstufenOn ? "AN" : "AUS");
  
  Serial1.begin(9600);
  delay(1000);
  
  Serial.println("Initialisiere DFPlayer Mini...");
  Serial.println("Verwende Hardware Serial1 (Pin 0/1)");
  
  if (!dfPlayer.begin(Serial1, true, true)) {
    Serial.println("FEHLER: DFPlayer Mini nicht gefunden!");
    Serial.println("Bitte prüfen:");
    Serial.println("1. Verkabelung korrekt? TX->Pin0, RX->Pin1 (DIREKT, kein Widerstand bei 3.3V)");
    Serial.println("2. VCC an 3.3V (nicht 5V) angeschlossen?");
    Serial.println("3. Kabel NACH Upload angeschlossen? (Vor Upload abziehen!)");
    Serial.println("4. SD-Karte eingesteckt und FAT32 formatiert?");
    Serial.println("5. MP3-Dateien im Ordner /mp3/ als 0001.mp3, 0002.mp3 usw.?");
    while(true) {
      delay(1000);
    }
  }
  Serial.println("DFPlayer Mini OK!");
  

  dfPlayer.setTimeOut(500);
  dfPlayer.volume(25);
  dfPlayer.EQ(DFPLAYER_EQ_NORMAL);
  dfPlayer.outputDevice(DFPLAYER_DEVICE_SD);
  
  WiFi.setHostname("Gong-Controller");
  
  Serial.print("Verbinde mit WiFi: ");
  Serial.println(ssid);
  Serial.println("Hostname: Gong-Controller");
  
  WiFi.config(local_IP, dnsServer, gateway, subnet);
  
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\nFEHLER: WiFi Verbindung fehlgeschlagen!");
    Serial.println("Bitte SSID und Passwort prüfen!");
    while(true) delay(1000);
  }
  
  addLog("System gestartet");
  
  Serial.println("\nWiFi verbunden!");
  Serial.print("IP Adresse: ");
  Serial.println(WiFi.localIP());
  Serial.println("\nWebserver gestartet!");
  Serial.println("Öffnen Sie im Browser: http://" + WiFi.localIP().toString());
  
  loadConfig();
  
  RTC.begin();
  Serial.println("Synchronisiere Uhrzeit über NTP...");
  unsigned long epoch = 0;
  int retry = 0;
  while (epoch == 0 && retry < 10) {
    epoch = WiFi.getTime();
    if (epoch == 0) delay(500);
    retry++;
  }
  
  if (epoch > 0) {
    RTCTime now(epoch);
    RTC.setTime(now);
    Serial.print("Uhrzeit synchronisiert: ");
    RTCTime currentTime;
    RTC.getTime(currentTime);
    Serial.println(String(currentTime.getHour()) + ":" + String(currentTime.getMinutes()));
  } else {
    Serial.println("NTP-Sync fehlgeschlagen.");
  }

  server.begin();
}

void loop() {
  static unsigned long lastWiFiCheck = 0;
  if (millis() - lastWiFiCheck > 10000) {
    lastWiFiCheck = millis();
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WLAN Verbindung verloren. Reconnect...");
      WiFi.begin(ssid, password);
    }
  }

  if (relayStartTime > 0 && millis() - relayStartTime >= RELAY_DURATION) {
    digitalWrite(RELAY_PIN, LOW);
    relayStartTime = 0;
    Serial.println("Gong beendet");
  }
  

  checkTimer();

  if (relay2PulseActive && (millis() - relay2PulseStart >= RELAY2_PULSE_DURATION)) {
    digitalWrite(RELAY2_PIN, LOW);
    relay2PulseActive = false;
    Serial.println("Endstufen-Relais: 100ms-Puls beendet");
  }

  bool inState = (digitalRead(ENDSTUFEN_IN_PIN) == LOW);
  if (inState != endstufenOn) {
    endstufenOn = inState;
    Serial.print("Endstufen Status geaendert (Eingang): ");
    Serial.println(endstufenOn ? "AN" : "AUS");
  }
  
  WiFiClient client = server.available();
  
  if (client) {
    Serial.println("Neuer Client verbunden");
    String currentLine = "";
    String request = "";
    
    while (client.connected()) {
      if (client.available()) {
        char c = client.read();
        request += c;
        
        if (c == '\n') {
          if (currentLine.length() == 0) {
            handleRequest(client, request);
            break;
          } else {
            currentLine = "";
          }
        } else if (c != '\r') {
          currentLine += c;
        }
      }
    }
    
    client.stop();
    Serial.println("Client getrennt");
  }
}

void handleRequest(WiFiClient &client, String request) {
  int firstLineEnd = request.indexOf('\n');
  String firstLine = request;
  if (firstLineEnd > 0) firstLine = request.substring(0, firstLineEnd);
  Serial.print("HTTP Request: ");
  Serial.println(firstLine);
  
  if (request.indexOf("GET /gong3x") >= 0) {
    addLog("3x Gong sofort gestartet");
    setTimer(0, 3, "Sofort");
    sendRedirect(client);
    return;
  }
  
  if (request.indexOf("GET /gong") >= 0) {
    addLog("Gong manuell");
    activateGong();
    sendRedirect(client);
    return;
  }
  
  if (request.indexOf("GET /timer?") >= 0) {
    int minIndex = request.indexOf("minutes=");
    int repIndex = request.indexOf("repeat=");
    int timeIndex = request.indexOf("time=");
    
    if (minIndex > 0 && repIndex > 0) {
      int minutes = request.substring(minIndex + 8, request.indexOf("&", minIndex)).toInt();
      int repeat = request.substring(repIndex + 7, request.indexOf(" ", repIndex)).toInt();
      String eventTime = "";
      if (timeIndex > 0) {
        eventTime = request.substring(timeIndex + 5, request.indexOf(" ", timeIndex));
        eventTime.replace("%3A", ":");
      }
      
      addLog("Timer gesetzt: " + String(minutes) + " Min (" + String(repeat) + "x) für " + eventTime);
      setTimer(minutes, repeat, eventTime);
      sendRedirect(client);
      return;
    }
  }
  
  if (request.indexOf("GET /canceltimer") >= 0) {
    addLog("Timer abgebrochen");
    cancelTimer();
    sendRedirect(client);
    return;
  }
  
  if (request.indexOf("GET /timerstatus") >= 0) {
    sendTimerStatus(client);
    return;
  }
  
  for (int i = 1; i <= 10; i++) {
    String path = "GET /durchsage" + String(i);
    if (request.indexOf(path) >= 0) {
      addLog("Durchsage " + String(i));
      playAnnouncement(i);
      sendRedirect(client);
      return;
    }
  }
  
  if (request.indexOf("GET /stop") >= 0) {
    addLog("Stopp-Befehl");
    stopPlayback();
    sendRedirect(client);
    return;
  }

  if (request.indexOf("GET /endstufen_toggle") >= 0) {
    desiredEndstufen = !desiredEndstufen;
    digitalWrite(RELAY2_PIN, HIGH);
    relay2PulseActive = true;
    relay2PulseStart = millis();

    Serial.print("Endstufen (Web) toggled. Gewuenscht: ");
    Serial.println(desiredEndstufen ? "AN" : "AUS");

    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: application/json");
    client.println("Connection: close");
    client.println();
    client.print("{\"desired\":");
    client.print(desiredEndstufen ? "true" : "false");
    client.print(",\"actual\":");
    client.print(endstufenOn ? "true" : "false");
    client.println("}");
    return;
  }

  if (request.indexOf("GET /endstufenstatus") >= 0) {
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: application/json");
    client.println("Connection: close");
    client.println();
    client.print("{\"desired\":");
    client.print(desiredEndstufen ? "true" : "false");
    client.print(",\"actual\":");
    client.print(endstufenOn ? "true" : "false");
    client.println("}");
    return;
  }

  
  if (request.indexOf("GET /admin/save_button") >= 0 || request.indexOf("POST /admin/save_button") >= 0) {
    int idIndex = request.indexOf("id=");
    int nameIndex = request.indexOf("name=");
    int trackIndex = request.indexOf("track=");
    
    if (idIndex > 0 && nameIndex > 0 && trackIndex > 0) {
      int id = request.substring(idIndex + 3, request.indexOf("&", idIndex)).toInt();
      int track = request.substring(trackIndex + 6, request.indexOf("&", trackIndex) > 0 ? request.indexOf("&", trackIndex) : request.indexOf(" ", trackIndex)).toInt();
      String nameStr = request.substring(nameIndex + 5, request.indexOf("&", nameIndex));
      nameStr.replace("%20", " ");
      
      if (id >= 7 && id <= 10) {
        int idx = id - 7;
        customButtons[idx].id = id;
        strncpy(customButtons[idx].name, nameStr.c_str(), 31);
        customButtons[idx].name[31] = '\0';
        customButtons[idx].track = track;
        customButtons[idx].enabled = (track > 0);
        saveConfig();
        Serial.print("Button ");
        Serial.print(id);
        Serial.print(" gespeichert.");
      }
    }
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: application/json");
    client.println("Connection: close");
    client.println();
    client.println("{\"status\":\"ok\"}");
    return;
  }

  if (request.indexOf("GET /admin") >= 0) {
    sendAdminPage(client);
    return;
  }
  
  if (request.indexOf("GET /logs") >= 0) {
    sendLogs(client);
    return;
  }
  
  sendMainPage(client);
}

void activateGong() {
  Serial.println("Gong aktiviert!");
  digitalWrite(RELAY_PIN, HIGH);
  relayStartTime = millis();
}

void playAnnouncement(int number) {
  if (isPlaying) {
    Serial.println("Bereits am Abspielen...");
    return;
  }
    
  int actualTrack = 0;
  
  if (number >= 1 && number <= 6) {
    int trackMap[7] = {0, 1, 2, 3, 4, 5, 6};
    actualTrack = trackMap[number];
  } else if (number >= 7 && number <= 10) {
    int buttonIndex = number - 7;
    if (buttonIndex >= 0 && buttonIndex < 4) {
      actualTrack = customButtons[buttonIndex].track;
      if (actualTrack <= 0) {
        Serial.println("Custom Button hat keine gültige Track-Nummer!");
        return;
      }
    } else {
      Serial.println("Ungültige Custom Button Nummer!");
      return;
    }
  } else {
    Serial.println("Ungültige Durchsage-Nummer!");
    return;
  }
  
  Serial.print("Spiele Durchsage ");
  Serial.print(number);
  Serial.print(" (Track ");
  Serial.print(actualTrack);
  Serial.println(")");
  
  dfPlayer.play(actualTrack);
  isPlaying = true;
  
  delay(1000);
  isPlaying = false;
}

void stopPlayback() {
  Serial.println("Stoppe Wiedergabe und alle Timer");
  dfPlayer.stop();
  digitalWrite(RELAY_PIN, LOW);
  relayStartTime = 0;
  isPlaying = false;
  
  cancelTimer();
}

void resetTimerVars() {
  timerActive = false;
  timerDuration = 0;
  timerRepeatsLeft = 0;
  lastRepeatTime = 0;
  timerStartTime = 0;
}

void setTimer(int minutes, int repeatCount, String eventTime) {
^^    ^^  timerActive = true;
  timerStartTime = millis();
  timerRepeatCount = repeatCount;
  timerRepeatsLeft = repeatCount;
  timerInitialRepeats = repeatCount;
  timerEventTime = eventTime;
  
  int offset = repeatCount * 2;
  long adjustedMinutes = (long)minutes - offset;
  
  if (adjustedMinutes <= 0) {
    timerDuration = 0;
    lastRepeatTime = millis() - REPEAT_INTERVAL; 
    Serial.print("Timer sofort gestartet (da Zeit erreicht), ");
  } else {
    timerDuration = (unsigned long)adjustedMinutes * 60000;
    lastRepeatTime = 0;
    Serial.print("Timer gesetzt: ");
    Serial.print(minutes);
    Serial.print(" Min (erster Gong in ");
    Serial.print(adjustedMinutes);
    Serial.print(" Min), ");
  }
  
  Serial.print(repeatCount);
  Serial.println("x gongen");
}

void cancelTimer() {
  resetTimerVars();
  Serial.println("Timer abgebrochen");
}

void checkTimer() {
  if (!timerActive) return;
  
  if (timerRepeatsLeft <= 0) {
    resetTimerVars();
    Serial.println("Timer beendet (keine Wiederholungen)");
    return;
  }

  unsigned long currentTime = millis();
  
  if (timerDuration > 0) {
    unsigned long elapsed = currentTime - timerStartTime;
    if (elapsed >= timerDuration) {
      if (lastRepeatTime == 0 || (currentTime - lastRepeatTime >= REPEAT_INTERVAL)) {
        activateGong();
        timerRepeatsLeft--;
        lastRepeatTime = currentTime;
        
        Serial.print("Timer-Gong! Noch ");
        Serial.print(timerRepeatsLeft);
        Serial.println(" übrig");
        
        if (timerRepeatsLeft <= 0) {
          resetTimerVars();
          Serial.println("Timer abgeschlossen");
        }
      }
    }
  } 
  else {
    unsigned long timeSinceLastGong = currentTime - lastRepeatTime;
    if (timeSinceLastGong >= REPEAT_INTERVAL) {
      activateGong();
      timerRepeatsLeft--;
      lastRepeatTime = currentTime;
      
      Serial.print("Intervall-Gong! Noch ");
      Serial.print(timerRepeatsLeft);
      Serial.println(" übrig");
      
      if (timerRepeatsLeft <= 0) {
        resetTimerVars();
        Serial.println("Intervall-Gong abgeschlossen");
      }
    }
  }
}


void sendLogs(WiFiClient &client) {
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: application/json");
  client.println("Connection: close");
  client.println();
  client.print("[");
  for (int i = 0; i < 10; i++) {
    int idx = (logIndex - 1 - i + 10) % 10;
    if (eventLogs[idx] != "") {
      if (i > 0) client.print(",");
      client.print("\"" + eventLogs[idx] + "\"");
    }
  }
  client.print("]");
}

void sendTimerStatus(WiFiClient &client) {
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: application/json");
  client.println("Connection: close");
  client.println();
  
  client.print("{\"active\":");
  client.print(timerActive ? "true" : "false");
  client.print(",\"remaining\":");
  
  if (timerActive) {
    unsigned long elapsed = millis() - timerStartTime;
    long remaining = (timerDuration - elapsed) / 1000;
    if (remaining < 0) remaining = 0;
    
    if (timerRepeatsLeft > 0 && elapsed >= timerDuration) {
      unsigned long sinceLastRepeat = millis() - lastRepeatTime;
      remaining = (REPEAT_INTERVAL - sinceLastRepeat) / 1000;
    }
    
    client.print(remaining);
  } else {
    client.print("0");
  }
  
  client.print(",\"repeatsLeft\":");
  client.print(timerRepeatsLeft);
  client.print(",\"initialRepeats\":");
  client.print(timerInitialRepeats);
  client.print(",\"eventTime\":\"");
  client.print(timerEventTime);
  client.println("\"}");
}

void sendMainPage(WiFiClient &client) {
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html; charset=UTF-8");
  client.println("Connection: close");
  client.println();
  
  client.println("<!DOCTYPE html>");
  client.println("<html lang='de'>");
  client.println("<head>");
  client.println("<meta charset='UTF-8'>");
  client.println("<meta name='viewport' content='width=device-width, initial-scale=1.0'>");
  client.println("<title>Gong & Durchsage System</title>");
  client.println("<style>");
  client.println("body { font-family: Arial, sans-serif; max-width: 600px; margin: 50px auto; padding: 20px; background: #f0f0f0; display: none; }");
  client.println("h1 { color: #333; text-align: center; }");
  client.println(".button-container { display: flex; flex-direction: column; gap: 15px; margin-top: 30px; }");
  client.println("button { padding: 20px; font-size: 18px; border: none; border-radius: 8px; cursor: pointer; transition: 0.3s; }");
  client.println("button:hover { transform: scale(1.05); box-shadow: 0 4px 8px rgba(0,0,0,0.2); }");
  client.println("button:disabled { opacity: 0.3; cursor: not-allowed; filter: grayscale(1); }");
  client.println(".gong-btn { background: #ff6b6b; color: white; font-weight: bold; font-size: 24px; }");
  client.println(".gong-btn:hover { background: #ff5252; }");
  client.println(".durchsage-btn { background: #4CAF50; color: white; }");
  client.println(".durchsage-btn:hover { background: #45a049; }");
  client.println(".stop-btn { background: #f44336; color: white; }");
  client.println(".stop-btn:hover { background: #da190b; }");
  client.println(".info { background: white; padding: 15px; border-radius: 8px; margin-top: 20px; }");
  client.println(".timer-section { background: white; padding: 20px; border-radius: 8px; margin-bottom: 20px; }");
  client.println(".timer-section h2 { margin-top: 0; color: #333; }");
  client.println(".timer-input { margin: 15px 0; }");
  client.println(".timer-input label { display: block; margin-bottom: 5px; font-weight: bold; }");
  client.println(".timer-input input { width: 100%; padding: 10px; font-size: 18px; border: 2px solid #ddd; border-radius: 5px; }");
  client.println(".timer-buttons { display: flex; gap: 10px; margin: 15px 0; }");
  client.println(".timer-btn { flex: 1; padding: 15px; background: #2196F3; color: white; border: none; border-radius: 8px; font-size: 16px; cursor: pointer; }");
  client.println(".timer-btn:hover { background: #1976D2; }");
  client.println(".timer-display { margin-top: 15px; padding: 15px; background: #e3f2fd; border-radius: 8px; text-align: center; }");
  client.println(".timer-display p { font-size: 24px; font-weight: bold; color: #1976D2; margin: 10px 0; }");
  client.println(".log-section { background: white; padding: 15px; border-radius: 8px; margin-top: 20px; font-family: monospace; font-size: 14px; border: 1px solid #ddd; }");
  client.println(".log-entry { padding: 3px 0; border-bottom: 1px solid #eee; }");
  client.println(".log-entry:last-child { border-bottom: none; }");
  client.println(".login-box { background: white; padding: 30px; border-radius: 12px; box-shadow: 0 4px 15px rgba(0,0,0,0.1); text-align: center; margin-top: 50px; }");
  client.println(".pin-input { width: 120px; font-size: 32px; text-align: center; padding: 10px; margin: 20px 0; border: 2px solid #ddd; border-radius: 8px; letter-spacing: 5px; }");
  client.println("</style>");
  client.println("</head>");
  client.println("<body>");
  
  client.println("<div id='loginSection' style='display:none;'>");
  client.println("<div class='login-box'>");
  client.println("<h1>🔐 System gesperrt</h1>");
  client.println("<p>Bitte 4-stelligen PIN eingeben:</p>");
  client.println("<input type='password' id='pinInput' class='pin-input' maxlength='4' inputmode='numeric' onkeyup='if(event.key===\"Enter\") doLogin()'>");
  client.println("<br>");
  client.println("<button class='timer-btn' style='width:100%; max-width:200px;' onclick='doLogin()'>Anmelden</button>");
  client.println("</div>");
  client.println("</div>");

  client.println("<div id='mainContent' style='display:none;'>");
  client.println("<h1>🔔 Gong & Durchsage System</h1>");
  
  client.println("<div style='background:white; padding:15px; border-radius:8px; margin-bottom:20px; display:flex; align-items:center; gap:10px; border: 2px solid #ff6b6b;'>");
  client.println("<input type='checkbox' id='enableEmergency' onchange='toggleEmergencyButtons()' style='width:25px; height:25px;'>");
  client.println("<label for='enableEmergency' style='font-weight:bold; color:#d32f2f; font-size:18px;'>⚠️ Notfall-Buttons freischalten</label>");
  client.println("</div>");

  client.println("<div class='timer-section'>");
  client.println("<h2>⏰ Veranstaltungs-Timer</h2>");
  client.println("<div id='currentTimeDisplay' style='font-size:20px; font-weight:bold; color:#333; text-align:center; margin-bottom:10px; background:#eee; padding:5px; border-radius:5px;'>--:--</div>");
  client.println("<div class='timer-input'>");
  client.println("<label>Beginn Uhrzeit:</label>");
  client.println("<input type='time' id='eventTime' style='width:100%; padding:10px; font-size:24px; border:2px solid #ddd; border-radius:8px; box-sizing:border-box;' />");
  client.println("</div>");
  client.println("<div class='timer-buttons'>");
  client.println("<button class='timer-btn' onclick='startTimer(1)'>1x Gong</button>");
  client.println("<button class='timer-btn' onclick='startTimer(3)'>3x alle 2 Min</button>");
  client.println("</div>");
  client.println("<div id='timerDisplay' class='timer-display' style='display:none;'>");
  client.println("<p id='timerText'>Timer: --:--</p>");
  client.println("<button class='stop-btn' onclick='cancelTimer()'>Timer abbrechen</button>");
  client.println("</div>");
  client.println("</div>");
  
  client.println("<div class='button-container'>");
  client.println("<button class='gong-btn' onclick='sendCommand(\"/gong\")'>🔔 GONG</button>");
  client.println("<button class='gong-btn' onclick='sendCommand(\"/gong3x\")'>🔔 3x GONG (alle 2 Min.)</button>");

  client.println("<div style='display:flex;gap:16px;align-items:center;flex-wrap:wrap;'>");
  client.println("<button id='endstufenBtn' class='durchsage-btn' style='background:#0066cc;font-size:20px;padding:18px 24px;' onclick='toggleEndstufen()'>Endstufen An / Aus</button>");
  client.println("<div id='endstufenIndicator' style='display:flex;align-items:center;gap:12px;padding:12px 16px;border-radius:12px;background:#fff;border:3px solid #ddd;margin-left:6px;'>");
  client.println("<div id='endstufenBadge' style='width:36px;height:36px;border-radius:50%;background:#f0f0f0;display:flex;align-items:center;justify-content:center;font-weight:bold;color:#333;'>--</div>");
  client.println("<div style='font-size:18px;font-weight:600;'>Endstufen: <span id='endstufenState' style='margin-left:8px;'>--</span></div>");
  client.println("</div>");
  client.println("</div>");

  client.println("<button class='gong-btn emergency-btn' disabled onmousedown='startHold(\"/durchsage1\", this)' onmouseup='cancelHold()' onmouseleave='cancelHold()' ontouchstart='startHold(\"/durchsage1\", this)' ontouchend='cancelHold()'>🚨 Generelle Evakuierung<br><small>3 Sek. halten</small></button>");
  client.println("<button class='gong-btn emergency-btn' disabled style='background:#ff0000;' onmousedown='startHold(\"/durchsage2\", this)' onmouseup='cancelHold()' onmouseleave='cancelHold()' ontouchstart='startHold(\"/durchsage2\", this)' ontouchend='cancelHold()'>⚠️ Amok Alarm<br><small>3 Sek. halten</small></button>");
  client.println("<button class='durchsage-btn emergency-btn' disabled style='background:#4CAF50;' onmousedown='startHold(\"/durchsage5\", this)' onmouseup='cancelHold()' onmouseleave='cancelHold()' ontouchstart='startHold(\"/durchsage5\", this)' ontouchend='cancelHold()'>✅ Amok Entwarnung<br><small>3 Sek. halten</small></button>");
  client.println("<button class='gong-btn emergency-btn' disabled style='background:#ff6600;' onmousedown='startHold(\"/durchsage3\", this)' onmouseup='cancelHold()' onmouseleave='cancelHold()' ontouchstart='startHold(\"/durchsage3\", this)' ontouchend='cancelHold()'>🔥 Feuer Evakuierung<br><small>3 Sek. halten</small></button>");
  client.println("<button class='durchsage-btn' style='background:#9C27B0;' onclick='sendCommand(\"/durchsage4\")'>🧪 TEST</button>");
  client.println("<button class='durchsage-btn' style='background:#FF69B4;' onclick='sendCommand(\"/durchsage6\")'>😄 Späßchen</button>");

  for (int i = 0; i < 4; i++) {
    if (customButtons[i].enabled && customButtons[i].track > 0) {
      String btnHtml = "<button class='durchsage-btn' onclick='sendCommand(\"/durchsage";
      btnHtml += String(7 + i);
      btnHtml += "\")'>⚙️ ";
      btnHtml += customButtons[i].name;
      btnHtml += "</button>";
      client.println(btnHtml);
    }
  }

  client.println("<button class='stop-btn' onclick='sendCommand(\"/stop\")'>⏹️ STOP</button>");
  client.println("<div style='margin-top:20px; border-top:2px solid #ddd; padding-top:20px;'>");
  client.println("<button class='admin-btn' style='background:#666;' onclick='location.href=\"/admin\"'>🔧 Admin Bereich</button>");
  client.println("</div>");
  client.println("</div>");
  
  client.println("<div class='log-section' id='logContainer'>");
  client.println("<strong>Letzte Ereignisse:</strong>");
  client.println("<div id='logEntries'>Lade Logs...</div>");
  client.println("</div>");
  client.println("</div>");
  
  client.println("<script>");
  client.println("let appInitialized = false;");
  client.println("function initApp() {");
  client.println("  if (appInitialized) return;");
  client.println("  appInitialized = true;");
  client.println("  const now = new Date();");
  client.println("  const future = new Date(now.getTime() + 20*60000);");
  client.println("  const h = String(future.getHours()).padStart(2, '0');");
  client.println("  const m = String(future.getMinutes()).padStart(2, '0');");
  client.println("  const el = document.getElementById('eventTime');");
  client.println("  if (el) el.value = h + ':' + m;");
  client.println("  updateTimer(); updateEndstufenStatus(); updateLogs(); updateCurrentTime();");
  client.println("  setInterval(updateTimer, 2000); setInterval(updateEndstufenStatus, 2000); setInterval(updateLogs, 3000); setInterval(updateCurrentTime, 1000);");
  client.println("}");
  client.println("function checkAuth() {");
  client.println("  const now = new Date().getTime();");
  client.println("  const authTime = sessionStorage.getItem('authTime');");
  client.println("  const pin = sessionStorage.getItem('systemPin');");
  client.println("  if (pin === '" + String(systemPin) + "' && authTime && (now - authTime < 300000)) {");
  client.println("    sessionStorage.setItem('authTime', now);");
  client.println("    document.getElementById('loginSection').style.display = 'none';");
  client.println("    document.getElementById('mainContent').style.display = 'block';");
  client.println("    document.body.style.display = 'block';");
  client.println("    initApp();");
  client.println("    return true;");
  client.println("  }");
  client.println("  document.getElementById('loginSection').style.display = 'block';");
  client.println("  document.getElementById('mainContent').style.display = 'none';");
  client.println("  document.body.style.display = 'block';");
  client.println("  return false;");
  client.println("}");
  client.println("function doLogin() {");
  client.println("  const pin = document.getElementById('pinInput').value;");
  client.println("  if (pin === '" + String(systemPin) + "') {");
  client.println("    sessionStorage.setItem('systemPin', pin);");
  client.println("    sessionStorage.setItem('authTime', new Date().getTime());");
  client.println("    checkAuth();");
  client.println("  } else {");
  client.println("    alert('Falscher PIN!');");
  client.println("    document.getElementById('pinInput').value = '';");
  client.println("  }");
  client.println("}");
  client.println("let timerInterval = null;");
  client.println("let holdTimer = null;");
  client.println("let holdProgress = 0;");
  client.println("function sendCommand(cmd) {");
  client.println("  if (!checkAuth()) return;");
  client.println("  fetch(cmd).then(() => console.log('Befehl gesendet: ' + cmd));");
  client.println("}");
  client.println("function startHold(cmd, btn) {");
  client.println("  holdProgress = 0;");
  client.println("  btn.style.opacity = '0.5';");
  client.println("  holdTimer = setInterval(() => {");
  client.println("    holdProgress += 100;");
  client.println("    if (holdProgress >= 3000) {");
  client.println("      clearInterval(holdTimer);");
  client.println("      btn.style.opacity = '1';");
  client.println("      sendCommand(cmd);");
  client.println("      btn.style.background = '#00ff00';");
  client.println("      setTimeout(() => { btn.style.background = ''; }, 500);");
  client.println("    }");
  client.println("  }, 100);");
  client.println("}");
  client.println("function toggleEmergencyButtons() {");
  client.println("  const enabled = document.getElementById('enableEmergency').checked;");
  client.println("  document.querySelectorAll('.emergency-btn').forEach(btn => btn.disabled = !enabled);");
  client.println("}");
  client.println("function updateCurrentTime() {");
  client.println("  const now = new Date();");
  client.println("  const h = String(now.getHours()).padStart(2, '0');");
  client.println("  const m = String(now.getMinutes()).padStart(2, '0');");
  client.println("  document.getElementById('currentTimeDisplay').innerText = 'Aktuelle Zeit: ' + h + ':' + m;");
  client.println("}");
  client.println("function toggleEndstufen() {");
  client.println("  if (!checkAuth()) return;");
  client.println("  fetch('/endstufen_toggle').then(r => r.json()).then(data => {");
  client.println("    // sofortige UI-Aktualisierung basierend auf Rückgabe");
  client.println("    const el = document.getElementById('endstufenState');");
  client.println("    const badge = document.getElementById('endstufenBadge');");
  client.println("    const container = document.getElementById('endstufenIndicator');");
  client.println("    if (el && badge && container) {");
  client.println("      el.innerText = data.actual ? 'AN' : 'AUS';");
  client.println("      badge.innerText = data.actual ? 'ON' : 'OFF';");
  client.println("      badge.style.background = data.actual ? '#2ecc71' : '#e74c3c';");
  client.println("      badge.style.color = '#fff';");
  client.println("      container.style.borderColor = data.actual ? '#2ecc71' : '#e74c3c';");
  client.println("    }");
  client.println("  }).catch(err => { console.log('Toggle error', err); updateEndstufenStatus(); });");
  client.println("}");

  client.println("function updateEndstufenStatus() {");
  client.println("  fetch('/endstufenstatus').then(r => r.json()).then(data => {");
  client.println("    const el = document.getElementById('endstufenState');");
  client.println("    const badge = document.getElementById('endstufenBadge');");
  client.println("    const container = document.getElementById('endstufenIndicator');");
  client.println("    if (!el || !container || !badge) return; ");
  client.println("    el.innerText = data.actual ? 'AN' : 'AUS';");
  client.println("    badge.innerText = data.actual ? 'ON' : 'OFF';");
  client.println("    badge.style.background = data.actual ? '#2ecc71' : '#e74c3c';");
  client.println("    badge.style.color = '#fff';");
  client.println("    container.style.borderColor = data.actual ? '#2ecc71' : '#e74c3c';");
  client.println("    container.style.background = data.actual ? '#e8f9ee' : '#fff0f0';");
  client.println("  }).catch(err => console.log('Endstufen status error', err));");
  client.println("}");
  client.println("function cancelHold() {");
  client.println("  if (holdTimer) {");
  client.println("    clearInterval(holdTimer);");
  client.println("    holdTimer = null;");
  client.println("    const btns = document.querySelectorAll('button');");
  client.println("    btns.forEach(b => b.style.opacity = '1');");
  client.println("  }");
  client.println("}");
  client.println("function updateLogs() {");
  client.println("  fetch('/logs').then(r => r.json()).then(data => {");
  client.println("    const container = document.getElementById('logEntries');");
  client.println("    if (!container) return;");
  client.println("    container.innerHTML = data.map(log => '<div class=\"log-entry\">' + log + '</div>').join('');");
  client.println("  });");
  client.println("}");
  client.println("function startTimer(repeat) {");
  client.println("  if (!checkAuth()) return;");
  client.println("  const timeVal = document.getElementById('eventTime').value;");
  client.println("  if (!timeVal) { alert('Bitte Uhrzeit wählen'); return; }");
  client.println("  const now = new Date();");
  client.println("  const [hours, minutes] = timeVal.split(':');");
  client.println("  const eventDate = new Date();");
  client.println("  eventDate.setHours(parseInt(hours), parseInt(minutes), 0, 0);");
  client.println("  if (eventDate < now) eventDate.setDate(eventDate.getDate() + 1);");
  client.println("  const diffMin = Math.round((eventDate - now) / 60000);");
  client.println("  fetch('/timer?minutes=' + diffMin + '&repeat=' + repeat + '&time=' + encodeURIComponent(timeVal))");
  client.println("    .then(() => {");
  client.println("      document.getElementById('timerDisplay').style.display = 'block';");
  client.println("      updateTimer();");
  client.println("      if (timerInterval) clearInterval(timerInterval);");
  client.println("      timerInterval = setInterval(updateTimer, 1000);");
  client.println("    });");
  client.println("}");
  client.println("function cancelTimer() {");
  client.println("  fetch('/canceltimer').then(() => {");
  client.println("    document.getElementById('timerDisplay').style.display = 'none';");
  client.println("    if (timerInterval) clearInterval(timerInterval);");
  client.println("  });");
  client.println("}");
  client.println("function updateTimer() {");
  client.println("  fetch('/timerstatus')");
  client.println("    .then(r => r.json())");
  client.println("    .then(data => {");
  client.println("      if (!data.active) {");
  client.println("        document.getElementById('timerDisplay').style.display = 'none';");
  client.println("        if (timerInterval) clearInterval(timerInterval);");
  client.println("        return;");
  client.println("      }");
  client.println("      document.getElementById('timerDisplay').style.display = 'block';");
  client.println("      const min = Math.floor(data.remaining / 60);");
  client.println("      const sec = data.remaining % 60;");
  client.println("      const countdown = min + ':' + (sec < 10 ? '0' : '') + sec;");
  client.println("      let msg = '';");
  client.println("      if (data.eventTime === 'Sofort') {");
  client.println("          msg = 'Sofort-Gong aktiv (' + data.repeatsLeft + 'x)';");
  client.println("      } else if (data.initialRepeats === 1) {");
  client.println("          let [h, m] = data.eventTime.split(':').map(Number);");
  client.println("          let t = new Date(); t.setHours(h, m - 2, 0, 0);");
  client.println("          msg = 'Gong gestellt auf ' + String(t.getHours()).padStart(2,'0') + ':' + String(t.getMinutes()).padStart(2,'0');");
  client.println("      } else {");
  client.println("          let [h, m] = data.eventTime.split(':').map(Number);");
  client.println("          let times = [];");
  client.println("          for (let i = 0; i < data.initialRepeats; i++) {");
  client.println("              let t = new Date();");
  client.println("              t.setHours(h, m - (data.initialRepeats - i) * 2, 0, 0);");
  client.println("              let ts = String(t.getHours()).padStart(2,'0') + ':' + String(t.getMinutes()).padStart(2,'0');");
  client.println("              times.push('Gong ' + (i+1) + ': ' + ts);");
  client.println("          }");
  client.println("          msg = times.join(' | ');");
  client.println("      }");
  client.println("      document.getElementById('timerText').innerHTML = '<div>' + msg + '</div><div style=\"font-size:16px; color:#666; margin-top:8px;\">Nächster Gong in: ' + countdown + '</div>';");
  client.println("    });");
  client.println("}");
  client.println("window.onload = function() {");
  client.println("  checkAuth();");
  client.println("};");
  client.println("</script>");
  client.println("</body>");
  client.println("</html>");
}

void sendAdminPage(WiFiClient &client) {
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html; charset=UTF-8");
  client.println("Connection: close");
  client.println();
  
  client.println("<!DOCTYPE html>");
  client.println("<html lang='de'>");
  client.println("<head>");
  client.println("<meta charset='UTF-8'>");
  client.println("<meta name='viewport' content='width=device-width, initial-scale=1.0'>");
  client.println("<title>Admin Panel</title>");
  client.println("<style>");
  client.println("body { font-family: Arial, sans-serif; max-width: 600px; margin: 50px auto; padding: 20px; background: #f0f0f0; }");
  client.println("h1 { color: #333; text-align: center; }");
  client.println(".button-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 15px; margin-top: 20px; }");
  client.println(".admin-btn { padding: 15px; font-size: 16px; border: none; border-radius: 8px; cursor: pointer; background: #2196F3; color: white; }");
  client.println(".admin-btn:hover { background: #1976D2; }");
  client.println(".back-btn { padding: 15px; background: #f44336; color: white; border: none; border-radius: 8px; cursor: pointer; margin-top: 20px; width: 100%; font-size: 18px; }");
  client.println(".admin-btn-disabled { opacity: 0.5; cursor: not-allowed; }");
  client.println(".input-group { margin-bottom: 15px; }");
  client.println(".input-group label { display: block; margin-bottom: 5px; font-weight: bold; }");
  client.println(".input-group input { width: 100%; padding: 10px; border: 2px solid #ddd; border-radius: 5px; box-sizing: border-box; }");
  client.println("</style>");
  client.println("</head>");
  client.println("<body>");
  client.println("<h1>🔧 Admin Panel - Custom Buttons</h1>");
  client.println("<p style='text-align:center; color:#666;'>Verwalte Buttons 7-10</p>");
  client.println("<div class='button-grid'>");
  
  for (int i = 0; i < 4; i++) {
    int btnId = customButtons[i].id;
    client.println("<div style='border:2px solid #ddd; padding:15px; border-radius:8px;'>");
    client.println("<h3>Button " + String(btnId) + "</h3>");
    client.println("<div class='input-group'>");
    client.println("<label>Name:</label>");
    client.println("<input type='text' id='name_" + String(btnId) + "' value='" + customButtons[i].name + "' placeholder='Button Name'>");
    client.println("</div>");
    client.println("<div class='input-group'>");
    client.println("<label>Audio Track Nummer (0 = deaktiviert, 7-10):</label>");
    client.println("<input type='number' id='track_" + String(btnId) + "' value='" + String(customButtons[i].track) + "' min='0' max='10'>");
    client.println("</div>");
    client.println("<button class='admin-btn' onclick='saveButton(" + String(btnId) + ")'>Speichern</button>");
    client.println("</div>");
  }
  
  client.println("</div>");
  client.println("<button class='back-btn' onclick='location.href=\"/\"'>Zurueck</button>");
  client.println("<script>");
  client.println("function saveButton(id) {");
  client.println("  const name = document.getElementById('name_' + id).value;");
  client.println("  const track = document.getElementById('track_' + id).value;");
  client.println("  fetch('/admin/save_button?id=' + id + '&name=' + encodeURIComponent(name) + '&track=' + track)");
  client.println("    .then(r => { alert('Button ' + id + ' gespeichert!'); location.href = '/'; });");
  client.println("}");
  client.println("</script>");
  client.println("</body>");
  client.println("</html>");
}

void sendRedirect(WiFiClient &client) {
  client.println("HTTP/1.1 303 See Other");
  client.println("Location: /");
  client.println("Connection: close");
  client.println();
}

void sendHTMLResponse(WiFiClient &client, String message) {
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html; charset=UTF-8");
  client.println("Connection: close");
  client.println();
  client.println("<!DOCTYPE html><html><head><meta charset='UTF-8'></head><body>");
  client.println("<h2>" + message + "</h2>");
  client.println("<a href='/'>Zurück</a>");
  client.println("</body></html>");
}



