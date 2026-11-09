#include <WiFi.h>
#include <WebServer.h>
#include "j1850vpw.h"

#define TX 26
#define RX 25

// Access Point Credentials
const char* ap_ssid     = "ESP32S3_OBD_Bus";
const char* ap_password = "12345678";

// Rolling log structures
const int MAX_LOGS = 100;
String obdLogs[MAX_LOGS];
int logCount = 0;

// Parsed Engine Metrics (Global tracking)
int currentRPM = 0;
int currentSpeed = 0;
float currentVoltage = 0.0;

// Command injection hooks
volatile bool hasCommandToSend = false;
uint8_t commandLength = 0;
uint8_t commandBytes[64]; 

WebServer server(80);
J1850VPW vpw;

void handleError(J1850_Operations op, J1850_ERRORS err);
void parseOBDMessage(uint8_t *data, uint8_t size);

// HTML, CSS, and Dynamic Client-Side JS Core
void handleRoot() {
    String html = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
    html += "<title>ESP32-S3 OBD Advanced Node</title>";
    html += "<style>";
    html += "body { font-family: Arial, sans-serif; background-color: #121212; color: #e0e0e0; margin: 0; padding: 20px; }";
    html += ".container { max-width: 800px; margin: 0 auto; background: #1e1e1e; padding: 20px; border-radius: 12px; box-shadow: 0 4px 10px rgba(0,0,0,0.5); }";
    html += "h1 { color: #00adb5; text-align: center; margin-top: 0; }";
    html += ".dash { display: grid; grid-template-columns: repeat(auto-fit, minmax(180px, 1fr)); gap: 15px; margin-bottom: 25px; }";
    html += ".gauge { background: #252525; padding: 15px; border-radius: 8px; text-align: center; border-left: 4px solid #00adb5; }";
    html += ".gauge h3 { margin: 0 0 10px 0; font-size: 0.9rem; color: #888; text-transform: uppercase; }";
    html += ".gauge div { font-size: 1.8rem; font-weight: bold; color: #00ff66; font-family: monospace; }";
    html += ".controls { display: flex; gap: 10px; margin-bottom: 15px; }";
    html += "input[type='text'], select { padding: 12px; border: 1px solid #3d3d3d; background: #2d2d2d; color: #fff; border-radius: 6px; font-family: monospace; font-size: 1rem; }";
    html += "#cmdInput { flex-grow: 1; text-transform: uppercase; }";
    html += "#searchInput { width: 100%; box-sizing: border-box; margin-bottom: 15px; }";
    html += "button { padding: 12px 24px; border: none; background: #00adb5; color: #fff; font-weight: bold; border-radius: 6px; cursor: pointer; }";
    html += "button:hover { background: #008c95; }";
    html += ".terminal { background: #050505; border-radius: 8px; height: 350px; overflow-y: auto; padding: 15px; font-family: monospace; font-size: 0.95rem; border: 1px solid #3d3d3d; line-height: 1.5; color: #a3e635; }";
    html += ".log-item { border-bottom: 1px solid #1a1a1a; padding: 5px 0; display: flex; justify-content: space-between; }";
    html += "</style>";
    html += "<script>";
    
    // Asynchronous injection transmitter
    html += "function sendCommand() {";
    html += "  var cmd = document.getElementById('cmdInput').value;";
    html += "  if(!cmd) return;";
    html += "  fetch('/send?cmd=' + encodeURIComponent(cmd)).then(res => res.text()).then(txt => {";
    html += "    if(txt !== 'OK') alert(txt);";
    html += "    else document.getElementById('cmdInput').value = '';";
    html += "  });";
    html += "}";

    // Dynamic filtering engine run inside browser
    html += "var allLogs = [];";
    html += "function renderLogs() {";
    html += "  var filterType = document.getElementById('filterType').value;";
    html += "  var search = document.getElementById('searchInput').value.toUpperCase();";
    html += "  var term = document.getElementById('term');";
    html += "  var filtered = allLogs.filter(log => {";
    html += "    if(filterType === 'TX' && !log.startsWith('[TX]')) return false;";
    html += "    if(filterType === 'RX' && !log.startsWith('[RX]')) return false;";
    html += "    if(search && !log.includes(search)) return false;";
    html += "    return true;";
    html += "  });";
    html += "  term.innerHTML = filtered.map(x => `<div class='log-item'>${x}</div>`).join('');";
    html += "}";
    
    // Background execution poll (Telemetry + Streams)
    html += "setInterval(function() {";
    html += "  fetch('/data').then(res => res.json()).then(data => {";
    html += "    document.getElementById('rpmVal').innerText = data.telemetry.rpm + ' RPM';";
    html += "    document.getElementById('speedVal').innerText = data.telemetry.speed + ' km/h';";
    html += "    document.getElementById('voltVal').innerText = data.telemetry.volt.toFixed(1) + ' V';";
    html += "    allLogs = data.logs;";
    html += "    renderLogs();";
    html += "  });";
    html += "}, 400);";
    html += "</script></head><body>";
    html += "<div class='container'>";
    html += "<h1>J1850 VPW Custom Interface</h1>";
    
    // Parsed Metrics Display Group
    html += "<div class='dash'>";
    html += "  <div class='gauge'><h3>Engine Speed</h3><div id='rpmVal'>- RPM</div></div>";
    html += "  <div class='gauge'><h3>Vehicle Velocity</h3><div id='speedVal'>- km/h</div></div>";
    html += "  <div class='gauge'><h3>Bus Voltage</h3><div id='voltVal'>- V</div></div>";
    html += "</div>";

    // TX Panel
    html += "<div class='controls'>";
    html += "  <input type='text' id='cmdInput' placeholder='HEX COMMAND (e.g. 68 6A F1 01 0C)' onkeydown='if(event.key===\"Enter\") sendCommand()'>";
    html += "  <button onclick='sendCommand()'>Send</button>";
    html += "</div>";

    // Filtering & Searching Box Context
    html += "<div class='controls'>";
    html += "  <select id='filterType' onchange='renderLogs()'><option value='ALL'>Show All Frames</option><option value='RX'>RX Only</option><option value='TX'>TX Only</option></select>";
    html += "  <input type='text' id='searchInput' placeholder='Search frame contents or headers...' oninput='renderLogs()' style='flex-grow:1;'>";
    html += "</div>";
    
    // Output Log Window
    html += "<div id='term' class='terminal'>Binding node arrays...</div>";
    html += "</div></body></html>";
    
    server.send(200, "text/html", html);
}

// JSON Payload API serving telemetry alongside transaction histories
void handleData() {
    String json = "{";
    json += "\"telemetry\":{\"rpm\":" + String(currentRPM) + ",\"speed\":" + String(currentSpeed) + ",\"volt\":" + String(currentVoltage) + "},";
    json += "\"logs\":[";
    for (int i = 0; i < logCount; i++) {
        json += "\"" + obdLogs[i] + "\"";
        if (i < logCount - 1) json += ",";
    }
    json += "]}";
    server.send(200, "application/json", json);
}

void handleSendCommand() {
    if (!server.hasArg("cmd")) {
        server.send(400, "text/plain", "Bad Request");
        return;
    }
    
    String rawCmd = server.arg("cmd");
    rawCmd.replace(" ", "");
    
    if (rawCmd.length() % 2 != 0 || rawCmd.length() == 0) {
        server.send(200, "text/plain", "Error: String sizing mismatch.");
        return;
    }
    
    commandLength = 0;
    for (size_t i = 0; i < rawCmd.length(); i += 2) {
        String byteString = rawCmd.substring(i, i + 2);
        commandBytes[commandLength++] = (uint8_t) strtol(byteString.c_str(), NULL, 16);
        if (commandLength >= 64) break;
    }
    
    hasCommandToSend = true;
    server.send(200, "text/plain", "OK");
}

// Call once, right after WiFi.softAP(...)
void disableApGateway() {
  esp_netif_t* ap = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
  if (ap == NULL) return;
  esp_netif_dhcps_stop(ap);
  uint8_t offerRouter = 0;   // no default gateway in the DHCP offer
  esp_netif_dhcps_option(ap, ESP_NETIF_OP_SET, ESP_NETIF_ROUTER_SOLICITATION_ADDRESS, &offerRouter, sizeof(offerRouter));
  esp_netif_dhcps_start(ap);
}

void setup() {
    Serial.begin(115200);
    Serial.println(PROPOGATION_DELAY);
    // Boot Configuration as a Wi-Fi Access Point (SoftAP)
    Serial.print("Configuring Access Point...");
    WiFi.mode(WIFI_AP);
    WiFi.setTxPower(WIFI_POWER_8_5dBm);
    WiFi.softAP(ap_ssid, ap_password);
    
    disableApGateway();

    IPAddress myIP = WiFi.softAPIP();
    Serial.print("AP IP address: ");
    Serial.println(myIP); // Defaults to 192.168.4.1

    server.on("/", handleRoot);
    server.on("/data", handleData);
    server.on("/send", handleSendCommand);
    server.begin();

    vpw.onError(handleError);
  
    vpw.init(RX, TX);

  // WiFi.persistent(false);   // stop writing AP config to NVS every boot
  // ... WiFi.softAP(...) and disableApGateway() first ...
  // vpw.init(RX, TX);         // THEN attach the RX interrupt

}



void loop() {
    server.handleClient();

    static uint8_t buff[BS];
    uint8_t dataSize;

    // 1. Process browser outgoing injection vectors
    if (hasCommandToSend) {
        vpw.send(commandBytes, commandLength);
        hasCommandToSend = false; 
        
        String txLog = "[TX] ";
        for (int i = 0; i < commandLength; i++) {
            if (i > 0) txLog += " ";
            if (commandBytes[i] < 0x10) txLog += "0";
            txLog += String(commandBytes[i], HEX);
        }
        txLog.toUpperCase();
        
        for (int i = min(logCount, MAX_LOGS - 1); i > 0; i--) {
            obdLogs[i] = obdLogs[i - 1];
        }
        obdLogs[0] = txLog;
        if (logCount < MAX_LOGS) logCount++;
    }

    // 2. Continuous acquisition from physical layer transceiver
    while ((dataSize = vpw.tryGetReceivedFrame(buff))) {
        // Parse metrics directly out of the frame array
        parseOBDMessage(buff, dataSize);

        String rxLog = "[RX] ";
        for (int i = 0; i < dataSize; i++) {
            if (i > 0) rxLog += " ";
            if (buff[i] < 0x10) rxLog += "0";
            rxLog += String(buff[i], HEX);
        }
        rxLog.toUpperCase();

        for (int i = min(logCount, MAX_LOGS - 1); i > 0; i--) {
            obdLogs[i] = obdLogs[i - 1];
        }
        obdLogs[0] = rxLog;
        if (logCount < MAX_LOGS) logCount++;
    }
}

// Custom parser targeting common Standard Mode $01 SAE J1979 / OBD responses
// Note: Depending on your specific vehicle architecture (e.g. GM Class 2),
// headers may span 3-bytes (ex: 48 6B 10). Adjust indices if looking past target fields.
void parseOBDMessage(uint8_t *data, uint8_t size) {
    if (size < 3) return;

    for (int i = 0; i < size - 1; i++) {
        // Look for Standard Diagnostics Response Flag (0x41)
        if (data[i] == 0x41) {
            uint8_t pid = data[i + 1];
            
            // Engine RPM processing (PID 0x0C)
            if (pid == 0x0C && (i + 3) < size) {
                uint8_t msb = data[i + 2];
                uint8_t lsb = data[i + 3];
                currentRPM = ((msb * 256) + lsb) / 4;
            }
        }
    }
    
}
void handleError(J1850_Operations op, J1850_ERRORS err) {
    if (err == J1850_OK) { 
        return; // No error occurred
    }
    
    String s = (op == J1850_Read) ? "READ ERR: " : "WRITE ERR: ";
    
    // Switch block to translate the library error codes
    switch (err) {
        case J1850_ERR_BUS_IS_BUSY: 
            Serial.println(s + "BUS BUSY"); 
            break;
        case J1850_ERR_BUS_ERROR: 
            Serial.println(s + "BUS ERROR"); 
            break;
        case J1850_ERR_ARBITRATION_LOST: 
            Serial.println(s + "ARBITRATION LOST"); 
            break;
        default: 
            Serial.println(s + "UNKNOWN CODE " + String(err, HEX)); 
            break;
    }
}
