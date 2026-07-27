import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor
from esphome.const import CONF_TYPE
from .. import balboa_spa_ns, BalboaSpa, CONF_BALBOA_SPA_ID

DEPENDENCIES = ["balboa_spa"]

BalboaBinarySensor = balboa_spa_ns.class_("BalboaBinarySensor", binary_sensor.BinarySensor, cg.Component)
SpaBinaryType = balboa_spa_ns.enum("SpaBinaryType")
BINARY_TYPES = {
    "heating": SpaBinaryType.HEATING,
    "priming": SpaBinaryType.PRIMING,
    "circulation_pump": SpaBinaryType.CIRCULATION_PUMP,
    "filter1_running": SpaBinaryType.FILTER1_RUNNING,
    "filter2_running": SpaBinaryType.FILTER2_RUNNING,
    "bus_connected": SpaBinaryType.BUS_CONNECTED,
}

CONFIG_SCHEMA = binary_sensor.binary_sensor_schema(BalboaBinarySensor).extend(
    {
        cv.GenerateID(CONF_BALBOA_SPA_ID): cv.use_id(BalboaSpa),
        cv.Required(CONF_TYPE): cv.enum(BINARY_TYPES, lower=True),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = await binary_sensor.new_binary_sensor(config)
    await cg.register_component(var, config)
    parent = await cg.get_variable(config[CONF_BALBOA_SPA_ID])
    cg.add(var.set_parent(parent))
    cg.add(var.set_binary_type(config[CONF_TYPE]))
