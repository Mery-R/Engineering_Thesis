#include "WebServerModule.h"
#include <ArduinoJson.h>
#include "SdModule.h"

extern SemaphoreHandle_t sdMutex;
extern SdModule sdModule;

WebServer server(80);

// -----------------------------------------------------
// --------------- Private Methods / Callbacks ---------
// -----------------------------------------------------

void handleRoot() {
    String html = R"rawliteral(
<!DOCTYPE html>
<html>
    <head>
        <title>GPS Trace</title>
        <meta name="viewport" content="width=device-width, initial-scale=1.0">
        <link rel="stylesheet" href="https://unpkg.com/leaflet@1.9.4/dist/leaflet.css"/>
        <script src="https://unpkg.com/leaflet@1.9.4/dist/leaflet.js"></script>
        <style>
            #map { height: 100vh; width: 100%; margin:0; padding:0; }
            body { margin:0; padding:0; }
            .controls {
                position: absolute;
                top: 10px;
                right: 10px;
                z-index: 1000;
                background: white;
                padding: 10px;
                border-radius: 5px;
                box-shadow: 0 0 10px rgba(0,0,0,0.2);
                font-family: sans-serif;
                display: flex;
                flex-direction: column;
                gap: 5px;
            }
            .controls input, .controls button {
                padding: 5px;
                font-size: 14px;
            }
        </style>
    </head>
    <body>
        <div class="controls">
            <label>Points: <input type="number" id="pointCount" value="50" min="1" max="500"></label>
            <label>Start: <input type="datetime-local" id="startTime"></label>
            <label>End: <input type="datetime-local" id="endTime"></label>
            <button id="applyFilter">Apply Filter</button>
            <button id="resetView">Reset Zoom</button>
        </div>

        <div id="map"></div>

        <script>
            var map = L.map('map').setView([0,0], 2);
            L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', { maxZoom: 19 }).addTo(map);

            var allData = []; // Store parsed data
            var layerGroup = L.layerGroup().addTo(map);

            // Fetch data once or periodically
            function fetchData() {
                fetch('/gpsdata')
                .then(response => response.text())
                .then(text => {
                    var lines = text.split('\n');
                    allData = [];
                    lines.forEach(line => {
                        if(line && line.trim().length > 0){
                            try {
                                var obj = JSON.parse(line);
                                // Pre-filter valid coordinates immediately
                                var lat = parseFloat(obj.lat);
                                var lon = parseFloat(obj.lon);
                                // Filter out 0,0 and invalid numbers
                                if(!isNaN(lat) && !isNaN(lon) && (Math.abs(lat) > 0.000001 || Math.abs(lon) > 0.000001)){
                                    obj.lat = lat;
                                    obj.lon = lon;
                                    // Ensure ts is available (check standard fields)
                                    // obj.ts or obj.timestamp
                                    if(!obj.ts && obj.timestamp) obj.ts = obj.timestamp;
                                    
                                    allData.push(obj);
                                }
                            } catch(e) { }
                        }
                    });
                    renderMap();
                })
                .catch(err => console.error("Fetch GPS error:", err));
            }

            function renderMap() {
                layerGroup.clearLayers();

                var limit = parseInt(document.getElementById('pointCount').value) || 100;
                var startVal = document.getElementById('startTime').value;
                var endVal = document.getElementById('endTime').value;
                
                var startTs = startVal ? new Date(startVal).getTime() : 0;
                var endTs = endVal ? new Date(endVal).getTime() : 9999999999999;

                // Filter data
                var filtered = allData.filter(d => {
                    var t = d.ts || 0;
                    return t >= startTs && t <= endTs;
                });

                // Apply limit (take last N points from valid set)
                var displayData = filtered.slice(-limit);

                if(displayData.length === 0) return;

                var polylinePoints = [];

                displayData.forEach(function(d, index){
                    var color = 'blue';
                    // Color logic: Last point red, recent green, others blue
                    if(index === displayData.length - 1) color = 'red';
                    else if(index >= displayData.length - 10) color = 'green';

                    var marker = L.circleMarker([d.lat, d.lon], {
                        radius: 4,
                        color: color,
                        fillColor: color,
                        fillOpacity: 1
                    });

                    // POPUP with Time
                    var dateStr = "Unknown Time";
                    if(d.ts) {
                        dateStr = new Date(d.ts).toLocaleString();
                    }
                    marker.bindPopup("<b>Time:</b> " + dateStr + "<br><b>Lat:</b> " + d.lat + "<br><b>Lon:</b> " + d.lon);
                    
                    marker.addTo(layerGroup);
                    polylinePoints.push([d.lat, d.lon]);
                });

                if(polylinePoints.length > 0) {
                     L.polyline(polylinePoints, {color: 'black', weight: 2}).addTo(layerGroup);
                     // Optional: auto-fit bounds on first load or forced
                     // map.fitBounds(polylinePoints); 
                }
            }

            // Controls
            document.getElementById('applyFilter').addEventListener('click', renderMap);
            
            document.getElementById('resetView').addEventListener('click', function(){
                if(allData.length > 0) {
                    // Collect all points currently displayed to fit bounds
                    var bounds = [];
                    layerGroup.eachLayer(function(layer){
                        if(layer instanceof L.CircleMarker) bounds.push(layer.getLatLng());
                    });
                    if(bounds.length > 0) map.fitBounds(bounds);
                }
            });

            // Auto-refresh every 5 seconds (fetches new data)
            setInterval(fetchData, 5000);
            fetchData();
        </script>
    </body>
</html>
)rawliteral";

    server.send(200, "text/html", html);
}

void handleGPSData() {
    // Support both /data_log.txt (JSON lines) and /data.csv (legacy)
    String targetFile = "";
    
    if (sdMutex) xSemaphoreTake(sdMutex, portMAX_DELAY);

    targetFile = sdModule.getLatestArchiveFilename();

    if (targetFile == "") {
        if (sdMutex) xSemaphoreGive(sdMutex);
        server.send(404, "text/plain", "No GPS data (File not found)");
        return;
    }

    File file = SD.open(targetFile);
    if(!file){
        if (sdMutex) xSemaphoreGive(sdMutex);
        server.send(500, "text/plain", "Cannot open file");
        return;
    }

    server.streamFile(file, "application/json");
    file.close();
    
    if (sdMutex) xSemaphoreGive(sdMutex);
}

// -----------------------------------------------------
// --------------- Public Methods ----------------------
// -----------------------------------------------------

void startWebServer(uint16_t port) {
    server.on("/", handleRoot);
    server.on("/gpsdata", handleGPSData);
    server.begin();
    Serial.printf("[WEB] Server started on port %d\n", port);
}
