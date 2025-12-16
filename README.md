# Web-basiertes Gong- und Durchsage-System

Dieses System realisiert eine Steuerung des Gongs, automatisierte Durchsagen und Endstufen-Schaltungen auf Basis eines Arduino R4 WiFi. Die Bedienung erfolgt kabellos über eine Benutzeroberfläche im Webbrowser (Smartphone, Tablet oder PC).

##  Funktionsumfang

* **Gong-Steuerung:** Manueller Auslöser oder Timer-gesteuert (z. B. 3x Gong im 2-Minuten-Takt).
* **Sicherheits-Durchsagen:** Abspielen von Warnhinweisen (Feuer, Evakuierung, Amok) mit Sicherheitsmechanismus (Taste muss 5 Sekunden gehalten werden).
* **Endstufen-Steuerung:** Ein-/Ausschalten der Verstärkertechnik mit Live-Statusanzeige (Rückmeldung über physikalischen Kontakt).
* **Custom-Buttons:** Vier frei konfigurierbare Tasten für individuelle Ansagen über ein Admin-Panel.

##  Hardware-Voraussetzungen

1.  **Mikrocontroller:** Arduino R4 WiFi
2.  **Audio:** DFPlayer Mini + SD-Karte (FAT32 formatiert)
3.  **Verstärkung:** Lautsprecher (3-5W) oder Verbindung zur ELA-Anlage
4.  **Schaltung:** 4-Relais-Modul (5V)
    * Relais 1: Gong-Signal
    * Relais 2: Endstufen-Impuls
5.  **Endstufen Schaltung** Eltako Stromstoßschalter 

##  Verkabelungsplan

> ** WICHTIGER HINWEIS ZUM UPLOAD:**
> Die Verbindungen an **Pin 0** und **Pin 1** müssen vor jedem Upload des Programmcodes getrennt werden. Nach dem Upload werden diese wieder verbunden.

| Komponente | Anschluss | Ziel am Arduino R4 WiFi | Anmerkung |
| :--- | :--- | :--- | :--- |
| **DFPlayer** | VCC | 3.3V | **Nicht** an 5V anschließen! |
| | GND | GND | |
| | TX | Pin 0 (RX) | Kabel vor Upload ziehen |
| | RX | Pin 1 (TX) | Kabel vor Upload ziehen |
| | SPK 1/2 | Lautsprecher | Audio-Ausgang |
| **Relais** | VCC | 5V | |
| | GND | GND | |
| | IN1 | Pin 7 | Schaltet den Gong |
| | IN2 | Pin 8 | Impuls für Endstufen |
| **Status** | Kontakt | Pin 11 | Gegen GND (LOW = AN) |

##  Einrichtung der SD-Karte

Die MP3-Dateien müssen im Hauptverzeichnis der SD-Karte in einem Ordner namens `mp3` liegen und mit vierstelligen Nummern benannt sein:

* `/mp3/0001.mp3` – Generelle Evakuierung
* `/mp3/0002.mp3` – Amok Alarm
* `/mp3/0003.mp3` – Feuer Evakuierung
* `/mp3/0004.mp3` – Test-Ansage
* `/mp3/0005.mp3` – Entwarnung
* `/mp3/0006.mp3` – Sonstiges
* `0007.mp3` bis `0010.mp3` – Reserviert für benutzerdefinierte Tasten.

##  Installation & Inbetriebnahme

1.  **Netzwerk konfigurieren:**
    Im Quellcode (`GongDurchsage.ino`) müssen die Variablen `ssid` und `password` an das lokale WLAN angepasst werden.
2.  **Upload:**
    Verbindungen zu Pin 0/1 trennen und Sketch auf den Arduino laden.
3.  **Start:**
    Kabel wieder verbinden. Der Arduino verbindet sich mit dem WLAN.
4.  **Zugriff:**
    Die IP-Adresse wird im Seriellen Monitor (Baudrate 115200) angezeigt. Diese Adresse im Browser aufrufen, um die Steuerung zu öffnen.

##  Bedienung der Weboberfläche

* **Gong:** Ein einfacher Klick löst den Gong aus.
* **Timer:** Nach Eingabe der Minuten und Wahl des Modus (1x oder 3x) läuft der Countdown automatisch.
* **Sicherheits-Buttons:** Kritische Durchsagen (rot markiert) werden erst abgespielt, wenn der Button **5 Sekunden gedrückt gehalten** wird. Dies verhindert versehentliches Auslösen.
* **Admin-Bereich:** Unter `/admin` können die Beschriftungen für die Tasten 7-10 angepasst und MP3-Tracks zugewiesen werden.
