#pragma once
#include <WebServer.h>

// WebServer normally replaces its single Digest challenge on every 401.
// Concurrent browsers then invalidate each other's credentials. Keep the
// dashboard challenge for this server instance; reboot generates new randomness.
// Password verification remains in WebServer::authenticate (no role/session cache).
class MonitorWebServer : public WebServer {
public:
  explicit MonitorWebServer(int port) : WebServer(port) {}

  void requestAuthentication(HTTPAuthMethod mode = BASIC_AUTH,
                             const char* realm = nullptr,
                             const String& message = String("")) {
    if (mode != DIGEST_AUTH) {
      WebServer::requestAuthentication(mode, realm, message);
      return;
    }
    const String wantedRealm = realm ? String(realm) : String("Login Required");
    if (!_snonce.length() || !_sopaque.length() || _srealm != wantedRealm) {
      _srealm = wantedRealm;
      _snonce = _getRandomHexString();
      _sopaque = _getRandomHexString();
    }
    sendHeader("Cache-Control", "no-store");
    sendHeader("WWW-Authenticate", String("Digest realm=\"") + _srealm +
        "\", qop=\"auth\", nonce=\"" + _snonce + "\", opaque=\"" + _sopaque + "\"");
    send(401, "text/html", message);
  }
};
