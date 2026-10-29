/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "wit_token_setup.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "mbedtls/platform_util.h"

#include "config_store.h"
#include "muse_wifi.h"
#include "wifi_mgr.h"

#define WIT_TOKEN_KEY "wit_token"
#define SETUP_LIFETIME_US (5 * 60 * 1000000LL)
#define BODY_MAX (MUSE_WIT_TOKEN_MAX * 3 + 64)

static const char *TAG = "wit.setup";
static httpd_handle_t s_server;
static atomic_int s_state;
static atomic_bool s_stop_pending;
static int64_t s_deadline_us;
static char s_url[48];
static bool s_wifi_held;

static const char SETUP_PAGE[] =
    "<!doctype html><html><head><meta charset=utf-8>"
    "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
    "<meta http-equiv=\"Cache-Control\" content=\"no-store\">"
    "<title>Muse speech setup</title><style>body{font:18px system-ui;max-width:36rem;"
    "margin:3rem auto;padding:0 1rem}input,button{box-sizing:border-box;font:inherit;"
    "padding:.8rem;width:100%;margin:.5rem 0}small{color:#555}</style></head><body>"
    "<h1>Muse speech setup</h1><p>Paste the client access token from your Wit.ai app.</p>"
    "<form method=post action=/token autocomplete=off>"
    "<input type=password name=token required maxlength=384 autocapitalize=none "
    "spellcheck=false aria-label=\"Wit.ai client token\">"
    "<button type=submit>Save and test</button></form>"
    "<form method=post action=/cancel><button type=submit>Cancel</button></form>"
    "<small>The stored token is never shown by this page.</small></body></html>";

static esp_err_t send_html(httpd_req_t *req, const char *status, const char *body)
{
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    return httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t root_handler(httpd_req_t *req)
{
    return send_html(req, "200 OK", SETUP_PAGE);
}

static bool token_char_ok(unsigned char c)
{
    return c >= 0x21 && c <= 0x7e;
}

static bool token_ok(const char *token)
{
    size_t len = strlen(token);
    if (!len || len > MUSE_WIT_TOKEN_MAX) return false;
    for (size_t i = 0; i < len; i++) {
        if (!token_char_ok((unsigned char)token[i])) return false;
    }
    return true;
}

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool decode_form_token(const char *body, char *out, size_t cap)
{
    static const char prefix[] = "token=";
    if (strncmp(body, prefix, sizeof(prefix) - 1) != 0 || strchr(body, '&')) return false;
    const char *p = body + sizeof(prefix) - 1;
    size_t n = 0;
    while (*p) {
        unsigned char c;
        if (*p == '+') {
            c = ' ';
            p++;
        } else if (*p == '%') {
            int hi = hex_value(p[1]), lo = p[1] ? hex_value(p[2]) : -1;
            if (hi < 0 || lo < 0) return false;
            c = (unsigned char)((hi << 4) | lo);
            p += 3;
        } else {
            c = (unsigned char)*p++;
        }
        if (n + 1 >= cap) return false;
        out[n++] = (char)c;
    }
    out[n] = '\0';
    return token_ok(out);
}

static bool decode_json_token(const char *body, size_t len, char *out, size_t cap)
{
    cJSON *root = cJSON_ParseWithLength(body, len);
    if (!root || !cJSON_IsObject(root) || !root->child || root->child->next
        || !root->child->string || strcmp(root->child->string, "token")
        || !cJSON_IsString(root->child)) {
        cJSON_Delete(root);
        return false;
    }
    const char *value = cJSON_GetStringValue(root->child);
    bool ok = value && strlen(value) < cap && token_ok(value);
    if (ok) strlcpy(out, value, cap);
    cJSON_Delete(root);
    return ok;
}

static bool read_token(httpd_req_t *req, char *token, size_t cap)
{
    if (req->content_len <= 0 || req->content_len >= BODY_MAX) return false;
    char body[BODY_MAX];
    size_t done = 0;
    while (done < req->content_len) {
        int got = httpd_req_recv(req, body + done, req->content_len - done);
        if (got <= 0) {
            mbedtls_platform_zeroize(body, sizeof(body));
            return false;
        }
        done += (size_t)got;
    }
    body[done] = '\0';

    char type[64] = {};
    if (httpd_req_get_hdr_value_str(req, "Content-Type", type, sizeof(type)) != ESP_OK) {
        mbedtls_platform_zeroize(body, sizeof(body));
        return false;
    }
    bool ok = !strncasecmp(type, "application/x-www-form-urlencoded", 33)
                  ? decode_form_token(body, token, cap)
                  : !strncasecmp(type, "application/json", 16)
                        && decode_json_token(body, done, token, cap);
    mbedtls_platform_zeroize(body, sizeof(body));
    return ok;
}

typedef enum {
    WIT_CHECK_OK,
    WIT_CHECK_INVALID,
    WIT_CHECK_UNAVAILABLE,
} wit_check_t;

static wit_check_t check_token(const char *token)
{
    static const char json[] =
        "{\"q\":\"Wit.ai is ready.\",\"viseme\":\"false\","
        "\"voice\":\"Charlie\",\"style\":\"default\"}";
    esp_http_client_config_t cfg = {
        .url = "https://api.wit.ai/synthesize?v=20250213",
        .method = HTTP_METHOD_POST,
        .timeout_ms = 20000,
        .buffer_size = 1024,
        .buffer_size_tx = 1024,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t http = esp_http_client_init(&cfg);
    if (!http) return WIT_CHECK_UNAVAILABLE;

    char auth[MUSE_WIT_TOKEN_MAX + 8];
    snprintf(auth, sizeof(auth), "Bearer %s", token);
    esp_http_client_set_header(http, "Authorization", auth);
    esp_http_client_set_header(http, "Accept", "audio/mpeg");
    esp_http_client_set_header(http, "Content-Type", "application/json");

    wit_check_t result = WIT_CHECK_UNAVAILABLE;
    esp_err_t err = esp_http_client_open(http, sizeof(json) - 1);
    if (err == ESP_OK && esp_http_client_write(http, json, sizeof(json) - 1) == (int)(sizeof(json) - 1)
        && esp_http_client_fetch_headers(http) >= 0) {
        int status = esp_http_client_get_status_code(http);
        if (status == 200) {
            char byte;
            result = esp_http_client_read(http, &byte, 1) == 1 ? WIT_CHECK_OK : WIT_CHECK_UNAVAILABLE;
        } else if (status == 401 || status == 403) {
            result = WIT_CHECK_INVALID;
        }
    }
    esp_http_client_close(http);
    esp_http_client_cleanup(http);
    mbedtls_platform_zeroize(auth, sizeof(auth));
    return result;
}

static esp_err_t token_handler(httpd_req_t *req)
{
    int prior = atomic_exchange(&s_state, MUSE_WIT_TESTING);
    if (prior == MUSE_WIT_TESTING) {
        return send_html(req, "409 Conflict", "<p>A token is already being tested.</p>");
    }

    char token[MUSE_WIT_TOKEN_MAX + 1] = {};
    if (!read_token(req, token, sizeof(token))) {
        atomic_store(&s_state, MUSE_WIT_INVALID);
        esp_err_t sent = send_html(req, "400 Bad Request", "<p>The token format is invalid. Return and try again.</p>");
        mbedtls_platform_zeroize(token, sizeof(token));
        return sent;
    }

    wit_check_t checked = check_token(token);
    esp_err_t sent;
    if (checked == WIT_CHECK_INVALID) {
        atomic_store(&s_state, MUSE_WIT_INVALID);
        sent = send_html(req, "401 Unauthorized", "<p>Wit.ai rejected that token. Return and try again.</p>");
    } else if (checked == WIT_CHECK_UNAVAILABLE) {
        atomic_store(&s_state, MUSE_WIT_UNAVAILABLE);
        sent = send_html(req, "502 Bad Gateway", "<p>Wit.ai could not be reached. The previous token was kept.</p>");
    } else if (!config_set_str(WIT_TOKEN_KEY, token)) {
        atomic_store(&s_state, MUSE_WIT_SETUP_ERROR);
        sent = send_html(req, "500 Internal Server Error", "<p>The token could not be stored.</p>");
    } else {
        atomic_store(&s_state, MUSE_WIT_READY);
        atomic_store(&s_stop_pending, true);
        sent = send_html(req, "200 OK", "<p>Wit.ai is ready. You may close this page.</p>");
    }
    mbedtls_platform_zeroize(token, sizeof(token));
    return sent;
}

static esp_err_t cancel_handler(httpd_req_t *req)
{
    atomic_store(&s_stop_pending, true);
    return send_html(req, "200 OK", "<p>Setup cancelled. You may close this page.</p>");
}

static const httpd_uri_t ROOT_URI = {
    .uri = "/", .method = HTTP_GET, .handler = root_handler,
};
static const httpd_uri_t TOKEN_URI = {
    .uri = "/token", .method = HTTP_POST, .handler = token_handler,
};
static const httpd_uri_t CANCEL_URI = {
    .uri = "/cancel", .method = HTTP_POST, .handler = cancel_handler,
};

void wit_token_setup_init(void)
{
    atomic_store(&s_state, config_key_lookup(WIT_TOKEN_KEY) == CONFIG_KEY_FOUND
                               ? MUSE_WIT_READY : MUSE_WIT_UNCONFIGURED);
}

bool wit_token_setup_get(char *out, size_t cap)
{
    if (!out || cap < 2) return false;
    out[0] = '\0';
    return config_get_str(WIT_TOKEN_KEY, out, cap) && token_ok(out);
}

static void stop_server(bool preserve_state)
{
    if (s_server) {
        httpd_handle_t server = s_server;
        s_server = NULL;
        httpd_stop(server);
    }
    if (s_wifi_held) {
        s_wifi_held = false;
        muse_wifi_keep_awake(false);
    }
    atomic_store(&s_stop_pending, false);
    s_url[0] = '\0';
    if (!preserve_state) wit_token_setup_init();
}

void wit_token_setup_stop(void)
{
    stop_server(false);
}

bool wit_token_setup_clear(void)
{
    stop_server(false);
    bool ok = config_erase_key(WIT_TOKEN_KEY)
              && config_key_lookup(WIT_TOKEN_KEY) == CONFIG_KEY_NOT_FOUND;
    atomic_store(&s_state, ok ? MUSE_WIT_UNCONFIGURED : MUSE_WIT_SETUP_ERROR);
    return ok;
}

bool wit_token_setup_start(char *url, size_t cap)
{
    if (!url || !cap) return false;
    url[0] = '\0';
    if (s_server) {
        s_deadline_us = esp_timer_get_time() + SETUP_LIFETIME_US;
        strlcpy(url, s_url, cap);
        atomic_store(&s_state, MUSE_WIT_SERVING);
        return true;
    }

    esp_netif_ip_info_t ip = {};
    esp_netif_t *netif = wifi_mgr_get_netif();
    if (!netif || esp_netif_get_ip_info(netif, &ip) != ESP_OK || !ip.ip.addr) {
        atomic_store(&s_state, MUSE_WIT_SETUP_ERROR);
        return false;
    }

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size = 6144;
    cfg.max_uri_handlers = 3;
    cfg.lru_purge_enable = true;
    cfg.recv_wait_timeout = 10;
    cfg.send_wait_timeout = 10;
    if (httpd_start(&s_server, &cfg) != ESP_OK
        || httpd_register_uri_handler(s_server, &ROOT_URI) != ESP_OK
        || httpd_register_uri_handler(s_server, &TOKEN_URI) != ESP_OK
        || httpd_register_uri_handler(s_server, &CANCEL_URI) != ESP_OK) {
        stop_server(true);
        atomic_store(&s_state, MUSE_WIT_SETUP_ERROR);
        return false;
    }

    snprintf(s_url, sizeof(s_url), "http://" IPSTR, IP2STR(&ip.ip));
    strlcpy(url, s_url, cap);
    s_deadline_us = esp_timer_get_time() + SETUP_LIFETIME_US;
    s_wifi_held = true;
    muse_wifi_keep_awake(true);
    atomic_store(&s_stop_pending, false);
    atomic_store(&s_state, MUSE_WIT_SERVING);
    ESP_LOGI(TAG, "token setup page started at %s", s_url);
    return true;
}

muse_wit_setup_state_t wit_token_setup_state(void)
{
    int state = atomic_load(&s_state);
    if (s_server && (atomic_load(&s_stop_pending) || esp_timer_get_time() >= s_deadline_us)) {
        bool preserve = state == MUSE_WIT_READY || state == MUSE_WIT_SETUP_ERROR;
        stop_server(preserve);
        state = atomic_load(&s_state);
    }
    return (muse_wit_setup_state_t)state;
}
