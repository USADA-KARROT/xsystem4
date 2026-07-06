/* Copyright (C) 2023 Nunuhara Cabbage <nunuhara@haniwa.technology>
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

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "system4/ain.h"

#include "vm/heap.h"
#include "vm/page.h"
#include "system4/string.h"
#include "parts.h"
#include "../parts/parts_internal.h"
#include "movie.h"
#include "hll.h"
#include "input.h"
#include <SDL.h>
#include "gfx/gfx.h"
#include "scene.h"
#include "sprite.h"

static void PartsEngine_ModuleInit(void)
{
	PE_Init();
}

static void PartsEngine_ModuleFini(void)
{
	PE_Reset();
}

static void PartsEngine_Update(int passed_time, bool is_skip, bool message_window_show)
{
	PE_Update(passed_time, message_window_show);
}

static void PartsEngine_Update_Pascha3PC(struct string *xxx1, struct string *xxx2, int passed_time, bool is_skip, bool message_window_show)
{
	PE_Update(passed_time, message_window_show);
}

// Oyako Rankan
static bool PartsEngine_AddDrawCutCGToPartsConstructionProcess_old(int parts_no,
		struct string *cg_name, int dx, int dy, int sx, int sy, int w, int h,
		int state)
{
	return PE_AddDrawCutCGToPartsConstructionProcess(parts_no, cg_name, dx, dy, w, h,
			sx, sy, w, h, 0, state);
}

// Oyako Rankan
static bool PartsEngine_AddCopyCutCGToPartsConstructionProcess_old(int parts_no,
		struct string *cg_name, int dx, int dy, int sx, int sy, int w, int h,
		int state)
{
	return PE_AddCopyCutCGToPartsConstructionProcess(parts_no, cg_name, dx, dy, w, h,
			sx, sy, w, h, 0, state);
}

// Rance 9: argument types changed from int to float
static void PartsEngine_SetComponentPos(int parts_no, float x, float y)
{
	PE_SetPos(parts_no, x, y);
}

// Rance 9: return type changed from int to float
static float PartsEngine_Parts_GetPartsUpperLeftPosX(int parts_no, int state)
{
	return PE_GetPartsUpperLeftPosX(parts_no, state);
}

static float PartsEngine_Parts_GetPartsUpperLeftPosY(int parts_no, int state)
{
	return PE_GetPartsUpperLeftPosY(parts_no, state);
}

// Rance 9: return type changed from int to float
static float PartsEngine_Parts_GetComponentPosX(int parts_no)
{
	return PE_GetPartsX(parts_no);
}

static float PartsEngine_GetComponentPosY(int parts_no)
{
	return PE_GetPartsY(parts_no);
}

// Rance 9: X/Y coordinate types changed from int to float
static void PartsEngine_AddComponentMotionPos(int parts_no, float begin_x, float begin_y,
		float end_x, float end_y, int begin_t, int end_t,
		struct string *curve_name)
{
	PE_AddMotionPos_curve(parts_no, begin_x, begin_y, end_x, end_y,
			begin_t, end_t, curve_name);
}

static void PartsEngine_add_construction_process(union vm_value *ints,
		union vm_value *floats, union vm_value *strings)
{
	int parts_no    = ints[0].i;
	int state       = ints[1].i;
	int command     = ints[2].i;
	int interp_type = ints[3].i;
	int sx          = ints[4].i;
	int sy          = ints[5].i;
	int sw          = ints[6].i;
	int sh          = ints[7].i;
	int dx          = ints[8].i;
	int dy          = ints[9].i;
	int dw          = ints[12].i;
	int dh          = ints[13].i;
	int r           = ints[14].i;
	int g           = ints[15].i;
	int b           = ints[16].i;
	int a           = ints[17].i;
	// int r2       = ints[18].i;
	// int g2       = ints[19].i;
	// int b2       = ints[20].i;
	int char_space  = ints[21].i;
	int line_space  = ints[22].i;
	int font_type   = ints[23].i;
	int font_size   = ints[24].i;
	int font_r      = ints[25].i;
	int font_g      = ints[26].i;
	int font_b      = ints[27].i;
	int edge_r      = ints[28].i;
	int edge_g      = ints[29].i;
	int edge_b      = ints[30].i;
	int full_size   = ints[31].i;
	float bold_weight = floats[0].f;
	float edge_weight = floats[1].f;
	struct string *text    = heap_get_string(strings[0].i);
	struct string *cg_name = heap_get_string(strings[1].i);

	switch (command) {
	case 0:  // CASConstructionProcess::SetCreate
		PE_AddCreateToPartsConstructionProcess(parts_no, dw, dh, state);
		break;
	case 1:  // CASConstructionProcess::SetCreatePixelOnly
		PE_AddCreatePixelOnlyToPartsConstructionProcess(parts_no, dw, dh, state);
		break;
	case 2:  // CASConstructionProcess::SetCreateCG
		PE_AddCreateCGToProcess(parts_no, cg_name, state);
		break;
	case 3:  // CASConstructionProcess::SetFill
		PE_AddFillToPartsConstructionProcess(parts_no,
				dx, dy, dw, dh, r, g, b, state);
		break;
	case 4:  // CASConstructionProcess::SetFillAlphaColor
		PE_AddFillAlphaColorToPartsConstructionProcess(parts_no,
				dx, dy, dw, dh, r, g, b, a, state);
		break;
	case 5:  // CASConstructionProcess::SetFillAMap
		PE_AddFillAMapToPartsConstructionProcess(parts_no,
				dx, dy, dw, dh, a, state);
		break;
	case 6:  // CASConstructionProcess::SetFillWithAlpha
		PE_AddFillWithAlphaToPartsConstructionProcess(parts_no,
				dx, dy, dw, dh, r, g, b, a, state);
		break;
	case 7:  // CASConstructionProcess::SetDrawText
		PE_AddDrawTextToPartsConstructionProcess(parts_no,
				dx, dy, text, font_type, font_size,
				font_r, font_g, font_b, bold_weight,
				edge_r, edge_g, edge_b, edge_weight,
				char_space, line_space, state);
		break;
	case 8:  // CASConstructionProcess::SetCopyText
		PE_AddCopyTextToPartsConstructionProcess(parts_no,
				dx, dy, text, font_type, font_size,
				font_r, font_g, font_b, bold_weight,
				edge_r, edge_g, edge_b, edge_weight,
				char_space, line_space, state);
		break;
	case 9:  // CASConstructionProcess::SetFillGradationHorizon
		WARNING("AddConstructProcess: FillGradationHorizon unimplemented");
		break;
	case 10:  // CASConstructionProcess::SetDrawRect
		PE_AddDrawRectToPartsConstructionProcess(parts_no,
				dx, dy, dw, dh, r, g, b, state);
		break;
	case 11:  // CASConstructionProcess::SetCutCGBlend (equal scale)
		PE_AddDrawCutCGToPartsConstructionProcess(parts_no, cg_name,
				dx, dy, dw, dh, sx, sy, dw, dh,
				interp_type, state);
		break;
	case 12:  // CASConstructionProcess::SetCutCGCopy (equal scale)
		PE_AddCopyCutCGToPartsConstructionProcess(parts_no, cg_name,
				dx, dy, dw, dh, sx, sy, dw, dh,
				interp_type, state);
		break;
	case 13:  // CASConstructionProcess::SetCutCGScaleBlend
		PE_AddDrawCutCGToPartsConstructionProcess(parts_no, cg_name,
				dx, dy, dw, dh, sx, sy, sw, sh,
				interp_type, state);
		break;
	case 14:  // CASConstructionProcess::SetCutCGScaleCopy
		PE_AddCopyCutCGToPartsConstructionProcess(parts_no, cg_name,
				dx, dy, dw, dh, sx, sy, sw, sh,
				interp_type, state);
		break;
	case 15:  // CASConstructionProcess::SetGrayFilter
		PE_AddGrayFilterToPartsConstructionProcess(parts_no,
				dx, dy, dw, dh, full_size, state);
		break;
	case 16:  // CASConstructionProcess::SetAddFilter
		WARNING("AddConstructProcess: AddFilter unimplemented");
		break;
	case 17:  // CASConstructionProcess::SetMulFilter
		WARNING("AddConstructProcess: MulFilter unimplemented");
		break;
	case 18:  // CASConstructionProcess::SetDrawLine
		WARNING("AddConstructProcess: DrawLine unimplemented");
		break;
	default:
		WARNING("AddConstructProcess: unknown command %d", command);
		break;
	}
}

// Generic dispatch function for PartsEngine operations.
// func_id selects the operation; arguments and return values are passed
// through three typed arrays (int/bool, float, string).
//
// Calling convention (from game script):
//   1. Push input values into the appropriate arrays.
//   2. Push a placeholder (0 or "") for each output slot.
//   3. Call PartsFunc; outputs are written back into the placeholder slots.
static int PartsEngine_PartsFunc(int func_id, struct page **array_int,
		struct page **array_float, struct page **array_string)
{
	int nr_ints = (array_int && *array_int) ? (*array_int)->nr_vars : 0;
	union vm_value *ints = nr_ints ? (*array_int)->values : NULL;
	int nr_floats = (array_float && *array_float) ? (*array_float)->nr_vars : 0;
	union vm_value *floats = nr_floats ? (*array_float)->values : NULL;
	int nr_strings = (array_string && *array_string) ? (*array_string)->nr_vars : 0;
	union vm_value *strings = nr_strings ? (*array_string)->values : NULL;

#define REQUIRE_INTS(n) \
	if (nr_ints != (n)) VM_ERROR("Invalid arguments for PartsFunc %d: expected %d ints, got %d", func_id, (n), nr_ints)
#define REQUIRE_FLOATS(n) \
	if (nr_floats < (n)) VM_ERROR("Invalid arguments for PartsFunc %d: expected %d floats, got %d", func_id, (n), nr_floats)
#define REQUIRE_STRINGS(n) \
	if (nr_strings < (n)) VM_ERROR("Invalid arguments for PartsFunc %d: expected %d strings, got %d", func_id, (n), nr_strings)

	switch (func_id) {
	case 0:  // void SetActiveLayer(int layer)
		REQUIRE_INTS(1);
		PE_set_active_controller(ints[0].i);
		return 1;
	case 1:  // int GetActiveLayer()
		REQUIRE_INTS(1);
		ints[0].i = PE_get_active_controller();
		return 1;
	case 2:  // int GetSystemOverlayLayer()
		REQUIRE_INTS(1);
		ints[0].i = PE_get_system_controller();
		return 1;
	case 3:  // void PauseMotion(bool pause)
		REQUIRE_INTS(1);
		PE_PauseMotion(!!ints[0].i);
		return 1;
	case 4:  // void SetWantSave(int parts_no, bool want_save)
		REQUIRE_INTS(2);
		PE_parts_set_want_save(ints[0].i, !!ints[1].i);
		return 1;
	case 6:  // bool SaveThumbnail(string filename, int reduction_factor)
		REQUIRE_INTS(2);
		REQUIRE_STRINGS(1);
		ints[1].i = PE_save_thumbnail(heap_get_string(strings[0].i), ints[0].i);
		return 1;
	case 40:  // float PARTS_GetAbsoluteX(int number)
		REQUIRE_INTS(1);
		REQUIRE_FLOATS(1);
		floats[0].f = PE_parts_get_absolute_x(ints[0].i);
		return 1;
	case 41:  // float PARTS_GetAbsoluteY(int number)
		REQUIRE_INTS(1);
		REQUIRE_FLOATS(1);
		floats[0].f = PE_parts_get_absolute_y(ints[0].i);
		return 1;
	case 42:  // int PARTS_GetAbsoluteZ(int number)
		REQUIRE_INTS(2);
		ints[1].i = PE_parts_get_absolute_z(ints[0].i);
		return 1;
	case 45:  // void PARTS_SetLockInputState(int number, bool lock)
		REQUIRE_INTS(2);
		PE_parts_set_lock_input_state(ints[0].i, !!ints[1].i);
		return 1;
	case 57:  // void AppendChild(int number, int child_number)
		REQUIRE_INTS(2);
		PE_SetParentPartsNumber(ints[1].i, ints[0].i);
		return 1;
	case 91:  // void SetLayoutBoxPadding(int parts_no, int top, int bottom, int left, int right)
		REQUIRE_INTS(5);
		PE_set_layoutbox_padding(ints[0].i, ints[1].i, ints[2].i, ints[3].i, ints[4].i);
		return 1;
	case 92:  // int GetLayoutBoxPaddingTop(int parts_no)
		REQUIRE_INTS(2);
		ints[1].i = PE_get_layoutbox_padding_top(ints[0].i);
		return 1;
	case 93:  // int GetLayoutBoxPaddingBottom(int parts_no)
		REQUIRE_INTS(2);
		ints[1].i = PE_get_layoutbox_padding_bottom(ints[0].i);
		return 1;
	case 94:  // int GetLayoutBoxPaddingLeft(int parts_no)
		REQUIRE_INTS(2);
		ints[1].i = PE_get_layoutbox_padding_left(ints[0].i);
		return 1;
	case 95:  // int GetLayoutBoxPaddingRight(int parts_no)
		REQUIRE_INTS(2);
		ints[1].i = PE_get_layoutbox_padding_right(ints[0].i);
		return 1;
	case 103:  // void GetPartsCGSurfaceArea(int parts_no, int *x, int *y, int *w, int *h, int state)
		REQUIRE_INTS(6);
		PE_GetPartsCGSurfaceArea(ints[0].i, &ints[1].i, &ints[2].i, &ints[3].i, &ints[4].i, ints[5].i);
		return 1;
	case 159:  // AddConstructProcess(ArrayInt[32], ArrayFloat[2], ArrayString[2])
		REQUIRE_INTS(32);
		REQUIRE_FLOATS(2);
		REQUIRE_STRINGS(2);
		PartsEngine_add_construction_process(ints, floats, strings);
		return 1;
	case 162:  // bool InitPartsMovie(int parts_no, int width, int height, int bg_r, int bg_g, int bg_b, int state)
		REQUIRE_INTS(8);
		ints[7].i = PE_init_parts_movie(ints[0].i, ints[1].i, ints[2].i, ints[3].i, ints[4].i, ints[5].i, ints[6].i);
		return 1;
	case 163:  // int GetMovieSprite(int parts_no, int state)
		REQUIRE_INTS(3);
		ints[2].i = PE_get_movie_sprite(ints[0].i, ints[1].i);
		return 1;
	default:
		WARNING("Unknown func_id: %d", func_id);
		return 0;
	}
#undef REQUIRE_INTS
}

HLL_WARN_UNIMPLEMENTED(, void, PartsEngine, StopSoundWithoutSystemSound);

static void PartsEngine_PreLink(void);
static void PartsEngine_PostLink(void);

HLL_LIBRARY(PartsEngine,
	    HLL_EXPORT(_PreLink, PartsEngine_PreLink),
	    HLL_EXPORT(_PostLink, PartsEngine_PostLink),
	    // for versions without PartsEngine.Init
	    HLL_EXPORT(_ModuleInit, PartsEngine_ModuleInit),
	    HLL_EXPORT(_ModuleFini, PartsEngine_ModuleFini),
	    // Oyako Rankan
	    HLL_EXPORT(Init, PE_Init),
	    HLL_EXPORT(Update, PartsEngine_Update),
	    HLL_TODO_EXPORT(UpdateGUIController, PartsEngine_UpdateGUIController),
	    HLL_EXPORT(GetFreeSystemPartsNumber, PE_GetFreeNumber),
	    // FIXME: what is the difference?
	    HLL_EXPORT(GetFreeSystemPartsNumberNotSaved, PE_GetFreeNumber),
	    HLL_EXPORT(IsExistParts, PE_IsExist),
	    HLL_EXPORT(SetPartsCG, PE_SetPartsCG),
	    HLL_EXPORT(GetPartsCGName, PE_GetPartsCGName),
	    HLL_EXPORT(SetPartsCGSurfaceArea, PE_SetPartsCGSurfaceArea),
	    HLL_EXPORT(SetLoopCG, PE_SetLoopCG),
	    HLL_EXPORT(SetLoopCGSurfaceArea, PE_SetLoopCGSurfaceArea),
	    HLL_EXPORT(SetText, PE_SetText),
	    HLL_EXPORT(AddPartsText, PE_AddPartsText),
	    HLL_TODO_EXPORT(DeletePartsTopTextLine, PartsEngine_DeletePartsTopTextLine),
	    HLL_EXPORT(SetPartsTextSurfaceArea, PE_SetPartsTextSurfaceArea),
	    HLL_TODO_EXPORT(SetPartsTextHighlight, PartsEngine_SetPartsTextHighlight),
	    HLL_TODO_EXPORT(AddPartsTextHighlight, PartsEngine_AddPartsTextHighlight),
	    HLL_TODO_EXPORT(ClearPartsTextHighlight, PartsEngine_ClearPartsTextHighlight),
	    HLL_TODO_EXPORT(SetPartsTextCountReturn, PartsEngine_SetPartsTextCountReturn),
	    HLL_TODO_EXPORT(GetPartsTextCountReturn, PartsEngine_GetPartsTextCountReturn),
	    HLL_EXPORT(SetFont, PE_SetFont),
	    HLL_EXPORT(SetPartsFontType, PE_SetPartsFontType),
	    HLL_EXPORT(SetPartsFontSize, PE_SetPartsFontSize),
	    HLL_EXPORT(SetPartsFontColor, PE_SetPartsFontColor),
	    HLL_EXPORT(SetPartsFontBoldWeight, PE_SetPartsFontBoldWeight),
	    HLL_EXPORT(SetPartsFontEdgeColor, PE_SetPartsFontEdgeColor),
	    HLL_EXPORT(SetPartsFontEdgeWeight, PE_SetPartsFontEdgeWeight),
	    HLL_EXPORT(SetTextCharSpace, PE_SetTextCharSpace),
	    HLL_EXPORT(SetTextLineSpace, PE_SetTextLineSpace),
	    HLL_EXPORT(SetHGaugeCG, PE_SetHGaugeCG),
	    HLL_EXPORT(SetHGaugeRate, PE_SetHGaugeRate_int),
	    HLL_EXPORT(SetVGaugeCG, PE_SetVGaugeCG),
	    HLL_EXPORT(SetVGaugeRate, PE_SetVGaugeRate_int),
	    HLL_EXPORT(SetHGaugeSurfaceArea, PE_SetHGaugeSurfaceArea),
	    HLL_EXPORT(SetVGaugeSurfaceArea, PE_SetVGaugeSurfaceArea),
	    HLL_EXPORT(SetNumeralCG, PE_SetNumeralCG),
	    HLL_EXPORT(SetNumeralLinkedCGNumberWidthWidthList, PE_SetNumeralLinkedCGNumberWidthWidthList),
	    HLL_TODO_EXPORT(SetNumeralFont, PartsEngine_SetNumeralFont),
	    HLL_EXPORT(SetNumeralNumber, PE_SetNumeralNumber),
	    HLL_EXPORT(SetNumeralShowComma, PE_SetNumeralShowComma),
	    HLL_EXPORT(SetNumeralSpace, PE_SetNumeralSpace),
	    HLL_EXPORT(SetNumeralLength, PE_SetNumeralLength),
	    HLL_EXPORT(SetNumeralSurfaceArea, PE_SetNumeralSurfaceArea),
	    HLL_EXPORT(SetPartsRectangleDetectionSize, PE_SetPartsRectangleDetectionSize),
	    HLL_TODO_EXPORT(SetPartsRectangleDetectionSurfaceArea, PartsEngine_SetPartsRectangleDetectionSurfaceArea),
	    HLL_EXPORT(SetPartsCGDetectionSize, PE_SetPartsCGDetectionSize),
	    HLL_TODO_EXPORT(SetPartsCGDetectionSurfaceArea, PartsEngine_SetPartsCGDetectionSurfaceArea),
	    HLL_EXPORT(SetPartsFlash, PE_SetPartsFlash),
	    HLL_EXPORT(IsPartsFlashEnd, PE_IsPartsFlashEnd),
	    HLL_EXPORT(GetPartsFlashCurrentFrameNumber, PE_GetPartsFlashCurrentFrameNumber),
	    HLL_EXPORT(BackPartsFlashBeginFrame, PE_BackPartsFlashBeginFrame),
	    HLL_EXPORT(StepPartsFlashFinalFrame, PE_StepPartsFlashFinalFrame),
	    HLL_TODO_EXPORT(SetPartsFlashSurfaceArea, PE_SetPartsFlashSurfaceArea),
	    HLL_EXPORT(SetPartsFlashAndStop, PE_SetPartsFlashAndStop),
	    HLL_EXPORT(StopPartsFlash, PE_StopPartsFlash),
	    HLL_EXPORT(StartPartsFlash, PE_StartPartsFlash),
	    HLL_EXPORT(GoFramePartsFlash, PE_GoFramePartsFlash),
	    HLL_EXPORT(GetPartsFlashEndFrame, PE_GetPartsFlashEndFrame),
	    HLL_EXPORT(ExistsFlashFile, PE_ExistsFlashFile),
	    HLL_EXPORT(ClearPartsConstructionProcess, PE_ClearPartsConstructionProcess),
	    HLL_EXPORT(AddCreateToPartsConstructionProcess, PE_AddCreateToPartsConstructionProcess),
	    HLL_EXPORT(AddCreatePixelOnlyToPartsConstructionProcess, PE_AddCreatePixelOnlyToPartsConstructionProcess),
	    HLL_EXPORT(AddCreateCGToProcess, PE_AddCreateCGToProcess),
	    HLL_EXPORT(AddFillToPartsConstructionProcess, PE_AddFillToPartsConstructionProcess),
	    HLL_EXPORT(AddFillAlphaColorToPartsConstructionProcess, PE_AddFillAlphaColorToPartsConstructionProcess),
	    HLL_EXPORT(AddFillAMapToPartsConstructionProcess, PE_AddFillAMapToPartsConstructionProcess),
	    HLL_EXPORT(AddFillWithAlphaToPartsConstructionProcess, PE_AddFillWithAlphaToPartsConstructionProcess),
	    HLL_TODO_EXPORT(AddFillGradationHorizonToPartsConstructionProcess, PartsEngine_AddFillGradationHorizonToPartsConstructionProcess),
	    HLL_EXPORT(AddDrawRectToPartsConstructionProcess, PE_AddDrawRectToPartsConstructionProcess),
	    HLL_EXPORT(AddDrawCutCGToPartsConstructionProcess, PartsEngine_AddDrawCutCGToPartsConstructionProcess_old),
	    HLL_EXPORT(AddCopyCutCGToPartsConstructionProcess, PartsEngine_AddCopyCutCGToPartsConstructionProcess_old),
	    HLL_EXPORT(AddGrayFilterToPartsConstructionProcess, PE_AddGrayFilterToPartsConstructionProcess),
	    HLL_TODO_EXPORT(AddAddFilterToPartsConstructionProcess, PartsEngine_AddAddFilterToPartsConstructionProcess),
	    HLL_TODO_EXPORT(AddMulFilterToPartsConstructionProcess, PartsEngine_AddMulFilterToPartsConstructionProcess),
	    HLL_EXPORT(BuildPartsConstructionProcess, PE_BuildPartsConstructionProcess),
	    HLL_EXPORT(AddDrawTextToPartsConstructionProcess, PE_AddDrawTextToPartsConstructionProcess),
	    HLL_EXPORT(AddCopyTextToPartsConstructionProcess, PE_AddCopyTextToPartsConstructionProcess),
	    HLL_EXPORT(SetPartsConstructionSurfaceArea, PE_SetPartsConstructionSurfaceArea),
	    HLL_EXPORT(ReleaseParts, PE_ReleaseParts),
	    HLL_EXPORT(ReleaseAllParts, PE_ReleaseAllParts),
	    HLL_EXPORT(ReleaseAllPartsWithoutSystem, PE_ReleaseAllPartsWithoutSystem),
	    HLL_EXPORT(SetPos, PE_SetPos),
	    HLL_EXPORT(SetZ, PE_SetZ),
	    HLL_EXPORT(SetShow, PE_SetShow),
	    HLL_EXPORT(SetAlpha, PE_SetAlpha),
	    HLL_EXPORT(SetPartsDrawFilter, PE_SetPartsDrawFilter),
	    HLL_EXPORT(SetAddColor, PE_SetAddColor),
	    HLL_EXPORT(SetMultiplyColor, PE_SetMultiplyColor),
	    HLL_EXPORT(SetPassCursor, PE_SetPassCursor),
	    HLL_EXPORT(SetClickable, PE_SetClickable),
	    HLL_EXPORT(SetSpeedupRateByMessageSkip, PE_SetSpeedupRateByMessageSkip),
	    HLL_TODO_EXPORT(SetResetTimerByChangeInputStatus, PartsEngine_SetResetTimerByChangeInputStatus),
	    HLL_EXPORT(GetPartsX, PE_GetPartsX),
	    HLL_EXPORT(GetPartsY, PE_GetPartsY),
	    HLL_EXPORT(GetPartsZ, PE_GetPartsZ),
	    HLL_EXPORT(GetPartsShow, PE_GetPartsShow),
	    HLL_EXPORT(GetPartsAlpha, PE_GetPartsAlpha),
	    HLL_TODO_EXPORT(GetAddColor, PartsEngine_GetAddColor),
	    HLL_TODO_EXPORT(GetMultiplyColor, PartsEngine_GetMultiplyColor),
	    HLL_EXPORT(GetPartsPassCursor, PE_GetPartsPassCursor),
	    HLL_EXPORT(GetPartsClickable, PE_GetPartsClickable),
	    HLL_TODO_EXPORT(GetPartsSpeedupRateByMessageSkip, PartsEngine_GetPartsSpeedupRateByMessageSkip),
	    HLL_TODO_EXPORT(GetResetTimerByChangeInputStatus, PartsEngine_GetResetTimerByChangeInputStatus),
	    HLL_EXPORT(GetPartsUpperLeftPosX, PE_GetPartsUpperLeftPosX),
	    HLL_EXPORT(GetPartsUpperLeftPosY, PE_GetPartsUpperLeftPosY),
	    HLL_EXPORT(GetPartsWidth, PE_GetPartsWidth),
	    HLL_EXPORT(GetPartsHeight, PE_GetPartsHeight),
	    HLL_EXPORT(SetInputState, PE_SetInputState),
	    HLL_EXPORT(GetInputState, PE_GetInputState),
	    HLL_EXPORT(SetPartsOriginPosMode, PE_SetPartsOriginPosMode),
	    HLL_EXPORT(GetPartsOriginPosMode, PE_GetPartsOriginPosMode),
	    HLL_EXPORT(SetParentPartsNumber, PE_SetParentPartsNumber),
	    HLL_EXPORT(SetPartsGroupNumber, PE_SetPartsGroupNumber),
	    HLL_EXPORT(SetPartsGroupDecideOnCursor, PE_SetPartsGroupDecideOnCursor),
	    HLL_EXPORT(SetPartsGroupDecideClick, PE_SetPartsGroupDecideClick),
	    HLL_EXPORT(SetOnCursorShowLinkPartsNumber, PE_SetOnCursorShowLinkPartsNumber),
	    HLL_EXPORT(SetPartsMessageWindowShowLink, PE_SetPartsMessageWindowShowLink),
	    HLL_EXPORT(GetPartsMessageWindowShowLink, PE_GetPartsMessageWindowShowLink),
	    HLL_EXPORT(AddMotionPos, PE_AddMotionPos_curve),
	    HLL_EXPORT(AddMotionAlpha, PE_AddMotionAlpha_curve),
	    HLL_TODO_EXPORT(AddMotionCG, PartsEngine_AddMotionCG),
	    HLL_EXPORT(AddMotionHGaugeRate, PE_AddMotionHGaugeRate_curve),
	    HLL_EXPORT(AddMotionVGaugeRate, PE_AddMotionVGaugeRate_curve),
	    HLL_EXPORT(AddMotionNumeralNumber, PE_AddMotionNumeralNumber_curve),
	    HLL_EXPORT(AddMotionMagX, PE_AddMotionMagX_curve),
	    HLL_EXPORT(AddMotionMagY, PE_AddMotionMagY_curve),
	    HLL_EXPORT(AddMotionRotateX, PE_AddMotionRotateX_curve),
	    HLL_EXPORT(AddMotionRotateY, PE_AddMotionRotateY_curve),
	    HLL_EXPORT(AddMotionRotateZ, PE_AddMotionRotateZ_curve),
	    HLL_EXPORT(AddMotionVibrationSize, PE_AddMotionVibrationSize),
	    HLL_EXPORT(AddWholeMotionVibrationSize, PE_AddWholeMotionVibrationSize),
	    HLL_EXPORT(AddMotionSound, PE_AddMotionSound),
	    HLL_TODO_EXPORT(SetSoundNumber, PartsEngine_SetSoundNumber),
	    HLL_TODO_EXPORT(GetSoundNumber, PartsEngine_GetSoundNumber),
	    HLL_EXPORT(SetClickMissSoundNumber, PE_SetClickMissSoundNumber),
	    HLL_TODO_EXPORT(GetClickMissSoundNumber, PartsEngine_GetClickMissSoundNumber),
	    HLL_EXPORT(BeginMotion, PE_BeginMotion),
	    HLL_EXPORT(EndMotion, PE_EndMotion),
	    HLL_EXPORT(IsMotion, PE_IsMotion),
	    HLL_EXPORT(SeekEndMotion, PE_SeekEndMotion),
	    HLL_EXPORT(UpdateMotionTime, PE_UpdateMotionTime),
	    HLL_EXPORT(BeginInput, PE_BeginInput),
	    HLL_EXPORT(EndInput, PE_EndInput),
	    HLL_EXPORT(GetClickPartsNumber, PE_GetClickPartsNumber),
	    HLL_TODO_EXPORT(GetFocusPartsNumber, PartsEngine_GetFocusPartsNumber),
	    HLL_TODO_EXPORT(SetFocusPartsNumber, PartsEngine_SetFocusPartsNumber),
	    HLL_TODO_EXPORT(PushGUIController, PartsEngine_PushGUIController),
	    HLL_TODO_EXPORT(PopGUIController, PartsEngine_PopGUIController),
	    HLL_EXPORT(SetPartsMagX, PE_SetPartsMagX),
	    HLL_EXPORT(SetPartsMagY, PE_SetPartsMagY),
	    HLL_EXPORT(SetPartsRotateX, PE_SetPartsRotateX),
	    HLL_EXPORT(SetPartsRotateY, PE_SetPartsRotateY),
	    HLL_EXPORT(SetPartsRotateZ, PE_SetPartsRotateZ),
	    HLL_EXPORT(SetPartsAlphaClipperPartsNumber, PE_SetPartsAlphaClipperPartsNumber),
	    HLL_EXPORT(SetPartsPixelDecide, PE_SetPartsPixelDecide),
	    HLL_EXPORT(IsCursorIn, PE_IsCursorIn),
	    HLL_EXPORT(SetThumbnailReductionSize, PE_SetThumbnailReductionSize),
	    HLL_EXPORT(SetThumbnailMode, PE_SetThumbnailMode),
	    HLL_EXPORT(Save, PE_Save),
	    HLL_EXPORT(SaveWithoutHideParts, PE_SaveWithoutHideParts),
	    HLL_EXPORT(Load, PE_Load),
	    // Rance 9
	    HLL_EXPORT(PartsFunc, PartsEngine_PartsFunc),
	    HLL_EXPORT(Release, PE_ReleaseParts),
	    HLL_TODO_EXPORT(ReleaseAll, PartsEngine_ReleaseAll),
	    HLL_EXPORT(ReleaseAllWithoutSystem, PE_ReleaseAllWithoutSystem),
	    HLL_EXPORT(GetFreeNumber, PE_GetFreeNumber),
	    HLL_EXPORT(IsExist, PE_IsExist),
	    HLL_EXPORT(AddController, PE_AddController),
	    HLL_EXPORT(RemoveController, PE_RemoveController),
	    HLL_EXPORT(UpdateComponent, PartsEngine_Update),
	    HLL_EXPORT(Parts_SetThumbnailReductionSize, PE_SetThumbnailReductionSize),
	    HLL_EXPORT(Parts_SetThumbnailMode, PE_SetThumbnailMode),
	    HLL_EXPORT(GetClickNumber, PE_GetClickPartsNumber),
	    HLL_EXPORT(StopSoundWithoutSystemSound, PartsEngine_StopSoundWithoutSystemSound),
	    HLL_TODO_EXPORT(ReleaseActivity, PartsEngine_ReleaseActivity),
	    HLL_TODO_EXPORT(CrateActivityBinary, PartsEngine_CrateActivityBinary),
	    HLL_TODO_EXPORT(ReadActivityBinary, PartsEngine_ReadActivityBinary),
	    HLL_EXPORT(ReleaseMessage, PE_ReleaseMessage),
	    HLL_EXPORT(PopMessage, PE_PopMessage),
	    HLL_EXPORT(GetMessagePartsNumber, PE_GetMessagePartsNumber),
	    HLL_EXPORT(GetMessageDelegateIndex, PE_GetMessageDelegateIndex),
	    HLL_EXPORT(GetDelegateIndex, PE_GetDelegateIndex),
	    HLL_EXPORT(GetMessageType, PE_GetMessageType),
	    HLL_EXPORT(GetMessageVariableCount, PE_GetMessageVariableCount),
	    HLL_EXPORT(GetMessageVariableType, PE_GetMessageVariableType),
	    HLL_EXPORT(GetMessageVariableInt, PE_GetMessageVariableInt),
	    HLL_EXPORT(GetMessageVariableFloat, PE_GetMessageVariableFloat),
	    HLL_EXPORT(GetMessageVariableBool, PE_GetMessageVariableBool),
	    HLL_EXPORT(GetMessageVariableString, PE_GetMessageVariableString),
	    HLL_EXPORT(SetDelegateIndex, PE_SetDelegateIndex),
	    HLL_TODO_EXPORT(SetFocus, PartsEngine_SetFocus),
	    HLL_TODO_EXPORT(IsFocus, PartsEngine_IsFocus),
	    HLL_EXPORT(SetComponentType, PE_SetComponentType),
	    HLL_EXPORT(GetComponentType, PE_GetComponentType),
	    HLL_EXPORT(SetComponentPos, PartsEngine_SetComponentPos),
	    HLL_EXPORT(SetComponentPosZ, PE_SetZ),
	    HLL_EXPORT(GetComponentPosX, PartsEngine_Parts_GetComponentPosX),
	    HLL_EXPORT(GetComponentPosY, PartsEngine_GetComponentPosY),
	    HLL_EXPORT(GetComponentPosZ, PE_GetPartsZ),
	    HLL_EXPORT(Parts_GetPartsUpperLeftPosX, PartsEngine_Parts_GetPartsUpperLeftPosX),
	    HLL_EXPORT(Parts_GetPartsUpperLeftPosY, PartsEngine_Parts_GetPartsUpperLeftPosY),
	    HLL_EXPORT(SetComponentOriginPosMode, PE_SetPartsOriginPosMode),
	    HLL_EXPORT(GetComponentOriginPosMode, PE_GetPartsOriginPosMode),
	    HLL_TODO_EXPORT(GetComponentWidth, PartsEngine_GetComponentWidth),
	    HLL_TODO_EXPORT(GetComponentHeight, PartsEngine_GetComponentHeight),
	    HLL_EXPORT(Parts_GetPartsWidth, PE_GetPartsWidth),
	    HLL_EXPORT(Parts_GetPartsHeight, PE_GetPartsHeight),
	    HLL_EXPORT(SetComponentShow, PE_SetShow),
	    HLL_EXPORT(IsComponentShow, PE_GetPartsShow),
	    HLL_EXPORT(SetComponentMessageWindowShowLink, PE_SetPartsMessageWindowShowLink),
	    HLL_EXPORT(IsComponentMessageWindowShowLink, PE_GetPartsMessageWindowShowLink),
	    HLL_EXPORT(SetComponentAlpha, PE_SetAlpha),
	    HLL_EXPORT(GetComponentAlpha, PE_GetPartsAlpha),
	    HLL_EXPORT(SetComponentAddColor, PE_SetAddColor),
	    HLL_TODO_EXPORT(GetComponentAddColorR, PartsEngine_GetComponentAddColorR),
	    HLL_TODO_EXPORT(GetComponentAddColorG, PartsEngine_GetComponentAddColorG),
	    HLL_TODO_EXPORT(GetComponentAddColorB, PartsEngine_GetComponentAddColorB),
	    HLL_EXPORT(SetComponentMulColor, PE_SetMultiplyColor),
	    HLL_TODO_EXPORT(GetComponentMulColorR, PartsEngine_GetComponentMulColorR),
	    HLL_TODO_EXPORT(GetComponentMulColorG, PartsEngine_GetComponentMulColorG),
	    HLL_TODO_EXPORT(GetComponentMulColorB, PartsEngine_GetComponentMulColorB),
	    HLL_EXPORT(SetComponentDrawFilter, PE_SetPartsDrawFilter),
	    HLL_TODO_EXPORT(GetComponentDrawFilter, PartsEngine_GetComponentDrawFilter),
	    HLL_EXPORT(SetComponentMagX, PE_SetPartsMagX),
	    HLL_EXPORT(SetComponentMagY, PE_SetPartsMagY),
	    HLL_TODO_EXPORT(GetComponentMagX, PartsEngine_GetComponentMagX),
	    HLL_TODO_EXPORT(GetComponentMagY, PartsEngine_GetComponentMagY),
	    HLL_EXPORT(SetComponentRotateX, PE_SetPartsRotateX),
	    HLL_EXPORT(SetComponentRotateY, PE_SetPartsRotateY),
	    HLL_EXPORT(SetComponentRotateZ, PE_SetPartsRotateZ),
	    HLL_TODO_EXPORT(GetComponentRotateX, PartsEngine_GetComponentRotateX),
	    HLL_TODO_EXPORT(GetComponentRotateY, PartsEngine_GetComponentRotateY),
	    HLL_EXPORT(GetComponentRotateZ, PE_GetPartsRotateZ),
	    HLL_EXPORT(SetComponentMargin, PE_SetComponentMargin),
	    HLL_EXPORT(GetComponentMarginTop, PE_GetComponentMarginTop),
	    HLL_EXPORT(GetComponentMarginBottom, PE_GetComponentMarginBottom),
	    HLL_EXPORT(GetComponentMarginLeft, PE_GetComponentMarginLeft),
	    HLL_EXPORT(GetComponentMarginRight, PE_GetComponentMarginRight),
	    HLL_EXPORT(SetComponentAlphaClipper, PE_SetPartsAlphaClipperPartsNumber),
	    HLL_TODO_EXPORT(GetComponentAlphaClipper, PartsEngine_GetComponentAlphaClipper),
	    HLL_TODO_EXPORT(SetComponentTextureFilterType, PartsEngine_SetComponentTextureFilterType),
	    HLL_TODO_EXPORT(GetComponentTextureFilterType, PartsEngine_GetComponentTextureFilterType),
	    HLL_TODO_EXPORT(SetComponentMipmap, PartsEngine_SetComponentMipmap),
	    HLL_TODO_EXPORT(IsComponentMipmap, PartsEngine_IsComponentMipmap),
	    HLL_EXPORT(SetComponentSpeedupRateByMessageSkip, PE_SetSpeedupRateByMessageSkip),
	    HLL_TODO_EXPORT(GetComponentSpeedupRateByMessageSkip, PartsEngine_GetComponentSpeedupRateByMessageSkip),
	    HLL_EXPORT(AddComponentMotionPos, PartsEngine_AddComponentMotionPos),
	    HLL_EXPORT(AddComponentMotionAlpha, PE_AddMotionAlpha_curve),
	    HLL_TODO_EXPORT(AddComponentMotionCG, PartsEngine_AddComponentMotionCG),
	    HLL_TODO_EXPORT(AddComponentMotionCGTermination, PartsEngine_AddComponentMotionCGTermination),
	    HLL_EXPORT(AddComponentMotionHGaugeRate, PE_AddMotionHGaugeRate_curve),
	    HLL_EXPORT(AddComponentMotionVGaugeRate, PE_AddMotionVGaugeRate_curve),
	    HLL_EXPORT(AddComponentMotionNumeralNumber, PE_AddMotionNumeralNumber_curve),
	    HLL_EXPORT(AddComponentMotionMagX, PE_AddMotionMagX_curve),
	    HLL_EXPORT(AddComponentMotionMagY, PE_AddMotionMagY_curve),
	    HLL_EXPORT(AddComponentMotionRotateX, PE_AddMotionRotateX_curve),
	    HLL_EXPORT(AddComponentMotionRotateY, PE_AddMotionRotateY_curve),
	    HLL_EXPORT(AddComponentMotionRotateZ, PE_AddMotionRotateZ_curve),
	    HLL_EXPORT(AddComponentMotionVibrationSize, PE_AddMotionVibrationSize),
	    HLL_TODO_EXPORT(SuspendBuildView, PartsEngine_SuspendBuildView),
	    HLL_TODO_EXPORT(SuspendBuildViewAt, PartsEngine_SuspendBuildViewAt),
	    HLL_TODO_EXPORT(ResumeBuildView, PartsEngine_ResumeBuildView),
	    HLL_TODO_EXPORT(SetButtonSize, PartsEngine_SetButtonSize),
	    HLL_TODO_EXPORT(SetButtonDrag, PartsEngine_SetButtonDrag),
	    HLL_TODO_EXPORT(IsButtonDrag, PartsEngine_IsButtonDrag),
	    HLL_TODO_EXPORT(SetButtonEnable, PartsEngine_SetButtonEnable),
	    HLL_TODO_EXPORT(IsButtonEnable, PartsEngine_IsButtonEnable),
	    HLL_TODO_EXPORT(SetButtonPixelDecide, PartsEngine_SetButtonPixelDecide),
	    HLL_TODO_EXPORT(IsButtonPixelDecide, PartsEngine_IsButtonPixelDecide),
	    HLL_TODO_EXPORT(SetButtonColor, PartsEngine_SetButtonColor),
	    HLL_TODO_EXPORT(GetButtonR, PartsEngine_GetButtonR),
	    HLL_TODO_EXPORT(GetButtonG, PartsEngine_GetButtonG),
	    HLL_TODO_EXPORT(GetButtonB, PartsEngine_GetButtonB),
	    HLL_TODO_EXPORT(SetButtonFontProperty, PartsEngine_SetButtonFontProperty),
	    HLL_TODO_EXPORT(GetButtonFontProperty, PartsEngine_GetButtonFontProperty),
	    HLL_TODO_EXPORT(SetButtonOnCursorSoundNumber, PartsEngine_SetButtonOnCursorSoundNumber),
	    HLL_TODO_EXPORT(SetButtonClickSoundNumber, PartsEngine_SetButtonClickSoundNumber),
	    HLL_TODO_EXPORT(GetButtonOnCursorSoundNumber, PartsEngine_GetButtonOnCursorSoundNumber),
	    HLL_TODO_EXPORT(GetButtonClickSoundNumber, PartsEngine_GetButtonClickSoundNumber),
	    HLL_TODO_EXPORT(SetButtonCGName, PartsEngine_SetButtonCGName),
	    HLL_TODO_EXPORT(GetButtonCGName, PartsEngine_GetButtonCGName),
	    HLL_TODO_EXPORT(SetButtonText, PartsEngine_SetButtonText),
	    HLL_TODO_EXPORT(GetButtonText, PartsEngine_GetButtonText),
	    HLL_TODO_EXPORT(SetCheckBoxSize, PartsEngine_SetCheckBoxSize),
	    HLL_TODO_EXPORT(SetCheckBoxDrag, PartsEngine_SetCheckBoxDrag),
	    HLL_TODO_EXPORT(IsCheckBoxDrag, PartsEngine_IsCheckBoxDrag),
	    HLL_TODO_EXPORT(CheckBoxChecked, PartsEngine_CheckBoxChecked),
	    HLL_TODO_EXPORT(IsCheckBoxChecked, PartsEngine_IsCheckBoxChecked),
	    HLL_TODO_EXPORT(SetCheckBoxColor, PartsEngine_SetCheckBoxColor),
	    HLL_TODO_EXPORT(GetCheckBoxR, PartsEngine_GetCheckBoxR),
	    HLL_TODO_EXPORT(GetCheckBoxG, PartsEngine_GetCheckBoxG),
	    HLL_TODO_EXPORT(GetCheckBoxB, PartsEngine_GetCheckBoxB),
	    HLL_TODO_EXPORT(SetCheckBoxFontProperty, PartsEngine_SetCheckBoxFontProperty),
	    HLL_TODO_EXPORT(GetCheckBoxFontProperty, PartsEngine_GetCheckBoxFontProperty),
	    HLL_TODO_EXPORT(SetCheckBoxOnCursorSoundNumber, PartsEngine_SetCheckBoxOnCursorSoundNumber),
	    HLL_TODO_EXPORT(SetCheckBoxClickSoundNumber, PartsEngine_SetCheckBoxClickSoundNumber),
	    HLL_TODO_EXPORT(GetCheckBoxOnCursorSoundNumber, PartsEngine_GetCheckBoxOnCursorSoundNumber),
	    HLL_TODO_EXPORT(GetCheckBoxClickSoundNumber, PartsEngine_GetCheckBoxClickSoundNumber),
	    HLL_TODO_EXPORT(SetCheckBoxCGName, PartsEngine_SetCheckBoxCGName),
	    HLL_TODO_EXPORT(GetCheckBoxCGName, PartsEngine_GetCheckBoxCGName),
	    HLL_TODO_EXPORT(SetCheckBoxText, PartsEngine_SetCheckBoxText),
	    HLL_TODO_EXPORT(GetCheckBoxText, PartsEngine_GetCheckBoxText),
	    HLL_TODO_EXPORT(SetVScrollbarOnCursorSoundNumber, PartsEngine_SetVScrollbarOnCursorSoundNumber),
	    HLL_TODO_EXPORT(SetVScrollbarClickSoundNumber, PartsEngine_SetVScrollbarClickSoundNumber),
	    HLL_TODO_EXPORT(GetVScrollbarOnCursorSoundNumber, PartsEngine_GetVScrollbarOnCursorSoundNumber),
	    HLL_TODO_EXPORT(GetVScrollbarClickSoundNumber, PartsEngine_GetVScrollbarClickSoundNumber),
	    HLL_TODO_EXPORT(SetVScrollbarSize, PartsEngine_SetVScrollbarSize),
	    HLL_TODO_EXPORT(SetVScrollbarUpHeight, PartsEngine_SetVScrollbarUpHeight),
	    HLL_TODO_EXPORT(SetVScrollbarDownHeight, PartsEngine_SetVScrollbarDownHeight),
	    HLL_TODO_EXPORT(GetVScrollbarUpHeight, PartsEngine_GetVScrollbarUpHeight),
	    HLL_TODO_EXPORT(GetVScrollbarDownHeight, PartsEngine_GetVScrollbarDownHeight),
	    HLL_TODO_EXPORT(SetVScrollbarTotalSize, PartsEngine_SetVScrollbarTotalSize),
	    HLL_TODO_EXPORT(SetVScrollbarViewSize, PartsEngine_SetVScrollbarViewSize),
	    HLL_TODO_EXPORT(SetVScrollbarScrollPos, PartsEngine_SetVScrollbarScrollPos),
	    HLL_TODO_EXPORT(SetVScrollbarScrollRate, PartsEngine_SetVScrollbarScrollRate),
	    HLL_TODO_EXPORT(SetVScrollbarMoveSizeByButton, PartsEngine_SetVScrollbarMoveSizeByButton),
	    HLL_TODO_EXPORT(GetVScrollbarTotalSize, PartsEngine_GetVScrollbarTotalSize),
	    HLL_TODO_EXPORT(GetVScrollbarViewSize, PartsEngine_GetVScrollbarViewSize),
	    HLL_TODO_EXPORT(GetVScrollbarScrollPos, PartsEngine_GetVScrollbarScrollPos),
	    HLL_TODO_EXPORT(GetVScrollbarScrollRate, PartsEngine_GetVScrollbarScrollRate),
	    HLL_TODO_EXPORT(GetVScrollbarMoveSizeByButton, PartsEngine_GetVScrollbarMoveSizeByButton),
	    HLL_TODO_EXPORT(SetVScrollbarCGName, PartsEngine_SetVScrollbarCGName),
	    HLL_TODO_EXPORT(GetVScrollbarCGName, PartsEngine_GetVScrollbarCGName),
	    HLL_TODO_EXPORT(SetHScrollbarOnCursorSoundNumber, PartsEngine_SetHScrollbarOnCursorSoundNumber),
	    HLL_TODO_EXPORT(SetHScrollbarClickSoundNumber, PartsEngine_SetHScrollbarClickSoundNumber),
	    HLL_TODO_EXPORT(GetHScrollbarOnCursorSoundNumber, PartsEngine_GetHScrollbarOnCursorSoundNumber),
	    HLL_TODO_EXPORT(GetHScrollbarClickSoundNumber, PartsEngine_GetHScrollbarClickSoundNumber),
	    HLL_TODO_EXPORT(SetHScrollbarSize, PartsEngine_SetHScrollbarSize),
	    HLL_TODO_EXPORT(SetHScrollbarLeftWidth, PartsEngine_SetHScrollbarLeftWidth),
	    HLL_TODO_EXPORT(SetHScrollbarRightWidth, PartsEngine_SetHScrollbarRightWidth),
	    HLL_TODO_EXPORT(GetHScrollbarLeftWidth, PartsEngine_GetHScrollbarLeftWidth),
	    HLL_TODO_EXPORT(GetHScrollbarRightWidth, PartsEngine_GetHScrollbarRightWidth),
	    HLL_TODO_EXPORT(SetHScrollbarTotalSize, PartsEngine_SetHScrollbarTotalSize),
	    HLL_TODO_EXPORT(SetHScrollbarViewSize, PartsEngine_SetHScrollbarViewSize),
	    HLL_TODO_EXPORT(SetHScrollbarScrollPos, PartsEngine_SetHScrollbarScrollPos),
	    HLL_TODO_EXPORT(SetHScrollbarScrollRate, PartsEngine_SetHScrollbarScrollRate),
	    HLL_TODO_EXPORT(SetHScrollbarMoveSizeByButton, PartsEngine_SetHScrollbarMoveSizeByButton),
	    HLL_TODO_EXPORT(GetHScrollbarTotalSize, PartsEngine_GetHScrollbarTotalSize),
	    HLL_TODO_EXPORT(GetHScrollbarViewSize, PartsEngine_GetHScrollbarViewSize),
	    HLL_TODO_EXPORT(GetHScrollbarScrollPos, PartsEngine_GetHScrollbarScrollPos),
	    HLL_TODO_EXPORT(GetHScrollbarScrollRate, PartsEngine_GetHScrollbarScrollRate),
	    HLL_TODO_EXPORT(GetHScrollbarMoveSizeByButton, PartsEngine_GetHScrollbarMoveSizeByButton),
	    HLL_TODO_EXPORT(SetHScrollbarCGName, PartsEngine_SetHScrollbarCGName),
	    HLL_TODO_EXPORT(GetHScrollbarCGName, PartsEngine_GetHScrollbarCGName),
	    HLL_TODO_EXPORT(SetTextBoxSize, PartsEngine_SetTextBoxSize),
	    HLL_TODO_EXPORT(SetTextBoxFontProperty, PartsEngine_SetTextBoxFontProperty),
	    HLL_TODO_EXPORT(GetTextBoxFontProperty, PartsEngine_GetTextBoxFontProperty),
	    HLL_TODO_EXPORT(SetTextBoxText, PartsEngine_SetTextBoxText),
	    HLL_TODO_EXPORT(GetTextBoxText, PartsEngine_GetTextBoxText),
	    HLL_TODO_EXPORT(SetTextBoxMaxTextLength, PartsEngine_SetTextBoxMaxTextLength),
	    HLL_TODO_EXPORT(GetTextBoxMaxTextLength, PartsEngine_GetTextBoxMaxTextLength),
	    HLL_TODO_EXPORT(SetTextBoxSelectColor, PartsEngine_SetTextBoxSelectColor),
	    HLL_TODO_EXPORT(GetTextBoxSelectR, PartsEngine_GetTextBoxSelectR),
	    HLL_TODO_EXPORT(GetTextBoxSelectG, PartsEngine_GetTextBoxSelectG),
	    HLL_TODO_EXPORT(GetTextBoxSelectB, PartsEngine_GetTextBoxSelectB),
	    HLL_TODO_EXPORT(SetTextBoxCGName, PartsEngine_SetTextBoxCGName),
	    HLL_TODO_EXPORT(GetTextBoxCGName, PartsEngine_GetTextBoxCGName),
	    HLL_TODO_EXPORT(OpenTextBoxIME, PartsEngine_OpenTextBoxIME),
	    HLL_TODO_EXPORT(CloseTextBoxIME, PartsEngine_CloseTextBoxIME),
	    HLL_TODO_EXPORT(SetListBoxSize, PartsEngine_SetListBoxSize),
	    HLL_TODO_EXPORT(SetListBoxLineHeight, PartsEngine_SetListBoxLineHeight),
	    HLL_TODO_EXPORT(GetListBoxLineHeight, PartsEngine_GetListBoxLineHeight),
	    HLL_TODO_EXPORT(SetListBoxMargin, PartsEngine_SetListBoxMargin),
	    HLL_TODO_EXPORT(GetListBoxWidthMargin, PartsEngine_GetListBoxWidthMargin),
	    HLL_TODO_EXPORT(GetListBoxHeightMargin, PartsEngine_GetListBoxHeightMargin),
	    HLL_TODO_EXPORT(SetListBoxCGName, PartsEngine_SetListBoxCGName),
	    HLL_TODO_EXPORT(GetListBoxCGName, PartsEngine_GetListBoxCGName),
	    HLL_TODO_EXPORT(SetListBoxScrollPos, PartsEngine_SetListBoxScrollPos),
	    HLL_TODO_EXPORT(GetListBoxScrollPos, PartsEngine_GetListBoxScrollPos),
	    HLL_TODO_EXPORT(AddListBoxItem, PartsEngine_AddListBoxItem),
	    HLL_TODO_EXPORT(InsertListBoxItem, PartsEngine_InsertListBoxItem),
	    HLL_TODO_EXPORT(SetListBoxItem, PartsEngine_SetListBoxItem),
	    HLL_TODO_EXPORT(GetListBoxItemCount, PartsEngine_GetListBoxItemCount),
	    HLL_TODO_EXPORT(GetListBoxItem, PartsEngine_GetListBoxItem),
	    HLL_TODO_EXPORT(EraseListBoxItem, PartsEngine_EraseListBoxItem),
	    HLL_TODO_EXPORT(ClearListBoxItem, PartsEngine_ClearListBoxItem),
	    HLL_TODO_EXPORT(GetListBoxOnCursorItemIndex, PartsEngine_GetListBoxOnCursorItemIndex),
	    HLL_TODO_EXPORT(GetListBoxOnCursorItem, PartsEngine_GetListBoxOnCursorItem),
	    HLL_TODO_EXPORT(SetListBoxFontProperty, PartsEngine_SetListBoxFontProperty),
	    HLL_TODO_EXPORT(GetListBoxFontProperty, PartsEngine_GetListBoxFontProperty),
	    HLL_TODO_EXPORT(SetListBoxSelectIndex, PartsEngine_SetListBoxSelectIndex),
	    HLL_TODO_EXPORT(GetListBoxSelectIndex, PartsEngine_GetListBoxSelectIndex),
	    HLL_TODO_EXPORT(SetComboBoxSize, PartsEngine_SetComboBoxSize),
	    HLL_TODO_EXPORT(SetComboBoxTextMargin, PartsEngine_SetComboBoxTextMargin),
	    HLL_TODO_EXPORT(GetComboBoxTextWidthMargin, PartsEngine_GetComboBoxTextWidthMargin),
	    HLL_TODO_EXPORT(GetComboBoxTextHeightMargin, PartsEngine_GetComboBoxTextHeightMargin),
	    HLL_TODO_EXPORT(SetComboBoxCGName, PartsEngine_SetComboBoxCGName),
	    HLL_TODO_EXPORT(GetComboBoxCGName, PartsEngine_GetComboBoxCGName),
	    HLL_TODO_EXPORT(SetComboBoxText, PartsEngine_SetComboBoxText),
	    HLL_TODO_EXPORT(GetComboBoxText, PartsEngine_GetComboBoxText),
	    HLL_TODO_EXPORT(SetComboBoxFontProperty, PartsEngine_SetComboBoxFontProperty),
	    HLL_TODO_EXPORT(GetComboBoxFontProperty, PartsEngine_GetComboBoxFontProperty),
	    HLL_TODO_EXPORT(SetMultiTextBoxSize, PartsEngine_SetMultiTextBoxSize),
	    HLL_TODO_EXPORT(SetMultiTextBoxFontProperty, PartsEngine_SetMultiTextBoxFontProperty),
	    HLL_TODO_EXPORT(GetMultiTextBoxFontProperty, PartsEngine_GetMultiTextBoxFontProperty),
	    HLL_TODO_EXPORT(SetMultiTextBoxText, PartsEngine_SetMultiTextBoxText),
	    HLL_TODO_EXPORT(GetMultiTextBoxText, PartsEngine_GetMultiTextBoxText),
	    HLL_TODO_EXPORT(SetMultiTextBoxMaxTextLength, PartsEngine_SetMultiTextBoxMaxTextLength),
	    HLL_TODO_EXPORT(GetMultiTextBoxMaxTextLength, PartsEngine_GetMultiTextBoxMaxTextLength),
	    HLL_TODO_EXPORT(SetMultiTextBoxSelectColor, PartsEngine_SetMultiTextBoxSelectColor),
	    HLL_TODO_EXPORT(GetMultiTextBoxSelectR, PartsEngine_GetMultiTextBoxSelectR),
	    HLL_TODO_EXPORT(GetMultiTextBoxSelectG, PartsEngine_GetMultiTextBoxSelectG),
	    HLL_TODO_EXPORT(GetMultiTextBoxSelectB, PartsEngine_GetMultiTextBoxSelectB),
	    HLL_TODO_EXPORT(SetMultiTextBoxCGName, PartsEngine_SetMultiTextBoxCGName),
	    HLL_TODO_EXPORT(GetMultiTextBoxCGName, PartsEngine_GetMultiTextBoxCGName),
	    HLL_EXPORT(SetLayoutBoxLayoutType, PE_SetLayoutBoxLayoutType),
	    HLL_EXPORT(GetLayoutBoxLayoutType, PE_GetLayoutBoxLayoutType),
	    HLL_EXPORT(SetLayoutBoxReturn, PE_SetLayoutBoxReturn),
	    HLL_EXPORT(IsLayoutBoxReturn, PE_IsLayoutBoxReturn),
	    HLL_EXPORT(GetLayoutBoxReturnSize, PE_GetLayoutBoxReturnSize),
	    HLL_EXPORT(SetLayoutBoxAlign, PE_SetLayoutBoxAlign),
	    HLL_EXPORT(GetLayoutBoxAlign, PE_GetLayoutBoxAlign),
	    HLL_EXPORT(Parts_SetPartsCG, PE_SetPartsCG),
	    HLL_EXPORT(Parts_GetPartsCGName, PE_GetPartsCGName),
	    HLL_EXPORT(Parts_SetPartsCGSurfaceArea, PE_SetPartsCGSurfaceArea),
	    HLL_EXPORT(Parts_SetLoopCG, PE_SetLoopCG),
	    HLL_EXPORT(Parts_SetLoopCGSurfaceArea, PE_SetLoopCGSurfaceArea),
	    HLL_EXPORT(Parts_SetText, PE_SetText),
	    HLL_EXPORT(Parts_AddPartsText, PE_AddPartsText),
	    HLL_TODO_EXPORT(Parts_DeletePartsTopTextLine, PartsEngine_Parts_DeletePartsTopTextLine),
	    HLL_EXPORT(Parts_SetPartsTextSurfaceArea, PE_SetPartsTextSurfaceArea),
	    HLL_TODO_EXPORT(Parts_SetPartsTextHighlight, PartsEngine_Parts_SetPartsTextHighlight),
	    HLL_TODO_EXPORT(Parts_AddPartsTextHighlight, PartsEngine_Parts_AddPartsTextHighlight),
	    HLL_TODO_EXPORT(Parts_ClearPartsTextHighlight, PartsEngine_Parts_ClearPartsTextHighlight),
	    HLL_TODO_EXPORT(Parts_SetPartsTextCountReturn, PartsEngine_Parts_SetPartsTextCountReturn),
	    HLL_TODO_EXPORT(Parts_GetPartsTextCountReturn, PartsEngine_Parts_GetPartsTextCountReturn),
	    HLL_EXPORT(Parts_SetFont, PE_SetFont),
	    HLL_EXPORT(Parts_SetPartsFontType, PE_SetPartsFontType),
	    HLL_EXPORT(Parts_SetPartsFontSize, PE_SetPartsFontSize),
	    HLL_EXPORT(Parts_SetPartsFontColor, PE_SetPartsFontColor),
	    HLL_EXPORT(Parts_SetPartsFontBoldWeight, PE_SetPartsFontBoldWeight),
	    HLL_EXPORT(Parts_SetPartsFontEdgeColor, PE_SetPartsFontEdgeColor),
	    HLL_EXPORT(Parts_SetPartsFontEdgeWeight, PE_SetPartsFontEdgeWeight),
	    HLL_EXPORT(Parts_SetTextCharSpace, PE_SetTextCharSpace),
	    HLL_EXPORT(Parts_SetTextLineSpace, PE_SetTextLineSpace),
	    HLL_EXPORT(Parts_SetHGaugeCG, PE_SetHGaugeCG),
	    HLL_EXPORT(Parts_SetHGaugeRate, PE_SetHGaugeRate),
	    HLL_EXPORT(Parts_SetVGaugeCG, PE_SetVGaugeCG),
	    HLL_EXPORT(Parts_SetVGaugeRate, PE_SetVGaugeRate),
	    HLL_EXPORT(Parts_SetHGaugeSurfaceArea, PE_SetHGaugeSurfaceArea),
	    HLL_EXPORT(Parts_SetVGaugeSurfaceArea, PE_SetVGaugeSurfaceArea),
	    HLL_EXPORT(Parts_SetNumeralCG, PE_SetNumeralCG),
	    HLL_EXPORT(Parts_SetNumeralLinkedCGNumberWidthWidthList, PE_SetNumeralLinkedCGNumberWidthWidthList),
	    HLL_TODO_EXPORT(Parts_SetNumeralFont, PartsEngine_Parts_SetNumeralFont),
	    HLL_EXPORT(Parts_SetNumeralNumber, PE_SetNumeralNumber),
	    HLL_EXPORT(Parts_SetNumeralShowComma, PE_SetNumeralShowComma),
	    HLL_EXPORT(Parts_SetNumeralSpace, PE_SetNumeralSpace),
	    HLL_EXPORT(Parts_SetNumeralLength, PE_SetNumeralLength),
	    HLL_EXPORT(Parts_SetNumeralSurfaceArea, PE_SetNumeralSurfaceArea),
	    HLL_EXPORT(Parts_SetPartsRectangleDetectionSize, PE_SetPartsRectangleDetectionSize),
	    HLL_TODO_EXPORT(Parts_SetPartsRectangleDetectionSurfaceArea, PartsEngine_Parts_SetPartsRectangleDetectionSurfaceArea),
	    HLL_EXPORT(Parts_SetPartsCGDetectionSize, PE_SetPartsCGDetectionSize),
	    HLL_TODO_EXPORT(Parts_SetPartsCGDetectionSurfaceArea, PartsEngine_Parts_SetPartsCGDetectionSurfaceArea),
	    HLL_EXPORT(Parts_SetPartsFlat, PE_SetPartsFlat),
	    HLL_EXPORT(Parts_IsPartsFlatEnd, PE_IsPartsFlatEnd),
	    HLL_EXPORT(Parts_GetPartsFlatCurrentFrameNumber, PE_GetPartsFlatCurrentFrameNumber),
	    HLL_EXPORT(Parts_BackPartsFlatBeginFrame, PE_BackPartsFlatBeginFrame),
	    HLL_EXPORT(Parts_StepPartsFlatFinalFrame, PE_StepPartsFlatFinalFrame),
	    HLL_EXPORT(Parts_SetPartsFlatSurfaceArea, PE_SetPartsFlatSurfaceArea),
	    HLL_EXPORT(Parts_SetPartsFlatAndStop, PE_SetPartsFlatAndStop),
	    HLL_EXPORT(Parts_StopPartsFlat, PE_StopPartsFlat),
	    HLL_EXPORT(Parts_StartPartsFlat, PE_StartPartsFlat),
	    HLL_EXPORT(Parts_GoFramePartsFlat, PE_GoFramePartsFlat),
	    HLL_EXPORT(Parts_GetPartsFlatEndFrame, PE_GetPartsFlatEndFrame),
	    HLL_EXPORT(Parts_ExistsFlatFile, PE_ExistsFlatFile),
	    HLL_TODO_EXPORT(Parts_GetPartsFlatDataInfo, PartsEngine_Parts_GetPartsFlatDataInfo),
	    HLL_TODO_EXPORT(Parts_ChangeFlatCG, PartsEngine_Parts_ChangeFlatCG),
	    HLL_TODO_EXPORT(Parts_ChangeFlatSound, PartsEngine_Parts_ChangeFlatSound),
	    HLL_EXPORT(Parts_ClearPartsConstructionProcess, PE_ClearPartsConstructionProcess),
	    HLL_EXPORT(Parts_AddCreateToPartsConstructionProcess, PE_AddCreateToPartsConstructionProcess),
	    HLL_EXPORT(Parts_AddCreatePixelOnlyToPartsConstructionProcess, PE_AddCreatePixelOnlyToPartsConstructionProcess),
	    HLL_EXPORT(Parts_AddCreateCGToProcess, PE_AddCreateCGToProcess),
	    HLL_EXPORT(Parts_AddFillToPartsConstructionProcess, PE_AddFillToPartsConstructionProcess),
	    HLL_EXPORT(Parts_AddFillAlphaColorToPartsConstructionProcess, PE_AddFillAlphaColorToPartsConstructionProcess),
	    HLL_EXPORT(Parts_AddFillAMapToPartsConstructionProcess, PE_AddFillAMapToPartsConstructionProcess),
	    HLL_EXPORT(Parts_AddFillWithAlphaToPartsConstructionProcess, PE_AddFillWithAlphaToPartsConstructionProcess),
	    HLL_TODO_EXPORT(Parts_AddFillGradationHorizonToPartsConstructionProcess, PartsEngine_Parts_AddFillGradationHorizonToPartsConstructionProcess),
	    HLL_EXPORT(Parts_AddDrawRectToPartsConstructionProcess, PE_AddDrawRectToPartsConstructionProcess),
	    HLL_EXPORT(Parts_AddDrawCutCGToPartsConstructionProcess, PE_AddDrawCutCGToPartsConstructionProcess),
	    HLL_EXPORT(Parts_AddCopyCutCGToPartsConstructionProcess, PE_AddCopyCutCGToPartsConstructionProcess),
	    HLL_EXPORT(Parts_AddGrayFilterToPartsConstructionProcess, PE_AddGrayFilterToPartsConstructionProcess),
	    HLL_TODO_EXPORT(Parts_AddAddFilterToPartsConstructionProcess, PartsEngine_Parts_AddAddFilterToPartsConstructionProcess),
	    HLL_TODO_EXPORT(Parts_AddMulFilterToPartsConstructionProcess, PartsEngine_Parts_AddMulFilterToPartsConstructionProcess),
	    HLL_TODO_EXPORT(Parts_AddDrawLineToPartsConstructionProcess, PartsEngine_Parts_AddDrawLineToPartsConstructionProcess),
	    HLL_EXPORT(Parts_BuildPartsConstructionProcess, PE_BuildPartsConstructionProcess),
	    HLL_EXPORT(Parts_AddDrawTextToPartsConstructionProcess, PE_AddDrawTextToPartsConstructionProcess),
	    HLL_EXPORT(Parts_AddCopyTextToPartsConstructionProcess, PE_AddCopyTextToPartsConstructionProcess),
	    HLL_EXPORT(Parts_SetPartsConstructionSurfaceArea, PE_SetPartsConstructionSurfaceArea),
	    HLL_EXPORT(Parts_CreateParts3DLayerPluginID, PE_CreateParts3DLayerPluginID),
	    HLL_EXPORT(Parts_GetParts3DLayerPluginID, PE_GetParts3DLayerPluginID),
	    HLL_EXPORT(Parts_ReleaseParts3DLayerPluginID, PE_ReleaseParts3DLayerPluginID),
	    HLL_EXPORT(Parts_SetPassCursor, PE_SetPassCursor),
	    HLL_EXPORT(Parts_SetClickable, PE_SetClickable),
	    HLL_TODO_EXPORT(Parts_SetResetTimerByChangeInputStatus, PartsEngine_Parts_SetResetTimerByChangeInputStatus),
	    HLL_EXPORT(Parts_SetDrag, PE_SetDrag),
	    HLL_EXPORT(Parts_SetParentPartsNumber, PE_SetParentPartsNumber),
	    HLL_EXPORT(Parts_SetInputState, PE_SetInputState),
	    HLL_EXPORT(Parts_SetOnCursorShowLinkPartsNumber, PE_SetOnCursorShowLinkPartsNumber),
	    HLL_TODO_EXPORT(Parts_SetSoundNumber, PartsEngine_Parts_SetSoundNumber),
	    HLL_EXPORT(Parts_SetPartsPixelDecide, PE_SetPartsPixelDecide),
	    HLL_EXPORT(Parts_GetPartsPassCursor, PE_GetPartsPassCursor),
	    HLL_EXPORT(Parts_GetPartsClickable, PE_GetPartsClickable),
	    HLL_TODO_EXPORT(Parts_GetResetTimerByChangeInputStatus, PartsEngine_Parts_GetResetTimerByChangeInputStatus),
	    HLL_TODO_EXPORT(Parts_GetPartsDrag, PartsEngine_Parts_GetPartsDrag),
	    HLL_TODO_EXPORT(Parts_GetParentPartsNumber, PartsEngine_Parts_GetParentPartsNumber),
	    HLL_EXPORT(Parts_GetInputState, PE_GetInputState),
	    HLL_TODO_EXPORT(Parts_GetOnCursorShowLinkPartsNumber, PartsEngine_Parts_GetOnCursorShowLinkPartsNumber),
	    HLL_TODO_EXPORT(Parts_GetSoundNumber, PartsEngine_Parts_GetSoundNumber),
	    HLL_TODO_EXPORT(Parts_IsPartsPixelDecide, PartsEngine_Parts_IsPartsPixelDecide),
	    HLL_EXPORT(Parts_IsCursorIn, PE_IsCursorIn),
	    HLL_TODO_EXPORT(SaveParts, PartsEngine_SaveParts),
	    HLL_TODO_EXPORT(LoadParts, PartsEngine_LoadParts)
	    );

static struct ain_hll_function *get_fun(int libno, const char *name)
{
	int fno = ain_get_library_function(ain, libno, name);
	return fno >= 0 ? &ain->libraries[libno].functions[fno] : NULL;
}

// v14 (Dohna Dohna) declares:
//   void RemoveController(wrap<array<int>> EraseNumberList, int Index);
// The wrap argument arrives from the FFI as an int heap slot, not the
// page** the legacy 'ref array<int>' declaration produces. Calling the
// legacy implementation with that cif dereferences a garbage pointer.
static void PE_v14_RemoveController(int erase_slot, int index)
{
	// The v14 wrap<array<int>> out-list is not populated: the fork this
	// port derives from never wrote it and Dohna Dohna runs fine without
	// it (the erased parts numbers are only diagnostics for the game
	// script). Writing it would require v14 array-page construction;
	// revisit with the parts message wave if a scene turns out to read it.
	(void)erase_slot;
	struct page *scratch = NULL;
	PE_RemoveController(&scratch, index);
	if (scratch) {
		delete_page_vars(scratch);
		free_page(scratch);
	}
}

// v14: Save/SaveWithoutHideParts/Load are declared with a wrap<> first
// argument, which the FFI passes as an int heap slot — not the page**
// the legacy declarations produce. Adapt around the legacy savers.
static bool PE_v14_Save(int buf_slot)
{
	struct page *buf = wrap_get_page(buf_slot, 0);
	bool ok = PE_Save(&buf);
	if (ok)
		wrap_set_slot(buf_slot, 0, heap_alloc_page(buf));
	return ok;
}

static bool PE_v14_SaveWithoutHideParts(int buf_slot)
{
	struct page *buf = wrap_get_page(buf_slot, 0);
	bool ok = PE_SaveWithoutHideParts(&buf);
	if (ok)
		wrap_set_slot(buf_slot, 0, heap_alloc_page(buf));
	return ok;
}

static bool PE_v14_Load(int buf_slot)
{
	struct page *buf = wrap_get_page(buf_slot, 0);
	return PE_Load(&buf);
}

// v14 declares: void UpdateComponent(int PassedTime, int ScaledPassedTime,
// bool MessageWindowShow, float MessageWindowMulColorRate,
// float MessageWindowAlphaRate). On v14 this call drives the whole frame
// on scenes that never call SystemService.UpdateView (e.g. the title
// screen), so pump events and motion here too.
// Fork-verified semantics: reentrancy guard (PE_Update can trigger VM
// callbacks that call UpdateComponent again), and present the frame here —
// on scenes that never call SystemService.UpdateView (e.g. the title
// screen) this is the only per-frame driver.
static bool pe_v14_in_update = false;
static void PE_v14_UpdateComponent(int passed_time, int scaled_passed_time,
		bool message_window_show, float mul_color_rate, float alpha_rate)
{
	(void)scaled_passed_time; (void)mul_color_rate; (void)alpha_rate;
	if (pe_v14_in_update) {
		handle_events();
		PE_UpdateComponent(passed_time);
		return;
	}
	pe_v14_in_update = true;
	handle_events();
	sprite_call_plugins();
	PE_UpdateMotionTime(passed_time, false);
	PE_Update(passed_time, message_window_show);
	{
		static uint32_t last_render_ms = 0;
		uint32_t now_ms = SDL_GetTicks();
		if (now_ms - last_render_ms >= 16) {
			scene_render();
			gfx_swap();
			last_render_ms = now_ms;
		}
	}
	pe_v14_in_update = false;
}
enum v14_cp_type {
	V14_CP_CREATE = 0,
	V14_CP_CREATE_PIXEL_ONLY = 1,
	V14_CP_CREATE_CG = 2,
	V14_CP_FILL = 3,
	V14_CP_FILL_ALPHA_COLOR = 4,
	V14_CP_FILL_AMAP = 5,
	V14_CP_FILL_WITH_ALPHA = 6,
	V14_CP_DRAW_TEXT = 7,
	V14_CP_COPY_TEXT = 8,
	V14_CP_FILL_GRADATION_HORIZON = 9,
	V14_CP_DRAW_RECT = 10,
	V14_CP_CUT_CG_BLEND = 11,
	V14_CP_CUT_CG_COPY = 12,
	V14_CP_CUT_CG_SCALE_BLEND = 13,
	V14_CP_CUT_CG_SCALE_COPY = 14,
	V14_CP_GRAY_FILTER = 15,
	V14_CP_ADD_FILTER = 16,
	V14_CP_MUL_FILTER = 17,
	V14_CP_DRAW_LINE = 18,
	V14_CP_CUT_CG_ALPHA_BLEND = 19,
	V14_CP_CUT_CG_SCALE_ALPHA_BLEND = 20,
	V14_CP_CUT_CG_ONLY_ALPHA = 21,
	V14_CP_CUT_CG_SCALE_ONLY_ALPHA = 22,
	V14_CP_ALPHA_BLEND_TEXT = 23,
	V14_CP_ONLY_ALPHA_TEXT = 24,
	V14_CP_MUL_AMAP_GRADATION_HORIZON = 25,
	V14_CP_MUL_AMAP_GRADATION_VERTICAL = 26,
	V14_CP_HBLUR_FILTER = 27,
	V14_CP_VBLUR_FILTER = 28,
	V14_CP_CG_BLEND = 29,
	V14_CP_DRAW_LINE_WITH_ALPHA = 30,
	V14_CP_DRAW_CIRCLE_ALPHA_BLEND_IN_RECT = 57,
};

/*
 * wrap_get_backing_array — extract the backing ARRAY_PAGE from a wrap<array<T>> slot.
 *
 * In v14, wrap<array<T>> may be:
 *   (a) A simple wrap container: member[0] → inner slot → ARRAY_PAGE
 *   (b) An IArray<T> implementation class (STRUCT_PAGE with ~40 members):
 *       the backing array is stored in a member of AIN array type.
 *
 * This function handles both cases by:
 *   1. Checking if the slot directly holds an ARRAY_PAGE
 *   2. Trying the simple wrap path (member[0])
 *   3. Using the AIN struct definition to find the array-type member
 *   4. Falling back to scanning for the first ARRAY_PAGE member
 */
static struct page *wrap_get_backing_array(int slot)
{
	if (slot < 0 || (size_t)slot >= heap_size) return NULL;
	if (heap[slot].type != VM_PAGE || !heap[slot].page) return NULL;
	struct page *p = heap[slot].page;

	// Case 1: slot directly holds an ARRAY_PAGE
	if (p->type == ARRAY_PAGE)
		return p;

	// Case 2: simple wrap — member[0] is inner slot pointing to ARRAY_PAGE
	if (p->nr_vars > 0) {
		int inner = p->values[0].i;
		if (inner > 0 && (size_t)inner < heap_size
		    && heap[inner].type == VM_PAGE && heap[inner].page
		    && heap[inner].page->type == ARRAY_PAGE)
			return heap[inner].page;
	}

	// Case 3: IArray class — use AIN struct definition to find array member
	if (p->type == STRUCT_PAGE && p->index >= 0
	    && ain && p->index < ain->nr_structures) {
		struct ain_struct *st = &ain->structures[p->index];
		for (int k = 0; k < st->nr_members && k < p->nr_vars; k++) {
			switch (st->members[k].type.data) {
			case AIN_ARRAY_TYPE:
			case AIN_ARRAY: {
				int v = p->values[k].i;
				if (v > 0 && (size_t)v < heap_size
				    && heap[v].type == VM_PAGE && heap[v].page
				    && heap[v].page->type == ARRAY_PAGE)
					return heap[v].page;
				break;
			}
			default:
				break;
			}
		}
	}

	// Case 4: fallback — scan all members for first ARRAY_PAGE
	if (p->type == STRUCT_PAGE) {
		for (int k = 0; k < p->nr_vars; k++) {
			int v = p->values[k].i;
			if (v > 0 && (size_t)v < heap_size
			    && heap[v].type == VM_PAGE && heap[v].page
			    && heap[v].page->type == ARRAY_PAGE)
				return heap[v].page;
		}
	}

	return NULL;
}

static void PartsEngine_AddPartsConstructionProcess(int parts_no, int wi_slot, int wf_slot, int ws_slot, int wp_slot, int state)
{
	struct page *ints = wrap_get_backing_array(wi_slot);
	if (!ints || ints->nr_vars < 1) {
		return;
	}
	// Sanity check: ints[0] (command) should be 0-200 range
	if (ints->values[0].i > 200 || ints->values[0].i < 0) {
		return;
	}

	int cmd = ints->values[0].i;

	// Trace: dump ints array for debugging Construction
	{
		static int apt = 0;
		if (apt < 5 && 0) { // disabled trace
			WARNING("AddCP: parts=%d cmd=%d nr_vars=%d vals=[", parts_no, cmd, ints->nr_vars);
			for (int _j = 0; _j < ints->nr_vars && _j < 16; _j++)
				WARNING("  [%d]=%d", _j, ints->values[_j].i);
			apt++;
		}
	}

	int dx = ints->nr_vars > 6 ? ints->values[6].i : 0;
	int dy = ints->nr_vars > 7 ? ints->values[7].i : 0;
	int dx2 = ints->nr_vars > 8 ? ints->values[8].i : 0;
	int dy2 = ints->nr_vars > 9 ? ints->values[9].i : 0;
	int dw = ints->nr_vars > 10 ? ints->values[10].i : 0;
	int dh = ints->nr_vars > 11 ? ints->values[11].i : 0;
	int r  = ints->nr_vars > 12 ? ints->values[12].i : 0;
	int g  = ints->nr_vars > 13 ? ints->values[13].i : 0;
	int b  = ints->nr_vars > 14 ? ints->values[14].i : 0;
	int a  = ints->nr_vars > 15 ? ints->values[15].i : 255;
	int interp = ints->nr_vars > 1 ? ints->values[1].i : 0;
	int sx = ints->nr_vars > 2 ? ints->values[2].i : 0;
	int sy = ints->nr_vars > 3 ? ints->values[3].i : 0;
	int sw = ints->nr_vars > 4 ? ints->values[4].i : 0;
	int sh = ints->nr_vars > 5 ? ints->values[5].i : 0;

	/* Get CG name from ArrayString if available */
	struct page *strs = wrap_get_backing_array(ws_slot);
	struct string *cg_name = NULL;
	struct string *text = NULL;
	if (strs) {
		if (strs->nr_vars > 1) {
			int s = strs->values[1].i;
			if (s > 0 && heap_index_valid(s) && heap[s].s)
				cg_name = heap[s].s;
		}
		if (strs->nr_vars > 0) {
			int s = strs->values[0].i;
			if (s > 0 && heap_index_valid(s) && heap[s].s)
				text = heap[s].s;
		}
	}

	switch (cmd) {
	case V14_CP_CREATE:
		if (dw == 0 && dh == 0) {
			dw = dx2 > 0 ? dx2 : 0;
			dh = dy2 > 0 ? dy2 : 0;
		}
		PE_AddCreateToPartsConstructionProcess(parts_no, dw, dh, state);
		break;
	case V14_CP_CREATE_PIXEL_ONLY:
		if (dw == 0 && dh == 0) {
			dw = dx2 > 0 ? dx2 : 0;
			dh = dy2 > 0 ? dy2 : 0;
		}
		PE_AddCreatePixelOnlyToPartsConstructionProcess(parts_no, dw, dh, state);
		break;
	case V14_CP_CREATE_CG:
		if (cg_name)
			PE_AddCreateCGToProcess(parts_no, cg_name, state);
		break;
	case V14_CP_FILL: {
		int fw = dw > 0 ? dw : (dx2 > 0 ? dx2 : 16384);
		int fh = dh > 0 ? dh : (dy2 > 0 ? dy2 : 16384);
		PE_AddFillToPartsConstructionProcess(parts_no, dx, dy, fw, fh, r, g, b, state);
		break;
	}
	case V14_CP_FILL_ALPHA_COLOR: {
		int fw = dw > 0 ? dw : (dx2 > 0 ? dx2 : 16384);
		int fh = dh > 0 ? dh : (dy2 > 0 ? dy2 : 16384);
		PE_AddFillAlphaColorToPartsConstructionProcess(parts_no, dx, dy, fw, fh, r, g, b, a, state);
		break;
	}
	case V14_CP_FILL_AMAP: {
		int fw = dw > 0 ? dw : (dx2 > 0 ? dx2 : 16384);
		int fh = dh > 0 ? dh : (dy2 > 0 ? dy2 : 16384);
		PE_AddFillAMapToPartsConstructionProcess(parts_no, dx, dy, fw, fh, a, state);
		break;
	}
	case V14_CP_FILL_WITH_ALPHA: {
		int fw = dw > 0 ? dw : (dx2 > 0 ? dx2 : 16384);
		int fh = dh > 0 ? dh : (dy2 > 0 ? dy2 : 16384);
		PE_AddFillAlphaColorToPartsConstructionProcess(parts_no, dx, dy, fw, fh, r, g, b, a, state);
		break;
	}
	case V14_CP_DRAW_TEXT:
		if (text) {
			int font_type = ints->nr_vars > 22 ? ints->values[22].i : 0;
			int font_size = ints->nr_vars > 30 ? ints->values[30].i : 16;
			int char_space = ints->nr_vars > 20 ? ints->values[20].i : 0;
			int line_space = ints->nr_vars > 21 ? ints->values[21].i : 0;
			struct page *floats = wrap_get_backing_array(wf_slot);
			float bold_weight = (floats && floats->nr_vars > 0) ? floats->values[0].f : 0.0f;
			float edge_weight = (floats && floats->nr_vars > 1) ? floats->values[1].f : 0.0f;
			int r2 = ints->nr_vars > 16 ? ints->values[16].i : 0;
			int g2 = ints->nr_vars > 17 ? ints->values[17].i : 0;
			int b2 = ints->nr_vars > 18 ? ints->values[18].i : 0;
			PE_AddDrawTextToPartsConstructionProcess(parts_no, dx, dy, text,
				font_type, font_size, r, g, b, bold_weight,
				r2, g2, b2, edge_weight, char_space, line_space, state);
		}
		break;
	case V14_CP_COPY_TEXT:
		if (text) {
			int font_type = ints->nr_vars > 22 ? ints->values[22].i : 0;
			int font_size = ints->nr_vars > 30 ? ints->values[30].i : 16;
			int char_space = ints->nr_vars > 20 ? ints->values[20].i : 0;
			int line_space = ints->nr_vars > 21 ? ints->values[21].i : 0;
			struct page *floats = wrap_get_backing_array(wf_slot);
			float bold_weight = (floats && floats->nr_vars > 0) ? floats->values[0].f : 0.0f;
			float edge_weight = (floats && floats->nr_vars > 1) ? floats->values[1].f : 0.0f;
			int r2 = ints->nr_vars > 16 ? ints->values[16].i : 0;
			int g2 = ints->nr_vars > 17 ? ints->values[17].i : 0;
			int b2 = ints->nr_vars > 18 ? ints->values[18].i : 0;
			PE_AddCopyTextToPartsConstructionProcess(parts_no, dx, dy, text,
				font_type, font_size, r, g, b, bold_weight,
				r2, g2, b2, edge_weight, char_space, line_space, state);
		}
		break;
	case V14_CP_FILL_GRADATION_HORIZON: {
		/* Gradient fill: use RGBA + RGBA2 for start/end colors, approximate with fill */
		int fw = dw > 0 ? dw : (dx2 > 0 ? dx2 : 16384);
		int fh = dh > 0 ? dh : (dy2 > 0 ? dy2 : 16384);
		PE_AddFillAlphaColorToPartsConstructionProcess(parts_no, dx, dy, fw, fh, r, g, b, a, state);
		break;
	}
	case V14_CP_DRAW_RECT: {
		int fw = dw > 0 ? dw : dx2;
		int fh = dh > 0 ? dh : dy2;
		PE_AddDrawRectToPartsConstructionProcess(parts_no, dx, dy, fw, fh, r, g, b, state);
		break;
	}
	case V14_CP_CUT_CG_BLEND:
	case V14_CP_CUT_CG_SCALE_BLEND:
	case V14_CP_CUT_CG_ALPHA_BLEND:
	case V14_CP_CUT_CG_SCALE_ALPHA_BLEND:
	case V14_CP_CG_BLEND:
		if (cg_name)
			PE_AddDrawCutCGToPartsConstructionProcess(parts_no, cg_name,
				dx, dy, dw, dh, sx, sy, sw, sh, interp, state);
		break;
	case V14_CP_CUT_CG_COPY:
	case V14_CP_CUT_CG_SCALE_COPY:
	case V14_CP_CUT_CG_ONLY_ALPHA:
	case V14_CP_CUT_CG_SCALE_ONLY_ALPHA:
		if (cg_name)
			PE_AddCopyCutCGToPartsConstructionProcess(parts_no, cg_name,
				dx, dy, dw, dh, sx, sy, sw, sh, interp, state);
		break;
	case V14_CP_GRAY_FILTER:
	case V14_CP_ADD_FILTER:
	case V14_CP_MUL_FILTER:
	case V14_CP_HBLUR_FILTER:
	case V14_CP_VBLUR_FILTER: {
		/* Filter stubs — no-op, the parts texture is already rendered */
		static int filter_warn = 0;
		if (filter_warn++ < 3)
			WARNING("AddPartsConstructionProcess: filter type %d (stub) parts=%d", cmd, parts_no);
		break;
	}
	case V14_CP_DRAW_LINE:
	case V14_CP_DRAW_LINE_WITH_ALPHA: {
		int x1 = dx, y1 = dy, x2 = dx2, y2 = dy2;
		int lx = x1 < x2 ? x1 : x2;
		int ly = y1 < y2 ? y1 : y2;
		int lw = abs(x2 - x1);
		int lh = abs(y2 - y1);
		if (lw == 0) lw = 1;
		if (lh == 0) lh = 1;
		PE_AddFillAlphaColorToPartsConstructionProcess(parts_no, lx, ly, lw, lh, r, g, b, a, state);
		break;
	}
	case V14_CP_ALPHA_BLEND_TEXT:
	case V14_CP_ONLY_ALPHA_TEXT:
		/* Text with alpha blending — use DrawText as approximation */
		if (text) {
			int font_type = ints->nr_vars > 22 ? ints->values[22].i : 0;
			int font_size = ints->nr_vars > 30 ? ints->values[30].i : 16;
			int char_space = ints->nr_vars > 20 ? ints->values[20].i : 0;
			int line_space = ints->nr_vars > 21 ? ints->values[21].i : 0;
			struct page *floats = wrap_get_backing_array(wf_slot);
			float bold_weight = (floats && floats->nr_vars > 0) ? floats->values[0].f : 0.0f;
			float edge_weight = (floats && floats->nr_vars > 1) ? floats->values[1].f : 0.0f;
			int r2 = ints->nr_vars > 16 ? ints->values[16].i : 0;
			int g2 = ints->nr_vars > 17 ? ints->values[17].i : 0;
			int b2 = ints->nr_vars > 18 ? ints->values[18].i : 0;
			PE_AddDrawTextToPartsConstructionProcess(parts_no, dx, dy, text,
				font_type, font_size, r, g, b, bold_weight,
				r2, g2, b2, edge_weight, char_space, line_space, state);
		}
		break;
	case V14_CP_MUL_AMAP_GRADATION_HORIZON:
	case V14_CP_MUL_AMAP_GRADATION_VERTICAL: {
		/* Gradient alpha map — approximate with FillAMap */
		int fw = dw > 0 ? dw : (dx2 > 0 ? dx2 : 16384);
		int fh = dh > 0 ? dh : (dy2 > 0 ? dy2 : 16384);
		PE_AddFillAMapToPartsConstructionProcess(parts_no, dx, dy, fw, fh, a, state);
		break;
	}
	case V14_CP_DRAW_CIRCLE_ALPHA_BLEND_IN_RECT: {
		/* Circle draw — approximate with fill alpha color */
		int fw = dw > 0 ? dw : (dx2 > 0 ? dx2 : 16384);
		int fh = dh > 0 ? dh : (dy2 > 0 ? dy2 : 16384);
		PE_AddFillAlphaColorToPartsConstructionProcess(parts_no, dx, dy, fw, fh, r, g, b, a, state);
		break;
	}
	default: {
		static int cp_warn = 0;
		if (cp_warn++ < 10) {
			WARNING("AddPartsConstructionProcess: unknown type %d parts=%d "
				"ints_nr=%d ints[0..3]=%d,%d,%d,%d",
				cmd, parts_no, ints->nr_vars,
				ints->nr_vars > 0 ? ints->values[0].i : -1,
				ints->nr_vars > 1 ? ints->values[1].i : -1,
				ints->nr_vars > 2 ? ints->values[2].i : -1,
				ints->nr_vars > 3 ? ints->values[3].i : -1);
		}
		break;
	}
	}
}
// Parts movie implementation (APEG audio playback via movie.h)
#define PARTS_MOVIE_MAX 16
static struct { int number; struct movie_context *mc; } parts_movies[PARTS_MOVIE_MAX];

static struct movie_context **parts_movie_get(int number)
{
	for (int i = 0; i < PARTS_MOVIE_MAX; i++)
		if (parts_movies[i].number == number)
			return &parts_movies[i].mc;
	return NULL;
}

static bool PE_stub_CreatePartsMovie(int number, struct string *filename,
	possibly_unused int soundid, possibly_unused int soundgroup,
	int red, int green, int blue, int state)
{
	// Free any existing context for this number.
	struct movie_context **slot = parts_movie_get(number);
	if (slot && *slot) {
		movie_free(*slot);
		*slot = NULL;
	}
	// Find a free slot.
	if (!slot) {
		for (int i = 0; i < PARTS_MOVIE_MAX; i++) {
			if (!parts_movies[i].mc && parts_movies[i].number == 0) {
				parts_movies[i].number = number;
				slot = &parts_movies[i].mc;
				break;
			}
		}
	}
	if (!slot)
		return false;
	struct movie_context *mc = movie_load(filename->text);
	if (!mc)
		return false;
	*slot = mc;

	// Fill the parts texture with the background color (shows when video not decoded).
	struct parts *p = parts_try_get(number);
	if (p && parts_state_valid(state - 1)) {
		struct parts_common *common = &p->states[state - 1].common;
		int tw = common->w, th = common->h;
		// If parts has no size yet, use the movie's native dimensions.
		if (tw <= 0 || th <= 0)
			movie_get_video_size(mc, &tw, &th);
		if (tw > 0 && th > 0) {
			// Colors are in YCbCr; just use black (0,0,0) for simplicity.
			SDL_Color bg = { 0, 0, 0, 255 };
			(void)red; (void)green; (void)blue;
			gfx_init_texture_rgba(&common->texture, tw, th, bg);
			if (common->w <= 0) {
				common->w = tw;
				common->h = th;
			}
			parts_dirty(p);
		}
	}
	return true;
}

static bool PE_stub_ReleasePartsMovie(int number, possibly_unused int state)
{
	struct movie_context **slot = parts_movie_get(number);
	if (slot && *slot) {
		movie_free(*slot);
		*slot = NULL;
	}
	return true;
}

static bool PE_stub_PlayPartsMovie(int number, int msec, possibly_unused int state)
{
	struct movie_context **slot = parts_movie_get(number);
	if (!slot || !*slot)
		return false;
	(void)msec;
	return movie_play(*slot);
}

static void PE_stub_SetMovieTime(possibly_unused int number, possibly_unused int msec, possibly_unused int state) { }

static bool PE_stub_IsEndPartsMovie(int number, possibly_unused int state)
{
	struct movie_context **slot = parts_movie_get(number);
	if (!slot || !*slot)
		return true;
	return movie_is_end(*slot);
}

static int PE_stub_GetPartsMovieEndTime(possibly_unused int number, possibly_unused int state) { return 0; }

static int PE_stub_GetPartsMovieCurrentTime(int number, possibly_unused int state)
{
	struct movie_context **slot = parts_movie_get(number);
	if (!slot || !*slot)
		return 0;
	return movie_get_position(*slot);
}

/* ======================================================================
 * v14 batch (fork port): panels, misc component accessors, message
 * window text stubs, back-scene save, child queries, parts movie.
 * Registered from PartsEngine_PostLink via pe_v14_register_batch();
 * static_library_register only fills NULL entries, so none of these
 * can shadow an upstream implementation.
 * ====================================================================== */

/* Component color/clip accessors — stubs with fork-verified defaults
 * (add color reads back 0, mul color reads back 255 = white). */
static int PE_v14_GetComponentAddColorR(int n) { (void)n; return 0; }
static int PE_v14_GetComponentAddColorG(int n) { (void)n; return 0; }
static int PE_v14_GetComponentAddColorB(int n) { (void)n; return 0; }
static int PE_v14_GetComponentMulColorR(int n) { (void)n; return 255; }
static int PE_v14_GetComponentMulColorG(int n) { (void)n; return 255; }
static int PE_v14_GetComponentMulColorB(int n) { (void)n; return 255; }
static void PE_v14_SetComponentEnableClipArea(int n, bool enable) { (void)n; (void)enable; }
static void PE_v14_SetComponentClipArea(int n, int x, int y, int w, int h)
{ (void)n; (void)x; (void)y; (void)w; (void)h; }
static int PE_v14_GetComponentClipAreaPosX(int n) { (void)n; return 0; }
static int PE_v14_GetComponentClipAreaPosY(int n) { (void)n; return 0; }
static int PE_v14_GetComponentClipAreaPosWidth(int n) { (void)n; return 0; }
static int PE_v14_GetComponentClipAreaPosHeight(int n) { (void)n; return 0; }
static void PE_v14_SetComponentReverseLR(int n, bool r) { (void)n; (void)r; }

static float PE_v14_GetComponentMagX(int n) { return PE_GetPartsMagX(n); }
static float PE_v14_GetComponentMagY(int n) { return PE_GetPartsMagY(n); }

/* Metadata label; no visual effect. */
static void PE_v14_Parts_SetComment(int parts_no, struct string *comment)
{ (void)parts_no; (void)comment; }

static int PE_v14_NumofChild(int number)
{
	struct parts *p = parts_try_get(number);
	if (!p)
		return 0;
	int count = 0;
	struct parts *child;
	PARTS_FOREACH_CHILD(child, p) {
		count++;
	}
	return count;
}

/* --- Panel support (fork implementation) --- */
static void PE_v14_SetPanelSize(int parts_no, int w, int h)
{
	PE_ClearPartsConstructionProcess(parts_no, 1);
	PE_AddCreateToPartsConstructionProcess(parts_no, w, h, 1);
	PE_BuildPartsConstructionProcess(parts_no, 1);
}

static void PE_v14_SetPanelColor(int parts_no, int r, int g, int b, int a)
{
	struct parts *p = parts_try_get(parts_no);
	if (!p)
		return;
	struct parts_construction_process *cproc =
		parts_get_construction_process(p, 0); /* state 0 = internal index for state 1 */
	int w = cproc->common.w;
	int h = cproc->common.h;
	if (w <= 0 || h <= 0)
		return;
	PE_AddFillAlphaColorToPartsConstructionProcess(parts_no, 0, 0, w, h, r, g, b, a, 1);
	PE_BuildPartsConstructionProcess(parts_no, 1);
}

/* --- Message window text queries ---
 * Text is rendered synchronously by the pe_v14_message adapters, so these
 * read back empty/default values (fork parity; the script only uses them
 * for layout bookkeeping). */
static struct string *PE_v14_GetMessageWindowFlatName(int parts_no)
{
	(void)parts_no;
	return string_ref(&EMPTY_STRING);
}

static struct string *PE_v14_GetMessageWindowText(int parts_no)
{
	(void)parts_no;
	return string_ref(&EMPTY_STRING);
}

static void PE_v14_SetMessageWindowTextOriginPosMode(int parts_no, int mode)
{ (void)parts_no; (void)mode; }

/* v14 declares: bool SaveBackScene(wrap<array<int>> SaveDataBuffer).
 * The back-scene snapshot is not implemented (backlog display, Wave 6);
 * report success so the script's 画面保管 step doesn't raise system.Error
 * and abort the transition bookkeeping. The fork's void stub returned an
 * undefined (in practice non-zero) value here, so 'true' matches the
 * behaviour the game was verified against. */
static bool PE_v14_SaveBackScene(int buf_slot)
{
	(void)buf_slot;
	return true;
}

/* Read back the message-window CG set by SetMessageWindowCGName. */
static struct string *PE_GetMessageWindowCGName(int parts_no)
{
	struct parts *parts = parts_try_get(parts_no);
	if (!parts)
		return string_ref(&EMPTY_STRING);
	struct parts_cg *cg = parts_get_cg(parts, PARTS_STATE_DEFAULT);
	if (cg && cg->name)
		return string_ref(cg->name);
	return string_ref(&EMPTY_STRING);
}

/* "Async" CG load: no thread loader, so load synchronously and return
 * true so the ain-side Observer (polling Parts_IsThreadLoading == false)
 * fires its callback immediately. */
static bool PartsEngine_Parts_SetPartsCGThread(int number, struct string *cgname, int state)
{
	PE_SetPartsCG(number, cgname, 0, state);
	return true;
}
#include "pe_v14_stubs.h"

/* Fill the v14-only names into the runtime library table. Called from
 * PartsEngine_PostLink, gated on the v14 declaration set; register only
 * fills entries that are still NULL after link_libraries(). */
static void pe_v14_register_batch(void)
{
	struct static_library *lib = &lib_PartsEngine;
#if 0 // BISECT round 3: group A (constructive) disabled, rest enabled
	static_library_register(lib, "AddPartsConstructionProcess", PartsEngine_AddPartsConstructionProcess);
	static_library_register(lib, "SetPanelSize", PE_v14_SetPanelSize);
	static_library_register(lib, "SetPanelColor", PE_v14_SetPanelColor);
#endif // BISECT round 3
	static_library_register(lib, "NumofChild", PE_v14_NumofChild);
	static_library_register(lib, "Parts_SetComment", PE_v14_Parts_SetComment);
	static_library_register(lib, "Parts_GetPartsSize", PE_GetPartsSize);
	static_library_register(lib, "Parts_GetPartsCGDeform", PE_GetPartsCGDeform);
	static_library_register(lib, "Parts_GetParentPartsNumber", PE_GetParentPartsNumber);
	static_library_register(lib, "GetComponentAddColorR", PE_v14_GetComponentAddColorR);
	static_library_register(lib, "GetComponentAddColorG", PE_v14_GetComponentAddColorG);
	static_library_register(lib, "GetComponentAddColorB", PE_v14_GetComponentAddColorB);
	static_library_register(lib, "GetComponentMulColorR", PE_v14_GetComponentMulColorR);
	static_library_register(lib, "GetComponentMulColorG", PE_v14_GetComponentMulColorG);
	static_library_register(lib, "GetComponentMulColorB", PE_v14_GetComponentMulColorB);
	static_library_register(lib, "GetComponentMagX", PE_v14_GetComponentMagX);
	static_library_register(lib, "GetComponentMagY", PE_v14_GetComponentMagY);
	static_library_register(lib, "SetComponentEnableClipArea", PE_v14_SetComponentEnableClipArea);
	static_library_register(lib, "SetComponentClipArea", PE_v14_SetComponentClipArea);
	static_library_register(lib, "GetComponentClipAreaPosX", PE_v14_GetComponentClipAreaPosX);
	static_library_register(lib, "GetComponentClipAreaPosY", PE_v14_GetComponentClipAreaPosY);
	static_library_register(lib, "GetComponentClipAreaPosWidth", PE_v14_GetComponentClipAreaPosWidth);
	static_library_register(lib, "GetComponentClipAreaPosHeight", PE_v14_GetComponentClipAreaPosHeight);
	static_library_register(lib, "SetComponentReverseLR", PE_v14_SetComponentReverseLR);
	static_library_register(lib, "GetMessageWindowText", PE_v14_GetMessageWindowText);
	static_library_register(lib, "GetMessageWindowFlatName", PE_v14_GetMessageWindowFlatName);
	static_library_register(lib, "SetMessageWindowTextOriginPosMode", PE_v14_SetMessageWindowTextOriginPosMode);
#if 0 // BISECT round 3
	static_library_register(lib, "SaveBackScene", PE_v14_SaveBackScene);
#endif
#if 1 // BISECT: prelink stubs disabled for fault isolation
	(void)0;
#else
#include "pe_v14_prelink.h"
#endif
}

static void PartsEngine_PreLink(void)
{
	struct ain_hll_function *fun;
	int libno = ain_get_library(ain, "PartsEngine");
	assert(libno >= 0);

	// v14 signature variants (declaration-driven, not version-driven)
	fun = get_fun(libno, "UpdateComponent");
	if (fun && fun->nr_arguments == 5) {
		static_library_replace(&lib_PartsEngine, "UpdateComponent",
				PE_v14_UpdateComponent);
	}

	if (get_fun(libno, "GetMessageUniqueID")) {
		extern void pe_v14_message_replace(void);
		pe_v14_message_replace();
	}

	fun = get_fun(libno, "Save");
	if (fun && fun->nr_arguments >= 1
			&& fun->arguments[0].type.data == AIN_WRAP) {
		static_library_replace(&lib_PartsEngine, "Save", PE_v14_Save);
		static_library_replace(&lib_PartsEngine, "SaveWithoutHideParts",
				PE_v14_SaveWithoutHideParts);
		static_library_replace(&lib_PartsEngine, "Load", PE_v14_Load);
	}

	fun = get_fun(libno, "RemoveController");
	if (fun && fun->nr_arguments >= 1
			&& fun->arguments[0].type.data == AIN_WRAP) {
		static_library_replace(&lib_PartsEngine, "RemoveController",
				PE_v14_RemoveController);
	}

	fun = get_fun(libno, "AddDrawCutCGToPartsConstructionProcess");
	if (fun && fun->nr_arguments == 12) {
		static_library_replace(&lib_PartsEngine, "AddDrawCutCGToPartsConstructionProcess",
				PE_AddDrawCutCGToPartsConstructionProcess);
	}
	fun = get_fun(libno, "AddCopyCutCGToPartsConstructionProcess");
	if (fun && fun->nr_arguments == 12) {
		static_library_replace(&lib_PartsEngine, "AddCopyCutCGToPartsConstructionProcess",
				PE_AddCopyCutCGToPartsConstructionProcess);
	}
	fun = get_fun(libno, "Update");
	if (fun && fun->nr_arguments == 5) {
		static_library_replace(&lib_PartsEngine, "Update",
				PartsEngine_Update_Pascha3PC);
	}
	if (get_fun(libno, "AddController")) {
		PE_enable_multi_controller();
	}
}

// Runs after link_libraries(): static_library_register() requires the
// runtime library table to exist.
static void PartsEngine_PostLink(void)
{
	int libno = ain_get_library(ain, "PartsEngine");
	if (libno < 0)
		return;
	// v14 Activity system + pactex loader (declaration-driven)
	if (get_fun(libno, "CreateActivity")) {
		extern void pe_v14_activity_prelink(void);
		pe_v14_activity_prelink();
	}

	// v14 new functions (not in the upstream export table)
	if (get_fun(libno, "GetMessageUniqueID")) {
		extern void pe_v14_message_register(void);
		pe_v14_message_register();
		pe_v14_register_batch();
	}
}
