#ifndef CONFIG_H
#define CONFIG_H

#define EEPROM_VERSION 3
typedef struct {
    int version;
    int canRXPin;
    int canTXPin;
    int canEnablePin;
    int claraRXPin;
    int claraTXPin;      // -1 = слухаємо Клару, але нічого їй не шлемо
    int claraBaud;       // 0 = монітор вимкнено
} EEPROMSettings;


class Config
{
  public:
    Config();
    void load();
    int getCanRXPin();
    void setCanRXPin(int pin);

    int getCanTXPin();
    void setCanTXPin(int pin);


    int getCanEnablePin();
    void setCanEnablePin(int pin);

    int getClaraRXPin();
    void setClaraRXPin(int pin);

    int getClaraTXPin();
    void setClaraTXPin(int pin);

    int getClaraBaud();
    void setClaraBaud(int baud);

    void saveSettings();

    // ESP32-C3 має лише GPIO0..10 і GPIO20/21: 11 — VDD_SPI, 12..17 — вбудований
    // flash, 18/19 — нативний USB (по ньому ж іде консоль і прошивка).
    static bool isValidPin(int pin);
  private:
    EEPROMSettings settings;

};
#endif
