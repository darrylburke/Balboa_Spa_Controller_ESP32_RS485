import os
from pathlib import Path
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart, time
from esphome.const import CONF_ID
from esphome.core.config import include_file
from esphome.helpers import walk_files

CODEOWNERS = ["@darrylb"]
DEPENDENCIES = ["uart"]
MULTI_CONF = True

balboa_spa_ns = cg.esphome_ns.namespace("balboa_spa")
BalboaSpa = balboa_spa_ns.class_("BalboaSpa", cg.Component, uart.UARTDevice)

CONF_BALBOA_SPA_ID = "balboa_spa_id"
CONF_READ_ONLY = "read_only"
CONF_UART_SELFTEST = "uart_selftest"
CONF_TIME_ID = "time_id"

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(BalboaSpa),
            cv.Optional(CONF_READ_ONLY, default=True): cv.boolean,
            # Bring-up only: transmit a pattern and check it reads back.
            # DISCONNECT the RS-485 module first and jumper TX->RX directly.
            cv.Optional(CONF_UART_SELFTEST, default=False): cv.boolean,
            cv.Optional(CONF_TIME_ID): cv.use_id(time.RealTimeClock),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(uart.UART_DEVICE_SCHEMA)
)


async def to_code(config):
    # Self-containment: replicate what `esphome: includes: [components/balboa_spa/protocol]`
    # does, so device YAMLs don't need to repeat that stanza.  Walk the protocol/
    # subdirectory and copy each file to src/protocol/ in the PlatformIO build tree:
    # .cpp files are picked up automatically by PlatformIO's src compiler, and headers
    # become resolvable at the `#include "protocol/..."` paths used in balboa_spa.h.
    protocol_dir = os.path.join(os.path.dirname(__file__), "protocol")
    component_dir = os.path.dirname(protocol_dir)
    for p in walk_files(protocol_dir):
        # ESPHome 2026.x include_file() takes pathlib.Path args (was str).
        basename = os.path.relpath(p, component_dir)  # e.g. "protocol/crc.h"
        include_file(Path(p), Path(basename))

    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
    cg.add(var.set_read_only(config[CONF_READ_ONLY]))
    cg.add(var.set_uart_selftest(config[CONF_UART_SELFTEST]))
    if CONF_TIME_ID in config:
        rtc = await cg.get_variable(config[CONF_TIME_ID])
        cg.add(var.set_time(rtc))
