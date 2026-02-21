/* SealEngine HLL library for Dohna Dohna (System43 / AIN v14)
 *
 * SealEngine is AliceSoft's next-generation 3D rendering HLL, successor
 * to ReignEngine/TapirEngine. It is used exclusively by Dohna Dohna
 * (no 2D DrawGraph fallback). This implementation maps SealEngine's 248
 * functions onto the existing ReignEngine/TapirEngine infrastructure.
 *
 * Copyright (C) 2026
 * Based on ReignEngine.c by kichikuou <KichikuouChrome@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <cglm/cglm.h>
#include <stdlib.h>
#include <string.h>

#include "system4.h"
#include "system4/aar.h"
#include "system4/string.h"

#include "asset_manager.h"
#include "hll.h"
#include "reign.h"
#include "sact.h"
#include "vm.h"
#include "vm/heap.h"
#include "vm/page.h"

/* ============================================================
 * Plugin management
 * SealEngine auto-creates plugins (no explicit CreatePlugin).
 * GetNumofPlugin() returns 1, IsExistPlugin(0) returns true.
 * ============================================================ */

#define SE_MAX_PLUGINS 2

static struct RE_plugin *se_plugins[SE_MAX_PLUGINS];
static bool se_initialized = false;
static int se_mag_speed = 1;

/* Seal-specific fields stored per-plugin */
static struct {
	float shadow_rate;
	vec3 shadow_light_vec;
	float shadow_min_radius;
	float soft_fog_edge_length;
	float edge_length;
	float edge_reduction_rate;
	vec3 edge_color;
	bool draw_dof;
	float dof_L;
	float dof_F;
	float dof_f;
	int light_param_count;
	float light_params[64];
	bool thread_loading_mode;
} se_plugin_ext[SE_MAX_PLUGINS];

/* Seal-specific fields stored per-instance */
#define SE_MAX_INSTANCES_PER_PLUGIN 1024
static struct {
	float grayscale_rate;
	bool use_mag_speed;
	bool loading;
} se_instance_ext[SE_MAX_PLUGINS][SE_MAX_INSTANCES_PER_PLUGIN];

static void se_ensure_init(void)
{
	if (se_initialized)
		return;
	se_initialized = true;
	memset(se_plugin_ext, 0, sizeof(se_plugin_ext));
	memset(se_instance_ext, 0, sizeof(se_instance_ext));
	/* Auto-create plugin 0: try ReignData.red first, fallback to Pact.afa */
	se_plugins[0] = RE_plugin_new(RE_TAPIR_PLUGIN);
	if (!se_plugins[0]) {
		struct archive *pact = asset_get_archive(ASSET_PACT);
		if (pact) {
			WARNING("SealEngine: using Pact.afa as 3D data source");
			se_plugins[0] = RE_plugin_new_with_archive(RE_TAPIR_PLUGIN, pact);
		}
	}
	if (!se_plugins[0])
		WARNING("SealEngine: failed to initialize plugin 0 (no 3D data source)");
}

static struct RE_plugin *se_get_plugin(int plugin)
{
	se_ensure_init();
	return (unsigned)plugin < SE_MAX_PLUGINS ? se_plugins[plugin] : NULL;
}

static struct RE_instance *se_get_instance(int plugin, int instance)
{
	struct RE_plugin *rp = se_get_plugin(plugin);
	if (!rp)
		return NULL;
	return (unsigned)instance < (unsigned)rp->nr_instances ? rp->instances[instance] : NULL;
}

static struct motion *se_get_motion(int plugin, int instance)
{
	struct RE_instance *ri = se_get_instance(plugin, instance);
	return ri ? ri->motion : NULL;
}

static struct motion *se_get_next_motion(int plugin, int instance)
{
	struct RE_instance *ri = se_get_instance(plugin, instance);
	return ri ? ri->next_motion : NULL;
}

static struct particle_effect *se_get_effect(int plugin, int instance)
{
	struct RE_instance *ri = se_get_instance(plugin, instance);
	return ri ? ri->effect : NULL;
}

/* [0] GetNumofPlugin() -> int */
static int SealEngine_GetNumofPlugin(void)
{
	se_ensure_init();
	return 1;
}

/* [1] IsExistPlugin(PluginNumber:int) -> bool */
static bool SealEngine_IsExistPlugin(int plugin)
{
	return se_get_plugin(plugin) != NULL;
}

/* [2] IsExist3DFile(FileName:string, Type:int) -> bool */
static bool SealEngine_IsExist3DFile(struct string *filename, int type)
{
	/* TODO: check if file exists in .afa archive */
	return true;
}

/* [3] IsExistPolyMotion(FileName:string) -> bool */
static bool SealEngine_IsExistPolyMotion(struct string *filename)
{
	return true;
}

/* [4] GetMotionNameList(FileName:string) -> ??? (type 79) */
HLL_WARN_UNIMPLEMENTED(0, int, SealEngine, GetMotionNameList, struct string *filename);

/* [5] UpdateAFA() -> void */
static void SealEngine_UpdateAFA(void)
{
	/* Refresh AFA file listing - no-op for now */
}

/* [6] SetMagSpeed(MagSpeed:int) -> void */
static void SealEngine_SetMagSpeed(int mag_speed)
{
	static int log_count = 0;
	if (log_count < 5) {
		WARNING("SealEngine.SetMagSpeed(%d)", mag_speed);
		log_count++;
	}
	se_mag_speed = mag_speed;
}

/* [7] CreateInstance(nPlugin:int) -> int */
static int SealEngine_CreateInstance(int plugin)
{
	struct RE_plugin *p = se_get_plugin(plugin);
	if (!p)
		return -1;
	int id = RE_create_instance(p);
	static int log_count = 0;
	if (log_count < 5) {
		WARNING("SealEngine.CreateInstance(plugin=%d) -> %d", plugin, id);
		log_count++;
	}
	return id;
}

/* [8] ReleaseInstance(nPlugin:int, nInstance:int) -> bool */
static bool SealEngine_ReleaseInstance(int plugin, int instance)
{
	return RE_release_instance(se_get_plugin(plugin), instance);
}

/* [9] ThreadLoadPolyInstanceList(nPlugin:int, listInstance:array<int>, listName:array<string>) -> bool
 * Synchronous implementation: load all instances immediately.
 * Returns FALSE to indicate "loading complete" (TRUE = "still loading"). */
static bool SealEngine_ThreadLoadPolyInstanceList(int plugin, struct page *instance_list, struct page *name_list)
{
	struct RE_plugin *p = se_get_plugin(plugin);
	if (!p) return false;

	if (!instance_list || !name_list) return false;

	int count = instance_list->nr_vars;
	if (name_list->nr_vars < count)
		count = name_list->nr_vars;

	for (int i = 0; i < count; i++) {
		int inst_id = instance_list->values[i].i;
		int name_slot = name_list->values[i].i;
		struct string *name = heap_get_string(name_slot);
		if (!name) continue;

		struct RE_instance *ri = se_get_instance(plugin, inst_id);
		if (!ri) continue;

		RE_instance_load(ri, name->text);
		if ((unsigned)inst_id < SE_MAX_INSTANCES_PER_PLUGIN)
			se_instance_ext[plugin][inst_id].loading = false;
	}
	return false;  /* "loading complete" */
}

/* [10] LoadInstance(nPlugin:int, nInstance:int, pIName:string) -> bool */
static bool SealEngine_LoadInstance(int plugin, int instance, struct string *name)
{
	static int log_count = 0;
	if (log_count < 5) {
		WARNING("SealEngine.LoadInstance(plugin=%d, instance=%d, name=\"%s\")",
			plugin, instance, name ? name->text : "(null)");
		log_count++;
	}
	struct RE_instance *ri = se_get_instance(plugin, instance);
	if (!ri)
		return false;
	bool result = RE_instance_load(ri, name->text);
	if ((unsigned)instance < SE_MAX_INSTANCES_PER_PLUGIN)
		se_instance_ext[plugin][instance].loading = false;
	return result;
}

/* [11] SaveInstance */
HLL_WARN_UNIMPLEMENTED(true, bool, SealEngine, SaveInstance,
	int plugin, int instance, struct string *filename);

/* [12] IsInstanceLoading(PluginNumber:int, InstanceNumber:int) -> bool */
static bool SealEngine_IsInstanceLoading(int plugin, int instance)
{
	/* Synchronous loading -> always false */
	return false;
}

/* [13] CreatePolygonInstance */
HLL_WARN_UNIMPLEMENTED(true, bool, SealEngine, CreatePolygonInstance,
	int plugin, int instance,
	/*hll_param*/ int xList, /*hll_param*/ int yList, /*hll_param*/ int zList,
	/*hll_param*/ int rList, /*hll_param*/ int gList, /*hll_param*/ int bList,
	bool withCollision);

/* [14] IsExistInstanceData */
static bool SealEngine_IsExistInstanceData(int plugin, int instance, struct string *filename)
{
	return true;
}

/* [15] GetInstanceName */
HLL_WARN_UNIMPLEMENTED(true, bool, SealEngine, GetInstanceName,
	int plugin, int instance, /*hll_param*/ int pIName);

/* [16] SetInstanceType(nPlugin:int, nInstance:int, nType:int) -> bool */
static bool SealEngine_SetInstanceType(int plugin, int instance, int type)
{
	return RE_instance_set_type(se_get_instance(plugin, instance), type);
}

/* [17] SetInstancePos */
static bool SealEngine_SetInstancePos(int plugin, int instance, float x, float y, float z)
{
	struct RE_instance *ri = se_get_instance(plugin, instance);
	if (!ri)
		return false;
	ri->pos[0] = x;
	ri->pos[1] = y;
	ri->pos[2] = -z;
	ri->local_transform_needs_update = true;
	return true;
}

/* [18] GetInstancePos */
static bool SealEngine_GetInstancePos(int plugin, int instance, float *x, float *y, float *z)
{
	struct RE_instance *ri = se_get_instance(plugin, instance);
	if (!ri)
		return false;
	*x = ri->pos[0];
	*y = ri->pos[1];
	*z = -ri->pos[2];
	return true;
}

/* [19-21] SetInstanceAngle / AngleP / AngleB */
static bool SealEngine_SetInstanceAngle(int plugin, int instance, float angle)
{
	struct RE_instance *ri = se_get_instance(plugin, instance);
	if (!ri) return false;
	ri->yaw = -angle;
	ri->local_transform_needs_update = true;
	return true;
}

static bool SealEngine_SetInstanceAngleP(int plugin, int instance, float angle_p)
{
	struct RE_instance *ri = se_get_instance(plugin, instance);
	if (!ri) return false;
	ri->pitch = angle_p;
	ri->local_transform_needs_update = true;
	return true;
}

static bool SealEngine_SetInstanceAngleB(int plugin, int instance, float angle_b)
{
	struct RE_instance *ri = se_get_instance(plugin, instance);
	if (!ri) return false;
	ri->roll = angle_b;
	ri->local_transform_needs_update = true;
	return true;
}

/* [22-24] GetInstanceAngle / AngleP / AngleB */
static bool SealEngine_GetInstanceAngle(int plugin, int instance, float *angle)
{
	struct RE_instance *ri = se_get_instance(plugin, instance);
	if (!ri) return false;
	*angle = -ri->yaw;
	return true;
}

static bool SealEngine_GetInstanceAngleP(int plugin, int instance, float *angle_p)
{
	struct RE_instance *ri = se_get_instance(plugin, instance);
	if (!ri) return false;
	*angle_p = ri->pitch;
	return true;
}

static bool SealEngine_GetInstanceAngleB(int plugin, int instance, float *angle_b)
{
	struct RE_instance *ri = se_get_instance(plugin, instance);
	if (!ri) return false;
	*angle_b = ri->roll;
	return true;
}

/* [25-30] Scale get/set */
static float SealEngine_GetInstanceScaleX(int p, int i) { struct RE_instance *ri = se_get_instance(p,i); return ri ? ri->scale[0] : 0.0; }
static float SealEngine_GetInstanceScaleY(int p, int i) { struct RE_instance *ri = se_get_instance(p,i); return ri ? ri->scale[1] : 0.0; }
static float SealEngine_GetInstanceScaleZ(int p, int i) { struct RE_instance *ri = se_get_instance(p,i); return ri ? ri->scale[2] : 0.0; }

static bool SealEngine_SetInstanceScaleX(int p, int i, float v) {
	struct RE_instance *ri = se_get_instance(p,i); if (!ri) return false;
	ri->scale[0] = v; ri->local_transform_needs_update = true; return true;
}
static bool SealEngine_SetInstanceScaleY(int p, int i, float v) {
	struct RE_instance *ri = se_get_instance(p,i); if (!ri) return false;
	ri->scale[1] = v; ri->local_transform_needs_update = true; return true;
}
static bool SealEngine_SetInstanceScaleZ(int p, int i, float v) {
	struct RE_instance *ri = se_get_instance(p,i); if (!ri) return false;
	ri->scale[2] = v; ri->local_transform_needs_update = true; return true;
}

/* [31] SetInstanceVertexPos */
static bool SealEngine_SetInstanceVertexPos(int p, int i, int idx, float x, float y, float z)
{
	return RE_instance_set_vertex_pos(se_get_instance(p, i), idx, x, y, z);
}

/* [32] SetInstanceVertexUV */
HLL_WARN_UNIMPLEMENTED(true, bool, SealEngine, SetInstanceVertexUV,
	int plugin, int instance, int index, float u, float v);

/* [33-34] Diffuse */
static bool SealEngine_SetInstanceDiffuse(int p, int i, float r, float g, float b)
{
	struct RE_instance *ri = se_get_instance(p,i); if (!ri) return false;
	ri->diffuse[0]=r; ri->diffuse[1]=g; ri->diffuse[2]=b; return true;
}
static bool SealEngine_GetInstanceDiffuse(int p, int i, float *r, float *g, float *b)
{
	struct RE_instance *ri = se_get_instance(p,i); if (!ri) return false;
	*r=ri->diffuse[0]; *g=ri->diffuse[1]; *b=ri->diffuse[2]; return true;
}

/* [35-36] Ambient */
static bool SealEngine_SetInstanceAmbient(int p, int i, float r, float g, float b)
{
	struct RE_instance *ri = se_get_instance(p,i); if (!ri) return false;
	ri->ambient[0]=r; ri->ambient[1]=g; ri->ambient[2]=b; return true;
}
static bool SealEngine_GetInstanceAmbient(int p, int i, float *r, float *g, float *b)
{
	struct RE_instance *ri = se_get_instance(p,i); if (!ri) return false;
	*r=ri->ambient[0]; *g=ri->ambient[1]; *b=ri->ambient[2]; return true;
}

/* [37-38] Alpha */
static bool SealEngine_SetInstanceAlpha(int p, int i, float a)
{
	struct RE_instance *ri = se_get_instance(p,i); if (!ri) return false;
	ri->alpha = a; return true;
}
static bool SealEngine_GetInstanceAlpha(int p, int i, float *a)
{
	struct RE_instance *ri = se_get_instance(p,i); if (!ri) return false;
	*a = ri->alpha; return true;
}

/* [39-40] GrayscaleRate (Seal-specific) */
static bool SealEngine_SetInstanceGrayscaleRate(int p, int i, float rate)
{
	if ((unsigned)p >= SE_MAX_PLUGINS || (unsigned)i >= SE_MAX_INSTANCES_PER_PLUGIN) return false;
	se_instance_ext[p][i].grayscale_rate = rate; return true;
}
static bool SealEngine_GetInstanceGrayscaleRate(int p, int i, float *rate)
{
	if ((unsigned)p >= SE_MAX_PLUGINS || (unsigned)i >= SE_MAX_INSTANCES_PER_PLUGIN) return false;
	*rate = se_instance_ext[p][i].grayscale_rate; return true;
}

/* [41-46] Draw flags */
static bool SealEngine_SetInstanceDraw(int p, int i, bool f) {
	struct RE_instance *ri = se_get_instance(p,i); if (!ri) return false; ri->draw = f; return true;
}
static bool SealEngine_SetInstanceDrawShadow(int p, int i, bool f) {
	struct RE_instance *ri = se_get_instance(p,i); if (!ri) return false; ri->draw_shadow = f; return true;
}
static bool SealEngine_SetInstanceDrawMakeShadow(int p, int i, bool f) {
	struct RE_instance *ri = se_get_instance(p,i); if (!ri) return false; ri->make_shadow = f; return true;
}
static bool SealEngine_GetInstanceDraw(int p, int i) {
	struct RE_instance *ri = se_get_instance(p,i); return ri ? ri->draw : false;
}
static bool SealEngine_GetInstanceDrawShadow(int p, int i) {
	struct RE_instance *ri = se_get_instance(p,i); return ri ? ri->draw_shadow : false;
}
static bool SealEngine_GetInstanceDrawMakeShadow(int p, int i) {
	struct RE_instance *ri = se_get_instance(p,i); return ri ? ri->make_shadow : false;
}

/* [47-48] DrawParam (TapirEngine extension) */
HLL_WARN_UNIMPLEMENTED(true, bool, SealEngine, SetInstanceDrawParam, int p, int i, int param, int value);
HLL_WARN_UNIMPLEMENTED(true, bool, SealEngine, GetInstanceDrawParam, int p, int i, int param, /*hll_param*/ int value);

/* [49-66] Motion control - delegates to RE_motion_* */
static bool SealEngine_LoadInstanceMotion(int p, int i, struct string *name) {
	return RE_instance_load_motion(se_get_instance(p, i), name->text);
}
static bool SealEngine_IsExistInstanceMotion(int p, int i, struct string *name) {
	/* TODO: check if motion file exists */
	return true;
}
static int SealEngine_GetInstanceMotionState(int p, int i) {
	return RE_motion_get_state(se_get_motion(p, i));
}
static float SealEngine_GetInstanceMotionFrame(int p, int i) {
	return RE_motion_get_frame(se_get_motion(p, i));
}
static bool SealEngine_SetInstanceMotionState(int p, int i, int state) {
	return RE_motion_set_state(se_get_motion(p, i), state);
}
static bool SealEngine_SetInstanceMotionFrame(int p, int i, float frame) {
	return RE_motion_set_frame(se_get_motion(p, i), frame);
}
static bool SealEngine_SetInstanceMotionFrameRange(int p, int i, float begin, float end) {
	return RE_motion_set_frame_range(se_get_motion(p, i), begin, end);
}
static bool SealEngine_SetInstanceMotionLoopFrameRange(int p, int i, float begin, float end) {
	return RE_motion_set_loop_frame_range(se_get_motion(p, i), begin, end);
}
static bool SealEngine_LoadInstanceNextMotion(int p, int i, struct string *name) {
	return RE_instance_load_next_motion(se_get_instance(p, i), name->text);
}
static bool SealEngine_SetInstanceNextMotionState(int p, int i, int state) {
	return RE_motion_set_state(se_get_next_motion(p, i), state);
}
static bool SealEngine_SetInstanceNextMotionFrame(int p, int i, float frame) {
	return RE_motion_set_frame(se_get_next_motion(p, i), frame);
}
static bool SealEngine_SetInstanceNextMotionFrameRange(int p, int i, float begin, float end) {
	return RE_motion_set_frame_range(se_get_next_motion(p, i), begin, end);
}
static bool SealEngine_SetInstanceNextMotionLoopFrameRange(int p, int i, float begin, float end) {
	return RE_motion_set_loop_frame_range(se_get_next_motion(p, i), begin, end);
}
static bool SealEngine_SetInstanceMotionBlendRate(int p, int i, float rate) {
	struct RE_instance *ri = se_get_instance(p,i); if (!ri) return false;
	ri->motion_blend_rate = rate; return true;
}
static bool SealEngine_SetInstanceMotionBlend(int p, int i, bool blend) {
	struct RE_instance *ri = se_get_instance(p,i); if (!ri) return false;
	ri->motion_blend = blend; return true;
}
static bool SealEngine_IsInstanceMotionBlend(int p, int i) {
	struct RE_instance *ri = se_get_instance(p,i); return ri ? ri->motion_blend : false;
}
static bool SealEngine_SwapInstanceMotion(int p, int i) {
	struct RE_instance *ri = se_get_instance(p, i);
	if (!ri) return false;
	struct motion *tmp = ri->motion; ri->motion = ri->next_motion; ri->next_motion = tmp;
	return true;
}
static bool SealEngine_FreeInstanceNextMotion(int p, int i) {
	return RE_instance_free_next_motion(se_get_instance(p, i));
}

/* [67-71] Material */
HLL_WARN_UNIMPLEMENTED(0, int, SealEngine, GetInstanceNumofMaterial, int p, int i);
HLL_WARN_UNIMPLEMENTED(true, bool, SealEngine, GetInstanceMaterialName, int p, int i, int n, /*hll_param*/ int name);
HLL_WARN_UNIMPLEMENTED(0.0, float, SealEngine, GetInstanceMaterialParam, int p, int i, int mat, int type);
HLL_WARN_UNIMPLEMENTED(true, bool, SealEngine, SetInstanceMaterialParam, int p, int i, int mat, int type, float param);
HLL_WARN_UNIMPLEMENTED(true, bool, SealEngine, SaveInstanceAddMaterialData, int p, int i);

/* [72-73] Target */
static bool SealEngine_SetInstanceTarget(int p, int i, int idx, int target) {
	struct RE_instance *ri = se_get_instance(p,i);
	if (!ri || idx < 0 || idx >= RE_NR_INSTANCE_TARGETS) return false;
	ri->target[idx] = target; return true;
}
static int SealEngine_GetInstanceTarget(int p, int i, int idx) {
	struct RE_instance *ri = se_get_instance(p,i);
	if (!ri || idx < 0 || idx >= RE_NR_INSTANCE_TARGETS) return -1;
	return ri->target[idx];
}

/* [74-75] FPS */
static float SealEngine_GetInstanceFPS(int p, int i) {
	struct RE_instance *ri = se_get_instance(p,i); return ri ? ri->fps : 0.0;
}
static bool SealEngine_SetInstanceFPS(int p, int i, float fps) {
	struct RE_instance *ri = se_get_instance(p,i); if (!ri) return false;
	ri->fps = fps; return true;
}

/* [76-80] Bone info */
HLL_WARN_UNIMPLEMENTED(0, int, SealEngine, GetInstanceNumofBone, int p, int i);
HLL_WARN_UNIMPLEMENTED(true, bool, SealEngine, GetInstanceBoneName, int p, int i, int bone, /*hll_param*/ int name);
HLL_WARN_UNIMPLEMENTED(true, bool, SealEngine, GetInstanceBoneParentIndex, int p, int i, int bone, /*hll_param*/ int parent);

static int SealEngine_GetInstanceBoneIndex(int p, int i, struct string *name) {
	return RE_instance_get_bone_index(se_get_instance(p, i), name->text);
}

static bool SealEngine_TransInstanceLocalPosToWorldPosByBone(int p, int i, int bone,
	float ox, float oy, float oz, float *x, float *y, float *z)
{
	vec3 offset = {ox, oy, -oz}, result;
	if (!RE_instance_trans_local_pos_to_world_pos_by_bone(se_get_instance(p, i), bone, offset, result))
		return false;
	*x = result[0]; *y = result[1]; *z = -result[2];
	return true;
}

/* [81-107] Bone physics / collision - stubs */
HLL_WARN_UNIMPLEMENTED(true, bool, SealEngine, SaveBoneFile, int p, int i);
HLL_WARN_UNIMPLEMENTED(false, bool, SealEngine, IsBoneCanIK, int p, int i, int bone);
HLL_WARN_UNIMPLEMENTED(true, bool, SealEngine, SetBoneCanIK, int p, int i, int bone, bool can);
HLL_WARN_UNIMPLEMENTED(true, bool, SealEngine, GetBoneEulerAngle, int p, int i, int bone, /*hll_param*/ int ep, /*hll_param*/ int eh, /*hll_param*/ int eb);
HLL_WARN_UNIMPLEMENTED(true, bool, SealEngine, GetBoneMinEulerAngle, int p, int i, int bone, /*hll_param*/ int ep, /*hll_param*/ int eh, /*hll_param*/ int eb);
HLL_WARN_UNIMPLEMENTED(true, bool, SealEngine, GetBoneMaxEulerAngle, int p, int i, int bone, /*hll_param*/ int ep, /*hll_param*/ int eh, /*hll_param*/ int eb);
HLL_WARN_UNIMPLEMENTED(true, bool, SealEngine, SetBoneMinEulerAngle, int p, int i, int bone, float ep, float eh, float eb);
HLL_WARN_UNIMPLEMENTED(true, bool, SealEngine, SetBoneMaxEulerAngle, int p, int i, int bone, float ep, float eh, float eb);
HLL_WARN_UNIMPLEMENTED(true, bool, SealEngine, SetBoneMass, int p, int i, int bone, float mass);
HLL_WARN_UNIMPLEMENTED(true, bool, SealEngine, SetBoneAirResistance, int p, int i, int bone, float ar);
HLL_WARN_UNIMPLEMENTED(true, bool, SealEngine, SetBoneRestitutionCoefficient, int p, int i, int bone, float rc);
HLL_WARN_UNIMPLEMENTED(0.0, float, SealEngine, GetBoneMass, int p, int i, int bone);
HLL_WARN_UNIMPLEMENTED(0.0, float, SealEngine, GetBoneAirResistance, int p, int i, int bone);
HLL_WARN_UNIMPLEMENTED(0.0, float, SealEngine, GetBoneRestitutionCoefficient, int p, int i, int bone);
HLL_WARN_UNIMPLEMENTED(0, int, SealEngine, GetNumofBoneCollisionShapeList, int p, int i);
HLL_WARN_UNIMPLEMENTED(0, int, SealEngine, GetBoneCollisionShapeType, int p, int i, int idx);
HLL_WARN_UNIMPLEMENTED(true, bool, SealEngine, GetBoneCollisionShapePoint, int p, int i, int idx, /*hll_param*/ int x, /*hll_param*/ int y, /*hll_param*/ int z);
HLL_WARN_UNIMPLEMENTED(true, bool, SealEngine, GetBoneCollisionShapePoint2, int p, int i, int idx, /*hll_param*/ int x, /*hll_param*/ int y, /*hll_param*/ int z);
HLL_WARN_UNIMPLEMENTED(0.0, float, SealEngine, GetBoneCollisionShapeRadius, int p, int i, int idx);
HLL_WARN_UNIMPLEMENTED(0, int, SealEngine, GetBoneCollisionShapeLinkedBoneIndex, int p, int i, int idx);
HLL_WARN_UNIMPLEMENTED(true, bool, SealEngine, SetBoneCollisionShapeType, int p, int i, int idx, int type);
HLL_WARN_UNIMPLEMENTED(true, bool, SealEngine, SetBoneCollisionShapePoint, int p, int i, int idx, float x, float y, float z);
HLL_WARN_UNIMPLEMENTED(true, bool, SealEngine, SetBoneCollisionShapePoint2, int p, int i, int idx, float x, float y, float z);
HLL_WARN_UNIMPLEMENTED(true, bool, SealEngine, SetBoneCollisionShapeRadius, int p, int i, int idx, float r);
HLL_WARN_UNIMPLEMENTED(true, bool, SealEngine, SetBoneCollisionShapeLinkedBoneIndex, int p, int i, int idx, int bone);
HLL_WARN_UNIMPLEMENTED(true, bool, SealEngine, AddBoneCollisionShape, int p, int i);
HLL_WARN_UNIMPLEMENTED(true, bool, SealEngine, EraseBoneCollisionShape, int p, int i, int idx);

/* [108-120] Mesh/polygon info */
HLL_WARN_UNIMPLEMENTED(0, int, SealEngine, GetInstanceNumofPolygon, int p, int i);
HLL_WARN_UNIMPLEMENTED(0, int, SealEngine, GetInstanceNumofVertex, int p, int i);
HLL_WARN_UNIMPLEMENTED(0, int, SealEngine, GetInstanceNumofNormal, int p, int i);
HLL_WARN_UNIMPLEMENTED(0, int, SealEngine, GetInstanceNumofMesh, int p, int i);
HLL_WARN_UNIMPLEMENTED(true, bool, SealEngine, GetInstanceMeshName, int p, int i, int mesh, /*hll_param*/ int name);
HLL_WARN_UNIMPLEMENTED(0, int, SealEngine, GetInstanceMeshMaterialIndex, int p, int i, int mesh);
HLL_WARN_UNIMPLEMENTED(0, int, SealEngine, GetInstanceMeshMaterialID, int p, int i, int mesh);
HLL_WARN_UNIMPLEMENTED(0, int, SealEngine, GetInstanceMeshNumofPolygon, int p, int i, int mesh);
HLL_WARN_UNIMPLEMENTED(false, bool, SealEngine, IsInstanceMeshAlphaBlending, int p, int i, int mesh);
HLL_WARN_UNIMPLEMENTED(0, int, SealEngine, GetInstanceTextureMemorySize, int p, int i);
HLL_WARN_UNIMPLEMENTED(0, int, SealEngine, GetInstanceInfoText, int p, int i);
HLL_WARN_UNIMPLEMENTED(0, int, SealEngine, GetInstanceMaterialInfoText, int p, int i);
HLL_WARN_UNIMPLEMENTED(true, bool, SealEngine, GetInstanceAABB, int p, int i,
	/*hll_param*/ int minx, /*hll_param*/ int miny, /*hll_param*/ int minz,
	/*hll_param*/ int maxx, /*hll_param*/ int maxy, /*hll_param*/ int maxz);

/* [121] CalcInstanceHeightDetection */
static bool SealEngine_CalcInstanceHeightDetection(int p, int i, float x, float z, float *height)
{
	*height = RE_instance_calc_height(se_get_instance(p, i), x, -z);
	return true;
}

/* [122-123] LineList (debug lines) */
HLL_WARN_UNIMPLEMENTED(true, bool, SealEngine, ClearLineList, int p, int i);
HLL_WARN_UNIMPLEMENTED(true, bool, SealEngine, AddLineList, int p, int i,
	float x0, float y0, float z0, int c0, float x1, float y1, float z1, int c1);

/* [124-125] ShadowVolumeBoneRadius */
static float SealEngine_GetInstanceShadowVolumeBoneRadius(int p, int i) {
	struct RE_instance *ri = se_get_instance(p,i); return ri ? ri->shadow_volume_bone_radius : 0.0;
}
static bool SealEngine_SetInstanceShadowVolumeBoneRadius(int p, int i, float r) {
	struct RE_instance *ri = se_get_instance(p,i); if (!ri) return false;
	ri->shadow_volume_bone_radius = r; return true;
}

/* [126-127] Light param load/store */
HLL_WARN_UNIMPLEMENTED(true, bool, SealEngine, LoadInstanceLightParam, int p, int i);
HLL_WARN_UNIMPLEMENTED(true, bool, SealEngine, StoreInstanceLightParam, int p, int i);

/* [128-129] UseMagSpeed */
static bool SealEngine_SetInstanceUseMagSpeed(int p, int i, bool use) {
	if ((unsigned)p >= SE_MAX_PLUGINS || (unsigned)i >= SE_MAX_INSTANCES_PER_PLUGIN) return false;
	se_instance_ext[p][i].use_mag_speed = use; return true;
}
static bool SealEngine_IsInstanceUseMagSpeed(int p, int i) {
	if ((unsigned)p >= SE_MAX_PLUGINS || (unsigned)i >= SE_MAX_INSTANCES_PER_PLUGIN) return false;
	return se_instance_ext[p][i].use_mag_speed;
}

/* [130-131] DebugDrawShadowVolume */
static bool SealEngine_GetInstanceDebugDrawShadowVolume(int p, int i) {
	struct RE_instance *ri = se_get_instance(p,i);
	return ri && ri->shadow_volume_instance && ri->shadow_volume_instance->draw;
}
static bool SealEngine_SetInstanceDebugDrawShadowVolume(int p, int i, bool f) {
	return RE_instance_set_debug_draw_shadow_volume(se_get_instance(p, i), f);
}

/* [132-133] Debug bone display */
HLL_WARN_UNIMPLEMENTED(true, bool, SealEngine, CreateInstanceDebugBoneList, int p, int i, int bone_inst, int on_cursor, int selected);
HLL_WARN_UNIMPLEMENTED(true, bool, SealEngine, CreateInstanceDebugBoneCollision, int p, int i, int bone_inst, int on_cursor, int selected);

/* [134] GetEffectFrameRange */
static bool SealEngine_GetEffectFrameRange(int p, int i, int *begin, int *end) {
	return RE_effect_get_frame_range(se_get_instance(p, i), begin, end);
}

/* [135-143] Camera */
static bool SealEngine_SetCameraPos(int p, float x, float y, float z) {
	struct RE_plugin *pl = se_get_plugin(p); if (!pl) return false;
	pl->camera.pos[0] = x; pl->camera.pos[1] = y; pl->camera.pos[2] = -z; return true;
}
static bool SealEngine_SetCameraAngle(int p, float a) {
	struct RE_plugin *pl = se_get_plugin(p); if (!pl) return false;
	pl->camera.yaw = -a; return true;
}
static bool SealEngine_SetCameraAngleP(int p, float a) {
	struct RE_plugin *pl = se_get_plugin(p); if (!pl) return false;
	pl->camera.pitch = a; return true;
}
static bool SealEngine_GetCameraPos(int p, float *x, float *y, float *z) {
	struct RE_plugin *pl = se_get_plugin(p); if (!pl) return false;
	*x = pl->camera.pos[0]; *y = pl->camera.pos[1]; *z = -pl->camera.pos[2]; return true;
}
static bool SealEngine_GetCameraAngle(int p, float *a) {
	struct RE_plugin *pl = se_get_plugin(p); if (!pl) return false;
	*a = -pl->camera.yaw; return true;
}
static bool SealEngine_GetCameraAngleP(int p, float *a) {
	struct RE_plugin *pl = se_get_plugin(p); if (!pl) return false;
	*a = pl->camera.pitch; return true;
}
static bool SealEngine_GetCameraXVector(int p, float *x, float *y, float *z) {
	/* TODO: compute from camera matrix */
	*x = 1; *y = 0; *z = 0; return true;
}
static bool SealEngine_GetCameraYVector(int p, float *x, float *y, float *z) {
	*x = 0; *y = 1; *z = 0; return true;
}
static bool SealEngine_GetCameraZVector(int p, float *x, float *y, float *z) {
	*x = 0; *y = 0; *z = 1; return true;
}

/* [144-151] DOF (Seal-specific) */
static bool SealEngine_SetDrawDOF(int p, bool f) {
	if ((unsigned)p >= SE_MAX_PLUGINS) return false;
	se_plugin_ext[p].draw_dof = f; return true;
}
static bool SealEngine_SetDOF_L(int p, float v) { if ((unsigned)p >= SE_MAX_PLUGINS) return false; se_plugin_ext[p].dof_L = v; return true; }
static bool SealEngine_SetDOF_F(int p, float v) { if ((unsigned)p >= SE_MAX_PLUGINS) return false; se_plugin_ext[p].dof_F = v; return true; }
static bool SealEngine_SetDOF_f(int p, float v) { if ((unsigned)p >= SE_MAX_PLUGINS) return false; se_plugin_ext[p].dof_f = v; return true; }
static bool SealEngine_GetDrawDOF(int p, bool *f) { if ((unsigned)p >= SE_MAX_PLUGINS) return false; *f = se_plugin_ext[p].draw_dof; return true; }
static bool SealEngine_GetDOF_L(int p, float *v) { if ((unsigned)p >= SE_MAX_PLUGINS) return false; *v = se_plugin_ext[p].dof_L; return true; }
static bool SealEngine_GetDOF_F(int p, float *v) { if ((unsigned)p >= SE_MAX_PLUGINS) return false; *v = se_plugin_ext[p].dof_F; return true; }
static bool SealEngine_GetDOF_f(int p, float *v) { if ((unsigned)p >= SE_MAX_PLUGINS) return false; *v = se_plugin_ext[p].dof_f; return true; }

/* [152-166] Shadow */
static bool SealEngine_SetShadowLightVector(int p, float x, float y, float z) {
	if ((unsigned)p >= SE_MAX_PLUGINS) return false;
	se_plugin_ext[p].shadow_light_vec[0]=x; se_plugin_ext[p].shadow_light_vec[1]=y; se_plugin_ext[p].shadow_light_vec[2]=z;
	return true;
}
static bool SealEngine_GetShadowLightVector(int p, float *x, float *y, float *z) {
	if ((unsigned)p >= SE_MAX_PLUGINS) return false;
	*x=se_plugin_ext[p].shadow_light_vec[0]; *y=se_plugin_ext[p].shadow_light_vec[1]; *z=se_plugin_ext[p].shadow_light_vec[2];
	return true;
}
static bool SealEngine_SetShadowRate(int p, float r) { if ((unsigned)p >= SE_MAX_PLUGINS) return false; se_plugin_ext[p].shadow_rate = r; return true; }
static float SealEngine_GetShadowRate(int p) { return (unsigned)p < SE_MAX_PLUGINS ? se_plugin_ext[p].shadow_rate : 0; }
static float SealEngine_GetShadowTargetDistance(int p, int n) { return 0; }
static int SealEngine_GetShadowMapResolutionLevel(int p) {
	struct RE_plugin *pl = se_get_plugin(p); return pl ? pl->shadow_map_resolution_level : 0;
}
static float SealEngine_GetShadowSplitDepth(int p, int n) { return 0; }
static bool SealEngine_SetShadowMapType(int p, int type) { return true; }
static bool SealEngine_SetShadowMapLightDir(int p, float x, float y, float z) {
	struct RE_plugin *pl = se_get_plugin(p); if (!pl) return false;
	pl->shadow_map_light_dir[0]=x; pl->shadow_map_light_dir[1]=y; pl->shadow_map_light_dir[2]=-z; return true;
}
static bool SealEngine_SetShadowFilterMag(int p, float m) { return true; }
static bool SealEngine_SetShadowTargetDistance(int p, int n, float d) { return true; }
static bool SealEngine_SetShadowMapResolutionLevel(int p, int l) {
	struct RE_plugin *pl = se_get_plugin(p); if (!pl) return false;
	pl->shadow_map_resolution_level = l; return true;
}
static bool SealEngine_SetShadowSplitDepth(int p, int n, float d) { return true; }
static bool SealEngine_SetShadowMinRadius(int p, float r) { if ((unsigned)p >= SE_MAX_PLUGINS) return false; se_plugin_ext[p].shadow_min_radius = r; return true; }
static float SealEngine_GetShadowMinRadius(int p) { return (unsigned)p < SE_MAX_PLUGINS ? se_plugin_ext[p].shadow_min_radius : 0; }

/* [167-182] Fog and edge */
static bool SealEngine_SetFogType(int p, int t) { struct RE_plugin *pl = se_get_plugin(p); if (!pl) return false; pl->fog_type = t; return true; }
static bool SealEngine_SetFogNear(int p, float v) { struct RE_plugin *pl = se_get_plugin(p); if (!pl) return false; pl->fog_near = v; return true; }
static bool SealEngine_SetFogFar(int p, float v) { struct RE_plugin *pl = se_get_plugin(p); if (!pl) return false; pl->fog_far = v; return true; }
static bool SealEngine_SetFogColor(int p, float r, float g, float b) {
	struct RE_plugin *pl = se_get_plugin(p); if (!pl) return false;
	pl->fog_color[0]=r; pl->fog_color[1]=g; pl->fog_color[2]=b; return true;
}
static int SealEngine_GetFogType(int p) { struct RE_plugin *pl = se_get_plugin(p); return pl ? pl->fog_type : 0; }
static float SealEngine_GetFogNear(int p) { struct RE_plugin *pl = se_get_plugin(p); return pl ? pl->fog_near : 0; }
static float SealEngine_GetFogFar(int p) { struct RE_plugin *pl = se_get_plugin(p); return pl ? pl->fog_far : 0; }
static void SealEngine_GetFogColor(int p, float *r, float *g, float *b) {
	struct RE_plugin *pl = se_get_plugin(p); if (!pl) return;
	*r=pl->fog_color[0]; *g=pl->fog_color[1]; *b=pl->fog_color[2];
}
static bool SealEngine_SetSoftFogEdgeLength(int p, float v) { if ((unsigned)p >= SE_MAX_PLUGINS) return false; se_plugin_ext[p].soft_fog_edge_length = v; return true; }
static float SealEngine_GetSoftFogEdgeLength(int p) { return (unsigned)p < SE_MAX_PLUGINS ? se_plugin_ext[p].soft_fog_edge_length : 0; }
static bool SealEngine_SetEdgeLength(int p, float v) { if ((unsigned)p >= SE_MAX_PLUGINS) return false; se_plugin_ext[p].edge_length = v; return true; }
static float SealEngine_GetEdgeLength(int p) { return (unsigned)p < SE_MAX_PLUGINS ? se_plugin_ext[p].edge_length : 0; }
static bool SealEngine_SetEdgeReductionRate(int p, float v) { if ((unsigned)p >= SE_MAX_PLUGINS) return false; se_plugin_ext[p].edge_reduction_rate = v; return true; }
static float SealEngine_GetEdgeReductionRate(int p) { return (unsigned)p < SE_MAX_PLUGINS ? se_plugin_ext[p].edge_reduction_rate : 0; }
static bool SealEngine_SetEdgeColor(int p, float r, float g, float b) {
	if ((unsigned)p >= SE_MAX_PLUGINS) return false;
	se_plugin_ext[p].edge_color[0]=r; se_plugin_ext[p].edge_color[1]=g; se_plugin_ext[p].edge_color[2]=b; return true;
}
static bool SealEngine_GetEdgeColor(int p, float *r, float *g, float *b) {
	if ((unsigned)p >= SE_MAX_PLUGINS) return false;
	*r=se_plugin_ext[p].edge_color[0]; *g=se_plugin_ext[p].edge_color[1]; *b=se_plugin_ext[p].edge_color[2]; return true;
}

/* [183-184] Viewport / Projection */
static bool SealEngine_SetViewport(int p, int x, int y, int w, int h) {
	struct RE_plugin *plugin = se_get_plugin(p);
	if (!plugin)
		return false;
	/* Auto-bind: if renderer not yet created, allocate a sprite and bind */
	if (!plugin->renderer) {
		int sp_no = 0; /* sprite slot 0 for 3D overlay */
		struct sact_sprite *sp = sact_try_get_sprite(sp_no);
		if (!sp)
			sact_create_sprite(sp_no, w > 0 ? w : 1920, h > 0 ? h : 1080, 0, 0, 0, 255);
		RE_plugin_bind(plugin, sp_no);
		WARNING("SealEngine: auto-bind plugin %d to sprite %d (%dx%d)", p, sp_no, w, h);
	}
	return RE_set_viewport(plugin, x, y, w, h);
}
static bool SealEngine_SetProjection(int p, float w, float h, float near, float far, float deg) {
	return RE_set_projection(se_get_plugin(p), w, h, near, far, deg);
}

/* [185-204] Render/draw modes */
static bool SealEngine_SetRenderMode(int p, int m) { struct RE_plugin *pl = se_get_plugin(p); if (!pl) return false; pl->render_mode = m; return true; }
static int SealEngine_GetRenderMode(int p) { struct RE_plugin *pl = se_get_plugin(p); return pl ? pl->render_mode : 0; }
static bool SealEngine_SetDrawOption(int p, int opt, int param) {
	struct RE_plugin *pl = se_get_plugin(p);
	if (!pl || opt < 0 || opt >= RE_DRAW_OPTION_MAX) return false;
	pl->draw_options[opt] = param; return true;
}
static int SealEngine_GetDrawOption(int p, int opt) {
	struct RE_plugin *pl = se_get_plugin(p);
	if (!pl || opt < 0 || opt >= RE_DRAW_OPTION_MAX) return 0;
	return pl->draw_options[opt];
}
static bool SealEngine_SetShadowMode(int p, int m) { struct RE_plugin *pl = se_get_plugin(p); if (!pl) return false; pl->shadow_mode = m; return true; }
static bool SealEngine_SetBumpMode(int p, int m) { struct RE_plugin *pl = se_get_plugin(p); if (!pl) return false; pl->bump_mode = m; return true; }
static bool SealEngine_SetFogMode(int p, int m) { struct RE_plugin *pl = se_get_plugin(p); if (!pl) return false; pl->fog_mode = m; return true; }
static bool SealEngine_SetSpecularMode(int p, int m) { struct RE_plugin *pl = se_get_plugin(p); if (!pl) return false; pl->specular_mode = m; return true; }
static bool SealEngine_SetLightMapMode(int p, int m) { struct RE_plugin *pl = se_get_plugin(p); if (!pl) return false; pl->light_map_mode = m; return true; }
static bool SealEngine_SetSoftFogEdgeMode(int p, int m) { struct RE_plugin *pl = se_get_plugin(p); if (!pl) return false; pl->soft_fog_edge_mode = m; return true; }
static bool SealEngine_SetSSAOMode(int p, int m) { struct RE_plugin *pl = se_get_plugin(p); if (!pl) return false; pl->ssao_mode = m; return true; }
HLL_WARN_UNIMPLEMENTED(true, bool, SealEngine, SetShaderDebugMode, int p, int m);
static int SealEngine_GetShadowMode(int p) { struct RE_plugin *pl = se_get_plugin(p); return pl ? pl->shadow_mode : 0; }
static int SealEngine_GetBumpMode(int p) { struct RE_plugin *pl = se_get_plugin(p); return pl ? pl->bump_mode : 0; }
static int SealEngine_GetFogMode(int p) { struct RE_plugin *pl = se_get_plugin(p); return pl ? pl->fog_mode : 0; }
static int SealEngine_GetSpecularMode(int p) { struct RE_plugin *pl = se_get_plugin(p); return pl ? pl->specular_mode : 0; }
static int SealEngine_GetLightMapMode(int p) { struct RE_plugin *pl = se_get_plugin(p); return pl ? pl->light_map_mode : 0; }
static int SealEngine_GetSoftFogEdgeMode(int p) { struct RE_plugin *pl = se_get_plugin(p); return pl ? pl->soft_fog_edge_mode : 0; }
static int SealEngine_GetSSAOMode(int p) { struct RE_plugin *pl = se_get_plugin(p); return pl ? pl->ssao_mode : 0; }
HLL_WARN_UNIMPLEMENTED(0, int, SealEngine, GetShaderDebugMode, int p);

/* [205-206] Debug mode */
HLL_WARN_UNIMPLEMENTED(true, bool, SealEngine, SetDebugMode, int p, int type, int mode);
HLL_WARN_UNIMPLEMENTED(0, int, SealEngine, GetDebugMode, int p, int type);

/* [207-214] Texture and bloom/glare */
static int SealEngine_GetTextureResolutionLevel(int p) { struct RE_plugin *pl = se_get_plugin(p); return pl ? pl->texture_resolution_level : 0; }
static int SealEngine_GetTextureFilterMode(int p) { struct RE_plugin *pl = se_get_plugin(p); return pl ? pl->texture_filter_mode : 0; }
static bool SealEngine_SetTextureResolutionLevel(int p, int l) { struct RE_plugin *pl = se_get_plugin(p); if (!pl) return false; pl->texture_resolution_level = l; return true; }
static bool SealEngine_SetTextureFilterMode(int p, int m) { struct RE_plugin *pl = se_get_plugin(p); if (!pl) return false; pl->texture_filter_mode = m; return true; }
static int SealEngine_GetBloomMode(int p) { struct RE_plugin *pl = se_get_plugin(p); return pl ? pl->bloom_mode : 0; }
static bool SealEngine_SetBloomMode(int p, int m) { struct RE_plugin *pl = se_get_plugin(p); if (!pl) return false; pl->bloom_mode = m; return true; }
static int SealEngine_GetGlareMode(int p) { struct RE_plugin *pl = se_get_plugin(p); return pl ? pl->glare_mode : 0; }
static bool SealEngine_SetGlareMode(int p, int m) { struct RE_plugin *pl = se_get_plugin(p); if (!pl) return false; pl->glare_mode = m; return true; }

/* [215-216] SSAO param */
HLL_WARN_UNIMPLEMENTED(0.0, float, SealEngine, GetSSAOParam, int p, int type);
HLL_WARN_UNIMPLEMENTED(true, bool, SealEngine, SetSSAOParam, int p, int type, float param);

/* [217] CalcIntersectEyeVec */
HLL_WARN_UNIMPLEMENTED(false, bool, SealEngine, CalcIntersectEyeVec,
	int p, int i, int mx, int my, /*hll_param*/ int fx, /*hll_param*/ int fy, /*hll_param*/ int fz);

/* [218-223] 2D detection and pathfinding (from TapirEngine) */
static bool SealEngine_Calc2DDetectionHeight(int p, float x, float z, float *height)
{
	/* Use plugin 0's designated detection instance */
	*height = 0;
	return true;
}
static bool SealEngine_Calc2DDetection(int p, float x0, float y0, float z0,
	float x1, float y1, float z1, float radius, float *x2, float *y2, float *z2)
{
	*x2 = x1; *y2 = y1; *z2 = z1;
	return true;
}
HLL_WARN_UNIMPLEMENTED(false, bool, SealEngine, Calc2DDetectionIntersectEyeVector,
	int p, int vx, int vy, /*hll_param*/ int x, /*hll_param*/ int y, /*hll_param*/ int z);
HLL_WARN_UNIMPLEMENTED(false, bool, SealEngine, FindPath,
	int p, float sx, float sy, float sz, float gx, float gy, float gz);
HLL_WARN_UNIMPLEMENTED(true, bool, SealEngine, GetPathLine,
	int p, /*hll_param*/ int xa, /*hll_param*/ int ya, /*hll_param*/ int za);
HLL_WARN_UNIMPLEMENTED(true, bool, SealEngine, GetOptimizedPathLine,
	int p, /*hll_param*/ int xa, /*hll_param*/ int ya, /*hll_param*/ int za);

/* [224] TransformPosToViewPos */
static bool SealEngine_TransformPosToViewPos(int p, float x, float y, float z, int *vx, int *vy)
{
	/* TODO: project 3D -> 2D using camera/projection */
	*vx = (int)x;
	*vy = (int)y;
	return true;
}

/* [225] IsLoading */
static bool SealEngine_IsLoading(int p)
{
	/* No async loading - always done */
	return false;
}

/* [226-228] Light param */
static bool SealEngine_ResetLightParam(int p) {
	if ((unsigned)p >= SE_MAX_PLUGINS) return false;
	memset(se_plugin_ext[p].light_params, 0, sizeof(se_plugin_ext[p].light_params));
	return true;
}
static bool SealEngine_SetLightParam(int p, int type, float value) {
	if ((unsigned)p >= SE_MAX_PLUGINS || type < 0 || type >= 64) return false;
	se_plugin_ext[p].light_params[type] = value; return true;
}
static float SealEngine_GetLightParam(int p, int type) {
	if ((unsigned)p >= SE_MAX_PLUGINS || type < 0 || type >= 64) return 0;
	return se_plugin_ext[p].light_params[type];
}

/* [229-234] Threading and suspend */
static bool SealEngine_SetThreadLoadingMode(int p, bool mode) {
	if ((unsigned)p >= SE_MAX_PLUGINS) return false;
	se_plugin_ext[p].thread_loading_mode = mode; return true;
}
static bool SealEngine_IsThreadLoadingMode(int p) {
	return (unsigned)p < SE_MAX_PLUGINS ? se_plugin_ext[p].thread_loading_mode : false;
}
static bool SealEngine_ClearCache(int p) { return true; }
static bool SealEngine_Suspend(int p) {
	struct RE_plugin *pl = se_get_plugin(p); if (!pl) return false;
	pl->suspended = true; return true;
}
static bool SealEngine_IsSuspend(int p) {
	struct RE_plugin *pl = se_get_plugin(p); return pl ? pl->suspended : false;
}
static bool SealEngine_Resume(int p) {
	struct RE_plugin *pl = se_get_plugin(p); if (!pl) return false;
	pl->suspended = false; return true;
}

/* [235] GetHistogram */
HLL_WARN_UNIMPLEMENTED(true, bool, SealEngine, GetHistogram, int p, /*hll_param*/ int list);

/* [236] GetNumofInstance */
static int SealEngine_GetNumofInstance(int p)
{
	struct RE_plugin *pl = se_get_plugin(p);
	return pl ? pl->nr_instances : 0;
}

/* [237-247] Tool_ functions (editor-only, stubs) */
HLL_WARN_UNIMPLEMENTED(false, bool, SealEngine, Tool_GetEyeVector, int p, int vx, int vy,
	/*hll_param*/ int bx, /*hll_param*/ int by, /*hll_param*/ int bz,
	/*hll_param*/ int ex, /*hll_param*/ int ey, /*hll_param*/ int ez);
HLL_WARN_UNIMPLEMENTED(true, bool, SealEngine, Tool_ReloadEffectDataEXFile, int p, int i, struct string *text);
HLL_WARN_UNIMPLEMENTED(true, bool, SealEngine, Tool_GetEmitterCurrentPos, int p, int i, int emitter,
	/*hll_param*/ int x, /*hll_param*/ int y, /*hll_param*/ int z);
HLL_WARN_UNIMPLEMENTED(true, bool, SealEngine, Tool_GetEmitterFrameRange, int p, int i, int emitter,
	/*hll_param*/ int begin, /*hll_param*/ int end);
HLL_WARN_UNIMPLEMENTED(true, bool, SealEngine, Tool_SetEmitterValidKey, int p, int i, int emitter, int key, bool valid);
HLL_WARN_UNIMPLEMENTED(true, bool, SealEngine, Tool_SetEmitterDebugLineShow, int p, int i, int emitter, bool show);
HLL_WARN_UNIMPLEMENTED(true, bool, SealEngine, Tool_SetEmitterWireFrameShow, int p, int i, int emitter, bool show);
HLL_WARN_UNIMPLEMENTED(0.0, float, SealEngine, Tool_Calc2DBezier, float ax, float ay, float bx, float by, float rate);
HLL_WARN_UNIMPLEMENTED(true, bool, SealEngine, Tool_ReloadPolyDataEXFile, struct string *name, struct string *text);
HLL_WARN_UNIMPLEMENTED(true, bool, SealEngine, Tool_ReloadMotionDataEXFile, struct string *obj, struct string *mot, struct string *text);
HLL_WARN_UNIMPLEMENTED(0, int, SealEngine, Tool_CreateFBXAscii, int p, int i);

/* ============================================================
 * HLL_LIBRARY declaration - all 248 functions in AIN order
 * ============================================================ */
HLL_LIBRARY(SealEngine,
	HLL_EXPORT(GetNumofPlugin, SealEngine_GetNumofPlugin),
	HLL_EXPORT(IsExistPlugin, SealEngine_IsExistPlugin),
	HLL_EXPORT(IsExist3DFile, SealEngine_IsExist3DFile),
	HLL_EXPORT(IsExistPolyMotion, SealEngine_IsExistPolyMotion),
	HLL_TODO_EXPORT(GetMotionNameList, SealEngine_GetMotionNameList),
	HLL_EXPORT(UpdateAFA, SealEngine_UpdateAFA),
	HLL_EXPORT(SetMagSpeed, SealEngine_SetMagSpeed),
	HLL_EXPORT(CreateInstance, SealEngine_CreateInstance),
	HLL_EXPORT(ReleaseInstance, SealEngine_ReleaseInstance),
	HLL_EXPORT(ThreadLoadPolyInstanceList, SealEngine_ThreadLoadPolyInstanceList),
	HLL_EXPORT(LoadInstance, SealEngine_LoadInstance),
	HLL_TODO_EXPORT(SaveInstance, SealEngine_SaveInstance),
	HLL_EXPORT(IsInstanceLoading, SealEngine_IsInstanceLoading),
	HLL_TODO_EXPORT(CreatePolygonInstance, SealEngine_CreatePolygonInstance),
	HLL_EXPORT(IsExistInstanceData, SealEngine_IsExistInstanceData),
	HLL_TODO_EXPORT(GetInstanceName, SealEngine_GetInstanceName),
	HLL_EXPORT(SetInstanceType, SealEngine_SetInstanceType),
	HLL_EXPORT(SetInstancePos, SealEngine_SetInstancePos),
	HLL_EXPORT(GetInstancePos, SealEngine_GetInstancePos),
	HLL_EXPORT(SetInstanceAngle, SealEngine_SetInstanceAngle),
	HLL_EXPORT(SetInstanceAngleP, SealEngine_SetInstanceAngleP),
	HLL_EXPORT(SetInstanceAngleB, SealEngine_SetInstanceAngleB),
	HLL_EXPORT(GetInstanceAngle, SealEngine_GetInstanceAngle),
	HLL_EXPORT(GetInstanceAngleP, SealEngine_GetInstanceAngleP),
	HLL_EXPORT(GetInstanceAngleB, SealEngine_GetInstanceAngleB),
	HLL_EXPORT(GetInstanceScaleX, SealEngine_GetInstanceScaleX),
	HLL_EXPORT(GetInstanceScaleY, SealEngine_GetInstanceScaleY),
	HLL_EXPORT(GetInstanceScaleZ, SealEngine_GetInstanceScaleZ),
	HLL_EXPORT(SetInstanceScaleX, SealEngine_SetInstanceScaleX),
	HLL_EXPORT(SetInstanceScaleY, SealEngine_SetInstanceScaleY),
	HLL_EXPORT(SetInstanceScaleZ, SealEngine_SetInstanceScaleZ),
	HLL_EXPORT(SetInstanceVertexPos, SealEngine_SetInstanceVertexPos),
	HLL_TODO_EXPORT(SetInstanceVertexUV, SealEngine_SetInstanceVertexUV),
	HLL_EXPORT(SetInstanceDiffuse, SealEngine_SetInstanceDiffuse),
	HLL_EXPORT(GetInstanceDiffuse, SealEngine_GetInstanceDiffuse),
	HLL_EXPORT(SetInstanceAmbient, SealEngine_SetInstanceAmbient),
	HLL_EXPORT(GetInstanceAmbient, SealEngine_GetInstanceAmbient),
	HLL_EXPORT(SetInstanceAlpha, SealEngine_SetInstanceAlpha),
	HLL_EXPORT(GetInstanceAlpha, SealEngine_GetInstanceAlpha),
	HLL_EXPORT(SetInstanceGrayscaleRate, SealEngine_SetInstanceGrayscaleRate),
	HLL_EXPORT(GetInstanceGrayscaleRate, SealEngine_GetInstanceGrayscaleRate),
	HLL_EXPORT(SetInstanceDraw, SealEngine_SetInstanceDraw),
	HLL_EXPORT(SetInstanceDrawShadow, SealEngine_SetInstanceDrawShadow),
	HLL_EXPORT(SetInstanceDrawMakeShadow, SealEngine_SetInstanceDrawMakeShadow),
	HLL_EXPORT(GetInstanceDraw, SealEngine_GetInstanceDraw),
	HLL_EXPORT(GetInstanceDrawShadow, SealEngine_GetInstanceDrawShadow),
	HLL_EXPORT(GetInstanceDrawMakeShadow, SealEngine_GetInstanceDrawMakeShadow),
	HLL_TODO_EXPORT(SetInstanceDrawParam, SealEngine_SetInstanceDrawParam),
	HLL_TODO_EXPORT(GetInstanceDrawParam, SealEngine_GetInstanceDrawParam),
	HLL_EXPORT(LoadInstanceMotion, SealEngine_LoadInstanceMotion),
	HLL_EXPORT(IsExistInstanceMotion, SealEngine_IsExistInstanceMotion),
	HLL_EXPORT(GetInstanceMotionState, SealEngine_GetInstanceMotionState),
	HLL_EXPORT(GetInstanceMotionFrame, SealEngine_GetInstanceMotionFrame),
	HLL_EXPORT(SetInstanceMotionState, SealEngine_SetInstanceMotionState),
	HLL_EXPORT(SetInstanceMotionFrame, SealEngine_SetInstanceMotionFrame),
	HLL_EXPORT(SetInstanceMotionFrameRange, SealEngine_SetInstanceMotionFrameRange),
	HLL_EXPORT(SetInstanceMotionLoopFrameRange, SealEngine_SetInstanceMotionLoopFrameRange),
	HLL_EXPORT(LoadInstanceNextMotion, SealEngine_LoadInstanceNextMotion),
	HLL_EXPORT(SetInstanceNextMotionState, SealEngine_SetInstanceNextMotionState),
	HLL_EXPORT(SetInstanceNextMotionFrame, SealEngine_SetInstanceNextMotionFrame),
	HLL_EXPORT(SetInstanceNextMotionFrameRange, SealEngine_SetInstanceNextMotionFrameRange),
	HLL_EXPORT(SetInstanceNextMotionLoopFrameRange, SealEngine_SetInstanceNextMotionLoopFrameRange),
	HLL_EXPORT(SetInstanceMotionBlendRate, SealEngine_SetInstanceMotionBlendRate),
	HLL_EXPORT(SetInstanceMotionBlend, SealEngine_SetInstanceMotionBlend),
	HLL_EXPORT(IsInstanceMotionBlend, SealEngine_IsInstanceMotionBlend),
	HLL_EXPORT(SwapInstanceMotion, SealEngine_SwapInstanceMotion),
	HLL_EXPORT(FreeInstanceNextMotion, SealEngine_FreeInstanceNextMotion),
	HLL_TODO_EXPORT(GetInstanceNumofMaterial, SealEngine_GetInstanceNumofMaterial),
	HLL_TODO_EXPORT(GetInstanceMaterialName, SealEngine_GetInstanceMaterialName),
	HLL_TODO_EXPORT(GetInstanceMaterialParam, SealEngine_GetInstanceMaterialParam),
	HLL_TODO_EXPORT(SetInstanceMaterialParam, SealEngine_SetInstanceMaterialParam),
	HLL_TODO_EXPORT(SaveInstanceAddMaterialData, SealEngine_SaveInstanceAddMaterialData),
	HLL_EXPORT(SetInstanceTarget, SealEngine_SetInstanceTarget),
	HLL_EXPORT(GetInstanceTarget, SealEngine_GetInstanceTarget),
	HLL_EXPORT(GetInstanceFPS, SealEngine_GetInstanceFPS),
	HLL_EXPORT(SetInstanceFPS, SealEngine_SetInstanceFPS),
	HLL_TODO_EXPORT(GetInstanceNumofBone, SealEngine_GetInstanceNumofBone),
	HLL_TODO_EXPORT(GetInstanceBoneName, SealEngine_GetInstanceBoneName),
	HLL_TODO_EXPORT(GetInstanceBoneParentIndex, SealEngine_GetInstanceBoneParentIndex),
	HLL_EXPORT(GetInstanceBoneIndex, SealEngine_GetInstanceBoneIndex),
	HLL_EXPORT(TransInstanceLocalPosToWorldPosByBone, SealEngine_TransInstanceLocalPosToWorldPosByBone),
	HLL_TODO_EXPORT(SaveBoneFile, SealEngine_SaveBoneFile),
	HLL_TODO_EXPORT(IsBoneCanIK, SealEngine_IsBoneCanIK),
	HLL_TODO_EXPORT(SetBoneCanIK, SealEngine_SetBoneCanIK),
	HLL_TODO_EXPORT(GetBoneEulerAngle, SealEngine_GetBoneEulerAngle),
	HLL_TODO_EXPORT(GetBoneMinEulerAngle, SealEngine_GetBoneMinEulerAngle),
	HLL_TODO_EXPORT(GetBoneMaxEulerAngle, SealEngine_GetBoneMaxEulerAngle),
	HLL_TODO_EXPORT(SetBoneMinEulerAngle, SealEngine_SetBoneMinEulerAngle),
	HLL_TODO_EXPORT(SetBoneMaxEulerAngle, SealEngine_SetBoneMaxEulerAngle),
	HLL_TODO_EXPORT(SetBoneMass, SealEngine_SetBoneMass),
	HLL_TODO_EXPORT(SetBoneAirResistance, SealEngine_SetBoneAirResistance),
	HLL_TODO_EXPORT(SetBoneRestitutionCoefficient, SealEngine_SetBoneRestitutionCoefficient),
	HLL_TODO_EXPORT(GetBoneMass, SealEngine_GetBoneMass),
	HLL_TODO_EXPORT(GetBoneAirResistance, SealEngine_GetBoneAirResistance),
	HLL_TODO_EXPORT(GetBoneRestitutionCoefficient, SealEngine_GetBoneRestitutionCoefficient),
	HLL_TODO_EXPORT(GetNumofBoneCollisionShapeList, SealEngine_GetNumofBoneCollisionShapeList),
	HLL_TODO_EXPORT(GetBoneCollisionShapeType, SealEngine_GetBoneCollisionShapeType),
	HLL_TODO_EXPORT(GetBoneCollisionShapePoint, SealEngine_GetBoneCollisionShapePoint),
	HLL_TODO_EXPORT(GetBoneCollisionShapePoint2, SealEngine_GetBoneCollisionShapePoint2),
	HLL_TODO_EXPORT(GetBoneCollisionShapeRadius, SealEngine_GetBoneCollisionShapeRadius),
	HLL_TODO_EXPORT(GetBoneCollisionShapeLinkedBoneIndex, SealEngine_GetBoneCollisionShapeLinkedBoneIndex),
	HLL_TODO_EXPORT(SetBoneCollisionShapeType, SealEngine_SetBoneCollisionShapeType),
	HLL_TODO_EXPORT(SetBoneCollisionShapePoint, SealEngine_SetBoneCollisionShapePoint),
	HLL_TODO_EXPORT(SetBoneCollisionShapePoint2, SealEngine_SetBoneCollisionShapePoint2),
	HLL_TODO_EXPORT(SetBoneCollisionShapeRadius, SealEngine_SetBoneCollisionShapeRadius),
	HLL_TODO_EXPORT(SetBoneCollisionShapeLinkedBoneIndex, SealEngine_SetBoneCollisionShapeLinkedBoneIndex),
	HLL_TODO_EXPORT(AddBoneCollisionShape, SealEngine_AddBoneCollisionShape),
	HLL_TODO_EXPORT(EraseBoneCollisionShape, SealEngine_EraseBoneCollisionShape),
	HLL_TODO_EXPORT(GetInstanceNumofPolygon, SealEngine_GetInstanceNumofPolygon),
	HLL_TODO_EXPORT(GetInstanceNumofVertex, SealEngine_GetInstanceNumofVertex),
	HLL_TODO_EXPORT(GetInstanceNumofNormal, SealEngine_GetInstanceNumofNormal),
	HLL_TODO_EXPORT(GetInstanceNumofMesh, SealEngine_GetInstanceNumofMesh),
	HLL_TODO_EXPORT(GetInstanceMeshName, SealEngine_GetInstanceMeshName),
	HLL_TODO_EXPORT(GetInstanceMeshMaterialIndex, SealEngine_GetInstanceMeshMaterialIndex),
	HLL_TODO_EXPORT(GetInstanceMeshMaterialID, SealEngine_GetInstanceMeshMaterialID),
	HLL_TODO_EXPORT(GetInstanceMeshNumofPolygon, SealEngine_GetInstanceMeshNumofPolygon),
	HLL_TODO_EXPORT(IsInstanceMeshAlphaBlending, SealEngine_IsInstanceMeshAlphaBlending),
	HLL_TODO_EXPORT(GetInstanceTextureMemorySize, SealEngine_GetInstanceTextureMemorySize),
	HLL_TODO_EXPORT(GetInstanceInfoText, SealEngine_GetInstanceInfoText),
	HLL_TODO_EXPORT(GetInstanceMaterialInfoText, SealEngine_GetInstanceMaterialInfoText),
	HLL_TODO_EXPORT(GetInstanceAABB, SealEngine_GetInstanceAABB),
	HLL_EXPORT(CalcInstanceHeightDetection, SealEngine_CalcInstanceHeightDetection),
	HLL_TODO_EXPORT(ClearLineList, SealEngine_ClearLineList),
	HLL_TODO_EXPORT(AddLineList, SealEngine_AddLineList),
	HLL_EXPORT(GetInstanceShadowVolumeBoneRadius, SealEngine_GetInstanceShadowVolumeBoneRadius),
	HLL_EXPORT(SetInstanceShadowVolumeBoneRadius, SealEngine_SetInstanceShadowVolumeBoneRadius),
	HLL_TODO_EXPORT(LoadInstanceLightParam, SealEngine_LoadInstanceLightParam),
	HLL_TODO_EXPORT(StoreInstanceLightParam, SealEngine_StoreInstanceLightParam),
	HLL_EXPORT(SetInstanceUseMagSpeed, SealEngine_SetInstanceUseMagSpeed),
	HLL_EXPORT(IsInstanceUseMagSpeed, SealEngine_IsInstanceUseMagSpeed),
	HLL_EXPORT(GetInstanceDebugDrawShadowVolume, SealEngine_GetInstanceDebugDrawShadowVolume),
	HLL_EXPORT(SetInstanceDebugDrawShadowVolume, SealEngine_SetInstanceDebugDrawShadowVolume),
	HLL_TODO_EXPORT(CreateInstanceDebugBoneList, SealEngine_CreateInstanceDebugBoneList),
	HLL_TODO_EXPORT(CreateInstanceDebugBoneCollision, SealEngine_CreateInstanceDebugBoneCollision),
	HLL_EXPORT(GetEffectFrameRange, SealEngine_GetEffectFrameRange),
	HLL_EXPORT(SetCameraPos, SealEngine_SetCameraPos),
	HLL_EXPORT(SetCameraAngle, SealEngine_SetCameraAngle),
	HLL_EXPORT(SetCameraAngleP, SealEngine_SetCameraAngleP),
	HLL_EXPORT(GetCameraPos, SealEngine_GetCameraPos),
	HLL_EXPORT(GetCameraAngle, SealEngine_GetCameraAngle),
	HLL_EXPORT(GetCameraAngleP, SealEngine_GetCameraAngleP),
	HLL_EXPORT(GetCameraXVector, SealEngine_GetCameraXVector),
	HLL_EXPORT(GetCameraYVector, SealEngine_GetCameraYVector),
	HLL_EXPORT(GetCameraZVector, SealEngine_GetCameraZVector),
	HLL_EXPORT(SetDrawDOF, SealEngine_SetDrawDOF),
	HLL_EXPORT(SetDOF_L, SealEngine_SetDOF_L),
	HLL_EXPORT(SetDOF_F, SealEngine_SetDOF_F),
	HLL_EXPORT(SetDOF_f, SealEngine_SetDOF_f),
	HLL_EXPORT(GetDrawDOF, SealEngine_GetDrawDOF),
	HLL_EXPORT(GetDOF_L, SealEngine_GetDOF_L),
	HLL_EXPORT(GetDOF_F, SealEngine_GetDOF_F),
	HLL_EXPORT(GetDOF_f, SealEngine_GetDOF_f),
	HLL_EXPORT(SetShadowLightVector, SealEngine_SetShadowLightVector),
	HLL_EXPORT(GetShadowLightVector, SealEngine_GetShadowLightVector),
	HLL_EXPORT(SetShadowRate, SealEngine_SetShadowRate),
	HLL_EXPORT(GetShadowRate, SealEngine_GetShadowRate),
	HLL_EXPORT(GetShadowTargetDistance, SealEngine_GetShadowTargetDistance),
	HLL_EXPORT(GetShadowMapResolutionLevel, SealEngine_GetShadowMapResolutionLevel),
	HLL_EXPORT(GetShadowSplitDepth, SealEngine_GetShadowSplitDepth),
	HLL_EXPORT(SetShadowMapType, SealEngine_SetShadowMapType),
	HLL_EXPORT(SetShadowMapLightDir, SealEngine_SetShadowMapLightDir),
	HLL_EXPORT(SetShadowFilterMag, SealEngine_SetShadowFilterMag),
	HLL_EXPORT(SetShadowTargetDistance, SealEngine_SetShadowTargetDistance),
	HLL_EXPORT(SetShadowMapResolutionLevel, SealEngine_SetShadowMapResolutionLevel),
	HLL_EXPORT(SetShadowSplitDepth, SealEngine_SetShadowSplitDepth),
	HLL_EXPORT(SetShadowMinRadius, SealEngine_SetShadowMinRadius),
	HLL_EXPORT(GetShadowMinRadius, SealEngine_GetShadowMinRadius),
	HLL_EXPORT(SetFogType, SealEngine_SetFogType),
	HLL_EXPORT(SetFogNear, SealEngine_SetFogNear),
	HLL_EXPORT(SetFogFar, SealEngine_SetFogFar),
	HLL_EXPORT(SetFogColor, SealEngine_SetFogColor),
	HLL_EXPORT(GetFogType, SealEngine_GetFogType),
	HLL_EXPORT(GetFogNear, SealEngine_GetFogNear),
	HLL_EXPORT(GetFogFar, SealEngine_GetFogFar),
	HLL_EXPORT(GetFogColor, SealEngine_GetFogColor),
	HLL_EXPORT(SetSoftFogEdgeLength, SealEngine_SetSoftFogEdgeLength),
	HLL_EXPORT(GetSoftFogEdgeLength, SealEngine_GetSoftFogEdgeLength),
	HLL_EXPORT(SetEdgeLength, SealEngine_SetEdgeLength),
	HLL_EXPORT(GetEdgeLength, SealEngine_GetEdgeLength),
	HLL_EXPORT(SetEdgeReductionRate, SealEngine_SetEdgeReductionRate),
	HLL_EXPORT(GetEdgeReductionRate, SealEngine_GetEdgeReductionRate),
	HLL_EXPORT(SetEdgeColor, SealEngine_SetEdgeColor),
	HLL_EXPORT(GetEdgeColor, SealEngine_GetEdgeColor),
	HLL_EXPORT(SetViewport, SealEngine_SetViewport),
	HLL_EXPORT(SetProjection, SealEngine_SetProjection),
	HLL_EXPORT(SetRenderMode, SealEngine_SetRenderMode),
	HLL_EXPORT(GetRenderMode, SealEngine_GetRenderMode),
	HLL_EXPORT(SetDrawOption, SealEngine_SetDrawOption),
	HLL_EXPORT(GetDrawOption, SealEngine_GetDrawOption),
	HLL_EXPORT(SetShadowMode, SealEngine_SetShadowMode),
	HLL_EXPORT(SetBumpMode, SealEngine_SetBumpMode),
	HLL_EXPORT(SetFogMode, SealEngine_SetFogMode),
	HLL_EXPORT(SetSpecularMode, SealEngine_SetSpecularMode),
	HLL_EXPORT(SetLightMapMode, SealEngine_SetLightMapMode),
	HLL_EXPORT(SetSoftFogEdgeMode, SealEngine_SetSoftFogEdgeMode),
	HLL_EXPORT(SetSSAOMode, SealEngine_SetSSAOMode),
	HLL_TODO_EXPORT(SetShaderDebugMode, SealEngine_SetShaderDebugMode),
	HLL_EXPORT(GetShadowMode, SealEngine_GetShadowMode),
	HLL_EXPORT(GetBumpMode, SealEngine_GetBumpMode),
	HLL_EXPORT(GetFogMode, SealEngine_GetFogMode),
	HLL_EXPORT(GetSpecularMode, SealEngine_GetSpecularMode),
	HLL_EXPORT(GetLightMapMode, SealEngine_GetLightMapMode),
	HLL_EXPORT(GetSoftFogEdgeMode, SealEngine_GetSoftFogEdgeMode),
	HLL_EXPORT(GetSSAOMode, SealEngine_GetSSAOMode),
	HLL_TODO_EXPORT(GetShaderDebugMode, SealEngine_GetShaderDebugMode),
	HLL_TODO_EXPORT(SetDebugMode, SealEngine_SetDebugMode),
	HLL_TODO_EXPORT(GetDebugMode, SealEngine_GetDebugMode),
	HLL_EXPORT(GetTextureResolutionLevel, SealEngine_GetTextureResolutionLevel),
	HLL_EXPORT(GetTextureFilterMode, SealEngine_GetTextureFilterMode),
	HLL_EXPORT(SetTextureResolutionLevel, SealEngine_SetTextureResolutionLevel),
	HLL_EXPORT(SetTextureFilterMode, SealEngine_SetTextureFilterMode),
	HLL_EXPORT(GetBloomMode, SealEngine_GetBloomMode),
	HLL_EXPORT(SetBloomMode, SealEngine_SetBloomMode),
	HLL_EXPORT(GetGlareMode, SealEngine_GetGlareMode),
	HLL_EXPORT(SetGlareMode, SealEngine_SetGlareMode),
	HLL_TODO_EXPORT(GetSSAOParam, SealEngine_GetSSAOParam),
	HLL_TODO_EXPORT(SetSSAOParam, SealEngine_SetSSAOParam),
	HLL_TODO_EXPORT(CalcIntersectEyeVec, SealEngine_CalcIntersectEyeVec),
	HLL_EXPORT(Calc2DDetectionHeight, SealEngine_Calc2DDetectionHeight),
	HLL_EXPORT(Calc2DDetection, SealEngine_Calc2DDetection),
	HLL_TODO_EXPORT(Calc2DDetectionIntersectEyeVector, SealEngine_Calc2DDetectionIntersectEyeVector),
	HLL_TODO_EXPORT(FindPath, SealEngine_FindPath),
	HLL_TODO_EXPORT(GetPathLine, SealEngine_GetPathLine),
	HLL_TODO_EXPORT(GetOptimizedPathLine, SealEngine_GetOptimizedPathLine),
	HLL_EXPORT(TransformPosToViewPos, SealEngine_TransformPosToViewPos),
	HLL_EXPORT(IsLoading, SealEngine_IsLoading),
	HLL_EXPORT(ResetLightParam, SealEngine_ResetLightParam),
	HLL_EXPORT(SetLightParam, SealEngine_SetLightParam),
	HLL_EXPORT(GetLightParam, SealEngine_GetLightParam),
	HLL_EXPORT(SetThreadLoadingMode, SealEngine_SetThreadLoadingMode),
	HLL_EXPORT(IsThreadLoadingMode, SealEngine_IsThreadLoadingMode),
	HLL_EXPORT(ClearCache, SealEngine_ClearCache),
	HLL_EXPORT(Suspend, SealEngine_Suspend),
	HLL_EXPORT(IsSuspend, SealEngine_IsSuspend),
	HLL_EXPORT(Resume, SealEngine_Resume),
	HLL_TODO_EXPORT(GetHistogram, SealEngine_GetHistogram),
	HLL_EXPORT(GetNumofInstance, SealEngine_GetNumofInstance),
	HLL_TODO_EXPORT(Tool_GetEyeVector, SealEngine_Tool_GetEyeVector),
	HLL_TODO_EXPORT(Tool_ReloadEffectDataEXFile, SealEngine_Tool_ReloadEffectDataEXFile),
	HLL_TODO_EXPORT(Tool_GetEmitterCurrentPos, SealEngine_Tool_GetEmitterCurrentPos),
	HLL_TODO_EXPORT(Tool_GetEmitterFrameRange, SealEngine_Tool_GetEmitterFrameRange),
	HLL_TODO_EXPORT(Tool_SetEmitterValidKey, SealEngine_Tool_SetEmitterValidKey),
	HLL_TODO_EXPORT(Tool_SetEmitterDebugLineShow, SealEngine_Tool_SetEmitterDebugLineShow),
	HLL_TODO_EXPORT(Tool_SetEmitterWireFrameShow, SealEngine_Tool_SetEmitterWireFrameShow),
	HLL_TODO_EXPORT(Tool_Calc2DBezier, SealEngine_Tool_Calc2DBezier),
	HLL_TODO_EXPORT(Tool_ReloadPolyDataEXFile, SealEngine_Tool_ReloadPolyDataEXFile),
	HLL_TODO_EXPORT(Tool_ReloadMotionDataEXFile, SealEngine_Tool_ReloadMotionDataEXFile),
	HLL_TODO_EXPORT(Tool_CreateFBXAscii, SealEngine_Tool_CreateFBXAscii)
);
