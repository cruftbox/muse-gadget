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

#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "muse_link.h"

void wit_token_setup_init(void);
bool wit_token_setup_get(char *out, size_t cap);
bool wit_token_setup_clear(void);
bool wit_token_setup_start(char *url, size_t cap);
void wit_token_setup_stop(void);
muse_wit_setup_state_t wit_token_setup_state(void);
