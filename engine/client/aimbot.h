#ifndef AIMBOT_H
#define AIMBOT_H

#include "xash3d_types.h"
#include "cvardef.h"
#include "cl_entity.h"
#include "q_client.h"

#ifndef MAX_PLAYERS
#define MAX_PLAYERS 32
#endif

#ifndef MAX_BACKTRACK_RECORDS
#define MAX_BACKTRACK_RECORDS 64
#endif

#define PRIORITY_CLOSEST	0
#define PRIORITY_CROSSHAIR	1
#define PRIORITY_ANY		2

typedef struct backtrack_record_s {
    float simulation_time;
    vec3_t origin;
    vec3_t angles;
    vec3_t head_position;
    qboolean valid;
} backtrack_record_t;

extern backtrack_record_t backtrack_records[MAX_PLAYERS][MAX_BACKTRACK_RECORDS];

extern convar_t nash3d_aim;
extern convar_t nash3d_aim_smooth;
extern convar_t nash3d_aim_bone;
extern convar_t nash3d_aim_autoshoot;
extern convar_t nash3d_aim_silent;
extern convar_t nash3d_aim_predict;
extern convar_t nash3d_aim_priority;
extern convar_t nash3d_aim_visibility_check;
extern convar_t nash3d_aim_debug;
extern convar_t nash3d_aim_smooth_distance_scale;
extern convar_t nash3d_aim_reaction_time;
extern convar_t nash3d_aim_multipoint;
extern convar_t nash3d_aim_reset_on_round;
extern convar_t nash3d_aim_backtrack;
extern convar_t nash3d_aim_backtrack_time;
extern convar_t nash3d_aim_ignore_team;
extern convar_t nash3d_aim_ignore_wall;
extern convar_t nash3d_aim_head_scale;
extern convar_t nash3d_aim_triggerbot;
extern convar_t nash3d_aim_triggerbot_delay;
extern convar_t nash3d_aim_nevermiss;
extern convar_t nash3d_aim_norecoil;

void CL_Aimbot_RegisterCVars(void);
void CL_Aimbot_Apply(vec3_t viewangles, usercmd_t *cmd);
void CL_Aimbot_DrawTargetHUD(void);
qboolean CL_Aimbot_IsEnabled(void);
void CL_Aimbot_ResetMemory(void);
void Aimbot_AddFriend_f(void);
void Aimbot_ListFriends_f(void);

void CL_Aimbot_UpdateBacktrackRecords(void);
backtrack_record_t *CL_Aimbot_GetBestBacktrackRecord(int player_index, vec3_t eye_pos);
cl_entity_t *CL_Aimbot_FindBestTargetWithBacktrack(vec3_t viewangles, vec3_t *best_head_pos);

qboolean CL_Aimbot_IsValidTarget(cl_entity_t *ent);
void CL_Aimbot_GetEyePosition(vec3_t eye_pos);
qboolean CL_Aimbot_IsTargetVisible(vec3_t start, vec3_t end);
float CL_Aimbot_GetTargetPriority(cl_entity_t *ent, vec3_t viewangles);
qboolean CL_Aimbot_IsFriend(int player_index);
qboolean CL_Aimbot_ShouldTargetTeam(int target_index);
void CL_Aimbot_CheckRoundReset(void);

void GetTargetPoint(cl_entity_t *target, vec3_t out);
void CL_Aimbot_PredictTargetPosition(cl_entity_t *ent, vec3_t current_pos, vec3_t predicted_pos);
void CL_Aimbot_SmoothAngles(vec3_t current, vec3_t target, vec3_t result, float smooth, float distance_to_target);

#endif
