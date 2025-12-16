

#include <WiFiS3.h>
#include <DFRobotDFPlayerMini.h>

// WiFi Zugangsdaten
const char* ssid = "WLAN-NAME";
const char* password = "WLAN-Passwort";

// Hardware Pins
#define RELAY_PIN 7
// --- Endstufen (zweites Relais, 100ms Pulse) ---
#define RELAY2_PIN 8
#define ENDSTUFEN_IN_PIN 11      // Arduino liest das 5V-Signal (Status-Eingang)

DFRobotDFPlayerMini dfPlayer;

// WiFi Server auf Port 80
WiFiServer server(80);

bool isPlaying = false;
unsigned long relayStartTime = 0;
const int RELAY_DURATION = 250; // Gong 0,2 Sekunde

bool endstufenOn = false;           // aktueller Zustand (gelesen von ENDSTUFEN_IN_PIN)
bool desiredEndstufen = false;      // gewünschter Zustand (wird auf ENDSTUFEN_OUT_PIN ausgegeben)
unsigned long relay2PulseStart = 0;
const unsigned long RELAY2_PULSE_DURATION = 100; // 100 ms Pulse
bool relay2PulseActive = false;

// --- Admin Panel & Custom Buttons (7-10) ---
struct CustomButton {
  int id;
  String name;
  int track;
  bool enabled;
};
CustomButton customButtons[4] = {
  {7, "Button 7", 0, false},
  {8, "Button 8", 0, false},
  {9, "Button 9", 0, false},
  {10, "Button 10", 0, false}
};

// Timer Variablen
bool timerActive = false;
unsigned long timerStartTime = 0;
unsigned long timerDuration = 0;
int timerRepeatCount = 0;  // 0 = einmal, 3 = dreimal
int timerRepeatsLeft = 0;
unsigned long lastRepeatTime = 0;
const unsigned long REPEAT_INTERVAL = 120000;

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);
  
  Serial.println("=== Gong und Durchsage System ===");
  
  // Relais Pin konfigurieren
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW); // Relais aus (LOW = aus)
  // Zweites Relais (Endstufen) und zugehörige Pins
  pinMode(RELAY2_PIN, OUTPUT);
  digitalWrite(RELAY2_PIN, LOW); // Relais2 aus

  pinMode(ENDSTUFEN_IN_PIN, INPUT_PULLUP); // Rückmeldungskontakt zwischen Pin und GND

  endstufenOn = (digitalRead(ENDSTUFEN_IN_PIN) == LOW); // LOW = Kontakt geschlossen = AN
  desiredEndstufen = endstufenOn;
  Serial.print("Endstufen initial: ");
  Serial.println(endstufenOn ? "AN" : "AUS");
  
  Serial1.begin(9600);
  delay(1000);
  
  Serial.println("Initialisiere DFPlayer Mini...");
  Serial.println("Verwende Hardware Serial1 (Pin 0/1)");
  
  if (!dfPlayer.begin(Serial1, true, true)) {  // isACK = true, doReset = true
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
  dfPlayer.volume(25);  // Lautstärke 0-30
  dfPlayer.EQ(DFPLAYER_EQ_NORMAL);
  dfPlayer.outputDevice(DFPLAYER_DEVICE_SD);
  
  // Hostname setzen (Netzwerkname)
  WiFi.setHostname("Gong-Controller");
  
  Serial.print("Verbinde mit WiFi: ");
  Serial.println(ssid);
  Serial.println("Hostname: Gong-Controller");
  
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
  
  Serial.println("\nWiFi verbunden!");
  Serial.print("IP Adresse: ");
  Serial.println(WiFi.localIP());
  Serial.println("\nWebserver gestartet!");
  Serial.println("Öffnen Sie im Browser: http://" + WiFi.localIP().toString());
  
  server.begin();
}

void loop() {
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
    activateGong();  // 1. Gong sofort
    setTimer(0, 2);  // Timer für 2. und 3. Gong (2 Wiederholungen)
    sendHTMLResponse(client, "3x Gong gestartet (sofort + 2x alle 2 Min)");
    return;
  }
  
  if (request.indexOf("GET /gong") >= 0) {
    activateGong();
    sendHTMLResponse(client, "Gong aktiviert!");
    return;
  }
  
  if (request.indexOf("GET /timer?") >= 0) {
    int minIndex = request.indexOf("minutes=");
    int repIndex = request.indexOf("repeat=");
    
    if (minIndex > 0 && repIndex > 0) {
      int minutes = request.substring(minIndex + 8, request.indexOf("&", minIndex)).toInt();
      int repeat = request.substring(repIndex + 7, request.indexOf(" ", repIndex)).toInt();
      
      setTimer(minutes, repeat);
      String msg = "Timer gesetzt: " + String(minutes) + " Min, ";
      msg += (repeat == 1) ? "1x gongen" : "3x alle 2 Min gongen";
      sendHTMLResponse(client, msg);
      return;
    }
  }
  
  if (request.indexOf("GET /canceltimer") >= 0) {
    cancelTimer();
    sendHTMLResponse(client, "Timer abgebrochen");
    return;
  }
  
  if (request.indexOf("GET /timerstatus") >= 0) {
    sendTimerStatus(client);
    return;
  }
  
  for (int i = 1; i <= 6; i++) {
    String path = "GET /durchsage" + String(i);
    if (request.indexOf(path) >= 0) {
      playAnnouncement(i);
      sendHTMLResponse(client, "Durchsage " + String(i) + " wird abgespielt");
      return;
    }
  }

  for (int i = 7; i <= 10; i++) {
    String path = "GET /durchsage" + String(i);
    if (request.indexOf(path) >= 0) {
      playAnnouncement(i);
      sendHTMLResponse(client, "Durchsage " + String(i) + " wird abgespielt");
      return;
    }
  }
  
  if (request.indexOf("GET /stop") >= 0) {
    stopPlayback();
    sendHTMLResponse(client, "Wiedergabe gestoppt");
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
    Serial.println("Save-Button-Route erkannt!");
    int idIndex = request.indexOf("id=");
    int nameIndex = request.indexOf("name=");
    int trackIndex = request.indexOf("track=");
    
    Serial.print("idIndex="); Serial.print(idIndex); Serial.print(" nameIndex="); Serial.print(nameIndex); Serial.print(" trackIndex="); Serial.println(trackIndex);
    
    if (idIndex > 0 && nameIndex > 0 && trackIndex > 0) {
      int id = request.substring(idIndex + 3, request.indexOf("&", idIndex)).toInt();
      int track = request.substring(trackIndex + 6, request.indexOf("&", trackIndex) > 0 ? request.indexOf("&", trackIndex) : request.indexOf(" ", trackIndex)).toInt();
      String name = request.substring(nameIndex + 5, request.indexOf("&", nameIndex));
      name.replace("%20", " ");
      
      Serial.print("Parsed: id="); Serial.print(id); Serial.print(" name="); Serial.print(name); Serial.print(" track="); Serial.println(track);
      
      if (id >= 7 && id <= 10) {
        int idx = id - 7;
        customButtons[idx].id = id;
        customButtons[idx].name = name;
        customButtons[idx].track = track;
        customButtons[idx].enabled = (track > 0);
        Serial.print("Button ");
        Serial.print(id);
        Serial.print(" gespeichert: ");
        Serial.print(name);
        Serial.print(" (Track ");
        Serial.print(track);
        Serial.println(")");
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
    int trackMap[7] = {0, 1, 2, 3, 4, 5, 6}; // Direkte 1:1 Zuordnung
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

void setTimer(int minutes, int repeatCount) {
  timerActive = true;
  timerStartTime = millis();
  timerDuration = (unsigned long)minutes * 60000;
  timerRepeatCount = repeatCount;
  timerRepeatsLeft = repeatCount;
  
  if (minutes == 0) {
    lastRepeatTime = millis();
  } else {
    lastRepeatTime = 0;
  }
  
  Serial.print("Timer gesetzt: ");
  if (minutes > 0) {
    Serial.print(minutes);
    Serial.print(" Minuten, ");
  } else {
    Serial.print("Sofort gestartet, ");
  }
  Serial.print(repeatCount);
  Serial.println("x gongen");
  
}

void cancelTimer() {
  timerActive = false;
  timerRepeatsLeft = 0;
  Serial.println("Timer abgebrochen");
}

void checkTimer() {
  if (!timerActive) return;
  
  unsigned long currentTime = millis();
  unsigned long elapsed = currentTime - timerStartTime;
  
  if (elapsed >= timerDuration) {
    if (timerRepeatsLeft > 0) {
      if (lastRepeatTime == 0 || (currentTime - lastRepeatTime >= REPEAT_INTERVAL)) {
        activateGong();
        timerRepeatsLeft--;
        lastRepeatTime = currentTime;
        
        Serial.print("Timer-Gong! Noch ");
        Serial.print(timerRepeatsLeft);
        Serial.println(" übrig");
        
        if (timerRepeatsLeft == 0) {
          timerActive = false;
          Serial.println("Timer abgeschlossen");
        }
      }
    }
  } else if (timerDuration == 0 && timerRepeatsLeft > 0) {
    unsigned long timeSinceLastGong = currentTime - lastRepeatTime;
    
    if (timeSinceLastGong >= REPEAT_INTERVAL) {
      Serial.print("3x-Gong Trigger! Zeit seit letztem: ");
      Serial.print(timeSinceLastGong / 1000);
      Serial.println(" Sekunden");
      
      activateGong();
      timerRepeatsLeft--;
      lastRepeatTime = currentTime;
      
      Serial.print("3x-Gong ausgeführt! Noch ");
      Serial.print(timerRepeatsLeft);
      Serial.println(" übrig");
      
      if (timerRepeatsLeft == 0) {
        timerActive = false;
        Serial.println("3x-Gong abgeschlossen - alle 3 Gongs fertig!");
      }
    }
  }
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
  client.println("}");
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
  client.println("body { font-family: Arial, sans-serif; max-width: 600px; margin: 50px auto; padding: 20px; background: #f0f0f0; }");
  client.println("h1 { color: #333; text-align: center; }");
  client.println(".button-container { display: flex; flex-direction: column; gap: 15px; margin-top: 30px; }");
  client.println("button { padding: 20px; font-size: 18px; border: none; border-radius: 8px; cursor: pointer; transition: 0.3s; }");
  client.println("button:hover { transform: scale(1.05); box-shadow: 0 4px 8px rgba(0,0,0,0.2); }");
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
  client.println("</style>");
  client.println("</head>");
  client.println("<body>");
  client.println("<h1>🔔 Gong & Durchsage System</h1>");
  
  client.println("<div class='timer-section'>");
  client.println("<h2>⏰ Timer</h2>");
  client.println("<div class='timer-input'>");
  client.println("<label>Zeit (Minuten):</label>");
  client.println("<input type='number' id='timerMinutes' value='20' min='1' max='120' />");
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

  client.println("<button class='gong-btn' onmousedown='startHold(\"/durchsage1\", this)' onmouseup='cancelHold()' onmouseleave='cancelHold()' ontouchstart='startHold(\"/durchsage1\", this)' ontouchend='cancelHold()'>🚨 Generelle Evakuierung<br><small>5 Sek. halten</small></button>");
  client.println("<button class='gong-btn' style='background:#ff0000;' onmousedown='startHold(\"/durchsage2\", this)' onmouseup='cancelHold()' onmouseleave='cancelHold()' ontouchstart='startHold(\"/durchsage2\", this)' ontouchend='cancelHold()'>⚠️ Amok Alarm<br><small>5 Sek. halten</small></button>");
  client.println("<button class='durchsage-btn' style='background:#4CAF50;' onmousedown='startHold(\"/durchsage5\", this)' onmouseup='cancelHold()' onmouseleave='cancelHold()' ontouchstart='startHold(\"/durchsage5\", this)' ontouchend='cancelHold()'>✅ Amok Entwarnung<br><small>5 Sek. halten</small></button>");
  client.println("<button class='gong-btn' style='background:#ff6600;' onmousedown='startHold(\"/durchsage3\", this)' onmouseup='cancelHold()' onmouseleave='cancelHold()' ontouchstart='startHold(\"/durchsage3\", this)' ontouchend='cancelHold()'>🔥 Feuer Evakuierung<br><small>5 Sek. halten</small></button>");
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
  client.println("<button class='admin-btn' style='background:#666;' onclick='location.href=\"/admin\"'>🔧 Admin</button>");
  client.println("</div>");
  client.println("<script>");
  client.println("let timerInterval = null;");
  client.println("let holdTimer = null;");
  client.println("let holdProgress = 0;");
  client.println("function sendCommand(cmd) {");
  client.println("  fetch(cmd).then(() => console.log('Befehl gesendet: ' + cmd));");
  client.println("}");
  client.println("function startHold(cmd, btn) {");
  client.println("  holdProgress = 0;");
  client.println("  btn.style.opacity = '0.5';");
  client.println("  holdTimer = setInterval(() => {");
  client.println("    holdProgress += 100;");
  client.println("    if (holdProgress >= 5000) {");
  client.println("      clearInterval(holdTimer);");
  client.println("      btn.style.opacity = '1';");
  client.println("      sendCommand(cmd);");
  client.println("      btn.style.background = '#00ff00';");
  client.println("      setTimeout(() => { btn.style.background = ''; }, 500);");
  client.println("    }");
  client.println("  }, 100);");
  client.println("}");
  client.println("function toggleEndstufen() {");
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
  client.println("function startTimer(repeat) {");
  client.println("  const minutes = document.getElementById('timerMinutes').value;");
  client.println("  if (minutes < 1) { alert('Bitte mindestens 1 Minute eingeben'); return; }");
  client.println("  fetch('/timer?minutes=' + minutes + '&repeat=' + repeat)");
  client.println("    .then(() => {");
  client.println("      document.getElementById('timerDisplay').style.display = 'block';");
  client.println("      updateTimer();");
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
  client.println("      const min = Math.floor(data.remaining / 60);");
  client.println("      const sec = data.remaining % 60;");
  client.println("      const text = min + ':' + (sec < 10 ? '0' : '') + sec;");
  client.println("      let msg = 'Timer: ' + text;");
  client.println("      if (data.repeatsLeft > 0) msg += ' (noch ' + data.repeatsLeft + 'x)';");
  client.println("      document.getElementById('timerText').innerText = msg;");
  client.println("    });");
  client.println("}");
  client.println("window.onload = function() { updateTimer(); updateEndstufenStatus(); setInterval(updateTimer, 2000); setInterval(updateEndstufenStatus, 2000); };");
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
  client.println("    .then(() => { alert('Button ' + id + ' gespeichert!'); location.href = '/'; });");
  client.println("}");
  client.println("</script>");
  client.println("</body>");
  client.println("</html>");
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



