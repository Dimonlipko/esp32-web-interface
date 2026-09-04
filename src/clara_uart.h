/*
 * This file is part of the esp32 web interface
 *
 * Монітор послідовного порту Клари (ccs32clara, термінал на UART4 @115200).
 *
 * Рядки з Клари складаються в кільцевий буфер із наскрізною нумерацією, а веб
 * забирає їх опитуванням: віддаємо все, що новіше за переданий seq. Так читач
 * бачить, скільки рядків він проґавив, і жодного стану на боці HTTP тримати не
 * треба — сторінку можна перезавантажити посеред потоку.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */
#ifndef CLARA_UART_H
#define CLARA_UART_H
#include <Arduino.h>

#define CLARA_LINES     128   // 10 КБ .bss; при 115200 це ~2 с суцільного потоку
#define CLARA_LINE_LEN  80

namespace ClaraUart {

void Init(int rxPin, int txPin, uint32_t baud);
void Loop();                        // вичитати UART у кільце; кличеться з loop()
void Send(const char* cmd);         // дописує \n; порожній рядок = просто Enter
void Clear();
void Note(const char* fmt, ...);    // службовий рядок від самого веб-інтерфейсу

// Формує {"head":N,"lost":M,"lines":[...]} з рядків, новіших за since.
size_t Json(uint32_t since, char* out, size_t cap);

uint32_t GetBaud();
bool IsRunning();

}
#endif
