# SPDX-License-Identifier: Apache-2.0
# UF2 output support; no automatic programming.
if(CONFIG_BUILD_OUTPUT_UF2)
  include(${ZEPHYR_BASE}/boards/common/uf2.board.cmake)
endif()
