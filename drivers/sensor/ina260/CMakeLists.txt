# Copyright (c) 2023 Golioth, Inc.
# SPDX-License-Identifier: Apache-2.0

target_sources(app PRIVATE ina260.c)
target_sources_ifdef(CONFIG_INA260_FAKE app PRIVATE ina260_fake.c)

zephyr_include_directories_ifdef(CONFIG_INA260 include)
