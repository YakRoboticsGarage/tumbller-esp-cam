#include "esp_camera.h"
#include "esp_timer.h"
#include "WiFi.h"
#include "WebServer.h"
#include "driver/gpio.h"

// WiFi credentials
const char* ssid = "WIFISSID";
const char* password = "WIFIPASSWORD";

// Web Server on port 80
WebServer server(80);

// Global variables for camera management
SemaphoreHandle_t cameraSemaphore = NULL;
unsigned long lastCaptureTime = 0;
const unsigned long captureInterval = 100; // Minimum time between captures in milliseconds

// Camera pins for ESP-EYE
#define PWDN_GPIO_NUM    -1
#define RESET_GPIO_NUM   -1
#define XCLK_GPIO_NUM    4
#define SIOD_GPIO_NUM    18
#define SIOC_GPIO_NUM    23
#define Y9_GPIO_NUM      36
#define Y8_GPIO_NUM      37
#define Y7_GPIO_NUM      38
#define Y6_GPIO_NUM      39
#define Y5_GPIO_NUM      35
#define Y4_GPIO_NUM      14
#define Y3_GPIO_NUM      13
#define Y2_GPIO_NUM      34
#define VSYNC_GPIO_NUM   5
#define HREF_GPIO_NUM    27
#define PCLK_GPIO_NUM    25

// Global variables to track current settings
framesize_t currentFrameSize = FRAMESIZE_HD;
int currentRotation = 90;

// LED Pin Definitions
#define RED_LED GPIO_NUM_21    // Red LED on GPIO 21
#define WHITE_LED GPIO_NUM_22  // White LED on GPIO 22

// Global variables for LED control
unsigned long previousRedBlink = 0;
unsigned long previousWhiteBlink = 0;
const long redBlinkInterval = 500;    // Blink interval for red LED (500ms)
const long whiteBlinkInterval = 1000;  // Blink interval for white LED (1 second)
bool redLedState = false;
bool whiteLedState = false;

void setupLEDs() {
    // Configure GPIO pins for LED output
    gpio_config_t io_conf = {};
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    
    // Set bit mask for the LEDs
    io_conf.pin_bit_mask = (1ULL << RED_LED) | (1ULL << WHITE_LED);
    
    // Configure GPIO with the given settings
    gpio_config(&io_conf);

    // Initialize LEDs to OFF state
    gpio_set_level(RED_LED, 0);
    gpio_set_level(WHITE_LED, 0);
}

void blinkRedLED() {
    unsigned long currentMillis = millis();
    if (currentMillis - previousRedBlink >= redBlinkInterval) {
        previousRedBlink = currentMillis;
        redLedState = !redLedState;
        gpio_set_level(RED_LED, redLedState);
    }
}

void blinkWhiteLED() {
    unsigned long currentMillis = millis();
    if (currentMillis - previousWhiteBlink >= whiteBlinkInterval) {
        previousWhiteBlink = currentMillis;
        whiteLedState = !whiteLedState;
        gpio_set_level(WHITE_LED, whiteLedState);
    }
}

// Function to initialize camera
bool initCamera() {
    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM;
    config.pin_sccb_sda = SIOD_GPIO_NUM;
    config.pin_sccb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;
    
    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_JPEG;
    config.grab_mode = CAMERA_GRAB_LATEST;
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.jpeg_quality = 10;
    config.frame_size = FRAMESIZE_HD;
    config.fb_count = 2;

    // Initialize the camera
    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("Camera init failed with error 0x%x", err);
        return false;
    }

    sensor_t * s = esp_camera_sensor_get();
    if (s) {
        s->set_brightness(s, 1);
        s->set_contrast(s, 1);
        s->set_saturation(s, 1);
        s->set_whitebal(s, 1);
        s->set_awb_gain(s, 1);
        s->set_wb_mode(s, 0);
        s->set_exposure_ctrl(s, 1);
        s->set_aec2(s, 0);
        s->set_gain_ctrl(s, 1);
        s->set_agc_gain(s, 0);
        s->set_gainceiling(s, (gainceiling_t)2);
        s->set_bpc(s, 1);
        s->set_wpc(s, 1);
        s->set_raw_gma(s, 1);
        s->set_lenc(s, 1);
        s->set_hmirror(s, 1);
        s->set_vflip(s, 1);
        s->set_dcw(s, 1);
    }

    return true;
}

void setRotation(int degrees) {
    sensor_t * s = esp_camera_sensor_get();
    if (!s) return;

    switch (degrees) {
        case 0:   // Normal
            s->set_hmirror(s, 0);
            s->set_vflip(s, 0);
            break;
        case 90:  // 90 degrees
            s->set_hmirror(s, 1);
            s->set_vflip(s, 1);
            break;
        case 180: // 180 degrees
            s->set_hmirror(s, 1);
            s->set_vflip(s, 0);
            break;
        case 270: // 270 degrees
            s->set_hmirror(s, 0);
            s->set_vflip(s, 1);
            break;
    }
    currentRotation = degrees;
}

void handleRotate() {
    if (server.hasArg("degrees")) {
        int degrees = server.arg("degrees").toInt();
        setRotation(degrees);
        server.send(200, "text/plain", "Rotation set to " + String(degrees) + " degrees");
    } else {
        server.send(400, "text/plain", "Missing degrees parameter");
    }
}

void handleSetResolution() {
    if (server.hasArg("size")) {
        String size = server.arg("size");
        framesize_t newSize;
        
        if (size == "SVGA") newSize = FRAMESIZE_SVGA;
        else if (size == "XGA") newSize = FRAMESIZE_XGA;
        else if (size == "HD") newSize = FRAMESIZE_HD;
        else if (size == "SXGA") newSize = FRAMESIZE_SXGA;
        else if (size == "UXGA") newSize = FRAMESIZE_UXGA;
        else {
            server.send(400, "text/plain", "Invalid resolution");
            return;
        }
        
        sensor_t * s = esp_camera_sensor_get();
        if (s) {
            s->set_framesize(s, newSize);
            currentFrameSize = newSize;
            server.send(200, "text/plain", "Resolution set to " + size);
        } else {
            server.send(500, "text/plain", "Camera sensor not found");
        }
    } else {
        server.send(400, "text/plain", "Missing size parameter");
    }
}

void handleGetImage() {
    // Check if enough time has passed since last capture
    if (millis() - lastCaptureTime < captureInterval) {
        server.send(503, "text/plain", "Too many requests");
        return;
    }

    // Try to take the semaphore
    if (xSemaphoreTake(cameraSemaphore, pdMS_TO_TICKS(500)) != pdTRUE) {
        server.send(503, "text/plain", "Camera busy");
        return;
    }

    camera_fb_t* fb = NULL;
    
    // Update last capture time
    lastCaptureTime = millis();

    try {
        fb = esp_camera_fb_get();
        
        if (!fb) {
            Serial.println("Camera capture failed");
            server.send(500, "text/plain", "Camera capture failed");
            xSemaphoreGive(cameraSemaphore);
            return;
        }

        if (fb->len > 0) {
            server.sendHeader("Content-Type", "image/jpeg");
            server.sendHeader("Content-Disposition", "inline; filename=capture.jpg");
            server.sendHeader("Content-Length", String(fb->len));
            server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
            server.sendHeader("Pragma", "no-cache");
            server.sendHeader("Expires", "0");
            
            server.send_P(200, "image/jpeg", (const char *)fb->buf, fb->len);
        } else {
            Serial.println("Invalid frame buffer length");
            server.send(500, "text/plain", "Invalid frame buffer");
        }

    } catch (...) {
        Serial.println("Exception in handleGetImage");
        server.send(500, "text/plain", "Internal error");
    }

    // Always cleanup
    if (fb) {
        esp_camera_fb_return(fb);
    }
    xSemaphoreGive(cameraSemaphore);
}

String getStreamPageJS() {
    return R"(
    <script>
        let retryCount = 0;
        const maxRetries = 3;
        
        function setResolution(size) {
            fetch('/setResolution?size=' + size)
                .then(response => {
                    if (!response.ok) {
                        throw new Error('Resolution change failed');
                    }
                    return response.text();
                })
                .then(data => {
                    document.getElementById('currentRes').textContent = size;
                    document.querySelectorAll('.resolution-controls button').forEach(btn => btn.classList.remove('active'));
                    document.getElementById(size).classList.add('active');
                    retryCount = 0;
                    updateImage();
                })
                .catch(error => {
                    console.error('Error:', error);
                    alert('Failed to change resolution. Please try again.');
                });
        }

        function rotate(degrees) {
            fetch('/rotate?degrees=' + degrees)
                .then(response => {
                    if (!response.ok) {
                        throw new Error('Rotation failed');
                    }
                    return response.text();
                })
                .then(data => {
                    document.getElementById('currentRot').textContent = degrees + '°';
                    document.querySelectorAll('.rotation-controls button').forEach(btn => btn.classList.remove('active'));
                    document.getElementById('rot' + degrees).classList.add('active');
                    retryCount = 0;
                    updateImage();
                })
                .catch(error => {
                    console.error('Error:', error);
                    alert('Failed to rotate image. Please try again.');
                });
        }

        function updateImage() {
            if (retryCount >= maxRetries) {
                console.error('Max retries reached, stopping updates');
                return;
            }

            const img = document.getElementById('camera');
            const newImg = new Image();
            
            newImg.onload = function() {
                img.src = this.src;
                retryCount = 0;
            };
            
            newImg.onerror = function() {
                console.error('Failed to load image, attempt: ' + (retryCount + 1));
                retryCount++;
                if (retryCount < maxRetries) {
                    setTimeout(updateImage, 1000);
                } else {
                    console.error('Failed to load image after ' + maxRetries + ' attempts');
                    alert('Camera connection lost. Please refresh the page.');
                }
            };

            newImg.src = '/getImage?' + new Date().getTime();
        }

        let updateInterval;
        
        function startUpdateInterval() {
            stopUpdateInterval();
            updateImage();
            updateInterval = setInterval(() => {
                if (retryCount < maxRetries) {
                    updateImage();
                } else {
                    stopUpdateInterval();
                }
            }, 1000);
        }

        function stopUpdateInterval() {
            if (updateInterval) {
                clearInterval(updateInterval);
            }
        }

        document.getElementById('SVGA').classList.add('active');
        document.getElementById('rot0').classList.add('active');
        startUpdateInterval();

        document.addEventListener('visibilitychange', function() {
            if (document.hidden) {
                stopUpdateInterval();
            } else {
                retryCount = 0;
                startUpdateInterval();
            }
        });
    </script>
    )";
}

void setup() {
    Serial.begin(115200);
    Serial.println("Starting setup...");

    // Initialize LEDs
    setupLEDs();

    // Create semaphore before initializing camera
    cameraSemaphore = xSemaphoreCreateMutex();
    if (!cameraSemaphore) {
        Serial.println("Failed to create camera semaphore!");
        return;
    }

    // Initialize camera with retry
    int retryCount = 0;
    const int maxRetries = 3;
    bool cameraInitialized = false;

    while (!cameraInitialized && retryCount < maxRetries) {
        if (initCamera()) {
            cameraInitialized = true;
            Serial.println("Camera initialized successfully!");
        } else {
            retryCount++;
            Serial.printf("Camera initialization attempt %d failed, retrying...\n", retryCount);
            delay(1000);
        }
    }

    if (!cameraInitialized) {
        Serial.println("Camera initialization failed after multiple attempts!");
        return;
    }

    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        blinkRedLED();  // Blink red LED while connecting
        delay(10);  // Short delay to prevent watchdog trigger
        Serial.print(".");
    }

    // Turn off red LED once connected
    gpio_set_level(RED_LED, 0);
    redLedState = false;

    Serial.println("");
    Serial.println("WiFi connected");
    Serial.print("Camera Ready! Use 'http://");
    Serial.print(WiFi.localIP());
    Serial.println("/stream' to connect");

    server.on("/getImage", HTTP_GET, handleGetImage);
    server.on("/setResolution", HTTP_GET, handleSetResolution);
    server.on("/rotate", HTTP_GET, handleRotate);
    
    server.on("/stream", HTTP_GET, []() {
        String html = "<html>"
            "<head>"
                "<style>"
                    "body { font-family: Arial, sans-serif; margin: 20px; }"
                    ".container { text-align: center; }"
                    "img { max-width: 100%; height: auto; margin: 10px 0; }"
                    ".controls { margin: 15px 0; }"
                    ".control-group { margin: 15px 0; padding: 10px; border: 1px solid #ddd; border-radius: 4px; }"
                    ".control-group h3 { margin: 0 0 10px 0; color: #333; }"
                    "button { padding: 8px 15px; margin: 0 5px; background-color: #4CAF50; color: white; "
                            "border: none; border-radius: 4px; cursor: pointer; }"
                    "button:hover { background-color: #45a049; }"
                    ".active { background-color: #357abd; }"
                    ".status-info { margin: 10px 0; font-size: 14px; color: #666; }"
                    ".rotation-controls button { background-color: #ff9800; }"
                    ".rotation-controls button:hover { background-color: #f57c00; }"
                    ".rotation-controls .active { background-color: #e65100; }"
                "</style>"
                "</head>"
            "<body>"
                "<div class=\"container\">"
                    "<img src=\"/getImage\" id=\"camera\">"
                    
                    "<div class=\"control-group\">"
                        "<h3>Resolution Control</h3>"
                        "<div class=\"controls resolution-controls\">"
                            "<button onclick=\"setResolution('SVGA')\" id=\"SVGA\">SVGA (800x600)</button>"
                            "<button onclick=\"setResolution('XGA')\" id=\"XGA\">XGA (1024x768)</button>"
                            "<button onclick=\"setResolution('HD')\" id=\"HD\">HD (1280x720)</button>"
                            "<button onclick=\"setResolution('SXGA')\" id=\"SXGA\">SXGA (1280x1024)</button>"
                            "<button onclick=\"setResolution('UXGA')\" id=\"UXGA\">UXGA (1600x1200)</button>"
                        "</div>"
                    "</div>"

                    "<div class=\"control-group\">"
                        "<h3>Rotation Control</h3>"
                        "<div class=\"controls rotation-controls\">"
                            "<button onclick=\"rotate(0)\" id=\"rot0\">0&deg;</button>"
                            "<button onclick=\"rotate(90)\" id=\"rot90\">90&deg;</button>"
                            "<button onclick=\"rotate(180)\" id=\"rot180\">180&deg;</button>"
                            "<button onclick=\"rotate(270)\" id=\"rot270\">270&deg;</button>"
                        "</div>"
                    "</div>"

                    "<div class=\"status-info\">"
                        "Current resolution: <span id=\"currentRes\">SVGA</span><br>"
                        "Current rotation: <span id=\"currentRot\">0&deg;</span>"
                    "</div>"
                "</div>";
        
        // Add the JavaScript
        html += getStreamPageJS();
        
        html += "</body></html>";
        
        server.send(200, "text/html", html);
    });

    // Root redirect
    server.on("/", HTTP_GET, []() {
        server.sendHeader("Location", "/stream", true);
        server.send(302, "text/plain", "");
    });

    // Add error handler for undefined endpoints
    server.onNotFound([]() {
        server.send(404, "text/plain", "Not Found");
    });

    server.begin();
    Serial.println("HTTP server started");

    // Final setup verification
    if (esp_camera_sensor_get()) {
        Serial.println("Camera sensor verified and ready");
    } else {
        Serial.println("Warning: Camera sensor verification failed");
    }
}

void loop() {
    server.handleClient();

    // Blink white LED
    blinkWhiteLED();

    // Add a small delay to prevent watchdog triggers
    delay(1);
}

// Helper function for clean shutdown if needed
void cleanShutdown() {
    // Turn off both LEDs
    gpio_set_level(RED_LED, 0);
    gpio_set_level(WHITE_LED, 0);
    redLedState = false;
    whiteLedState = false;

    if (cameraSemaphore) {
        vSemaphoreDelete(cameraSemaphore);
        cameraSemaphore = NULL;
    }
    esp_camera_deinit();
    WiFi.disconnect(true);
}