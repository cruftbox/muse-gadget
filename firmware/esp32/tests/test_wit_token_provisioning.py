# Copyright (c) 2026 Muse Gadget contributors.
# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SETUP_C = ROOT / "main" / "wit_token_setup.c"
CONFIG_STORE_C = ROOT / "main" / "config_store.c"
MUSE_CMAKE = ROOT / "components" / "muse" / "CMakeLists.txt"
CHAT_CPP = ROOT / "components" / "muse" / "muse_chat_session.cpp"
MUSE_LINK_H = ROOT / "components" / "muse" / "muse_link.h"
SETTINGS_UI_C = ROOT / "components" / "muse" / "muse_settings_ui.c"


def _function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 0
    for i in range(brace, len(source)):
        if source[i] == "{":
            depth += 1
        elif source[i] == "}":
            depth -= 1
            if depth == 0:
                return source[brace + 1 : i]
    raise AssertionError(f"could not find end of {signature}")


class WitTokenProvisioningContractTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.setup = SETUP_C.read_text(encoding="utf-8")
        cls.config = CONFIG_STORE_C.read_text(encoding="utf-8")
        cls.cmake = MUSE_CMAKE.read_text(encoding="utf-8")
        cls.chat = CHAT_CPP.read_text(encoding="utf-8")
        cls.link = MUSE_LINK_H.read_text(encoding="utf-8")
        cls.ui = SETTINGS_UI_C.read_text(encoding="utf-8")

    def test_build_has_no_embedded_wit_token(self) -> None:
        self.assertNotIn("WIT_CLIENT_TOKEN", self.cmake)
        self.assertNotIn("wit_token.txt", self.cmake)
        self.assertNotIn("wit_token_start", self.chat)
        self.assertIn("muse_link_wit_token_get", self.chat)

    def test_pairing_and_setup_reset_erase_wit_token(self) -> None:
        pairing = _function_body(self.config, "bool config_clear_pairing(")
        setup = _function_body(self.config, "bool config_clear_setup(")
        self.assertIn('"wit_token"', pairing)
        self.assertIn('"wit_token"', setup)
        self.assertIn("clear_and_verify", pairing)
        self.assertIn("clear_and_verify", setup)

    def test_token_is_validated_before_it_is_stored(self) -> None:
        handler = _function_body(self.setup, "static esp_err_t token_handler(")
        check = handler.index("check_token(token)")
        store = handler.index("config_set_str(WIT_TOKEN_KEY, token)")
        self.assertLess(check, store)
        self.assertIn("The previous token was kept", handler)
        self.assertIn("mbedtls_platform_zeroize(token", handler)

    def test_setup_server_is_bounded_and_local_action_controlled(self) -> None:
        self.assertIn('static const httpd_uri_t ROOT_URI', self.setup)
        self.assertIn('.uri = "/token", .method = HTTP_POST', self.setup)
        self.assertIn('.uri = "/cancel", .method = HTTP_POST', self.setup)
        self.assertIn("SETUP_LIFETIME_US (5 * 60 * 1000000LL)", self.setup)
        self.assertIn('"Cache-Control", "no-store"', self.setup)
        self.assertIn("req->content_len >= BODY_MAX", self.setup)
        self.assertIn("muse_link_wit_setup_start", self.ui)
        self.assertIn("muse_link_wit_setup_stop", self.ui)

    def test_stored_token_is_not_rendered(self) -> None:
        page_start = self.setup.index("static const char SETUP_PAGE[]")
        page_end = self.setup.index("static esp_err_t send_html", page_start)
        page = self.setup[page_start:page_end]
        self.assertNotIn("%s", page)
        self.assertNotIn("WIT_TOKEN_KEY", page)
        self.assertIn("The stored token is never shown", page)

    def test_audio_download_is_bounded_and_runs_outside_session_task(self) -> None:
        self.assertIn("WIT_STREAM_BYTES (32 * 1024)", self.chat)
        self.assertIn('xTaskCreateWithCaps(wit_tts_task, "wit_tts"', self.chat)
        self.assertIn("xStreamBufferSend(s_wit_stream", self.chat)
        self.assertIn("xStreamBufferReceive(s_wit_stream", self.chat)
        self.assertIn("cancel_wit_tts();", self.chat)
        self.assertIn("req->gen != s_gen.load()", self.chat)

    def test_wit_request_settings_are_shared(self) -> None:
        self.assertIn("MUSE_WIT_TTS_API_VERSION", self.link)
        self.assertIn("MUSE_WIT_TTS_URL", self.link)
        self.assertIn("CONFIG_MUSE_WIT_TTS_VOICE", self.chat)
        self.assertIn("CONFIG_MUSE_WIT_TTS_MAX_BYTES", self.chat)


if __name__ == "__main__":
    unittest.main()
