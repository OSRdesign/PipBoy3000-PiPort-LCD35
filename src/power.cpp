#include "power.h"
#include "display.h" // I2C_SDA / I2C_SCL

#define XPOWERS_CHIP_AXP2101
#include <XPowersLib.h>

static XPowersPMU s_pmu;

// Rail voltages/enables transcribed verbatim from Waveshare's own tested
// demo (Arduino/examples/02_axp2101_example/02_axp2101_example.ino) - this
// chip powers the board's other rails (display, codec, etc.), so these
// values are copied exactly rather than guessed; getting one wrong risks
// overvolting a rail.
void Power_Init() {
  if (!s_pmu.begin(Wire, AXP2101_SLAVE_ADDRESS, I2C_SDA, I2C_SCL)) {
    return; // nothing we can usefully do if the PMU isn't answering
  }

  s_pmu.setVbusVoltageLimit(XPOWERS_AXP2101_VBUS_VOL_LIM_4V36);
  s_pmu.setVbusCurrentLimit(XPOWERS_AXP2101_VBUS_CUR_LIM_1500MA);
  s_pmu.setSysPowerDownVoltage(2600);

  s_pmu.setDC1Voltage(3300);
  s_pmu.setDC2Voltage(1000);
  s_pmu.setDC3Voltage(3300);
  s_pmu.setDC4Voltage(1000);
  s_pmu.setDC5Voltage(3300);
  s_pmu.setALDO1Voltage(3300);
  s_pmu.setALDO2Voltage(3300);
  s_pmu.setALDO3Voltage(3300);
  s_pmu.setALDO4Voltage(3300);
  s_pmu.setBLDO1Voltage(1500);
  s_pmu.setBLDO2Voltage(2800);
  s_pmu.setCPUSLDOVoltage(1000);
  s_pmu.setDLDO1Voltage(3300);
  s_pmu.setDLDO2Voltage(3300);

  s_pmu.enableDC2();
  s_pmu.enableDC3();
  s_pmu.enableDC4();
  s_pmu.enableDC5();
  s_pmu.enableALDO1();
  s_pmu.enableALDO2();
  s_pmu.enableALDO3();
  s_pmu.enableALDO4();
  s_pmu.enableBLDO1();
  s_pmu.enableBLDO2();
  s_pmu.enableCPUSLDO();
  s_pmu.enableDLDO1();
  s_pmu.enableDLDO2();

  s_pmu.disableTSPinMeasure(); // no battery temp sensor on this board's pack
  s_pmu.enableBattDetection();
  s_pmu.enableVbusVoltageMeasure();
  s_pmu.enableBattVoltageMeasure();
  s_pmu.enableSystemVoltageMeasure();
  s_pmu.setChargingLedMode(XPOWERS_CHG_LED_OFF);

  s_pmu.setPrechargeCurr(XPOWERS_AXP2101_PRECHARGE_50MA);
  s_pmu.setChargerConstantCurr(XPOWERS_AXP2101_CHG_CUR_200MA);
  s_pmu.setChargerTerminationCurr(XPOWERS_AXP2101_CHG_ITERM_25MA);
  s_pmu.setChargeTargetVoltage(XPOWERS_AXP2101_CHG_VOL_4V1);

  s_pmu.disableIRQ(XPOWERS_AXP2101_ALL_IRQ);
  s_pmu.clearIrqStatus();
  s_pmu.enableIRQ(XPOWERS_AXP2101_PKEY_SHORT_IRQ | XPOWERS_AXP2101_PKEY_LONG_IRQ);
}

float Power_GetVolts() {
  return s_pmu.getBattVoltage() / 1000.0f;
}

uint8_t Power_GetPercent() {
  if (!s_pmu.isBatteryConnect()) return 100; // running off USB/no pack - show full
  int pct = s_pmu.getBatteryPercent();
  if (pct < 0) return 0;
  if (pct > 100) return 100;
  return (uint8_t)pct;
}

PressEvent Power_PollKey() {
  s_pmu.getIrqStatus();
  PressEvent ev = PRESS_NONE;
  if (s_pmu.isPekeyShortPressIrq()) ev = PRESS_SHORT;
  else if (s_pmu.isPekeyLongPressIrq()) ev = PRESS_LONG;
  s_pmu.clearIrqStatus();
  return ev;
}
