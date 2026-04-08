#include "file_api.h"
#include <LittleFS.h>
#include "debug.h"

static File uploadFile;

static void listRecursive(File dir, const String &base, AsyncResponseStream *res, bool &first)
{
    File file = dir.openNextFile();

    while (file)
    {
        String name = file.name();
        String path = base + "/" + name;

        if (file.isDirectory())
        {
            listRecursive(file, path, res, first);
        }
        else
        {
            if (!first)
                res->print(",");
            first = false;

            res->print("{\"type\":\"file\",\"name\":\"");
            res->print(name);
            res->print("\",\"path\":\"");
            res->print(path);
            res->print("\",\"size\":");
            res->print(file.size());
            res->print("}");
        }

        file = dir.openNextFile();
    }
}

// ===== LIST =====
static void handleList(AsyncWebServerRequest *req)
{
    AsyncResponseStream *res = req->beginResponseStream("application/json");
    res->print("[");

    bool first = true;

    File root = LittleFS.open("/");
    listRecursive(root, "", res, first);

    res->print("]");
    req->send(res);
}

// ===== LOAD =====
static void handleLoad(AsyncWebServerRequest *req)
{
    if (!req->hasParam("path"))
        return req->send(400, "text/plain", "Missing path");

    String path = req->getParam("path")->value();

    // reject directories explicitly
    File f = LittleFS.open(path, "r");
    if (f && f.isDirectory())
    {
        f.close();
        return req->send(400, "text/plain", "Is directory");
    }
    if (f)
        f.close();

    bool isGzip = false;

    if (!LittleFS.exists(path))
    {
        if (LittleFS.exists(path + ".gz"))
        {
            path += ".gz";
            isGzip = true;
        }
        else
        {
            return req->send(404, "text/plain", "Not found");
        }
    }

    String contentType = "text/plain";
    if (path.endsWith(".html") || path.endsWith(".html.gz"))
        contentType = "text/html";
    else if (path.endsWith(".js") || path.endsWith(".js.gz"))
        contentType = "application/javascript";
    else if (path.endsWith(".css") || path.endsWith(".css.gz"))
        contentType = "text/css";
    else if (path.endsWith(".json") || path.endsWith(".json.gz"))
        contentType = "application/json";
    else if (path.endsWith(".ini") || path.endsWith(".ini.gz"))
        contentType = "text/plain";
    else if (path.endsWith(".py") || path.endsWith(".py.gz"))
        contentType = "text/x-python";
    else if (path.endsWith(".csv") || path.endsWith(".csv.gz"))
        contentType = "text/csv";
    else if (path.endsWith(".cpp") || path.endsWith(".cpp.gz"))
        contentType = "text/x-c++src";
    else if (path.endsWith(".h") || path.endsWith(".h.gz"))
        contentType = "text/x-c++hdr";

    AsyncWebServerResponse *response = req->beginResponse(LittleFS, path, contentType);

    if (path.endsWith(".gz"))
        response->addHeader("Content-Encoding", "gzip");

    req->send(response);
}

// ===== SAVE (stream) =====
static void handleSave(AsyncWebServerRequest *req,
                       uint8_t *data,
                       size_t len,
                       size_t index,
                       size_t total)
{
    if (index == 0)
    {
        if (total > 100 * 1024)
        {
            req->send(413, "text/plain", "Too large");
            return;
        }

        if (!req->hasHeader("X-Path"))
        {
            req->send(400, "text/plain", "Missing path");
            return;
        }

        String path = req->getHeader("X-Path")->value();
        DEBUG("SAVE: %s (%u bytes)\n", path.c_str(), (unsigned int)total);

        uploadFile = LittleFS.open(path, "w");
        if (!uploadFile)
        {
            req->send(500, "text/plain", "Open failed");
            return;
        }
    }

    if (uploadFile)
        uploadFile.write(data, len);

    if (index + len == total)
    {
        if (uploadFile)
            uploadFile.close();

        req->send(200, "text/plain", "OK");
        DEBUG("SAVE DONE\n");
    }
}

void handleSaveBody(AsyncWebServerRequest *req,
                    uint8_t *data,
                    size_t len,
                    size_t index,
                    size_t total)
{
    if (index == 0)
    {
        if (!req->hasHeader("X-Path"))
        {
            req->send(400, "text/plain", "Missing path");
            return;
        }

        String path = req->getHeader("X-Path")->value();

        uploadFile = LittleFS.open(path, "w");
        if (!uploadFile)
        {
            req->send(500, "text/plain", "Open failed");
            return;
        }
    }

    if (uploadFile)
        uploadFile.write(data, len);

    if (index + len == total)
    {
        if (uploadFile)
            uploadFile.close();
        req->send(200, "text/plain", "OK");
    }
}

// upload-style (chunked / unknown length)
void handleSaveUpload(AsyncWebServerRequest *req,
                      String filename,
                      size_t index,
                      uint8_t *data,
                      size_t len,
                      bool final)
{
    if (index == 0)
    {
        if (!req->hasHeader("X-Path"))
            return;

        String path = req->getHeader("X-Path")->value();

        uploadFile = LittleFS.open(path, "w");
    }

    if (uploadFile)
        uploadFile.write(data, len);

    if (final)
    {
        if (uploadFile)
            uploadFile.close();
        req->send(200, "text/plain", "OK");
    }
}

// ===== DELETE =====
static void handleDelete(AsyncWebServerRequest *req)
{
    if (!req->hasParam("path", true))
        return req->send(400, "text/plain", "Missing path");

    String path = req->getParam("path", true)->value();

    if (!LittleFS.exists(path))
        return req->send(404, "text/plain", "Not found");

    LittleFS.remove(path);
    req->send(200, "text/plain", "OK");
}

static void handleRename(AsyncWebServerRequest *req)
{
    if (!req->hasParam("from", true) || !req->hasParam("to", true))
        return req->send(400, "text/plain", "Missing params");

    String from = req->getParam("from", true)->value();
    String to = req->getParam("to", true)->value();

    if (!LittleFS.exists(from))
        return req->send(404, "text/plain", "Not found");

    File src = LittleFS.open(from, "r");
    File dst = LittleFS.open(to, "w");

    uint8_t buf[512];
    while (src.available())
    {
        size_t n = src.read(buf, sizeof(buf));
        dst.write(buf, n);
    }

    src.close();
    dst.close();

    LittleFS.remove(from);

    req->send(200, "text/plain", "OK");
}

// ===== INIT =====
void fileApiInit(AsyncWebServer &server)
{
    server.on("/list", HTTP_GET, handleList);
    server.on("/file", HTTP_GET, handleLoad);

    server.on("/save", HTTP_POST, [](AsyncWebServerRequest *req) {}, // no-op
              NULL, handleSave);

    // server.on("/save", HTTP_POST, [](AsyncWebServerRequest *req) {
    //     DEBUG_PRINTLN(">>> SAVE ROUTE HIT");
    // }, // required
    //           handleSaveUpload,                                      // 👈 for chunked
    //           handleSaveBody                                         // 👈 for raw
    // );

    // server.on("/save", HTTP_POST, [](AsyncWebServerRequest *req)
    //           {
    //               // this MUST exist or ESP returns 501
    //           },
    //           [](AsyncWebServerRequest *req, String filename, size_t index, uint8_t *data, size_t len, bool final)
    //           {

    //         static File f;

    //         if (index == 0) {
    //             if (!req->hasHeader("X-Path")) {
    //                 req->send(400, "text/plain", "Missing path");
    //                 return;
    //             }

    //             String path = req->getHeader("X-Path")->value();

    //             f = LittleFS.open(path, "w");
    //             if (!f) {
    //                 req->send(500, "text/plain", "Open failed");
    //                 return;
    //             }
    //         }

    //         if (f) f.write(data, len);

    //         if (final) {
    //             if (f) f.close();
    //             req->send(200, "text/plain", "OK");
    //         } });

    server.on("/delete", HTTP_POST, handleDelete);

    server.onNotFound([](AsyncWebServerRequest *req)
                      {
                        DEBUG_PRINTLN(">>> NOT FOUND ROUTE HIT");
        String path = req->url();
        String type = "text/plain";
        if (path.endsWith(".html")) type = "text/html";
        else if (path.endsWith(".js")) type = "application/javascript";
        else if (path.endsWith(".css")) type = "text/css";

        if (LittleFS.exists(path + ".gz")) {
            auto res = req->beginResponse(LittleFS, path + ".gz", type);
            res->addHeader("Content-Encoding", "gzip");
            req->send(res);
            return;
        }
        if (LittleFS.exists(path)) {
            req->send(LittleFS, path, type);
            return;
        }
    req->send(404); });

    server.on("/rename", HTTP_POST, handleRename);
}