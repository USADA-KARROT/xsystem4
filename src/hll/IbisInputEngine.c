/* Copyright (C) 2019 Nunuhara Cabbage <nunuhara@haniwa.technology>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://gnu.org/licenses/>.
 */

#include "hll.h"
#include "input.h"
#include "sact.h"
#include "vm/heap.h"

// v14 AIN_WRAP wrappers for pointer-based functions
// AIN declares Mouse_GetPos(wrap<int> x, wrap<int> y) — wrap handles, not int*

static int IbisInputEngine_Mouse_GetPos(int x_slot, int y_slot)
{
	int x, y;
	int result = sact_Mouse_GetPos(&x, &y);
	wrap_set_int(x_slot, x);
	wrap_set_int(y_slot, y);
	return result;
}

static void IbisInputEngine_MouseWheel_GetCount(int forward_slot, int back_slot)
{
	int forward, back;
	mouse_get_wheel(&forward, &back);
	wrap_set_int(forward_slot, forward);
	wrap_set_int(back_slot, back);
}

bool IbisInputEngine_Mouse_MovePosImmediately(int x, int y)
{
	mouse_set_pos(x, y);
	return true;
}

bool IbisInputEngine_Mouse_HideCursorByGame(bool hide)
{
	return mouse_show_cursor(!hide);
}

//bool Mouse_IsHideCursorByGame(void);
//void Mouse_HideByStepMessage(void);
HLL_QUIET_UNIMPLEMENTED(, void, IbisInputEngine, Mouse_HideByStepMessage);

static void IbisInputEngine_Joystick_ClearCaptureFlag(void)
{
	// TODO
}

//int IbisInputEngine_Joystick_GetNumofDevice(void);
HLL_WARN_UNIMPLEMENTED(0, int,  IbisInputEngine, Joystick_GetNumofDevice);

bool IbisInputEngine_Joystick_IsKeyDown(int DeviceNumber, int JoystickCode)
{
	// TODO
	return false;
}

//float IbisInputEngine_Joystick_GetAxis(int DeviceNumber, int AxisType);
HLL_WARN_UNIMPLEMENTED(0.0, float, IbisInputEngine, Joystick_GetAxis, int dev, int axis);

HLL_LIBRARY(IbisInputEngine,
	    HLL_EXPORT(Mouse_GetPos, IbisInputEngine_Mouse_GetPos),
	    HLL_EXPORT(Mouse_MovePosImmediately, IbisInputEngine_Mouse_MovePosImmediately),
	    HLL_EXPORT(Mouse_HideCursorByGame, IbisInputEngine_Mouse_HideCursorByGame),
	    HLL_TODO_EXPORT(Mouse_IsHideCursorByGame, IbisInputEngine_Mouse_IsHideCursorByGame),
	    HLL_EXPORT(Mouse_HideByStepMessage, IbisInputEngine_Mouse_HideByStepMessage),
	    HLL_EXPORT(MouseWheel_ClearCount, mouse_clear_wheel),
	    HLL_EXPORT(MouseWheel_GetCount, IbisInputEngine_MouseWheel_GetCount),
	    HLL_EXPORT(Key_IsDown, sact_Key_IsDown),
	    HLL_EXPORT(Joystick_ClearCaptureFlag, IbisInputEngine_Joystick_ClearCaptureFlag),
	    HLL_EXPORT(Joystick_GetNumofDevice, IbisInputEngine_Joystick_GetNumofDevice),
	    HLL_EXPORT(Joystick_IsKeyDown, IbisInputEngine_Joystick_IsKeyDown),
	    HLL_EXPORT(Joystick_GetAxis, IbisInputEngine_Joystick_GetAxis));
