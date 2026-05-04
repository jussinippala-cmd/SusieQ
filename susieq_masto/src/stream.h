#pragma once

// MJPEG stream server on port 81 — in a separate compilation unit
// because esp_http_server.h and ESPAsyncWebServer.h have clashing
// HTTP_GET/HTTP_POST enum definitions.

void stream_server_init();
