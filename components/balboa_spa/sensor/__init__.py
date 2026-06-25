import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import CONF_TYPE, UNIT_EMPTY
from .. import balboa_spa_ns, BalboaSpa, CONF_BALBOA_SPA_ID

DEPENDENCIES = ["balboa_spa"]

BalboaSensor = balboa_spa_ns.class_("BalboaSensor", sensor.Sensor, cg.Component)
SpaSensorType = balboa_spa_ns.enum("SpaSensorType")
SENSOR_TYPES = {
    "current_temperature": SpaSensorType.CURRENT_TEMPERATURE,
    "target_temperature": SpaSensorType.TARGET_TEMPERATURE,
}

CONFIG_SCHEMA = sensor.sensor_schema(BalboaSensor).extend(
    {
        cv.GenerateID(CONF_BALBOA_SPA_ID): cv.use_id(BalboaSpa),
        cv.Required(CONF_TYPE): cv.enum(SENSOR_TYPES, lower=True),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = await sensor.new_sensor(config)
    await cg.register_component(var, config)
    parent = await cg.get_variable(config[CONF_BALBOA_SPA_ID])
    cg.add(var.set_parent(parent))
    cg.add(var.set_sensor_type(config[CONF_TYPE]))
