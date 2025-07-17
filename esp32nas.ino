#include <WiFi.h>
#include <WebServer.h>
#include <FS.h>   
#include <SD.h>   
#include <SPI.h>  
#include "esp_task_wdt.h"

const char* ssid = "redacted";
const char* password = "redacted";

const char* nas_username = "admin";
const char* nas_password = "123";

#define SD_SCK    18
#define SD_MISO   19
#define SD_MOSI   23
#define SD_CS     15

#define WDT_TIMEOUT_SECONDS 30

const long MAX_FILE_SIZE_MB = 10;
const long MAX_FILE_SIZE_BYTES = MAX_FILE_SIZE_MB * 1024 * 1024;

WebServer server(80);

File uploadedFile;
bool isUploading = false;
unsigned long uploadedBytes = 0;
unsigned long totalUploadSize = 0;

String getIndexPage(String message = "", String messageType = "info"); 
String getFileListHtml(String path);
String getWifiSignalStrengthHtml();

void handleRoot();
void handleListFiles();
void handleDownload();
void handleUpload();
void handleDelete();
void handleNotFound();

bool isAuthenticated(); 

void testSDCard() {
  Serial.println("--- Starting SD Card Read/Write Test ---");
  const char* testFileName = "/test_write.txt";
  const char* testContent = "Hello from ESP32!";

  Serial.print("Creating/writing file: ");
  Serial.print(testFileName);
  File testFile = SD.open(testFileName, FILE_WRITE);
  if (!testFile) {
    Serial.println(" - FAILED to open for writing!");
    Serial.println("--- SD Card Test FAILED ---");
    return;
  }
  testFile.print(testContent);
  testFile.close();
  Serial.println(" - SUCCESS (written)");

  Serial.print("Reading file: ");
  Serial.print(testFileName);
  testFile = SD.open(testFileName, FILE_READ);
  if (!testFile) {
    Serial.println(" - FAILED to open for reading!");
    Serial.println("--- SD Card Test FAILED ---");
    return;
  }
  String readContent = "";
  while (testFile.available()) {
    readContent += (char)testFile.read();
  }
  testFile.close();
  Serial.print(" - SUCCESS (read content: \"");
  Serial.print(readContent);
  Serial.println("\")");

  if (readContent == testContent) {
    Serial.println("Content verification: MATCH!");
  } else {
    Serial.println("Content verification: MISMATCH!");
    Serial.println("--- SD Card Test FAILED (Content Mismatch) ---");
    return;
  }

  Serial.print("Deleting file: ");
  Serial.print(testFileName);
  if (SD.remove(testFileName)) {
    Serial.println(" - SUCCESS (deleted)");
  } else {
    Serial.println(" - FAILED to delete!");
    Serial.println("--- SD Card Test FAILED (Deletion) ---");
    return;
  }

  Serial.println("--- SD Card Test PASSED ---");
}

void setup() {
  Serial.begin(115200);
  while (!Serial);

  Serial.printf("Setting up Watchdog Timer with %d seconds timeout.\n", WDT_TIMEOUT_SECONDS);
  esp_task_wdt_config_t wdt_config = {
    .timeout_ms = WDT_TIMEOUT_SECONDS * 1000,
    .trigger_panic = true
  };
  esp_task_wdt_init(&wdt_config);
  esp_task_wdt_add(NULL);

  Serial.print("Attempting to connect to WiFi: ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);

  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 20) {
    delay(500);
    Serial.print(".");
    esp_task_wdt_reset();
    retries++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected successfully!");
    Serial.print("ESP32 IP Address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nFailed to connect to WiFi. Please check your SSID and password.");
    Serial.println("Restarting ESP32 in 5 seconds...");
    delay(5000);
    ESP.restart();
  }

  Serial.print("Initializing SD card on CS pin: ");
  Serial.println(SD_CS);
  if (!SD.begin(SD_CS)) {
    Serial.println("SD Card Mount Failed!");
    Serial.println("Please check the following:");
    Serial.println("1. Wiring: Ensure CS, SCK, MISO, MOSI, VCC, GND are connected correctly.");
    Serial.println("2. SD_CS Pin: Verify that #define SD_CS in the code matches the actual GPIO pin.");
    Serial.println("3. SD Card: Is it formatted as FAT32? Is it inserted properly? Is it a working card?");
    return;
  }

  uint8_t cardType = SD.cardType();
  if (cardType == CARD_NONE) {
    Serial.println("No SD card detected.");
    return;
  }
  Serial.print("SD Card Type: ");
  if (cardType == CARD_MMC) {
    Serial.println("MMC");
  } else if (cardType == CARD_SD) {
    Serial.println("SDSC");
  } else if (cardType == CARD_SDHC) {
    Serial.println("SDHC");
  } else {
    Serial.println("UNKNOWN");
  }
  Serial.printf("SD Card Size: %lluMB\n", SD.cardSize() / (1024 * 1024));

  testSDCard();

  server.on("/", HTTP_GET, handleRoot);
  server.on("/list", HTTP_GET, handleListFiles);
  server.on("/download", HTTP_GET, handleDownload);
  server.on("/upload", HTTP_POST, handleUpload, handleUpload);
  server.on("/delete", HTTP_GET, handleDelete);

  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("HTTP server started successfully!");
}

void loop() {
  esp_task_wdt_reset();
  server.handleClient();
}

bool isAuthenticated() {
  
  if (server.authenticate(nas_username, nas_password)) {
    return true; 
  }

  server.requestAuthentication();
  return false; 
}

String getIndexPage(String message, String messageType) {
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>Simple ESP32 NAS</title>
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <script src="https://cdn.tailwindcss.com"></script>
    <style>
        body { font-family: 'Inter', sans-serif; }
        .file-item {
            display: flex;
            justify-content: space-between;
            align-items: center;
            padding: 8px 0;
            border-bottom: 1px solid #eee;
        }
        .file-item:last-child {
            border-bottom: none;
        }
        .file-actions button {
            margin-left: 8px;
        }
    </style>
</head>
<body class="bg-gray-100 p-4 sm:p-8">
    <div class="max-w-4xl mx-auto bg-white p-6 rounded-lg shadow-lg">
        <div class="flex justify-between items-center mb-6 pb-4 border-b border-gray-200">
            <h1 class="text-3xl font-bold text-gray-800">Simple ESP32 NAS</h1>
            <!-- Removed Logout Button: No server-side session to manage -->
        </div>
        )rawliteral";

  if (message != "") {
    String bgColor, borderColor, textColor;
    if (messageType == "success") {
      bgColor = "bg-green-100"; borderColor = "border-green-400"; textColor = "text-green-700";
    } else if (messageType == "error") {
      bgColor = "bg-red-100"; borderColor = "border-red-400"; textColor = "text-red-700";
    } else { 
      bgColor = "bg-blue-100"; borderColor = "border-blue-400"; textColor = "text-blue-700";
    }
    
    html += "<div id='messageBox' class='" + bgColor + " " + borderColor + " " + textColor + " px-4 py-3 rounded-lg relative mb-4' role='alert'>";
    html += "<span class='block sm:inline'>" + message + "</span>";
    html += "<span class='absolute top-0 bottom-0 right-0 px-4 py-3 cursor-pointer' onclick=\"document.getElementById('messageBox').style.display='none';\">";
    html += "<svg class='fill-current h-6 w-6 " + textColor + "' role='button' xmlns='http://www.w3.org/2000/svg' viewBox='0 0 20 20'><title>Close</title><path d='M14.348 14.849a1.2 1.2 0 0 1-1.697 0L10 11.819l-2.651 3.029a1.2 1.2 0 1 1-1.697-1.697l2.758-3.15-2.759-3.152a1.2 1.2 0 1 1 1.697-1.697L10 8.183l2.651-3.031a1.2 1.2 0 1 1 1.697 1.697l-2.758 3.152 2.758 3.15a1.2 1.2 0 0 1 0 1.698z'/></svg>";
    html += "</span></div>";
  }

  html += getWifiSignalStrengthHtml();

  html += R"rawliteral(
        <div class="mb-8">
            <h2 class="text-2xl font-semibold mb-4 text-gray-700">File Operations</h2>

            <div class="mb-6 p-4 border border-gray-200 rounded-lg bg-gray-50">
                <h3 class="text-xl font-medium mb-2 text-gray-600">Upload File (Max )rawliteral";
  html += String(MAX_FILE_SIZE_MB) + "MB)";
  html += R"rawliteral(</h3>
                <form id="uploadForm" class="flex flex-col sm:flex-row items-start sm:items-center space-y-4 sm:space-y-0 sm:space-x-4">
                    <input type="file" id="fileInput" name="file" class="block w-full text-sm text-gray-500 file:mr-4 file:py-2 file:px-4 file:rounded-lg file:border-0 file:text-sm file:font-semibold file:bg-blue-50 file:text-blue-700 hover:file:bg-blue-100 cursor-pointer rounded-lg border border-gray-300 p-2">
                    <button type="submit" class="bg-green-600 hover:bg-green-700 text-white font-bold py-2 px-4 rounded-lg shadow-md transition duration-150 ease-in-out">Upload</button>
                    <div id="progressBarContainer" class="w-full bg-gray-200 rounded-full h-4 relative hidden mt-2">
                        <div id="progressBar" class="bg-blue-500 h-4 rounded-full text-xs flex justify-center items-center text-white" style="width: 0%;"></div>
                        <span id="progressText" class="absolute inset-0 flex items-center justify-center text-xs font-semibold text-gray-700">0%</span>
                    </div>
                </form>
            </div>

            <h2 class="text-2xl font-semibold mb-4 text-gray-700">Files on SD Card</h2>
            <div id="fileList" class="bg-gray-50 p-4 rounded-lg border border-gray-200">
                Loading files...
            </div>
        </div>

        <script>
            function showMessage(msg, type = 'info') {
                const messageBox = document.getElementById('messageBox');
                let bgColor, borderColor, textColor;
                if (type === "success") {
                    bgColor = "bg-green-100"; borderColor = "border-green-400"; textColor = "text-green-700";
                } else if (type === "error") {
                    bgColor = "bg-red-100"; borderColor = "border-red-400"; textColor = "text-red-700";
                } else { 
                    bgColor = "bg-blue-100"; borderColor = "border-blue-400"; textColor = "text-blue-700";
                }

                if (messageBox) {
                    messageBox.className = `${bgColor} ${borderColor} ${textColor} px-4 py-3 rounded-lg relative mb-4`;
                    messageBox.querySelector('span.block').textContent = msg;
                    messageBox.querySelector('svg').className = `fill-current h-6 w-6 ${textColor}`;
                    messageBox.style.display = 'block'; 
                } else {
                    console.log(`Message (${type}): ${msg}`);
                }
            }
            async function fetchFileList() {
                try {
                    const response = await fetch('/list');
                    if (!response.ok) {
                        throw new Error(`HTTP error! status: ${response.status}`);
                    }
                    const data = await response.text();
                    document.getElementById('fileList').innerHTML = data;
                } catch (error) {
                    console.error('Error fetching file list:', error);
                    document.getElementById('fileList').innerHTML = '<p class="text-red-500">Error loading files.</p>';
                    showMessage('Error loading files: ' + error.message, 'error');
                }
            }
    
            document.getElementById('uploadForm').addEventListener('submit', async function(event) {
                event.preventDefault();
                const fileInput = document.getElementById('fileInput');
                const progressBarContainer = document.getElementById('progressBarContainer');
                const progressBar = document.getElementById('progressBar');
                const progressText = document.getElementById('progressText');

                if (fileInput.files.length === 0) {
                    showMessage('Please select a file to upload.', 'info');
                    return;
                }
                const file = fileInput.files[0];

                const MAX_FILE_SIZE_BYTES_JS = )rawliteral";
  html += String(MAX_FILE_SIZE_BYTES);
  html += R"rawliteral(;
                if (file.size > MAX_FILE_SIZE_BYTES_JS) {
                    showMessage(`File is too large. Maximum allowed size is ${MAX_FILE_SIZE_BYTES_JS / (1024 * 1024)} MB.`, 'error');
                    fileInput.value = '';
                    return;
                }

                const formData = new FormData();
                formData.append('file', file);

                progressBarContainer.classList.remove('hidden');
                progressBar.style.width = '0%';
                progressText.textContent = '0%';

                const xhr = new XMLHttpRequest();
                xhr.open('POST', '/upload', true);

                xhr.upload.addEventListener('progress', function(e) {
                    if (e.lengthComputable) {
                        const percentComplete = (e.loaded / e.total) * 100;
                        progressBar.style.width = percentComplete.toFixed(0) + '%';
                        progressText.textContent = percentComplete.toFixed(0) + '%';
                    }
                });

                xhr.onload = function() {
                    progressBarContainer.classList.add('hidden');
                    if (xhr.status === 200) {
                        showMessage(xhr.responseText, 'success');
                        fileInput.value = '';
                        fetchFileList();
                    } else if (xhr.status === 401) {
                        showMessage('Authentication required for upload. Please re-enter credentials.', 'error');
                        console.error('Error uploading file: Unauthorized');
                    } else {
                        showMessage('Error uploading file: ' + xhr.responseText, 'error');
                        console.error('Error uploading file:', xhr.status, xhr.responseText);
                    }
                };

                xhr.onerror = function() {
                    progressBarContainer.classList.add('hidden');
                    showMessage('Network error during upload. Check console for details.', 'error');
                    console.error('Network error during upload.');
                };

                xhr.send(formData);
            });

            async function deleteFile(filename) {
                if (confirm(`Are you sure you want to delete "${filename}"?`)) {
                    try {
                        const response = await fetch(`/delete?file=${encodeURIComponent(filename)}`);
                        if (!response.ok) {
                            if (response.status === 401) {
                                showMessage('Authentication required for deletion. Please re-enter credentials.', 'error');
                                return;
                            }
                            throw new Error(`HTTP error! status: ${response.status}`);
                        }
                        const result = await response.text();
                        showMessage(result, 'success');
                        fetchFileList();
                    } catch (error) {
                        console.error('Error deleting file:', error);
                        showMessage('Error deleting file: ' + error.message, 'error');
                    }
                }
            }

            window.onload = fetchFileList;
        </script>
    </div>
</body>
</html>
)rawliteral";
  return html;
}

String getFileListHtml(String path) {
  String html = "<ul class='list-disc pl-5 space-y-2'>"; 
  File root = SD.open(path);

  if (!root) {
    return "<p class='text-red-500'>Failed to open directory.</p>";
  }
  if (!root.isDirectory()) {
    return "<p class='text-red-500'>Not a directory.</p>";
  }

  File file = root.openNextFile();
  while (file) {
    html += "<li class='file-item bg-white p-3 rounded-lg shadow-sm border border-gray-100'>"; 
    if (file.isDirectory()) {
      html += "<span class='text-blue-700 font-semibold flex items-center'><svg xmlns='http://www.w3.org/2000/svg' class='h-5 w-5 mr-2 text-blue-500' viewBox='0 0 20 20' fill='currentColor'><path d='M2 6a2 2 0 012-2h5l2 2h5a2 2 0 012 2v6a2 2 0 01-2 2H4a2 2 0 01-2-2V6z' /></svg>" + String(file.name()) + "/</span>"; 
    } else {
      html += "<span class='text-gray-800 flex items-center'><svg xmlns='http://www.w3.org/2000/svg' class='h-5 w-5 mr-2 text-gray-400' viewBox='0 0 20 20' fill='currentColor'><path fill-rule='evenodd' d='M4 4a2 2 0 012-2h4.586a1 1 0 01.707.293l4.414 4.414a1 1 0 01.293.707V16a2 2 0 01-2 2H6a2 2 0 01-2-2V4zm2 6a1 1 0 011-1h6a1 1 0 110 2H7a1 1 0 01-1-1zm1-3a1 1 0 100 2h6a1 1 0 100-2H7z' clip-rule='evenodd' /></svg>" + String(file.name()) + " <span class='text-xs text-gray-500 ml-2'>(" + String(file.size()) + " bytes)</span></span>";
      html += "<div class='file-actions flex items-center'>"; 
      html += "<a href='/download?file=" + String(file.name()) + "' class='bg-blue-500 hover:bg-blue-600 text-white text-xs py-1 px-2 rounded-lg shadow-sm transition duration-150 ease-in-out' download>Download</a>";
      html += "<button onclick='deleteFile(\"" + String(file.name()) + "\")' class='bg-red-500 hover:bg-red-600 text-white text-xs py-1 px-2 rounded-lg shadow-sm transition duration-150 ease-in-out'>Delete</button>";
      html += "</div>";
    }
    html += "</li>";
    file = root.openNextFile();
  }
  html += "</ul>";

  if (html == "<ul class='list-disc pl-5 space-y-2'></ul>") { 
    html = "<p class='text-gray-500 text-center py-4'>No files or folders found on the SD card.</p>";
  }
  return html;
}

String getWifiSignalStrengthHtml() {
  long rssi = WiFi.RSSI();
  String strengthText;
  String colorClass;
  String warningMessage = "";
  int level;

  if (rssi > -50) {
    strengthText = "Excellent";
    colorClass = "text-green-600";
    level = 5;
  } else if (rssi > -60) {
    strengthText = "Good";
    colorClass = "text-lime-600";
    level = 4;
  } else if (rssi > -70) {
    strengthText = "Fair";
    colorClass = "text-yellow-600";
    level = 3;
    warningMessage = " (Wi-Fi may cause problems)";
  } else if (rssi > -80) {
    strengthText = "Weak";
    colorClass = "text-orange-600";
    level = 2;
    warningMessage = " (Wi-Fi likely to cause problems)";
  } else {
    strengthText = "Very Weak";
    colorClass = "text-red-600";
    level = 1;
    warningMessage = " (Wi-Fi will cause significant problems)";
  }

  String html = "<div class='mb-4 text-center p-2 rounded-lg bg-gray-50 border border-gray-200'>";
  html += "<p class='text-gray-700 text-lg font-semibold'>Wi-Fi Signal: <span class='" + colorClass + "'>" + strengthText + " (Level " + String(level) + ")</span></p>";
  if (warningMessage != "") {
    html += "<p class='text-sm text-red-500 mt-1'>" + warningMessage + "</p>"; 
  }
  html += "</div>";
  return html;
}




void handleRoot() {
  if (!isAuthenticated()) {
    return;
  }
  String message = "";
  String messageType = "info";
  if (server.hasArg("message")) {
    message = server.arg("message");
  }
  if (server.hasArg("type")) {
    messageType = server.arg("type");
  }
  server.send(200, "text/html", getIndexPage(message, messageType));
}

void handleListFiles() {
  if (!isAuthenticated()) {
    return;
  }
  server.send(200, "text/html", getFileListHtml("/"));
}

void handleDownload() {
  if (!isAuthenticated()) {
    return;
  }

  String fileName = server.arg("file");
  if (!fileName) {
    server.send(400, "text/plain", "Error: File name not specified for download.");
    return;
  }

  File file = SD.open("/" + fileName);
  if (!file) {
    server.send(404, "text/plain", "Error: File not found on SD card.");
    return;
  }

  server.streamFile(file, file.name());
  file.close();
}

void handleUpload() {
  esp_task_wdt_reset();

  if (!isAuthenticated()) {
    server.send(401, "text/plain", "Unauthorized. Please re-enter credentials.");
    return;
  }

  HTTPUpload& upload = server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    isUploading = true;
    uploadedBytes = 0;
    totalUploadSize = upload.totalSize;

    String filename = upload.filename;
    if (!filename.startsWith("/")) {
      filename = "/" + filename;
    }

    Serial.print("Starting upload of: ");
    Serial.println(filename);
    Serial.printf("Total size expected (from header): %lu bytes (%.2f MB)\n", totalUploadSize, (float)totalUploadSize / (1024.0 * 1024.0));

    if (totalUploadSize > MAX_FILE_SIZE_BYTES) {
      Serial.printf("Error: File too large! Max allowed: %lu bytes (%.2f MB)\n", MAX_FILE_SIZE_BYTES, (float)MAX_FILE_SIZE_BYTES / (1024.0 * 1024.0));
      server.send(413, "text/plain", "Error: File too large. Max " + String(MAX_FILE_SIZE_MB) + "MB allowed.");
      isUploading = false;
      return;
    }

    uploadedFile = SD.open(filename, FILE_WRITE);
    if (!uploadedFile) {
      Serial.println("Failed to open file for writing on SD card at UPLOAD_FILE_START.");
      server.send(500, "text/plain", "Error: Failed to open file for writing.");
      isUploading = false;
      return;
    }
    Serial.println("File opened for writing at UPLOAD_FILE_START.");

  } else if (upload.status == UPLOAD_FILE_WRITE) {
    esp_task_wdt_reset();

    if (!uploadedFile) {
      Serial.println("Error: uploadedFile is null during UPLOAD_FILE_WRITE. Aborting.");
      server.send(500, "text/plain", "Error: File handle lost during upload.");
      isUploading = false;
      return;
    }

    size_t bytesWritten = uploadedFile.write(upload.buf, upload.currentSize);
    if (bytesWritten != upload.currentSize) {
      Serial.printf("WARNING: Mismatch in bytes written! Expected: %u, Actual: %u\n", upload.currentSize, bytesWritten);
    }

    uploadedBytes += bytesWritten;
    if (totalUploadSize > 0) {
      int percent = (uploadedBytes * 100) / totalUploadSize;
      Serial.printf("Uploaded: %lu bytes (%d%%) (Chunk size: %u, Written: %u)\n", uploadedBytes, percent, upload.currentSize, bytesWritten);
    } else {
      Serial.printf("Uploaded: %lu bytes (Chunk size: %u, Written: %u)\n", uploadedBytes, upload.currentSize, bytesWritten);
    }

  } else if (upload.status == UPLOAD_FILE_END) {
    if (uploadedFile) {
      uploadedFile.close();
      Serial.println("File closed after successful upload.");
    }
    Serial.print("\nUpload finished. Actual bytes received: ");
    Serial.println(uploadedBytes);
    server.send(200, "text/plain", "File uploaded successfully!");
    isUploading = false;
    uploadedBytes = 0;
    totalUploadSize = 0;
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    if (uploadedFile) {
      uploadedFile.close();
      Serial.println("File closed after aborted upload.");
    }
    Serial.println("\nUpload aborted.");
    server.send(500, "text/plain", "Error: File upload aborted.");
    isUploading = false;
    uploadedBytes = 0;
    totalUploadSize = 0;
  }
}

void handleDelete() {
  if (!isAuthenticated()) {
    server.send(401, "text/plain", "Unauthorized. Please re-enter credentials.");
    return;
  }

  String fileName = server.arg("file");
  if (!fileName) {
    server.send(400, "text/plain", "Error: File name not specified for deletion.");
    return;
  }

  if (SD.remove("/" + fileName)) {
    server.send(200, "text/plain", "File deleted successfully: " + fileName);
  } else {
    server.send(500, "text/plain", "Error: Failed to delete file: " + fileName + ". File might not exist or is a directory.");
  }
}

void handleNotFound() {
  server.send(404, "text/plain", "404: Not Found");
}
