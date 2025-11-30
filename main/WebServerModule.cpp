#include "WebServerModule.h"
#include <ArduinoJson.h>

WebServer server(80);

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
        </style>
    </head>
    <body>
        <div style="position:absolute;top:10px;right:10px;z-index:1000;background:white;padding:5px;border-radius:5px;font-size:16px;">
            Number of points: <input type="number" id="pointCount" value="50" min="1" max="100" style="width:80px; padding:5px; font-size:16px;">
            <button id="resetView" style="padding:8px 12px; font-size:16px; cursor:pointer;"> Reset View </button>
        </div>

        <div id="map"></div>

        <script>
            var map = L.map('map').setView([0,0], 2);
            L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', { maxZoom: 19 }).addTo(map);

            var coordsGlobal = []; // przechowuje aktualne punkty

            function updateMap() {
                fetch('/gpsdata')
                .then(response => response.text())
                .then(data => {
                    var lines = data.split('\n');
                    var coords = [];
                    lines.forEach(line => {
                        if(line && line.trim().length > 0){
                            try {
                                var obj = JSON.parse(line);
                                var lat = parseFloat(obj.lat);
                                var lon = parseFloat(obj.lon);
                                if(!isNaN(lat) && !isNaN(lon)){
                                    coords.push([lat, lon]);
                                }
                            } catch(e) {
                                // Ignore parse errors
                            }
                        }
                    });

                    var pointLimit = parseInt(document.getElementById('pointCount').value) || coords.length;
                    coords = coords.slice(-pointLimit);
                    coordsGlobal = coords; // zapisujemy globalnie do przycisku reset

                    if(coords.length > 0){
                        map.eachLayer(function(layer){
                            if(layer instanceof L.Polyline || layer instanceof L.CircleMarker) map.removeLayer(layer);
                        });

                        coords.forEach(function(c, index){
                            var color = 'blue';
                            if(index === coords.length - 1) color = 'red';
                            else if(index >= coords.length - 10) color = 'green';
                            L.circleMarker(c, {radius:4, color:color, fillColor:color, fillOpacity:1}).addTo(map);
                        });

                        L.polyline(coords, {color:'black'}).addTo(map);
                    }
                })
                .catch(err => console.error("Błąd fetch GPS:", err));
            }

            // odśwież co 3 sekundy
            setInterval(updateMap, 3000);
            updateMap();

            // reset widoku przy kliknięciu
            document.getElementById('resetView').addEventListener('click', function(){
                if(coordsGlobal.length > 0){
                    map.fitBounds(coordsGlobal);
                }
            });
        </script>
    </body>
</html>
)rawliteral";

    server.send(200, "text/html", html);
}

void handleGPSData() {
    // Support both /data_log.txt (JSON lines) and /data.csv (legacy)
    const char* targetFile = "/data.json";
    
    if (!SD.exists(targetFile)) {
        server.send(404, "text/plain", "Brak danych GPS");
        return;
    }

    File file = SD.open(targetFile);
    if(!file){
        server.send(500, "text/plain", "Nie można otworzyć pliku");
        return;
    }

    server.streamFile(file, "application/json");
    file.close();
}

void startWebServer(uint16_t port) {
    server.on("/", handleRoot);
    server.on("/gpsdata", handleGPSData);
    server.begin();
    Serial.printf("[WEB] Serwer wystartował na porcie %d\n", port);
}
