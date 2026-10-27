// ============================================================
// SMART ELECTRIC HELMET
// XIAO ESP32-C6
//
// FINAL PIN MAP - DO NOT CHANGE
// ============================================================
//
// D0 / GPIO0  -> NE555 AC electric-field detector
// D1 / GPIO1  -> SIM800L TX
// D2 / GPIO2  -> SIM800L RX
// D4 / GPIO22 -> VCNL4040 SDA
// D5 / GPIO23 -> VCNL4040 SCL
// D6 / GPIO16 -> VCNL4040 INT
// D8 / GPIO19 -> MAX98357A WS/LRC
// D9 / GPIO20 -> MAX98357A BCLK
// D10/GPIO18  -> MAX98357A DIN
//
// Audio:
// 48 kHz / 16-bit / mono PCM
//
// ============================================================

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <HardwareSerial.h>
#include <Adafruit_VCNL4040.h>
#include <driver/i2s_std.h>

#include "audio_data_pcm.h"


// ============================================================
// PIN DEFINITIONS
// ============================================================

#define AC_DETECT_PIN  0

#define SIM800_RX_PIN  1
#define SIM800_TX_PIN  2

#define I2C_SDA        22
#define I2C_SCL        23

#define VCNL_INT       16

#define I2S_WS         19
#define I2S_BCLK       20
#define I2S_DIN        18


// ============================================================
// WIFI ACCESS POINT
// ============================================================

const char* AP_SSID     = "SMART_HELMET_C6";
const char* AP_PASSWORD = "Helmet@123";

WebServer server(80);


// ============================================================
// SIM800L
// ============================================================

HardwareSerial sim800(1);

// IMPORTANT:
// Replace this with your actual emergency phone number.
const char* EMERGENCY_NUMBER = "+918778096703";

#define SIM800_BAUD 9600


// ============================================================
// VCNL4040
// ============================================================

Adafruit_VCNL4040 vcnl4040;


// ============================================================
// I2S
// ============================================================

i2s_chan_handle_t tx_handle = nullptr;

#define SAMPLE_RATE    48000
#define DMA_DESC_NUM   8
#define DMA_FRAME_NUM  256


// ============================================================
// HELMET DETECTION
// ============================================================
//
// IMPORTANT:
// This threshold may need adjustment depending on where the
// VCNL4040 is mounted inside the helmet.
//
// First watch the proximity value in Serial Monitor.
// Increase/decrease this value if required.
//
// ============================================================

#define HELMET_PROX_THRESHOLD 500

unsigned long helmetCandidateStart = 0;
unsigned long helmetRemovedStart = 0;

const unsigned long HELMET_WORN_CONFIRM_MS = 500;
const unsigned long HELMET_REMOVED_CONFIRM_MS = 1500;


// ============================================================
// HAZARD DETECTION
// ============================================================

unsigned long hazardCandidateStart = 0;

const unsigned long HAZARD_CONFIRM_MS = 400;


// ============================================================
// SYSTEM STATE
// ============================================================

enum SystemState
{
  SYSTEM_STARTUP,
  WAITING_FOR_HELMET,
  MONITORING,
  ELECTRICAL_HAZARD,
  HAZARD_CLEARED
};

SystemState systemState = SYSTEM_STARTUP;


// ============================================================
// SYSTEM VARIABLES
// ============================================================

bool helmetWorn = false;
bool electricalHazard = false;

bool gsmRegistered = false;
bool lbsAvailable = false;

bool audioActive = false;
bool smsSent = false;

bool alertSent = false;

double latitude = 0.0;
double longitude = 0.0;

int gsmSignal = 0;

uint16_t proximityValue = 0;

unsigned long detectionCount = 0;

String lastEvent = "System starting";
String currentAlert = "None";
String lastLocation = "Unavailable";


// ============================================================
// EVENT LOG
// ============================================================

#define MAX_EVENTS 20

String eventLog[MAX_EVENTS];
int eventIndex = 0;

void addEvent(String event)
{
  String entry = String(millis() / 1000) + "s - " + event;

  eventLog[eventIndex] = entry;

  eventIndex++;

  if (eventIndex >= MAX_EVENTS)
    eventIndex = 0;

  lastEvent = event;

  Serial.println("[EVENT] " + event);
}


// ============================================================
// STATE NAME
// ============================================================

String getStateName()
{
  switch (systemState)
  {
    case SYSTEM_STARTUP:
      return "SYSTEM STARTUP";

    case WAITING_FOR_HELMET:
      return "WAITING FOR HELMET";

    case MONITORING:
      return "MONITORING";

    case ELECTRICAL_HAZARD:
      return "ELECTRICAL HAZARD";

    case HAZARD_CLEARED:
      return "HAZARD CLEARED";
  }

  return "UNKNOWN";
}


// ============================================================
// I2S SETUP
// ============================================================

void setupI2S()
{
  i2s_chan_config_t chan_cfg =
      I2S_CHANNEL_DEFAULT_CONFIG(
        I2S_NUM_0,
        I2S_ROLE_MASTER
      );

  chan_cfg.dma_desc_num = DMA_DESC_NUM;
  chan_cfg.dma_frame_num = DMA_FRAME_NUM;

  ESP_ERROR_CHECK(
    i2s_new_channel(
      &chan_cfg,
      &tx_handle,
      NULL
    )
  );

  i2s_std_config_t std_cfg =
  {
    .clk_cfg =
      I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),

    .slot_cfg =
      I2S_STD_MSB_SLOT_DEFAULT_CONFIG(
        I2S_DATA_BIT_WIDTH_16BIT,
        I2S_SLOT_MODE_MONO
      ),

    .gpio_cfg =
    {
      .mclk = I2S_GPIO_UNUSED,
      .bclk = (gpio_num_t)I2S_BCLK,
      .ws   = (gpio_num_t)I2S_WS,
      .dout = (gpio_num_t)I2S_DIN,
      .din  = I2S_GPIO_UNUSED
    }
  };

  ESP_ERROR_CHECK(
    i2s_channel_init_std_mode(
      tx_handle,
      &std_cfg
    )
  );

  ESP_ERROR_CHECK(
    i2s_channel_enable(tx_handle)
  );

  Serial.println("I2S audio ready");
}


// ============================================================
// PLAY PCM AUDIO
// ============================================================

void playPCM(
  const int16_t *audio,
  uint32_t samples
)
{
  if (tx_handle == nullptr)
    return;

  audioActive = true;

  const size_t CHUNK_SAMPLES = 512;

  int16_t buffer[CHUNK_SAMPLES];

  uint32_t pos = 0;

  while (pos < samples)
  {
    uint32_t count =
      samples - pos;

    if (count > CHUNK_SAMPLES)
      count = CHUNK_SAMPLES;

    memcpy_P(
      buffer,
      audio + pos,
      count * sizeof(int16_t)
    );

    size_t bytesWritten = 0;

    esp_err_t err =
      i2s_channel_write(
        tx_handle,
        buffer,
        count * sizeof(int16_t),
        &bytesWritten,
        portMAX_DELAY
      );

    if (err != ESP_OK)
    {
      Serial.printf(
        "I2S error: %d\n",
        err
      );

      break;
    }

    pos += count;
  }

  memset(
    buffer,
    0,
    sizeof(buffer)
  );

  size_t bytesWritten = 0;

  i2s_channel_write(
    tx_handle,
    buffer,
    256 * sizeof(int16_t),
    &bytesWritten,
    portMAX_DELAY
  );

  audioActive = false;
}


// ============================================================
// AUDIO ANNOUNCEMENTS
// ============================================================

void announceStartup()
{
  Serial.println("A01: System active");

  playPCM(
    A01_audio,
    A01_samples
  );
}


void announceHelmetRemoved()
{
  Serial.println("A02: Please wear helmet");

  playPCM(
    A02_audio,
    A02_samples
  );
}


void announceElectricalDanger()
{
  Serial.println("A04: ELECTRICAL DANGER");

  playPCM(
    A04_audio,
    A04_samples
  );
}


void announceHazardCleared()
{
  Serial.println("A05: Hazard cleared");

  playPCM(
    A05_audio,
    A05_samples
  );
}


void announceSMS()
{
  Serial.println("A06: Emergency alert sent");

  playPCM(
    A06_audio,
    A06_samples
  );
}


// ============================================================
// SIM800 COMMAND
// ============================================================

String sendAT(
  String command,
  unsigned long waitTime = 1500
)
{
  while (sim800.available())
    sim800.read();

  sim800.println(command);

  unsigned long start = millis();

  String response = "";

  while (
    millis() - start < waitTime
  )
  {
    while (sim800.available())
    {
      char c = sim800.read();

      response += c;

      Serial.write(c);
    }
  }

  return response;
}


// ============================================================
// GSM REGISTRATION
// ============================================================

bool checkGSMRegistration()
{
  String response =
    sendAT(
      "AT+CREG?",
      1200
    );

  if (
    response.indexOf("+CREG: 0,1") >= 0 ||
    response.indexOf("+CREG: 0,5") >= 0 ||
    response.indexOf("+CREG: 1,1") >= 0 ||
    response.indexOf("+CREG: 1,5") >= 0
  )
  {
    gsmRegistered = true;

    return true;
  }

  gsmRegistered = false;

  return false;
}


// ============================================================
// GSM SIGNAL
// ============================================================

void updateGSMInfo()
{
  String response =
    sendAT(
      "AT+CSQ",
      1000
    );

  int pos =
    response.indexOf("+CSQ:");

  if (pos >= 0)
  {
    int comma =
      response.indexOf(
        ',',
        pos
      );

    if (comma > pos)
    {
      String value =
        response.substring(
          pos + 5,
          comma
        );

      value.trim();

      gsmSignal =
        value.toInt();
    }
  }
}


// ============================================================
// LBS LOCATION
// ============================================================

bool getLBSLocation()
{
  Serial.println();
  Serial.println("Requesting SIM800L LBS location...");

  String response =
    sendAT(
      "AT+CIPGSMLOC=1,1",
      10000
    );

  int pos =
    response.indexOf(
      "+CIPGSMLOC:"
    );

  if (pos < 0)
  {
    lbsAvailable = false;

    lastLocation = "Unavailable";

    Serial.println(
      "LBS location unavailable"
    );

    return false;
  }

  int lineEnd =
    response.indexOf(
      '\n',
      pos
    );

  if (lineEnd < 0)
    lineEnd = response.length();

  String line =
    response.substring(
      pos,
      lineEnd
    );

  line.trim();

  Serial.println(
    "LBS RESPONSE:"
  );

  Serial.println(line);

  // Expected format:
  //
  // +CIPGSMLOC: 0,LONGITUDE,LATITUDE,DATE,TIME

  int firstComma =
    line.indexOf(',');

  if (firstComma < 0)
  {
    lbsAvailable = false;
    return false;
  }

  int secondComma =
    line.indexOf(
      ',',
      firstComma + 1
    );

  if (secondComma < 0)
  {
    lbsAvailable = false;
    return false;
  }

  int thirdComma =
    line.indexOf(
      ',',
      secondComma + 1
    );

  if (thirdComma < 0)
  {
    lbsAvailable = false;
    return false;
  }

  String longitudeString =
    line.substring(
      firstComma + 1,
      secondComma
    );

  String latitudeString =
    line.substring(
      secondComma + 1,
      thirdComma
    );

  longitude =
    longitudeString.toDouble();

  latitude =
    latitudeString.toDouble();

  if (
    latitude == 0.0 &&
    longitude == 0.0
  )
  {
    lbsAvailable = false;

    lastLocation =
      "Unavailable";

    return false;
  }

  lbsAvailable = true;

  lastLocation =
    "https://maps.google.com/?q=" +
    String(latitude, 6) +
    "," +
    String(longitude, 6);

  Serial.println(
    "Latitude: " +
    String(latitude, 6)
  );

  Serial.println(
    "Longitude: " +
    String(longitude, 6)
  );

  return true;
}


// ============================================================
// SEND SMS
// ============================================================

bool sendSMS()
{
  if (!gsmRegistered)
  {
    Serial.println(
      "GSM not registered. SMS skipped."
    );

    return false;
  }

  Serial.println(
    "Sending emergency SMS..."
  );

  sendAT(
    "AT+CMGF=1",
    1000
  );

  while (sim800.available())
    sim800.read();

  sim800.print(
    "AT+CMGS=\""
  );

  sim800.print(
    EMERGENCY_NUMBER
  );

  sim800.println(
    "\""
  );

  unsigned long start =
    millis();

  bool promptFound = false;

  while (
    millis() - start < 5000
  )
  {
    if (sim800.available())
    {
      String r =
        sim800.readString();

      Serial.println(r);

      if (
        r.indexOf(">") >= 0
      )
      {
        promptFound = true;
        break;
      }
    }
  }

  if (!promptFound)
  {
    Serial.println(
      "SMS prompt not received"
    );

    return false;
  }


  String message =
    "SMART HELMET ALERT\n"
    "ELECTRICAL HAZARD DETECTED\n"
    "Helmet: WORN\n"
    "AC Field: DETECTED\n";


  if (lbsAvailable)
  {
    message +=
      "Location: https://maps.google.com/?q=";

    message +=
      String(latitude, 6);

    message += ",";

    message +=
      String(longitude, 6);

    message += "\n";

    message +=
      "Source: SIM800L LBS";
  }
  else
  {
    message +=
      "Location: UNAVAILABLE\n";

    message +=
      "Source: SIM800L LBS";
  }


  sim800.print(message);

  sim800.write(26);

  start = millis();

  String response = "";

  while (
    millis() - start < 10000
  )
  {
    while (sim800.available())
    {
      char c =
        sim800.read();

      response += c;

      Serial.write(c);
    }

    if (
      response.indexOf("OK") >= 0 ||
      response.indexOf("+CMGS:") >= 0
    )
    {
      Serial.println();
      Serial.println(
        "SMS SENT SUCCESSFULLY"
      );

      return true;
    }

    if (
      response.indexOf("ERROR") >= 0
    )
    {
      Serial.println();
      Serial.println(
        "SMS ERROR"
      );

      return false;
    }
  }

  return false;
}


// ============================================================
// HELMET SENSOR
// ============================================================

void updateHelmetDetection()
{
  proximityValue =
    vcnl4040.getProximity();

  bool detected =
    proximityValue >
    HELMET_PROX_THRESHOLD;


  // ----------------------------------------------------------
  // HEAD / HELMET PRESENT
  // ----------------------------------------------------------

  if (detected)
  {
    helmetRemovedStart = 0;

    if (!helmetWorn)
    {
      if (helmetCandidateStart == 0)
      {
        helmetCandidateStart =
          millis();
      }

      if (
        millis() -
        helmetCandidateStart >=
        HELMET_WORN_CONFIRM_MS
      )
      {
        helmetWorn = true;

        helmetCandidateStart = 0;

        systemState =
          MONITORING;

        addEvent(
          "Helmet worn - monitoring enabled"
        );

        currentAlert =
          "Safety monitoring active";

        // Only announce once
        announceStartup();
      }
    }
  }


  // ----------------------------------------------------------
  // HEAD / HELMET NOT PRESENT
  // ----------------------------------------------------------

  else
  {
    helmetCandidateStart = 0;

    if (helmetWorn)
    {
      if (helmetRemovedStart == 0)
      {
        helmetRemovedStart =
          millis();
      }

      if (
        millis() -
        helmetRemovedStart >=
        HELMET_REMOVED_CONFIRM_MS
      )
      {
        helmetWorn = false;

        helmetRemovedStart = 0;

        systemState =
          WAITING_FOR_HELMET;

        addEvent(
          "Helmet removed"
        );

        currentAlert =
          "Please wear helmet";

        announceHelmetRemoved();
      }
    }
  }
}


// ============================================================
// AC HAZARD DETECTION
// ============================================================

void updateElectricalHazard()
{
  bool acDetected =
    digitalRead(
      AC_DETECT_PIN
    ) == HIGH;


  // ----------------------------------------------------------
  // AC FIELD PRESENT
  // ----------------------------------------------------------

  if (acDetected)
  {
    if (hazardCandidateStart == 0)
    {
      hazardCandidateStart =
        millis();
    }

    if (
      !electricalHazard &&
      millis() -
      hazardCandidateStart >=
      HAZARD_CONFIRM_MS
    )
    {
      electricalHazard = true;

      detectionCount++;

      systemState =
        ELECTRICAL_HAZARD;

      currentAlert =
        "Electrical hazard detected";

      addEvent(
        "ELECTRICAL HAZARD CONFIRMED"
      );

      // ------------------------------------------------------
      // ONLY ONE ALERT PER HAZARD EVENT
      // ------------------------------------------------------

      if (!alertSent)
      {
        alertSent = true;

        // Highest-priority voice alert
        announceElectricalDanger();

        // Update GSM state
        checkGSMRegistration();

        updateGSMInfo();

        // Obtain LBS
        getLBSLocation();

        // Send SMS
        bool sent =
          sendSMS();

        if (sent)
        {
          smsSent = true;

          currentAlert =
            "Emergency alert sent";

          addEvent(
            "Emergency SMS sent"
          );

          announceSMS();
        }
        else
        {
          smsSent = false;

          currentAlert =
            "Hazard detected - SMS failed";

          addEvent(
            "Emergency SMS failed"
          );
        }
      }
    }
  }


  // ----------------------------------------------------------
  // AC FIELD CLEARED
  // ----------------------------------------------------------

  else
  {
    hazardCandidateStart = 0;

    if (electricalHazard)
    {
      electricalHazard = false;

      systemState =
        HAZARD_CLEARED;

      addEvent(
        "Electrical hazard cleared"
      );

      currentAlert =
        "Electrical hazard cleared";

      announceHazardCleared();

      // ------------------------------------------------------
      // Allow next hazard to create a new alert
      // ------------------------------------------------------

      alertSent = false;

      smsSent = false;

      delay(300);

      if (helmetWorn)
        systemState = MONITORING;
      else
        systemState = WAITING_FOR_HELMET;
    }
  }
}


// ============================================================
// HTML DASHBOARD
// ============================================================

String htmlHeader(
  String title
)
{
  String page = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name="viewport"
      content="width=device-width, initial-scale=1">

<meta http-equiv="refresh"
      content="3">

<title>Smart Helmet</title>

<style>

* {
  box-sizing:border-box;
}

body {
  margin:0;
  font-family:Arial,Helvetica,sans-serif;
  background:#0b1118;
  color:#eaf4ff;
}

.header {
  padding:20px;
  background:#111c27;
  border-bottom:1px solid #263747;
}

.title {
  font-size:25px;
  font-weight:bold;
}

.subtitle {
  color:#7f96aa;
  margin-top:5px;
}

.container {
  max-width:1100px;
  margin:auto;
  padding:18px;
}

.grid {
  display:grid;
  grid-template-columns:
  repeat(auto-fit,minmax(230px,1fr));
  gap:15px;
}

.card {
  background:#121d28;
  border:1px solid #263746;
  border-radius:14px;
  padding:20px;
}

.card h3 {
  margin-top:0;
  color:#8fa9bd;
}

.value {
  font-size:26px;
  font-weight:bold;
  margin-top:10px;
}

.safe {
  color:#35e58b;
}

.danger {
  color:#ff4f5e;
}

.warning {
  color:#ffc857;
}

.info {
  color:#42b7ff;
}

.section {
  margin-top:20px;
}

.log {
  background:#080d12;
  border-radius:10px;
  padding:12px;
  font-family:monospace;
  font-size:13px;
  line-height:1.7;
  overflow:auto;
}

.footer {
  margin-top:25px;
  color:#65798b;
  font-size:13px;
}

</style>
</head>

<body>

<div class="header">

<div class="title">
SMART ELECTRIC HELMET
</div>

<div class="subtitle">
ESP32-C6 Safety Monitoring System
</div>

</div>

<div class="container">

)rawliteral";

  page +=
    "<h2>" +
    title +
    "</h2>";

  return page;
}


// ============================================================
// DASHBOARD PAGE
// ============================================================

void handleDashboard()
{
  String page =
    htmlHeader(
      "Safety Dashboard"
    );


  page += R"rawliteral(

<div class="grid">

<div class="card">
<h3>Helmet Status</h3>
<div class="value">
)rawliteral";


  if (helmetWorn)
    page +=
      "<span class='safe'>WORN</span>";
  else
    page +=
      "<span class='warning'>NOT WORN</span>";


  page += R"rawliteral(
</div>
</div>


<div class="card">
<h3>Electrical Safety</h3>
<div class="value">
)rawliteral";


  if (electricalHazard)
    page +=
      "<span class='danger'>DANGER</span>";
  else
    page +=
      "<span class='safe'>SAFE</span>";


  page += R"rawliteral(
</div>
</div>


<div class="card">
<h3>GSM</h3>
<div class="value">
)rawliteral";


  if (gsmRegistered)
    page +=
      "<span class='safe'>REGISTERED</span>";
  else
    page +=
      "<span class='danger'>OFFLINE</span>";


  page += R"rawliteral(
</div>
</div>


<div class="card">
<h3>LBS Location</h3>
<div class="value">
)rawliteral";


  if (lbsAvailable)
    page +=
      "<span class='safe'>READY</span>";
  else
    page +=
      "<span class='warning'>UNAVAILABLE</span>";


  page += R"rawliteral(
</div>
</div>


<div class="card">
<h3>Audio</h3>
<div class="value">
)rawliteral";


  if (audioActive)
    page +=
      "<span class='info'>ACTIVE</span>";
  else
    page +=
      "<span class='safe'>READY</span>";


  page += R"rawliteral(
</div>
</div>


<div class="card">
<h3>System State</h3>
<div class="value">
)rawliteral";


  page +=
    getStateName();


  page += R"rawliteral(
</div>
</div>

</div>

<div class="section">

<div class="card">

<h3>Current Alert</h3>

<div class="value">
)rawliteral";


  page +=
    currentAlert;


  page += R"rawliteral(
</div>

</div>

</div>


<div class="section">

<div class="card">

<h3>Electrical Hazard</h3>

<p>
AC Field:
)rawliteral";


  if (electricalHazard)
    page +=
      "<b class='danger'>DETECTED</b>";
  else
    page +=
      "<b class='safe'>CLEAR</b>";


  page += R"rawliteral(
</p>

<p>
Hazard Events:
)rawliteral";

  page +=
    String(detectionCount);


  page += R"rawliteral(
</p>

<p>
SMS:
)rawliteral";


  if (smsSent)
    page +=
      "<b class='safe'>SENT</b>";
  else
    page +=
      "Not sent";


  page += R"rawliteral(
</p>

</div>

</div>


<div class="section">

<div class="card">

<h3>Helmet Sensor</h3>

<p>
Proximity:
)rawliteral";

  page +=
    String(proximityValue);

  page += R"rawliteral(
</p>

<p>
Threshold:
)rawliteral";

  page +=
    String(HELMET_PROX_THRESHOLD);

  page += R"rawliteral(
</p>

</div>

</div>


<div class="section">

<div class="card">

<h3>GSM / LBS</h3>

<p>
Signal:
)rawliteral";

  page +=
    String(gsmSignal);

  page +=
    " / 31";

  page += R"rawliteral(
</p>

<p>
Location:
)rawliteral";


  if (lbsAvailable)
  {
    page +=
      "<a href='" +
      lastLocation +
      "' target='_blank'>View Location</a>";
  }
  else
  {
    page +=
      "Unavailable";
  }


  page += R"rawliteral(
</p>

</div>

</div>


<div class="section">

<div class="card">

<h3>Event Log</h3>

<div class="log">
)rawliteral";


  for (int i = 0; i < MAX_EVENTS; i++)
  {
    int index =
      (eventIndex + i) %
      MAX_EVENTS;

    if (eventLog[index].length() > 0)
    {
      page +=
        eventLog[index];

      page +=
        "<br>";
    }
  }


  page += R"rawliteral(
</div>

</div>

</div>


<div class="footer">

SMART ELECTRIC HELMET |
ESP32-C6 LOCAL DASHBOARD |
No internet required

</div>

</div>

</body>
</html>

)rawliteral";


  server.send(
    200,
    "text/html",
    page
  );
}


// ============================================================
// HELMET PAGE
// ============================================================

void handleHelmet()
{
  String page =
    htmlHeader(
      "Helmet Monitoring"
    );

  page +=
    "<div class='card'>";

  page +=
    "<h3>Helmet Status</h3>";

  if (helmetWorn)
    page +=
      "<div class='value safe'>WORN</div>";
  else
    page +=
      "<div class='value warning'>NOT WORN</div>";

  page +=
    "<p>Proximity value: " +
    String(proximityValue) +
    "</p>";

  page +=
    "<p>Detection threshold: " +
    String(HELMET_PROX_THRESHOLD) +
    "</p>";

  page +=
    "</div></div></body></html>";

  server.send(
    200,
    "text/html",
    page
  );
}


// ============================================================
// ELECTRICAL PAGE
// ============================================================

void handleElectrical()
{
  String page =
    htmlHeader(
      "Electrical Safety"
    );

  page +=
    "<div class='card'>";

  page +=
    "<h3>Non-Contact AC Field Detection</h3>";

  if (electricalHazard)
  {
    page +=
      "<div class='value danger'>"
      "⚠ ELECTRICAL HAZARD"
      "</div>";

    page +=
      "<p>AC field detected nearby.</p>";
  }
  else
  {
    page +=
      "<div class='value safe'>SAFE</div>";

    page +=
      "<p>No confirmed AC electric field.</p>";
  }

  page +=
    "<p>Helmet: ";

  page +=
    helmetWorn ? "WORN" : "NOT WORN";

  page +=
    "</p>";

  page +=
    "</div></div></body></html>";

  server.send(
    200,
    "text/html",
    page
  );
}


// ============================================================
// GSM PAGE
// ============================================================

void handleGSM()
{
  String page =
    htmlHeader(
      "GSM / LBS"
    );

  page +=
    "<div class='card'>";

  page +=
    "<h3>GSM Registration</h3>";

  page +=
    gsmRegistered
    ? "<div class='value safe'>REGISTERED</div>"
    : "<div class='value danger'>OFFLINE</div>";

  page +=
    "<p>Signal: " +
    String(gsmSignal) +
    " / 31</p>";

  page +=
    "<h3>LBS</h3>";

  page +=
    lbsAvailable
    ? "<div class='value safe'>READY</div>"
    : "<div class='value warning'>UNAVAILABLE</div>";

  if (lbsAvailable)
  {
    page +=
      "<p>Latitude: " +
      String(latitude, 6) +
      "</p>";

    page +=
      "<p>Longitude: " +
      String(longitude, 6) +
      "</p>";

    page +=
      "<p><a href='" +
      lastLocation +
      "' target='_blank'>"
      "View Location"
      "</a></p>";
  }

  page +=
    "</div></div></body></html>";

  server.send(
    200,
    "text/html",
    page
  );
}


// ============================================================
// AUDIO PAGE
// ============================================================

void handleAudio()
{
  String page =
    htmlHeader(
      "Audio System"
    );

  page +=
    "<div class='card'>";

  page +=
    "<h3>MAX98357A Audio</h3>";

  page +=
    audioActive
    ? "<div class='value info'>ACTIVE</div>"
    : "<div class='value safe'>READY</div>";

  page +=
    "<p>Sample rate: 48000 Hz</p>";

  page +=
    "<p>Format: 16-bit PCM Mono</p>";

  page +=
    "<p>WS/LRC: GPIO19</p>";

  page +=
    "<p>BCLK: GPIO20</p>";

  page +=
    "<p>DIN: GPIO18</p>";

  page +=
    "</div></div></body></html>";

  server.send(
    200,
    "text/html",
    page
  );
}


// ============================================================
// EVENT PAGE
// ============================================================

void handleEvents()
{
  String page =
    htmlHeader(
      "Event Log"
    );

  page +=
    "<div class='card'>";

  page +=
    "<div class='log'>";

  for (int i = 0; i < MAX_EVENTS; i++)
  {
    int index =
      (eventIndex + i) %
      MAX_EVENTS;

    if (eventLog[index].length() > 0)
    {
      page +=
        eventLog[index];

      page +=
        "<br>";
    }
  }

  page +=
    "</div>";

  page +=
    "</div></div></body></html>";

  server.send(
    200,
    "text/html",
    page
  );
}


// ============================================================
// SYSTEM HEALTH PAGE
// ============================================================

void handleHealth()
{
  String page =
    htmlHeader(
      "System Health"
    );

  page +=
    "<div class='card'>";

  page +=
    "<h3>Controller</h3>";

  page +=
    "<p>ESP32-C6: ONLINE</p>";

  page +=
    "<p>Wi-Fi AP: ONLINE</p>";

  page +=
    "<p>VCNL4040: ";

  page +=
    (proximityValue > 0)
    ? "ACTIVE"
    : "CHECK";

  page +=
    "</p>";

  page +=
    "<p>AC Detector: ACTIVE</p>";

  page +=
    "<p>Audio: ACTIVE</p>";

  page +=
    "<p>GSM: ";

  page +=
    gsmRegistered
    ? "REGISTERED"
    : "OFFLINE";

  page +=
    "</p>";

  page +=
    "</div></div></body></html>";

  server.send(
    200,
    "text/html",
    page
  );
}


// ============================================================
// WEB SERVER
// ============================================================

void setupWebServer()
{
  server.on(
    "/",
    handleDashboard
  );

  server.on(
    "/helmet",
    handleHelmet
  );

  server.on(
    "/electrical",
    handleElectrical
  );

  server.on(
    "/gsm",
    handleGSM
  );

  server.on(
    "/audio",
    handleAudio
  );

  server.on(
    "/events",
    handleEvents
  );

  server.on(
    "/health",
    handleHealth
  );

  server.begin();

  Serial.println(
    "Web server started"
  );
}


// ============================================================
// SETUP
// ============================================================

void setup()
{
  Serial.begin(115200);

  delay(1500);

  Serial.println();
  Serial.println(
    "========================================"
  );

  Serial.println(
    " SMART ELECTRIC HELMET"
  );

  Serial.println(
    " XIAO ESP32-C6"
  );

  Serial.println(
    "========================================"
  );


  // ----------------------------------------------------------
  // AC DETECTOR
  // ----------------------------------------------------------

  pinMode(
    AC_DETECT_PIN,
    INPUT
  );

  Serial.println(
    "AC detector GPIO0 ready"
  );


  // ----------------------------------------------------------
  // I2C
  // ----------------------------------------------------------

  Wire.begin(
    I2C_SDA,
    I2C_SCL
  );

  Serial.println(
    "I2C initialized"
  );


  // ----------------------------------------------------------
  // VCNL4040
  // ----------------------------------------------------------

  if (!vcnl4040.begin())
  {
    Serial.println(
      "ERROR: VCNL4040 not found"
    );
  }
  else
  {
    Serial.println(
      "VCNL4040 found"
    );

    vcnl4040.enableProximity(
      true
    );

    vcnl4040.setProximityHighResolution(
      true
    );
  }


  // ----------------------------------------------------------
  // SIM800L
  // ----------------------------------------------------------

  sim800.begin(
    SIM800_BAUD,
    SERIAL_8N1,
    SIM800_RX_PIN,
    SIM800_TX_PIN
  );

  delay(1000);

  Serial.println(
    "SIM800L UART initialized"
  );

  sendAT(
    "AT",
    1000
  );

  sendAT(
    "ATE0",
    1000
  );


  // ----------------------------------------------------------
  // AUDIO
  // ----------------------------------------------------------

  setupI2S();


  // ----------------------------------------------------------
  // WIFI AP
  // ----------------------------------------------------------

  WiFi.mode(
    WIFI_AP
  );

  WiFi.softAP(
    AP_SSID,
    AP_PASSWORD
  );

  Serial.println();
  Serial.println(
    "Wi-Fi Access Point:"
  );

  Serial.println(
    AP_SSID
  );

  Serial.println(
    "Password:"
  );

  Serial.println(
    AP_PASSWORD
  );

  Serial.println(
    "Dashboard:"
  );

  Serial.println(
    WiFi.softAPIP()
  );


  // ----------------------------------------------------------
  // WEB SERVER
  // ----------------------------------------------------------

  setupWebServer();


  // ----------------------------------------------------------
  // INITIAL STATE
  // ----------------------------------------------------------

  systemState =
    WAITING_FOR_HELMET;

  addEvent(
    "System initialized"
  );

  currentAlert =
    "Waiting for helmet";

  Serial.println();
  Serial.println(
    "SYSTEM READY"
  );

  Serial.println(
    "========================================"
  );
}


// ============================================================
// LOOP
// ============================================================

void loop()
{
  server.handleClient();


  // ----------------------------------------------------------
  // HELMET SENSOR
  // ----------------------------------------------------------

  static unsigned long lastHelmetRead = 0;

  if (
    millis() -
    lastHelmetRead >= 100
  )
  {
    lastHelmetRead =
      millis();

    updateHelmetDetection();
  }


  // ----------------------------------------------------------
  // ELECTRICAL HAZARD
  // ----------------------------------------------------------

  static unsigned long lastHazardRead = 0;

  if (
    millis() -
    lastHazardRead >= 50
  )
  {
    lastHazardRead =
      millis();

    updateElectricalHazard();
  }


  // ----------------------------------------------------------
  // GSM STATUS
  // ----------------------------------------------------------

  static unsigned long lastGSMCheck = 0;

  if (
    millis() -
    lastGSMCheck >= 15000
  )
  {
    lastGSMCheck =
      millis();

    checkGSMRegistration();

    if (gsmRegistered)
      updateGSMInfo();
  }


  // ----------------------------------------------------------
  // SERIAL DEBUG
  // ----------------------------------------------------------

  static unsigned long lastDebug = 0;

  if (
    millis() -
    lastDebug >= 2000
  )
  {
    lastDebug =
      millis();

    Serial.println();
    Serial.println(
      "----------- STATUS -----------"
    );

    Serial.print(
      "Helmet: "
    );

    Serial.println(
      helmetWorn
      ? "WORN"
      : "NOT WORN"
    );

    Serial.print(
      "Proximity: "
    );

    Serial.println(
      proximityValue
    );

    Serial.print(
      "AC Field: "
    );

    Serial.println(
      electricalHazard
      ? "DETECTED"
      : "CLEAR"
    );

    Serial.print(
      "GSM: "
    );

    Serial.println(
      gsmRegistered
      ? "REGISTERED"
      : "OFFLINE"
    );

    Serial.print(
      "LBS: "
    );

    Serial.println(
      lbsAvailable
      ? "READY"
      : "UNAVAILABLE"
    );

    Serial.print(
      "State: "
    );

    Serial.println(
      getStateName()
    );

    Serial.println(
      "------------------------------"
    );
  }
}