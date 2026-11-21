#include "battery_level.h"
#include "sensor_data.h"

namespace gateway
{
namespace
{
constexpr uint8_t MAX_BATTERY_PERCENT = 100U;

bool isBatteryCurveValid(const BatteryCurve &curve)
{
    if (curve.points == nullptr || curve.count < 2U ||
        curve.count > MAX_BATTERY_PERCENT + 1U)
    {
        return false;
    }
    if (curve.points[0].percent != 0U ||
        curve.points[curve.count - 1U].percent != MAX_BATTERY_PERCENT)
    {
        return false;
    }

    for (size_t index = 0U; index < curve.count; ++index)
    {
        const BatteryCurvePoint &point = curve.points[index];
        if (point.millivolts == SENSOR_VALUE_UNAVAILABLE ||
            point.percent > MAX_BATTERY_PERCENT)
        {
            return false;
        }
        if (index > 0U)
        {
            const BatteryCurvePoint &previous = curve.points[index - 1U];
            if (point.millivolts <= previous.millivolts ||
                point.percent <= previous.percent)
            {
                return false;
            }
        }
    }
    return true;
}

uint8_t interpolateLevel(uint16_t millivolts, const BatteryCurvePoint &lower,
                         const BatteryCurvePoint &upper)
{
    const uint32_t voltageSpan = upper.millivolts - lower.millivolts;
    const uint32_t voltageOffset = millivolts - lower.millivolts;
    const uint32_t percentSpan = upper.percent - lower.percent;
    // Add half the divisor to round to the nearest whole percentage.
    const uint32_t increase = (voltageOffset * percentSpan + voltageSpan / 2U) / voltageSpan;
    return static_cast<uint8_t>(lower.percent + increase);
}

uint8_t levelFromCurve(uint16_t millivolts, const BatteryCurve &curve)
{
    if (millivolts <= curve.points[0].millivolts)
    {
        return curve.points[0].percent;
    }
    for (size_t index = 1U; index < curve.count; ++index)
    {
        const BatteryCurvePoint &upper = curve.points[index];
        if (millivolts <= upper.millivolts)
        {
            return interpolateLevel(millivolts, curve.points[index - 1U], upper);
        }
    }
    return curve.points[curve.count - 1U].percent;
}
} // namespace

bool calculateBatteryLevel(uint16_t millivolts, const BatteryCurve &curve, uint8_t &percent)
{
    // The node uses this value when no battery voltage is available.
    if (millivolts == SENSOR_VALUE_UNAVAILABLE || !isBatteryCurveValid(curve))
    {
        return false;
    }

    percent = levelFromCurve(millivolts, curve);
    return true;
}
} // namespace gateway
