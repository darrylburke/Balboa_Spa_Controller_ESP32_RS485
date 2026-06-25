import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import climate
from esphome.const import CONF_ID
from .. import balboa_spa_ns, BalboaSpa, CONF_BALBOA_SPA_ID

DEPENDENCIES = ["balboa_spa"]

BalboaClimate = balboa_spa_ns.class_("BalboaClimate", climate.Climate, cg.Component)

# ESPHome 2025.2.0 does not expose climate_schema() or new_climate(); the
# correct pattern is to extend climate.CLIMATE_SCHEMA and call register_climate.
CONFIG_SCHEMA = (
    climate.CLIMATE_SCHEMA.extend(
        {
            cv.GenerateID(): cv.declare_id(BalboaClimate),
            cv.GenerateID(CONF_BALBOA_SPA_ID): cv.use_id(BalboaSpa),
        }
    ).extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await climate.register_climate(var, config)
    parent = await cg.get_variable(config[CONF_BALBOA_SPA_ID])
    cg.add(var.set_parent(parent))
