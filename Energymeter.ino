#include <WiFi.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <SD.h>
#include <SPI.h>
#include "ZMPT101B.h"
#include "ACS712.h"
#include "time.h"
#include "esp_sntp.h"

// WiFi credentials
const char* ssid = "1003";
const char* password = "10031003";

const char *ntpServer1 = "pool.ntp.org";
const char *ntpServer2 = "time.nist.gov";
const long gmtOffset_sec = 6*3600;
const int daylightOffset_sec = 00;

const char *time_zone = "CET-1CEST,M3.5.0,M10.5.0/3"; 

// Create WebSocket server on port 81
WebSocketsServer webSocket = WebSocketsServer(81);

// Create web server on port 80
AsyncWebServer server(80);

// SD Card pin configuration
const int SD_CS_PIN = 5;  // Chip Select pin for SD card

// Sensor pins
ZMPT101B voltageSensor(34,50.0);
ACS712 currentsensor(35,3.3,4095,100);

// Calibration constants (adjust based on your sensors)
#define SENSITIVITY 320.0f  // Adjust multiplier for your current sensor

// Variables to store sensor readings
float voltage = 0.0;
float current = 0.0;
float voltage1 = 0.0;
float current1 = 0.0;
float power =0.0;
float power1=0.0;
int8_t hour=0;
int8_t mins=0;
int8_t secs=0;
int i;
// Timing variables
unsigned long lastSensorRead = 0;
unsigned long lastWebSocketSend = 0;
unsigned long lastSDWrite = 0;
const unsigned long SENSOR_INTERVAL = 100;    // Read sensors every 100ms
const unsigned long WEBSOCKET_INTERVAL = 500; // Send data every 500ms
const unsigned long SD_WRITE_INTERVAL = 5000; // Write to SD every 5 seconds

// SD Card variables
String dataFileName = "/energy_data.csv";
bool sdCardAvailable = false;

void printLocalTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("No time available (yet)");
    return;
  }
  hour=timeinfo.tm_hour;
  mins=timeinfo.tm_min;
  secs=timeinfo.tm_sec;
  Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
}

void timeavailable(struct timeval *t) {
  Serial.println("Got time adjustment from NTP!");
  printLocalTime();
}

void setup() {
    Serial.begin(115200);
    voltageSensor.setSensitivity(SENSITIVITY);
    // Initialize sensor pins
    
    // Initialize SD card
    initializeSDCard();
    
    // Connect to WiFi
    WiFi.begin(ssid, password);

    esp_sntp_servermode_dhcp(1);
    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.println("Connecting to WiFi...");
    }
    
    Serial.println("WiFi connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());

    sntp_set_time_sync_notification_cb(timeavailable);
    
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer1, ntpServer2);
    // Initialize WebSocket server
    webSocket.begin();
    webSocket.onEvent(webSocketEvent);


    
    // Serve the HTML page
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    if(!request->authenticate("admin", "1234")) {
        return request->requestAuthentication();
    }
    String html = getHTMLPage();
    request->send(200, "text/html", html);
    });
    
    // API endpoint to get historical data
    server.on("/api/history", HTTP_GET, [](AsyncWebServerRequest *request){
        if (!sdCardAvailable) {
            request->send(500, "application/json", "{\"error\":\"SD card not available\"}");
            return;
        }
        
        String limit = "100"; // Default limit
        if (request->hasParam("limit")) {
            limit = request->getParam("limit")->value();
        }
        
        String jsonData = getHistoricalData(limit.toInt());
        request->send(200, "application/json", jsonData);
    });
    
    // API endpoint to clear historical data
    server.on("/api/clear", HTTP_POST, [](AsyncWebServerRequest *request){
        if (!sdCardAvailable) {
            request->send(500, "application/json", "{\"error\":\"SD card not available\"}");
            return;
        }
        
        if (SD.remove(dataFileName)) {
            // Recreate file with header
            createDataFile();
            request->send(200, "application/json", "{\"success\":true,\"message\":\"Data cleared successfully\"}");
        } else {
            request->send(500, "application/json", "{\"error\":\"Failed to clear data\"}");
        }
    });
    
    server.begin();
    Serial.println("Web server started");
    Serial.println("WebSocket server started on port 81");
}

void loop() {
    printLocalTime();
    webSocket.loop();
    
    unsigned long currentTime = millis();
    
    // Read sensors at specified interval
    if (currentTime - lastSensorRead >= SENSOR_INTERVAL) {
        readSensors();
        lastSensorRead = currentTime;
    }
    
    // Send data via WebSocket at specified interval
    if (currentTime - lastWebSocketSend >= WEBSOCKET_INTERVAL) {
        sendSensorData();
        lastWebSocketSend = currentTime;
    }
    
    // Write to SD card at specified interval
    if (currentTime - lastSDWrite >= SD_WRITE_INTERVAL && sdCardAvailable) {
        writeToSDCard();
        lastSDWrite = currentTime;
    }
}

void initializeSDCard() {
    Serial.println("Initializing SD card...");
    
    if (!SD.begin(SD_CS_PIN)) {
        Serial.println("SD card initialization failed!");
        sdCardAvailable = false;
        return;
    }
    
    Serial.println("SD card initialized successfully");
    sdCardAvailable = true;
    
    // Create data file if it doesn't exist
    if (!SD.exists(dataFileName)) {
        createDataFile();
    }
}

void createDataFile() {
    File dataFile = SD.open(dataFileName, FILE_WRITE);
    if (dataFile) {
        dataFile.println("timestamp,voltage,current,power");
        dataFile.close();
        Serial.println("Data file created with header");
    } else {
        Serial.println("Error creating data file");
    }
}

void readSensors() {
    // Read voltage sensor
  for(i=0;i<20;i++){
    int adc=analogRead(35);
    voltage1=voltageSensor.getRmsVoltage();
    float adc_voltage1 = adc * (3.3 / 4096.0);
    float current_voltage1 = (adc_voltage1 * 2);
    current1 = (current_voltage1 - 2.5) / 0.100;
    current1=-(current1/1000);
    power1+=current1*voltage1;
  //Serial.println("Voltage: " + String(voltage) + "V, Current: " + String(current) + "A, Power: " + String(power) + "W");
  }
  power=power1/20;
  current=current1;
  voltage=voltage1;
  power1=0;
}

void writeToSDCard() {
    File dataFile = SD.open(dataFileName, FILE_APPEND);
    if (dataFile) {
        String dataString = String(hour)+":"+String(mins)+":"+String(secs) + "," + 
                           String(voltage, 2) + "," + 
                           String(current, 2) + "," + 
                           String(power, 2);
        dataFile.println(dataString);
        dataFile.close();
        Serial.println("Data written to SD card: " + dataString);
    } else {
        Serial.println("Error opening data file for writing");
    }
}

String getHistoricalData(int limit) {
    if (!sdCardAvailable) {
        return "{\"error\":\"SD card not available\"}";
    }
    
    File dataFile = SD.open(dataFileName, FILE_READ);
    if (!dataFile) {
        return "{\"error\":\"Could not open data file\"}";
    }
    
    // Count total lines first
    int totalLines = 0;
    while (dataFile.available()) {
        if (dataFile.read() == '\n') totalLines++;
    }
    
    dataFile.close();
    dataFile = SD.open(dataFileName, FILE_READ);
    
    // Skip header
    dataFile.readStringUntil('\n');
    totalLines--; // Don't count header
    
    // Calculate how many lines to skip
    int linesToSkip = max(0, totalLines - limit);
    
    // Skip older entries
    for (int i = 0; i < linesToSkip; i++) {
        dataFile.readStringUntil('\n');
    }
    
    DynamicJsonDocument doc(8192);
    JsonArray dataArray = doc.createNestedArray("data");
    
    // Read the remaining lines
    while (dataFile.available()) {
        String line = dataFile.readStringUntil('\n');
        if (line.length() > 0) {
            JsonObject entry = dataArray.createNestedObject();
            
            int firstComma = line.indexOf(',');
            int secondComma = line.indexOf(',', firstComma + 1);
            int thirdComma = line.indexOf(',', secondComma + 1);
            
            if (firstComma > 0 && secondComma > 0 && thirdComma > 0) {
                entry["timestamp"] = line.substring(0, firstComma);
                entry["voltage"] = line.substring(firstComma + 1, secondComma).toFloat();
                entry["current"] = line.substring(secondComma + 1, thirdComma).toFloat();
                entry["power"] = line.substring(thirdComma + 1).toFloat();
            }
        }
    }
    
    dataFile.close();
    
    doc["total"] = dataArray.size();
    doc["sdAvailable"] = sdCardAvailable;
    
    String jsonString;
    serializeJson(doc, jsonString);
    return jsonString;
}

void sendSensorData() {
    // Create JSON object
    DynamicJsonDocument doc(300);
    doc["voltage"] = voltage;
    doc["current"] = current;
    doc["power"] = power;
    doc["timestamp"] = millis();
    doc["sdAvailable"] = sdCardAvailable;
    
    // Serialize JSON to string
    String jsonString;
    serializeJson(doc, jsonString);
    
    // Send to all connected WebSocket clients
    webSocket.broadcastTXT(jsonString);
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
    switch(type) {
        case WStype_DISCONNECTED:
            Serial.printf("[%u] Disconnected!\n", num);
            break;
            
        case WStype_CONNECTED: {
            IPAddress ip = webSocket.remoteIP(num);
            Serial.printf("[%u] Connected from %d.%d.%d.%d url: %s\n", num, ip[0], ip[1], ip[2], ip[3], payload);
            
            // Send current sensor data to newly connected client
            sendSensorData();
            break;
        }
        
        case WStype_TEXT: {
            Serial.printf("[%u] Received Text: %s\n", num, payload);
            
            // Parse incoming message
            DynamicJsonDocument doc(200);
            deserializeJson(doc, payload);
            
            if (doc["action"] == "getHistory") {
                int limit = doc["limit"] | 100;
                String historyData = getHistoricalData(limit);
                webSocket.sendTXT(num, historyData);
            }
            break;
        }
            
        default:
            break;
    }
}

String getHTMLPage() {
    return R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8" />
    <title>Smart Energy Meter</title>
    <style>
    *{
        margin: 0;
        box-sizing: content-box;
        padding: auto;
    }
    body
    {
        font-family: "Arial", sans-serif;
        background: linear-gradient(120deg, #667eea 0%, #333 100%);
        padding: 20px;
        min-height: 100dvh;
    }
    .container
    {
        max-width: 800px;
        margin: 0 auto;
    }
    .header
    {
        text-align: center;
        color: white;
        margin-bottom: 30px;
    }
    .header h1
    {
        font-size: x-large;
        margin-bottom: 20px;
    }
    .metrics-grid
    {
        display: grid;
        grid-template-columns: repeat(auto-fit, minmax(300px,1fr));
        gap: 20px;
        margin-bottom: 30px;
    }
    .metric-card 
    {
        background: azure;
        border-radius: 15px;
        padding: 25px;
        text-align: center;
        box-shadow: 0 8px 32px rgba(100, 50, 40, 0.1);
        backdrop-filter: blur(20px);
        transition: transform 0.3s ease;
    }
    .metric-card:hover 
    {
        transform: translateY(-5px);
    }
    .metric-icon 
    {
        font-size: 3rem;
        margin-bottom: 15px;
    }
    .voltage-icon { color: #FF6B6B; }
    .current-icon { color: #4ECDC4; }
    .power-icon { color: #45B7D1; }
    .metric-value 
    {
        font-size: 2.5rem;
        font-weight: bold;
        margin-bottom: 10px;
        color: #333;
    }
    .metric-label 
    {
        font-size: 1.1rem;
        color: #666;
        margin-bottom: 5px;
    }
    .metric-unit 
    {
        font-size: 1rem;
        color: #888;
    }
    .controls-panel {
        background: rgba(255, 255, 255, 0.95);
        border-radius: 15px;
        padding: 25px;
        margin-bottom: 30px;
        box-shadow: 0 8px 32px rgba(0, 0, 0, 0.1);
        text-align: center;
    }
    .btn {
        background: #45B7D1;
        color: white;
        border: none;
        padding: 12px 24px;
        border-radius: 8px;
        cursor: pointer;
        margin: 0 10px;
        font-size: 1rem;
        transition: background 0.3s ease;
    }
    .btn:hover {
        background: #357ABD;
    }
    .btn.danger {
        background: #FF6B6B;
    }
    .btn.danger:hover {
        background: #E55A5A;
    }
    .history-panel {
        background: rgba(255, 255, 255, 0.95);
        border-radius: 15px;
        padding: 25px;
        box-shadow: 0 8px 32px rgba(0, 0, 0, 0.1);
        display: none;
    }
    .history-table {
        width: 100%;
        border-collapse: collapse;
        margin-top: 20px;
    }
    .history-table th,
    .history-table td {
        padding: 12px;
        text-align: left;
        border-bottom: 1px solid #ddd;
    }
    .history-table th {
        background-color: #f2f2f2;
        font-weight: bold;
    }
    .history-table tr:hover {
        background-color: #f5f5f5;
    }
    @keyframes pulse
    {
        0% { opacity: 1; }
        50% { opacity: 0.5; }
        100% { opacity: 1; }
    }
    .updating 
    {
        animation: pulse 1s infinite;
    }
    .status-indicator {
        display: inline-block;
        width: 10px;
        height: 10px;
        border-radius: 50%;
        margin-right: 10px;
    }
    .connected {
        background-color: #4CAF50;
    }
    .disconnected {
        background-color: #f44336;
    }
    .sd-status {
        margin-left: 20px;
        font-size: 0.9rem;
    }
    </style>
</head>
<body>
    <div class="container">
        <div class="header">
            <h1>⚡ SMART ENERGY METER ⚡</h1>
            <div>
                <span class="status-indicator disconnected" id="status-indicator"></span>
                <span id="connection-status">Connecting...</span>
                <span class="sd-status" id="sd-status">📱 SD: Unknown</span>
            </div>
        </div>
        <div class="metrics-grid">
            <div class="metric-card">
                <div class="metric-icon voltage-icon">⚡</div>
                <div class="metric-value" id="voltage-value">220.00</div>
                <div class="metric-label">Voltage</div>
                <div class="metric-unit">Volts (V)</div>
            </div>
            <div class="metric-card">
                <div class="metric-icon current-icon">🔌</div>
                <div class="metric-value" id="current-value">0.00</div>
                <div class="metric-label">Current</div>
                <div class="metric-unit">Amperes (A)</div>
            </div>
            <div class="metric-card">
                <div class="metric-icon power-icon">💡</div>
                <div class="metric-value" id="power-value">0.00</div>
                <div class="metric-label">Power</div>
                <div class="metric-unit">Watts (W)</div>
            </div>
        </div>
        
        <div class="controls-panel">
            <h3>Data Management</h3>
            <button class="btn" onclick="showHistory()">📊 View History</button>
            <button class="btn" onclick="downloadData()">💾 Download CSV</button>
            <button class="btn danger" onclick="clearData()">🗑️ Clear Data</button>
        </div>
        
        <div class="history-panel" id="history-panel">
            <h3>Historical Data</h3>
            <button class="btn" onclick="hideHistory()">❌ Close</button>
            <div id="history-content">Loading...</div>
        </div>
    </div>

    <script>
        let ws;
        const voltageElement = document.getElementById('voltage-value');
        const currentElement = document.getElementById('current-value');
        const powerElement = document.getElementById('power-value');
        const statusIndicator = document.getElementById('status-indicator');
        const connectionStatus = document.getElementById('connection-status');
        const sdStatus = document.getElementById('sd-status');

        function connectWebSocket() {
            ws = new WebSocket('ws://' + window.location.hostname + ':81');
            
            ws.onopen = function() {
                console.log('WebSocket Connected');
                statusIndicator.className = 'status-indicator connected';
                connectionStatus.textContent = 'Connected';
            };
            
            ws.onmessage = function(event) {
                try {
                    const data = JSON.parse(event.data);
                    
                    if (data.voltage !== undefined) {
                        voltageElement.textContent = data.voltage.toFixed(2);
                        voltageElement.parentElement.classList.add('updating');
                        setTimeout(() => voltageElement.parentElement.classList.remove('updating'), 1000);
                    }
                    
                    if (data.current !== undefined) {
                        currentElement.textContent = data.current.toFixed(2);
                        currentElement.parentElement.classList.add('updating');
                        setTimeout(() => currentElement.parentElement.classList.remove('updating'), 1000);
                    }
                    
                    if (data.power !== undefined) {
                        powerElement.textContent = data.power.toFixed(2);
                        powerElement.parentElement.classList.add('updating');
                        setTimeout(() => powerElement.parentElement.classList.remove('updating'), 1000);
                    }
                    
                    if (data.sdAvailable !== undefined) {
                        sdStatus.textContent = data.sdAvailable ? '📱 SD: Available' : '📱 SD: Not Available';
                        sdStatus.style.color = data.sdAvailable ? '#4CAF50' : '#f44336';
                    }
                    
                    // Handle historical data response
                    if (data.data !== undefined) {
                        displayHistoryData(data);
                    }
                } catch (e) {
                    console.error('Error parsing WebSocket data:', e);
                }
            };
            
            ws.onclose = function() {
                console.log('WebSocket Disconnected');
                statusIndicator.className = 'status-indicator disconnected';
                connectionStatus.textContent = 'Disconnected';
                setTimeout(connectWebSocket, 3000);
            };
            
            ws.onerror = function(error) {
                console.error('WebSocket Error:', error);
                statusIndicator.className = 'status-indicator disconnected';
                connectionStatus.textContent = 'Connection Error';
            };
        }

        function showHistory() {
            document.getElementById('history-panel').style.display = 'block';
            loadHistoryData();
        }

        function hideHistory() {
            document.getElementById('history-panel').style.display = 'none';
        }

        function loadHistoryData() {
            fetch('/api/history?limit=50')
                .then(response => response.json())
                .then(data => displayHistoryData(data))
                .catch(error => {
                    console.error('Error loading history:', error);
                    document.getElementById('history-content').innerHTML = 'Error loading data';
                });
        }

        function displayHistoryData(data) {
            if (data.error) {
                document.getElementById('history-content').innerHTML = 'Error: ' + data.error;
                return;
            }

            let html = '<table class="history-table"><thead><tr><th>Time</th><th>Voltage (V)</th><th>Current (A)</th><th>Power (W)</th></tr></thead><tbody>';
            
            data.data.forEach(entry => {
                html += `<tr>
                    <td>${entry.timestamp}</td>
                    <td>${entry.voltage.toFixed(2)}</td>
                    <td>${entry.current.toFixed(2)}</td>
                    <td>${entry.power.toFixed(2)}</td>
                </tr>`;
            });
            
            html += '</tbody></table>';
            html += `<p>Total records: ${data.total}</p>`;
            
            document.getElementById('history-content').innerHTML = html;
        }

        function downloadData() {
            window.open('/api/history?limit=1000', '_blank');
        }

        function clearData() {
            if (confirm('Are you sure you want to clear all historical data? This action cannot be undone.')) {
                fetch('/api/clear', { method: 'POST' })
                    .then(response => response.json())
                    .then(data => {
                        if (data.success) {
                            alert('Data cleared successfully!');
                            if (document.getElementById('history-panel').style.display === 'block') {
                                loadHistoryData();
                            }
                        } else {
                            alert('Error clearing data: ' + (data.error || 'Unknown error'));
                        }
                    })
                    .catch(error => {
                        console.error('Error clearing data:', error);
                        alert('Error clearing data');
                    });
            }
        }

        window.addEventListener('load', connectWebSocket);
    </script>
</body>
</html>
    )rawliteral";
}