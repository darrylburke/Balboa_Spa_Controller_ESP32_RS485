import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import text_sensor
from esphome.const import CONF_TYPE
from .. import balboa_spa_ns, BalboaSpa, CONF_BALBOA_SPA_ID

DEPENDENCIES = ["balboa_spa"]

BalboaTextSensor = balboa_spa_ns.class_("BalboaTextSensor", text_sensor.TextSensor, cg.Component)
SpaTextType = balboa_spa_ns.enum("SpaTextType")
TEXT_TYPES = {
    "model": SpaTextType.MODEL,
    "version": SpaTextType.VERSION,
    "notification": SpaTextType.NOTIFICATION,
}

CONFIG_SCHEMA = text_sensor.text_sensor_schema(BalboaTextSensor).extend(
    {
        cv.GenerateID(CONF_BALBOA_SPA_ID): cv.use_id(BalboaSpa),
        cv.Required(CONF_TYPE): cv.enum(TEXT_TYPES, lower=True),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = await text_sensor.new_text_sensor(config)
    await cg.register_component(var, config)
    parent = await cg.get_variable(config[CONF_BALBOA_SPA_ID])
    cg.add(var.set_parent(parent))
    cg.add(var.set_text_type(config[CONF_TYPE]))
