/*
 * This file is part of the esp32 web interface
 *
 * Кільце останніх кадрів шини — те саме, що робить CAN-аналізатор, тільки
 * пам'яті на порядки менше. Пише сюди OICan::Loop(), читає HTTP-задача
 * опитуванням /api/canmon, тож, як і в терміналі Клари, кадри нумеруються
 * наскрізно: читач бачить, скільки він проґавив, а сервер не тримає стану.
 *
 * Вмикається окремо, бо в звичайному режимі TWAI стоїть за апаратним фільтром
 * на 0x580+nodeId і 0x7DE — інакше на завантаженій шині черга прийому лягала б
 * під трафіком, який нікому не потрібен.
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
#ifndef CAN_MONITOR_H
#define CAN_MONITOR_H
#include <Arduino.h>
#include "driver/twai.h"

#define CANMON_FRAMES     256   // ~5 КБ .bss
#define CANMON_MAX_REPLY  120   // скільки кадрів максимум віддаємо за один запит

namespace CanMonitor {

void Push(const twai_message_t* frame);   // кличеться з OICan::Loop()
void Clear();

bool IsEnabled();
void SetEnabled(bool on);

// Програмний фільтр поверх апаратного: mask 0 = пропускати все.
// Кадр проходить, коли (id & mask) == (filterId & mask).
void SetFilter(uint32_t id, uint32_t mask);
uint32_t GetFilterId();
uint32_t GetFilterMask();

// {"head":N,"lost":M,"on":0|1,"drops":D,"frames":[{"t":ms,"id":"7DE","x":0,"r":0,"d":"01 02"}]}
size_t Json(uint32_t since, char* out, size_t cap);

}
#endif
