#include "common.h"
#include "client.h"
#include "cl_entity.h"
#include "aimbot.h"

#ifndef qtrue
#define qtrue 1
#endif

#ifndef qfalse
#define qfalse 0
#endif

#ifndef MAX_PLAYERS
#define MAX_PLAYERS 32
#endif

#ifndef MAX_BACKTRACK_RECORDS
#define MAX_BACKTRACK_RECORDS 64
#endif

#define WEAPON_AWP 18
#define WEAPON_SCOUT 3

static char friendslist[MAX_PLAYERS][32];
static int friendslist_count = 0;

static int last_target_index = -1;
static float last_target_time = 0.0f;
static int round_reset_counter = 0;

static char last_target_name[64] = {0};
static vec3_t last_target_origin = {0, 0, 0};

backtrack_record_t backtrack_records[MAX_PLAYERS][MAX_BACKTRACK_RECORDS];

static vec3_t last_punch_angle = {0, 0, 0};

CVAR_DEFINE_AUTO( nash3d_aim, "0", FCVAR_ARCHIVE, "" );
CVAR_DEFINE_AUTO( nash3d_aim_smooth, "0.01", FCVAR_ARCHIVE, "" );
CVAR_DEFINE_AUTO( nash3d_aim_bone, "0", FCVAR_ARCHIVE, "" );
CVAR_DEFINE_AUTO( nash3d_aim_autoshoot, "0", FCVAR_ARCHIVE, "" );
CVAR_DEFINE_AUTO( nash3d_aim_silent, "0", FCVAR_ARCHIVE, "" );
CVAR_DEFINE_AUTO( nash3d_aim_predict, "1", FCVAR_ARCHIVE, "" );
CVAR_DEFINE_AUTO( nash3d_aim_priority, "0", FCVAR_ARCHIVE, "" );
CVAR_DEFINE_AUTO( nash3d_aim_visibility_check, "1", FCVAR_ARCHIVE, "" );
CVAR_DEFINE_AUTO( nash3d_aim_debug, "0", FCVAR_ARCHIVE, "" );
CVAR_DEFINE_AUTO( nash3d_aim_smooth_distance_scale, "1", FCVAR_ARCHIVE, "" );
CVAR_DEFINE_AUTO( nash3d_aim_reaction_time, "0", FCVAR_ARCHIVE, "" );
CVAR_DEFINE_AUTO( nash3d_aim_multipoint, "0", FCVAR_ARCHIVE, "" );
CVAR_DEFINE_AUTO( nash3d_aim_reset_on_round, "1", FCVAR_ARCHIVE, "" );
CVAR_DEFINE_AUTO( nash3d_aim_backtrack, "0", FCVAR_ARCHIVE, "" );
CVAR_DEFINE_AUTO( nash3d_aim_backtrack_time, "200", FCVAR_ARCHIVE, "" );
CVAR_DEFINE_AUTO( nash3d_aim_ignore_team, "1", FCVAR_ARCHIVE, "" );
CVAR_DEFINE_AUTO( nash3d_aim_ignore_wall, "0", FCVAR_ARCHIVE, "" );
CVAR_DEFINE_AUTO( nash3d_aim_head_scale, "1.0", FCVAR_ARCHIVE, "" );
CVAR_DEFINE_AUTO( nash3d_aim_triggerbot, "0", FCVAR_ARCHIVE, "" );
CVAR_DEFINE_AUTO( nash3d_aim_triggerbot_delay, "0", FCVAR_ARCHIVE, "" );
CVAR_DEFINE_AUTO( nash3d_aim_nevermiss, "0", FCVAR_ARCHIVE, "" );
CVAR_DEFINE_AUTO( nash3d_aim_norecoil, "0", FCVAR_ARCHIVE, "" );

static float AngleDifff(float a, float b)
{
	float diff = a - b;
	while (diff > 180.0f) diff -= 360.0f;
	while (diff < -180.0f) diff += 360.0f;
	return diff;
}

static qboolean CL_Aimbot_IsCrosshairOnTarget(vec3_t viewangles, cl_entity_t *target)
{
	vec3_t target_pos, eye_pos, delta, angles_to_target;
	float yaw_diff, pitch_diff;

	if (!target) return qfalse;

	GetTargetPoint(target, target_pos);
	CL_Aimbot_GetEyePosition(eye_pos);
	VectorSubtract(target_pos, eye_pos, delta);
	VectorAngles(delta, angles_to_target);

	yaw_diff = fabsf(AngleDifff(viewangles[YAW], angles_to_target[YAW]));
	pitch_diff = fabsf(AngleDifff(viewangles[PITCH], angles_to_target[PITCH]));

	return (yaw_diff < 2.0f && pitch_diff < 2.0f);
}

void Aimbot_AddFriend_f(void)
{
	if (friendslist_count >= MAX_PLAYERS)
	{
		Con_Printf("Friend list full (max %d)\n", MAX_PLAYERS);
		return;
	}
	if (Cmd_Argc() < 2)
	{
		Con_Printf("Usage: nash3d_friend_add <name>\n");
		return;
	}
	Q_strncpy(friendslist[friendslist_count], Cmd_Argv(1), sizeof(friendslist[0]) - 1);
	friendslist[friendslist_count][sizeof(friendslist[0]) - 1] = 0;
	friendslist_count++;
	Con_Printf("Added friend: %s\n", Cmd_Argv(1));
}

void Aimbot_ListFriends_f(void)
{
	int i;
	Con_Printf("=== Friend List (%d/%d) ===\n", friendslist_count, MAX_PLAYERS);
	for (i = 0; i < friendslist_count; i++)
	{
		Con_Printf("%d: %s\n", i + 1, friendslist[i]);
	}
	Con_Printf("========================\n");
}

static void CL_Aimbot_ClearLastTarget(void)
{
	last_target_index = -1;
	last_target_time = 0.0f;
	last_target_name[0] = '\0';
	VectorClear(last_target_origin);
}

void CL_Aimbot_ResetMemory(void)
{
	CL_Aimbot_ClearLastTarget();
	memset(backtrack_records, 0, sizeof(backtrack_records));
}

void CL_Aimbot_CheckRoundReset(void)
{
	static int last_health = 100;
	int current_health;
	cl_entity_t *cached;
	qboolean invalid;
	vec3_t tmp;

	if (!nash3d_aim_reset_on_round.value)
		return;

	current_health = cl.local.health;

	if (last_health <= 0 && current_health > 0)
	{
		CL_Aimbot_ResetMemory();
		round_reset_counter++;
	}

	if (cl.time - last_target_time > 30.0f && last_target_index != -1)
	{
		CL_Aimbot_ResetMemory();
	}

	last_health = current_health;

	if (last_target_index < 1 || last_target_index > MAX_PLAYERS)
		return;

	cached = CL_GetEntityByIndex(last_target_index);
	invalid = qfalse;

	if (!cached)
		invalid = qtrue;
	else
	{
		if (cached->curstate.modelindex == 0 || cached->curstate.solid == SOLID_NOT)
			invalid = qtrue;
		if (cached->curstate.effects & EF_NODRAW)
			invalid = qtrue;
#ifdef MOVETYPE_TOSS
		if (cached->curstate.movetype == MOVETYPE_TOSS)
			invalid = qtrue;
#endif
		if (!cl.players[last_target_index - 1].name[0] || Q_strcmp(cl.players[last_target_index - 1].name, last_target_name) != 0)
			invalid = qtrue;
		VectorSubtract(cached->origin, last_target_origin, tmp);
		if (VectorLength(tmp) > 128.0f)
			invalid = qtrue;
	}

	if (invalid)
	{
		CL_Aimbot_ResetMemory();
	}
}

void CL_Aimbot_RegisterCVars(void)
{
	Cvar_RegisterVariable(&nash3d_aim);
	Cvar_RegisterVariable(&nash3d_aim_smooth);
	Cvar_RegisterVariable(&nash3d_aim_bone);
	Cvar_RegisterVariable(&nash3d_aim_autoshoot);
	Cvar_RegisterVariable(&nash3d_aim_silent);
	Cvar_RegisterVariable(&nash3d_aim_predict);
	Cvar_RegisterVariable(&nash3d_aim_priority);
	Cvar_RegisterVariable(&nash3d_aim_visibility_check);
	Cvar_RegisterVariable(&nash3d_aim_debug);
	Cvar_RegisterVariable(&nash3d_aim_smooth_distance_scale);
	Cvar_RegisterVariable(&nash3d_aim_reaction_time);
	Cvar_RegisterVariable(&nash3d_aim_multipoint);
	Cvar_RegisterVariable(&nash3d_aim_reset_on_round);
	Cvar_RegisterVariable(&nash3d_aim_backtrack);
	Cvar_RegisterVariable(&nash3d_aim_backtrack_time);
	Cvar_RegisterVariable(&nash3d_aim_ignore_team);
	Cvar_RegisterVariable(&nash3d_aim_ignore_wall);
	Cvar_RegisterVariable(&nash3d_aim_head_scale);
	Cvar_RegisterVariable(&nash3d_aim_triggerbot);
	Cvar_RegisterVariable(&nash3d_aim_triggerbot_delay);
	Cvar_RegisterVariable(&nash3d_aim_nevermiss);
	Cvar_RegisterVariable(&nash3d_aim_norecoil);

	Cmd_AddCommand("nash3d_friend_add", Aimbot_AddFriend_f, "Add player to friend list");
	Cmd_AddCommand("nash3d_friend_list", Aimbot_ListFriends_f, "List friends");
}

qboolean CL_Aimbot_IsEnabled(void)
{
	if (!nash3d_aim.value) return qfalse;
	if (cls.state != ca_active) return qfalse;
	if (cl.local.health <= 0) return qfalse;
	return qtrue;
}

qboolean CL_Aimbot_IsFriend(int player_index)
{
	int i;
	if (player_index < 1 || player_index >= MAX_PLAYERS) return qfalse;
	if (friendslist_count == 0) return qfalse;
	if (!cl.players[player_index - 1].name[0]) return qfalse;
	for (i = 0; i < friendslist_count; i++)
	{
		if (!Q_stricmp(friendslist[i], cl.players[player_index - 1].name)) return qtrue;
	}
	return qfalse;
}

qboolean CL_Aimbot_ShouldTargetTeam(int target_index)
{
	cl_entity_t *local = CL_GetLocalPlayer();
	cl_entity_t *target = CL_GetEntityByIndex(target_index);
	player_info_t *local_player;
	player_info_t *target_player;
	qboolean local_is_ct, target_is_ct;
	qboolean ignore_team;

	if (!local || !target) return qfalse;

	ignore_team = (nash3d_aim_ignore_team.value > 0.0f);
	if (!ignore_team) return qtrue;

	if (local->curstate.team != 0 && target->curstate.team != 0)
	{
		if (local->curstate.team == target->curstate.team) return qfalse;
	}

	local_player = &cl.players[cl.playernum];
	target_player = &cl.players[target_index - 1];

	if (!local_player->model[0] || !target_player->model[0])
		return qtrue;

	local_is_ct = (Q_stristr(local_player->model, "gsg9") || Q_stristr(local_player->model, "sas") ||
		Q_stristr(local_player->model, "gign") || Q_stristr(local_player->model, "urban") ||
		Q_stristr(local_player->model, "_ct"));
	target_is_ct = (Q_stristr(target_player->model, "gsg9") || Q_stristr(target_player->model, "sas") ||
		Q_stristr(target_player->model, "gign") || Q_stristr(target_player->model, "urban") ||
		Q_stristr(target_player->model, "_ct"));

	if ((local_is_ct && target_is_ct) || (!local_is_ct && !target_is_ct))
		return qfalse;

	return qtrue;
}

qboolean CL_Aimbot_IsValidTarget(cl_entity_t *ent)
{
	if (!ent || !ent->player) return qfalse;
	if (ent->index == cl.playernum + 1) return qfalse;
	if (ent->curstate.messagenum < CL_GetLocalPlayer()->curstate.messagenum) return qfalse;
	if (ent->origin[0] == 0 && ent->origin[1] == 0 && ent->origin[2] == 0) return qfalse;
	if (ent->curstate.modelindex == 0 || !ent->model) return qfalse;
	if (ent->curstate.effects & EF_NOINTERP) return qfalse;
	if (ent->curstate.movetype == MOVETYPE_NONE || ent->curstate.movetype == MOVETYPE_NOCLIP) return qfalse;
#ifdef MOVETYPE_TOSS
	if (ent->curstate.movetype == MOVETYPE_TOSS) return qfalse;
#endif
	if (CL_Aimbot_IsFriend(ent->index)) return qfalse;
	if (!CL_Aimbot_ShouldTargetTeam(ent->index)) return qfalse;
	return qtrue;
}

void GetTargetPoint(cl_entity_t *target, vec3_t out)
{
	vec3_t forward, right, up;
	float head_height;

	if (!target) { VectorClear(out); return; }

	VectorCopy(target->origin, out);
	head_height = 68.0f * nash3d_aim_head_scale.value;
	out[2] += head_height;

	AngleVectors(target->angles, forward, right, up);
	out[0] += forward[0] * 2.0f;
	out[1] += forward[1] * 2.0f;
}

void CL_Aimbot_GetEyePosition(vec3_t eye_pos)
{
	cl_entity_t *local = CL_GetLocalPlayer();
	if (!local) { VectorCopy(refState.vieworg, eye_pos); return; }

	VectorCopy(local->origin, eye_pos);

	if (local->curstate.usehull == 1)
		eye_pos[2] += 28.0f;
	else if (cl.local.waterlevel >= 2)
		eye_pos[2] += 12.0f;
	else
		eye_pos[2] += 68.0f;
}

qboolean CL_Aimbot_IsTargetVisible(vec3_t start, vec3_t end)
{
	pmtrace_t trace;
	if (nash3d_aim_ignore_wall.value) return qtrue;
	if (!nash3d_aim_visibility_check.value) return qtrue;
	trace = CL_TraceLine(start, end, PM_STUDIO_BOX);
	return (trace.fraction >= 0.97f);
}

void CL_Aimbot_PredictTargetPosition(cl_entity_t *ent, vec3_t current_pos, vec3_t predicted_pos)
{
	vec3_t velocity, delta;
	float distance, time_to_hit;
	VectorCopy(current_pos, predicted_pos);
	if (!nash3d_aim_predict.value) return;
	VectorCopy(ent->curstate.velocity, velocity);
	VectorSubtract(current_pos, cl.simorg, delta);
	distance = VectorLength(delta);
	time_to_hit = distance / 2000.0f;
	predicted_pos[0] += velocity[0] * time_to_hit;
	predicted_pos[1] += velocity[1] * time_to_hit;
	predicted_pos[2] += velocity[2] * time_to_hit * 0.5f;
}

float CL_Aimbot_GetTargetPriority(cl_entity_t *ent, vec3_t viewangles)
{
	int priority_mode = (int)nash3d_aim_priority.value;

	switch (priority_mode)
	{
	case PRIORITY_CROSSHAIR:
	{
		vec3_t target_pos, eye_pos, delta, angles_to_target;
		float yaw_diff, pitch_diff;
		GetTargetPoint(ent, target_pos);
		VectorCopy(refState.vieworg, eye_pos);
		VectorSubtract(target_pos, eye_pos, delta);
		VectorAngles(delta, angles_to_target);
		yaw_diff = AngleDifff(viewangles[YAW], angles_to_target[YAW]);
		pitch_diff = AngleDifff(viewangles[PITCH], angles_to_target[PITCH]);
		return sqrtf(yaw_diff*yaw_diff + pitch_diff*pitch_diff);
	}
	case PRIORITY_ANY:
		return 1.0f;
	case PRIORITY_CLOSEST:
	default:
	{
		vec3_t delta, target_pos;
		GetTargetPoint(ent, target_pos);
		VectorSubtract(target_pos, cl.simorg, delta);
		return VectorLength(delta);
	}
	}
}

cl_entity_t *CL_Aimbot_FindBestTargetWithBacktrack(vec3_t viewangles, vec3_t *best_head_pos)
{
	cl_entity_t *best_target = NULL;
	float best_priority = 999999.0f;
	vec3_t eye_pos;
	int i;

	CL_Aimbot_GetEyePosition(eye_pos);

	if (last_target_index != -1 && cl.time - last_target_time < 5.0f)
	{
		cl_entity_t *cached = CL_GetEntityByIndex(last_target_index);
		if (cached && CL_Aimbot_IsValidTarget(cached))
		{
			vec3_t cached_head_pos;
			backtrack_record_t *crec = NULL;
			if (nash3d_aim_backtrack.value)
			{
				crec = CL_Aimbot_GetBestBacktrackRecord(cached->index, eye_pos);
				if (crec) VectorCopy(crec->head_position, cached_head_pos);
				else GetTargetPoint(cached, cached_head_pos);
			}
			else
				GetTargetPoint(cached, cached_head_pos);

			if (CL_Aimbot_IsTargetVisible(eye_pos, cached_head_pos) || nash3d_aim_ignore_wall.value)
			{
				VectorCopy(cached_head_pos, *best_head_pos);
				return cached;
			}
		}
	}

	for (i = 1; i <= cl.maxclients; i++)
	{
		cl_entity_t *ent = CL_GetEntityByIndex(i);
		vec3_t target_head_pos;
		backtrack_record_t *rec = NULL;

		if (!CL_Aimbot_IsValidTarget(ent)) continue;

		if (nash3d_aim_backtrack.value)
		{
			rec = CL_Aimbot_GetBestBacktrackRecord(i, eye_pos);
			if (rec)
				VectorCopy(rec->head_position, target_head_pos);
			else
				GetTargetPoint(ent, target_head_pos);
		}
		else
			GetTargetPoint(ent, target_head_pos);

		if (!rec)
		{
			if (!CL_Aimbot_IsTargetVisible(eye_pos, target_head_pos))
				continue;
		}
		else
		{
			if (!nash3d_aim_ignore_wall.value && nash3d_aim_visibility_check.value)
			{
				pmtrace_t tr = CL_TraceLine(eye_pos, target_head_pos, PM_STUDIO_BOX);
				if (tr.fraction < 0.97f) continue;
			}
		}

		{
			float priority = CL_Aimbot_GetTargetPriority(ent, viewangles);
			if (priority < best_priority)
			{
				best_priority = priority;
				best_target = ent;
				VectorCopy(target_head_pos, *best_head_pos);
			}
		}
	}

	if (!best_target && last_target_index != -1)
	{
		cl_entity_t *cached = CL_GetEntityByIndex(last_target_index);
		if (cached && CL_Aimbot_IsValidTarget(cached))
		{
			vec3_t cached_head_pos;
			backtrack_record_t *crec = NULL;
			if (nash3d_aim_backtrack.value)
			{
				crec = CL_Aimbot_GetBestBacktrackRecord(cached->index, eye_pos);
				if (crec) VectorCopy(crec->head_position, cached_head_pos);
				else GetTargetPoint(cached, cached_head_pos);
			}
			else
				GetTargetPoint(cached, cached_head_pos);

			if (CL_Aimbot_IsTargetVisible(eye_pos, cached_head_pos) || nash3d_aim_ignore_wall.value)
			{
				best_target = cached;
				VectorCopy(cached_head_pos, *best_head_pos);
			}
		}
	}

	return best_target;
}

void CL_Aimbot_SmoothAngles(vec3_t current, vec3_t target, vec3_t result, float smooth, float distance_to_target)
{
	vec3_t delta;
	float s, dist_scale, lerp;
	int i;

	for (i = 0; i < 3; i++)
		delta[i] = AngleDifff(target[i], current[i]);

	s = smooth;
	if (s < 0.0001f) s = 0.0001f;

	dist_scale = 1.0f;
	if (nash3d_aim_smooth_distance_scale.value && distance_to_target > 0.0f)
	{
		dist_scale = 1.0f + (distance_to_target / 500.0f);
		if (dist_scale < 1.0f) dist_scale = 1.0f;
	}

	lerp = 1.0f / (1.0f + s * dist_scale * 10.0f);
	if (lerp < 0.01f) lerp = 0.01f;
	if (lerp > 1.0f) lerp = 1.0f;

	for (i = 0; i < 3; i++)
		result[i] = current[i] + delta[i] * lerp;

	if (result[PITCH] > 89.0f) result[PITCH] = 89.0f;
	if (result[PITCH] < -89.0f) result[PITCH] = -89.0f;
}

void CL_Aimbot_UpdateBacktrackRecords(void)
{
	int i, j;
	float now = cl.time;
	static float last_update = 0.0f;

	if (!nash3d_aim_backtrack.value) return;
	if (now - last_update < 0.04f) return;
	last_update = now;

	for (i = 1; i <= cl.maxclients; i++)
	{
		cl_entity_t *ent = CL_GetEntityByIndex(i);
		if (!ent || !ent->player || ent->index == cl.playernum + 1)
			continue;
		if (ent->curstate.messagenum < cl.parsecount)
			continue;
		if (ent->curstate.solid == SOLID_NOT)
			continue;
		if (ent->curstate.effects & EF_NODRAW)
			continue;

		for (j = MAX_BACKTRACK_RECORDS - 1; j > 0; j--)
			backtrack_records[i][j] = backtrack_records[i][j - 1];

		backtrack_records[i][0].simulation_time = now;
		VectorCopy(ent->origin, backtrack_records[i][0].origin);
		VectorCopy(ent->angles, backtrack_records[i][0].angles);
		GetTargetPoint(ent, backtrack_records[i][0].head_position);
		backtrack_records[i][0].valid = qtrue;
	}
}

backtrack_record_t *CL_Aimbot_GetBestBacktrackRecord(int player_index, vec3_t eye_pos)
{
	float now = cl.time;
	float max_time;
	backtrack_record_t *best = NULL;
	float best_score = -1.0f;
	int i;

	if (!nash3d_aim_backtrack.value) return NULL;
	if (player_index < 1 || player_index > cl.maxclients) return NULL;

	max_time = nash3d_aim_backtrack_time.value / 1000.0f;
	if (max_time <= 0.0f) max_time = 0.2f;

	for (i = 0; i < MAX_BACKTRACK_RECORDS; i++)
	{
		backtrack_record_t *rec = &backtrack_records[player_index][i];
		float age, time_score, dist_score, score;
		vec3_t d;
		float dist;

		if (!rec->valid) continue;
		age = now - rec->simulation_time;
		if (age < 0.0f || age > max_time) continue;

		if (!nash3d_aim_ignore_wall.value && nash3d_aim_visibility_check.value)
		{
			pmtrace_t tr = CL_TraceLine(eye_pos, rec->head_position, PM_STUDIO_BOX);
			if (tr.fraction < 0.85f) continue;
		}

		time_score = 1.0f - (age / max_time);
		if (time_score < 0.0f) time_score = 0.0f;

		VectorSubtract(rec->head_position, eye_pos, d);
		dist = VectorLength(d);
		dist_score = 1.0f;
		if (dist < 2000.0f)
			dist_score = 1.0f - (dist / 2000.0f);

		score = time_score * 0.8f + dist_score * 0.2f;

		if (score > best_score)
		{
			best_score = score;
			best = rec;
		}
	}

	return best;
}

static float CL_Aimbot_CalculateAngleDifference(vec3_t angle1, vec3_t angle2)
{
	vec3_t delta;
	delta[0] = angle2[0] - angle1[0];
	delta[1] = angle2[1] - angle1[1];
	delta[2] = angle2[2] - angle1[2];
	if (delta[0] > 180.0f) delta[0] -= 360.0f;
	if (delta[0] < -180.0f) delta[0] += 360.0f;
	if (delta[1] > 180.0f) delta[1] -= 360.0f;
	if (delta[1] < -180.0f) delta[1] += 360.0f;
	return sqrtf(delta[0]*delta[0] + delta[1]*delta[1] + delta[2]*delta[2]);
}

static qboolean CL_Aimbot_IsSniperWeapon(int weapon_id)
{
	return (weapon_id == WEAPON_AWP || weapon_id == WEAPON_SCOUT);
}

static int CL_Aimbot_GetCurrentWeapon(void)
{
	int weapon_id = cl.frames[cl.parsecountmod].clientdata.m_iId;
	int i;

	if (weapon_id > 0)
		return weapon_id;

	for (i = 0; i < MAX_LOCAL_WEAPONS; i++)
	{
		if (cl.frames[cl.parsecountmod].weapondata[i].m_iId > 0)
			return cl.frames[cl.parsecountmod].weapondata[i].m_iId;
	}

	return 0;
}

static void CL_Aimbot_HandleSniperScope(usercmd_t *cmd, int current_weapon)
{
	static int sniper_state = 0;
	static float state_time = 0.0f;

	if (!CL_Aimbot_IsSniperWeapon(current_weapon))
	{
		sniper_state = 0;
		return;
	}

	switch (sniper_state)
	{
	case 0:
		cmd->buttons |= IN_ATTACK2;
		sniper_state = 1;
		state_time = cl.time;
		break;
	case 1:
		if (cl.time - state_time >= 0.15f)
		{
			cmd->buttons |= IN_ATTACK;
			sniper_state = 2;
			state_time = cl.time;
		}
		break;
	case 2:
		if (cl.time - state_time >= 0.05f)
		{
			Cmd_ExecuteString("slot3\n");
			sniper_state = 3;
			state_time = cl.time;
		}
		break;
	case 3:
		if (cl.time - state_time >= 0.1f)
		{
			Cmd_ExecuteString("slot1\n");
			sniper_state = 0;
		}
		break;
	}
}

static void CL_Aimbot_HandleNeverMiss(usercmd_t *cmd, cl_entity_t *target, vec3_t target_angles)
{
	if (!nash3d_aim_nevermiss.value || !target) return;
	if (cmd->buttons & IN_ATTACK)
	{
		VectorCopy(target_angles, cmd->viewangles);
	}
}

static void CL_Aimbot_ApplyNoRecoil(vec3_t viewangles, usercmd_t *cmd, qboolean is_shooting, qboolean silent)
{
	vec3_t punch;
	if (!nash3d_aim_norecoil.value) return;

	if (!is_shooting)
	{
		VectorClear(last_punch_angle);
		return;
	}

	VectorCopy(cl.punchangle, punch);

	if (cmd && !silent)
	{
		viewangles[PITCH] -= punch[PITCH] * 2.0f;
		viewangles[YAW] -= punch[YAW] * 2.0f;
		viewangles[ROLL] -= punch[ROLL] * 2.0f;

		if (viewangles[PITCH] > 89.0f) viewangles[PITCH] = 89.0f;
		if (viewangles[PITCH] < -89.0f) viewangles[PITCH] = -89.0f;
	}

	if (cmd)
	{
		cmd->viewangles[PITCH] -= punch[PITCH] * 2.0f;
		cmd->viewangles[YAW] -= punch[YAW] * 2.0f;
		cmd->viewangles[ROLL] -= punch[ROLL] * 2.0f;

		if (cmd->viewangles[PITCH] > 89.0f) cmd->viewangles[PITCH] = 89.0f;
		if (cmd->viewangles[PITCH] < -89.0f) cmd->viewangles[PITCH] = -89.0f;
	}

	VectorCopy(punch, last_punch_angle);
}

void CL_Aimbot_Apply(vec3_t viewangles, usercmd_t *cmd)
{
	static float last_shot_time = 0.0f;
	qboolean should_shoot = qfalse;
	qboolean has_valid_target = qfalse;
	qboolean silent;
	cl_entity_t *current_target = NULL;
	vec3_t target_angles = {0, 0, 0};

	if (!cmd) return;

	silent = (nash3d_aim_silent.value != 0.0f);

	if (last_target_index != -1)
	{
		cl_entity_t *cached = CL_GetEntityByIndex(last_target_index);
		qboolean invalid = qfalse;

		if (!cached)
			invalid = qtrue;
		else
		{
			if (cached->curstate.modelindex == 0 || cached->curstate.solid == SOLID_NOT)
				invalid = qtrue;
			if (cached->curstate.effects & EF_NODRAW)
				invalid = qtrue;
#ifdef MOVETYPE_TOSS
			if (cached->curstate.movetype == MOVETYPE_TOSS)
				invalid = qtrue;
#endif
			{
				vec3_t tmp;
				VectorSubtract(cached->origin, last_target_origin, tmp);
				if (VectorLength(tmp) > 128.0f)
					invalid = qtrue;
			}
		}

		if (invalid)
			CL_Aimbot_ClearLastTarget();
	}

	CL_Aimbot_UpdateBacktrackRecords();
	CL_Aimbot_CheckRoundReset();

	{
		vec3_t best_head_pos;
		cl_entity_t *target = CL_Aimbot_FindBestTargetWithBacktrack(viewangles, &best_head_pos);
		current_target = target;

		if (CL_Aimbot_IsEnabled() && target)
		{
			vec3_t eye_pos, delta, smoothed;
			float distance_to_target;

			has_valid_target = qtrue;

			last_target_index = target->index;
			last_target_time = cl.time;

			if (last_target_index > 0 && last_target_index <= MAX_PLAYERS)
			{
				Q_strncpy(last_target_name, cl.players[last_target_index - 1].name, sizeof(last_target_name) - 1);
				last_target_name[sizeof(last_target_name) - 1] = '\0';
			}
			VectorCopy(target->origin, last_target_origin);

			CL_Aimbot_GetEyePosition(eye_pos);
			VectorSubtract(best_head_pos, eye_pos, delta);

			target_angles[YAW] = atan2f(delta[1], delta[0]) * (180.0f / M_PI);
			{
				float xy = sqrtf(delta[0]*delta[0] + delta[1]*delta[1]);
				target_angles[PITCH] = -atan2f(delta[2], xy) * (180.0f / M_PI);
			}
			target_angles[ROLL] = 0.0f;

			while (target_angles[YAW] > 180.0f) target_angles[YAW] -= 360.0f;
			while (target_angles[YAW] < -180.0f) target_angles[YAW] += 360.0f;
			if (target_angles[PITCH] > 89.0f) target_angles[PITCH] = 89.0f;
			if (target_angles[PITCH] < -89.0f) target_angles[PITCH] = -89.0f;

			distance_to_target = VectorLength(delta);

			if (silent)
			{
				VectorCopy(target_angles, cmd->viewangles);
			}
			else
			{
				CL_Aimbot_SmoothAngles(viewangles, target_angles, smoothed, nash3d_aim_smooth.value, distance_to_target);
				VectorCopy(smoothed, viewangles);
				VectorCopy(smoothed, cmd->viewangles);
			}

			if (nash3d_aim_autoshoot.value)
			{
				float angdiff = CL_Aimbot_CalculateAngleDifference(cmd->viewangles, target_angles);
				float thr = nash3d_aim_backtrack.value ? 2.0f : 3.0f;

				if (angdiff < thr)
				{
					int current_weapon = CL_Aimbot_GetCurrentWeapon();

					if (CL_Aimbot_IsSniperWeapon(current_weapon))
					{
						CL_Aimbot_HandleSniperScope(cmd, current_weapon);
					}
					else
					{
						should_shoot = qtrue;
					}
				}
			}
		}

		if (nash3d_aim_triggerbot.value && current_target)
		{
			has_valid_target = qtrue;
			if (CL_Aimbot_IsCrosshairOnTarget(viewangles, current_target))
			{
				float delay = nash3d_aim_triggerbot_delay.value / 1000.0f;
				if (cl.time - last_shot_time > delay)
				{
					should_shoot = qtrue;
				}
			}
		}

		if (nash3d_aim_nevermiss.value && current_target)
		{
			CL_Aimbot_HandleNeverMiss(cmd, current_target, target_angles);
		}

		if (should_shoot && !CL_Aimbot_IsSniperWeapon(CL_Aimbot_GetCurrentWeapon()))
		{
			cmd->buttons |= IN_ATTACK;
			last_shot_time = cl.time;
		}

		if (!has_valid_target)
		{
			CL_Aimbot_ClearLastTarget();
			VectorClear(last_punch_angle);
		}
	}

	if (nash3d_aim_norecoil.value)
	{
		qboolean is_shooting = (cmd->buttons & IN_ATTACK) || has_valid_target;
		CL_Aimbot_ApplyNoRecoil(viewangles, cmd, is_shooting, silent);
	}

	if (cmd)
	{
		int i;
		for (i = 0; i < 3; i++)
		{
			while (cmd->viewangles[i] > 180.0f) cmd->viewangles[i] -= 360.0f;
			while (cmd->viewangles[i] < -180.0f) cmd->viewangles[i] += 360.0f;
		}
	}

	if (!silent)
	{
		int i;
		for (i = 0; i < 3; i++)
		{
			while (viewangles[i] > 180.0f) viewangles[i] -= 360.0f;
			while (viewangles[i] < -180.0f) viewangles[i] += 360.0f;
		}
	}
}

void CL_Aimbot_GetModelAngles(vec3_t viewangles, vec3_t model_angles)
{
	VectorCopy(viewangles, model_angles);
}

void CL_Aimbot_DrawTargetHUD(void)
{
	if (!nash3d_aim.value) return;
	if (last_target_index < 1 || last_target_index > MAX_PLAYERS) return;
	{
		cl_entity_t *target = CL_GetEntityByIndex(last_target_index);
		if (!target || !target->model) return;
	}
}