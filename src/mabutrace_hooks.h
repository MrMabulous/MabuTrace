/*
 * Copyright (C) 2020 Matthias Bühlmann
 *
 * This file is part of MabuTrace.
 *
 * MabuTrace is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * MabuTrace is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with MabuTrace.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef __MABUTRACE_HOOKS_H__
#define __MABUTRACE_HOOKS_H__

#ifdef __cplusplus
extern "C" {
#endif


#ifndef __ASSEMBLER__
void trace_task_switch(unsigned char type);

// This macro is called when a task is about to be switched out.
#define traceTASK_SWITCHED_OUT() \
  do { \
    trace_task_switch(7); \
  } while(0)

// This macro is called when a task has just been switched in.
#define traceTASK_SWITCHED_IN() \
  do { \
    trace_task_switch(6); \
  } while(0)

#endif

#ifdef __cplusplus
}
#endif

#endif // FREERTOS_TRACE_HOOKS_H