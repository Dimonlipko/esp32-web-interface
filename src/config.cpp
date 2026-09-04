#include "config.h"
#include <EEPROM.h>
Config::Config() {
}

bool Config::isValidPin(int pin) {
    return (pin >= 0 && pin <= 10) || pin == 20 || pin == 21;
}

void Config::load() {

    EEPROM.begin(sizeof(settings));
    EEPROM.get(0, settings);
    if (settings.version != EEPROM_VERSION) {
        //defaults: розводка плати Tesla_Wheel_Heater_ESP32 (ESP32-C3 Super Mini)
        settings.version = EEPROM_VERSION;
        settings.canTXPin = GPIO_NUM_21;
        settings.canRXPin = GPIO_NUM_20;
        settings.canEnablePin = 0;
        settings.claraRXPin = GPIO_NUM_10;   // піни LIN виведені на розʼєм UART
        settings.claraTXPin = GPIO_NUM_9;
        settings.claraBaud = 115200;         // термінал ccs32clara, UART4 8N1
        saveSettings();
    }

    // Сміття в NVS або конфіг від звичайного ESP32 (напр. GPIO16/17) поклав би
    // twai_driver_install з ESP_ERR_INVALID_ARG — CAN мовчки не піднявся б.
    if (!isValidPin(settings.canTXPin)) settings.canTXPin = GPIO_NUM_21;
    if (!isValidPin(settings.canRXPin)) settings.canRXPin = GPIO_NUM_20;
    if (settings.canEnablePin != 0 && !isValidPin(settings.canEnablePin))
        settings.canEnablePin = 0;
    if (!isValidPin(settings.claraRXPin)) settings.claraBaud = 0;
    if (settings.claraTXPin >= 0 && !isValidPin(settings.claraTXPin))
        settings.claraTXPin = -1;
}
int Config::getCanRXPin() {
    return settings.canRXPin;
}

int Config::getCanTXPin() {
    return settings.canTXPin;
}

int Config::getCanEnablePin() {
    return settings.canEnablePin;
}

int Config::getClaraRXPin() {
    return settings.claraRXPin;
}

int Config::getClaraTXPin() {
    return settings.claraTXPin;
}

int Config::getClaraBaud() {
    return settings.claraBaud;
}


void Config::setCanEnablePin(int pin) {
    settings.canEnablePin = pin;
}

void Config::setCanTXPin(int pin) {
    settings.canTXPin = pin;
}

void Config::setCanRXPin(int pin) {
    settings.canRXPin = pin;
}

void Config::setClaraRXPin(int pin) {
    settings.claraRXPin = pin;
}

void Config::setClaraTXPin(int pin) {
    settings.claraTXPin = pin;
}

void Config::setClaraBaud(int baud) {
    settings.claraBaud = baud;
}

void Config::saveSettings() {
    EEPROM.put(0, settings); //save all change to eeprom
    EEPROM.commit();
}
