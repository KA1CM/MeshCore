#include <Arduino.h>   // needed for PlatformIO
#include <Mesh.h>
#include "MyMesh.h"

#if defined(ESP32) && defined(BOT_NTP_SYNC)
  #include <WiFi.h>
  #include <time.h>
  #include <esp_sntp.h>
#endif

// Believe it or not, this std C function is busted on some platforms!
static uint32_t _atoi(const char* sp) {
  uint32_t n = 0;
  while (*sp && *sp >= '0' && *sp <= '9') {
    n *= 10;
    n += (*sp++ - '0');
  }
  return n;
}

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  #include <InternalFileSystem.h>
  #if defined(QSPIFLASH)
    #include <CustomLFS_QSPIFlash.h>
    DataStore store(InternalFS, QSPIFlash, rtc_clock);
  #else
  #if defined(EXTRAFS)
    #include <CustomLFS.h>
    CustomLFS ExtraFS(0xD4000, 0x19000, 128);
    DataStore store(InternalFS, ExtraFS, rtc_clock);
  #else
    DataStore store(InternalFS, rtc_clock);
  #endif
  #endif
#elif defined(RP2040_PLATFORM)
  #include <LittleFS.h>
  DataStore store(LittleFS, rtc_clock);
#elif defined(ESP32)
  #include <SPIFFS.h>
  DataStore store(SPIFFS, rtc_clock);
#endif

#ifdef ESP32
  #ifdef WIFI_SSID
    #include <helpers/esp32/SerialWifiInterface.h>
    SerialWifiInterface serial_interface;
    #ifndef TCP_PORT
      #define TCP_PORT 5000
    #endif
  #elif defined(BLE_PIN_CODE)
    #include <helpers/esp32/SerialBLEInterface.h>
    SerialBLEInterface serial_interface;
  #elif defined(SERIAL_RX)
    #include <helpers/ArduinoSerialInterface.h>
    ArduinoSerialInterface serial_interface;
    HardwareSerial companion_serial(1);
  #else
    #include <helpers/ArduinoSerialInterface.h>
    ArduinoSerialInterface serial_interface;
  #endif
#elif defined(RP2040_PLATFORM)
  //#ifdef WIFI_SSID
  //  #include <helpers/rp2040/SerialWifiInterface.h>
  //  SerialWifiInterface serial_interface;
  //  #ifndef TCP_PORT
  //    #define TCP_PORT 5000
  //  #endif
  // #elif defined(BLE_PIN_CODE)
  //   #include <helpers/rp2040/SerialBLEInterface.h>
  //   SerialBLEInterface serial_interface;
  #if defined(SERIAL_RX)
    #include <helpers/ArduinoSerialInterface.h>
    ArduinoSerialInterface serial_interface;
    HardwareSerial companion_serial(1);
  #else
    #include <helpers/ArduinoSerialInterface.h>
    ArduinoSerialInterface serial_interface;
  #endif
#elif defined(NRF52_PLATFORM)
  #ifdef BLE_PIN_CODE
    #include <helpers/nrf52/SerialBLEInterface.h>
    SerialBLEInterface serial_interface;
  #else
    #include <helpers/ArduinoSerialInterface.h>
    ArduinoSerialInterface serial_interface;
  #endif
#elif defined(STM32_PLATFORM)
  #include <helpers/ArduinoSerialInterface.h>
  ArduinoSerialInterface serial_interface;
#else
  #error "need to define a serial interface"
#endif

/* GLOBAL OBJECTS */
#ifdef DISPLAY_CLASS
  #include "UITask.h"
  UITask ui_task(&board, &serial_interface);
#endif

StdRNG fast_rng;
SimpleMeshTables tables;
MyMesh the_mesh(radio_driver, fast_rng, rtc_clock, tables, store
   #ifdef DISPLAY_CLASS
      , &ui_task
   #endif
);

/* END GLOBAL OBJECTS */

void halt() {
  while (1) ;
}

/* BOT NTP TIME SYNC */
#if defined(ESP32) && defined(BOT_NTP_SYNC)

enum BotNtpState {
  BOT_NTP_IDLE,
  BOT_NTP_WIFI_CONNECTING,
  BOT_NTP_WAITING_FOR_TIME
};

static BotNtpState bot_ntp_state = BOT_NTP_IDLE;

static unsigned long bot_ntp_started_at = 0;
static unsigned long bot_ntp_last_sync = 0;
static unsigned long bot_ntp_last_attempt = 0;

static const unsigned long BOT_NTP_INTERVAL_MS = 24UL * 60UL * 60UL * 1000UL;
static const unsigned long BOT_NTP_WIFI_TIMEOUT_MS = 15000UL;
static const unsigned long BOT_NTP_TIME_TIMEOUT_MS = 15000UL;
static const unsigned long BOT_NTP_RETRY_MS = 5UL * 60UL * 1000UL;

// Anything after 2025-01-01 is clearly a valid NTP result for this firmware.
static const time_t BOT_NTP_VALID_TIME = 1735689600;

static void botNtpWifiOff() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

static void botNtpStart() {
  Serial.println("[NTP] Starting WiFi");

  WiFi.mode(WIFI_STA);
  WiFi.begin(BOT_NTP_SSID, BOT_NTP_PASSWORD);

  bot_ntp_started_at = millis();
  bot_ntp_last_attempt = bot_ntp_started_at;
  bot_ntp_state = BOT_NTP_WIFI_CONNECTING;
}

static void botNtpLoop() {
  unsigned long now_ms = millis();

  if (bot_ntp_state == BOT_NTP_IDLE) {
    bool never_synced = (bot_ntp_last_sync == 0);

    if ((never_synced && (bot_ntp_last_attempt == 0 ||
                          now_ms - bot_ntp_last_attempt >= BOT_NTP_RETRY_MS)) ||
        (!never_synced &&
         now_ms - bot_ntp_last_sync >= BOT_NTP_INTERVAL_MS)) {
      botNtpStart();
    }

    return;
  }

  if (bot_ntp_state == BOT_NTP_WIFI_CONNECTING) {
    if (WiFi.status() == WL_CONNECTED) {
      Serial.print("[NTP] WiFi connected, IP: ");
      Serial.println(WiFi.localIP());

      // UTC only. MeshCore stores Unix time, so timezone/DST is irrelevant here.
      configTime(0, 0, "pool.ntp.org", "time.nist.gov");

      bot_ntp_started_at = now_ms;
      bot_ntp_state = BOT_NTP_WAITING_FOR_TIME;
      return;
    }

    if (now_ms - bot_ntp_started_at >= BOT_NTP_WIFI_TIMEOUT_MS) {
      Serial.println("[NTP] WiFi connection timed out");
      botNtpWifiOff();
      bot_ntp_state = BOT_NTP_IDLE;
    }

    return;
  }

  if (bot_ntp_state == BOT_NTP_WAITING_FOR_TIME) {
    // Do not trust time(nullptr) merely because it contains a plausible
    // timestamp. The ESP32 clock may already contain MeshCore's previous
    // time. Wait until SNTP explicitly confirms a fresh synchronization.
    if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
      time_t ntp_now = time(nullptr);

      if (ntp_now >= BOT_NTP_VALID_TIME) {
        rtc_clock.setCurrentTime((uint32_t)ntp_now);
        bot_ntp_last_sync = now_ms;

        Serial.print("[NTP] RTC synchronized: ");
        Serial.println((unsigned long)ntp_now);

        botNtpWifiOff();
        bot_ntp_state = BOT_NTP_IDLE;
        return;
      }
    }

    if (now_ms - bot_ntp_started_at >= BOT_NTP_TIME_TIMEOUT_MS) {
      Serial.println("[NTP] Time synchronization timed out");
      botNtpWifiOff();
      bot_ntp_state = BOT_NTP_IDLE;
    }
  }
}
#endif

/* WIFI RECONNECT TRACKERS */
#if defined(ESP32) && defined(WIFI_SSID)
  bool wifi_needs_reconnect = false;
  unsigned long last_wifi_reconnect_attempt = 0;
#endif

void setup() {
  Serial.begin(115200);

  board.begin();

#ifdef DISPLAY_CLASS
  DisplayDriver* disp = NULL;
  if (display.begin()) {
    disp = &display;
    disp->startFrame();
  #ifdef ST7789
    disp->setTextSize(2);
  #endif
    disp->drawTextCentered(disp->width() / 2, 28, "Loading...");
    disp->endFrame();
  }
#endif

  if (!radio_init()) { halt(); }

  fast_rng.begin(radio_driver.getRngSeed());

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  InternalFS.begin();
  #if defined(QSPIFLASH)
    if (!QSPIFlash.begin()) {
      // debug output might not be available at this point, might be too early. maybe should fall back to InternalFS here?
      MESH_DEBUG_PRINTLN("CustomLFS_QSPIFlash: failed to initialize");
    } else {
      MESH_DEBUG_PRINTLN("CustomLFS_QSPIFlash: initialized successfully");
    }
  #else
  #if defined(EXTRAFS)
      ExtraFS.begin();
  #endif
  #endif
  store.begin();
  the_mesh.begin(
    #ifdef DISPLAY_CLASS
        disp != NULL
    #else
        false
    #endif
  );

#ifdef BLE_PIN_CODE
  serial_interface.begin(BLE_NAME_PREFIX, the_mesh.getNodePrefs()->node_name, the_mesh.getBLEPin());
#else
  serial_interface.begin(Serial);
#endif
  the_mesh.startInterface(serial_interface);
#elif defined(RP2040_PLATFORM)
  LittleFS.begin();
  store.begin();
  the_mesh.begin(
    #ifdef DISPLAY_CLASS
        disp != NULL
    #else
        false
    #endif
  );

  //#ifdef WIFI_SSID
  //  WiFi.begin(WIFI_SSID, WIFI_PWD);
  //  serial_interface.begin(TCP_PORT);
  // #elif defined(BLE_PIN_CODE)
  //   char dev_name[32+16];
  //   sprintf(dev_name, "%s%s", BLE_NAME_PREFIX, the_mesh.getNodeName());
  //   serial_interface.begin(dev_name, the_mesh.getBLEPin());
  #if defined(SERIAL_RX)
    companion_serial.setPins(SERIAL_RX, SERIAL_TX);
    companion_serial.begin(115200);
    serial_interface.begin(companion_serial);
  #else
    serial_interface.begin(Serial);
  #endif
    the_mesh.startInterface(serial_interface);
#elif defined(ESP32)
  SPIFFS.begin(true);
  store.begin();
  the_mesh.begin(
    #ifdef DISPLAY_CLASS
        disp != NULL
    #else
        false
    #endif
  );

#ifdef WIFI_SSID
  board.setInhibitSleep(true);   // prevent sleep when WiFi is active
  WiFi.setAutoReconnect(true);

  WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info){
      if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
          WIFI_DEBUG_PRINTLN("WiFi disconnected. Flagging for reconnect...");
          wifi_needs_reconnect = true;
      } else if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
          WIFI_DEBUG_PRINTLN("WiFi connected successfully!");
          wifi_needs_reconnect = false;
      }
  });

  WiFi.begin(WIFI_SSID, WIFI_PWD);
  serial_interface.begin(TCP_PORT);
#elif defined(BLE_PIN_CODE)
  serial_interface.begin(BLE_NAME_PREFIX, the_mesh.getNodePrefs()->node_name, the_mesh.getBLEPin());
#elif defined(SERIAL_RX)
  companion_serial.setPins(SERIAL_RX, SERIAL_TX);
  companion_serial.begin(115200);
  serial_interface.begin(companion_serial);
#else
  serial_interface.begin(Serial);
#endif
  the_mesh.startInterface(serial_interface);
#else
  #error "need to define filesystem"
#endif

  sensors.begin();

#if ENV_INCLUDE_GPS == 1
  the_mesh.applyGpsPrefs();
#endif

#ifdef DISPLAY_CLASS
  ui_task.begin(disp, &sensors, the_mesh.getNodePrefs());  // still want to pass this in as dependency, as prefs might be moved
#endif

  board.onBootComplete();
}

void loop() {
  the_mesh.loop();
  sensors.loop();
#ifdef DISPLAY_CLASS
  ui_task.loop();
#endif
  rtc_clock.tick();

#if defined(ESP32) && defined(BOT_NTP_SYNC)
  botNtpLoop();
#endif

  if (!the_mesh.hasPendingWork()) {
#if defined(NRF52_PLATFORM)
    board.sleep(0); // nrf ignores seconds param, sleeps whenever possible
#endif
  }

#if defined(ESP32) && defined(WIFI_SSID)
  // Safely attempt to reconnect every 10 seconds if flagged
  if (wifi_needs_reconnect && (millis() - last_wifi_reconnect_attempt > 10000)) {
    WIFI_DEBUG_PRINTLN("Attempting manual WiFi reconnect...");
    WiFi.disconnect();
    WiFi.reconnect();
    last_wifi_reconnect_attempt = millis();
  }
#endif
}
