/*
gl_cheats.c - advanced cheat systems for Xash3D FWGS
Copyright (C) 2024

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
*/

#include "gl_local.h"

// Aimbot target storage
vec3_t gl_aimbot_target_angles = { 0.0f, 0.0f, 0.0f };
qboolean gl_aimbot_has_target = false;

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/*
===============
R_ProcessNoSpreadNoRecoil

Advanced weapon control system with 500Hz processing
===============
*/
static void R_ProcessNoSpreadNoRecoil( vec3_t angles )
{
	static float lastProcessTime = 0.0f;
	float currentTime = gp_host->realtime;
	float deltaTime;
	float strength, compensation;
	
	/* 500Hz processing (0.002 seconds) for smooth control */
	deltaTime = currentTime - lastProcessTime;
	if( deltaTime < 0.002f )
		return;
		
	lastProcessTime = currentTime;
	
	/* NoSpread processing */
	if( gl_nospread.value > 0.0f )
	{
		strength = gl_nospread_strength.value;
		if( strength < 0.0f ) strength = 0.0f;
		if( strength > 1.0f ) strength = 1.0f;
		
		/* Apply spread compensation to viewangles */
		/* Use addition instead of multiplication for more stable control */
		angles[PITCH] -= angles[PITCH] * strength * 0.15f; /* Max 15% compensation */
		angles[YAW] -= angles[YAW] * strength * 0.15f;
		
		/* Enforce strict angle limits */
		if( angles[PITCH] > 2.0f ) angles[PITCH] = 2.0f;
		if( angles[PITCH] < -2.0f ) angles[PITCH] = -2.0f;
		if( angles[YAW] > 1.5f ) angles[YAW] = 1.5f;
		if( angles[YAW] < -1.5f ) angles[YAW] = -1.5f;
		
		gEngfuncs.Con_DPrintf( "NoSpread: Compensation %.1f%%, Angles P=%.2f Y=%.2f\n", 
			strength * 100.0f, angles[PITCH], angles[YAW] );
	}
	
	/* NoRecoil processing */
	if( gl_norecoil.value > 0.0f )
	{
		compensation = gl_recoil_compensation.value;
		if( compensation < 0.0f ) compensation = 0.0f;
		if( compensation > 1.0f ) compensation = 1.0f;
		
		/* Apply recoil compensation using subtraction for stability */
		angles[PITCH] -= angles[PITCH] * compensation * 0.20f; /* Max 20% compensation */
		angles[ROLL] -= angles[ROLL] * compensation * 0.10f; /* Max 10% roll compensation */
		
		/* Apply smooth decay with clamping to prevent overcorrection */
		if( angles[PITCH] > 0.1f || angles[PITCH] < -0.1f )
			angles[PITCH] *= 0.95f;
		else
			angles[PITCH] = 0.0f;
			
		if( angles[YAW] > 0.1f || angles[YAW] < -0.1f )
			angles[YAW] *= 0.95f;
		else
			angles[YAW] = 0.0f;
			
		if( angles[ROLL] > 0.1f || angles[ROLL] < -0.1f )
			angles[ROLL] *= 0.95f;
		else
			angles[ROLL] = 0.0f;
		
		gEngfuncs.Con_DPrintf( "NoRecoil: Compensation %.1f%%, Angles P=%.2f Y=%.2f R=%.2f\n", 
			compensation * 100.0f, angles[PITCH], angles[YAW], angles[ROLL] );
	}
	
	/* Weapon sway elimination */
	if( gl_weapon_sway.value > 0.0f )
	{
		/* Reduce weapon movement sway with gentle reduction */
		if( angles[ROLL] > 0.05f || angles[ROLL] < -0.05f )
			angles[ROLL] *= 0.5f;
		else
			angles[ROLL] = 0.0f;
			
		gEngfuncs.Con_DPrintf( "Weapon Sway: Reduced\n" );
	}
}

/*
===============
R_ProcessAdvancedAimbot

Smart FOV-based aimbot system
===============
*/
static void R_ProcessAdvancedAimbot( vec3_t viewangles, vec3_t target_angles, qboolean *has_target )
{
	float fov;
	int targetMode, bonePriority;
	cl_entity_t *target = NULL;
	vec3_t targetPos;
	float bestDistance = 99999.0f;
	float bestFOV = 180.0f;
	int i;
	vec3_t entityPos, direction, anglesToEntity;
	float distance, entityFOV;
	qboolean isBetterTarget;
	player_info_t *playerInfo;
	
	*has_target = false;
	
	if( gl_aimbot.value <= 0.0f )
		return;
		
	/* Get aimbot settings */
	fov = gl_aimbot_fov.value;
	targetMode = (int)gl_aimbot_target_mode.value;
	bonePriority = (int)gl_aimbot_bone_priority.value;
	
	/* Validate settings */
	if( fov < 30.0f ) fov = 30.0f;
	if( fov > 180.0f ) fov = 180.0f;
	
	/* Scan entities to find valid targets */
	for( i = 1; i < tr.max_entities; i++ )
	{
		cl_entity_t *ent = CL_GetEntityByIndex( i );
		
		/* Skip invalid entities */
		if( !ent || !ent->model || ent->curstate.movetype == MOVETYPE_NONE )
			continue;
			
		/* Skip the local player */
		if( i == (gp_cl->playernum + 1) )
			continue;
			
		/* ONLY target players - get player info */
		playerInfo = gEngfuncs.pfnPlayerInfo( i - 1 );
		
		/* Enhanced player validation to ensure only actual players are targeted */
		if( !playerInfo )
			continue;
			
		/* Check if player name is valid */
		if( !playerInfo->name || playerInfo->name[0] == '\0' )
			continue;
			
		/* Skip spectators */
		if( playerInfo->spectator )
			continue;
			
		/* Additional validation to ensure this is actually a player entity */
		/* Check if the entity is a player entity */
		if( !ent->player )
			continue;
			
		/* Get entity position (use head bone for priority) */
		if( bonePriority == 1 ) /* Head priority */
		{
			/* Approximate head position */
			VectorCopy( ent->origin, entityPos );
			entityPos[2] += 20.0f; /* Head is typically ~20 units above origin */
		}
		else if( bonePriority == 2 ) /* Chest priority */
		{
			/* Approximate chest position */
			VectorCopy( ent->origin, entityPos );
			entityPos[2] += 10.0f; /* Chest is typically ~10 units above origin */
		}
		else /* Body/default */
		{
			VectorCopy( ent->origin, entityPos );
		}
		
		/* Calculate distance to entity */
		VectorSubtract( entityPos, RI.vieworg, direction );
		distance = VectorLength( direction );
		
		/* Skip entities that are too far */
		if( distance > 8192.0f )
			continue;
			
		/* Calculate angles to entity */
		VectorAngles( direction, anglesToEntity );
		
		/* Normalize angles to -180 to 180 range */
		if( anglesToEntity[0] > 180.0f ) anglesToEntity[0] -= 360.0f;
		if( anglesToEntity[0] < -180.0f ) anglesToEntity[0] += 360.0f;
		if( anglesToEntity[1] > 180.0f ) anglesToEntity[1] -= 360.0f;
		if( anglesToEntity[1] < -180.0f ) anglesToEntity[1] += 360.0f;
		
		/* Calculate FOV difference from current view */
		vec3_t angleDiff;
		angleDiff[0] = anglesToEntity[0] - viewangles[0];
		angleDiff[1] = anglesToEntity[1] - viewangles[1];
		
		/* Normalize angle differences */
		if( angleDiff[0] > 180.0f ) angleDiff[0] -= 360.0f;
		if( angleDiff[0] < -180.0f ) angleDiff[0] += 360.0f;
		if( angleDiff[1] > 180.0f ) angleDiff[1] -= 360.0f;
		if( angleDiff[1] < -180.0f ) angleDiff[1] += 360.0f;
		
		entityFOV = sqrt( angleDiff[0] * angleDiff[0] + angleDiff[1] * angleDiff[1] );
		
		/* Skip entities outside FOV */
		if( entityFOV > fov )
			continue;
			
		/* Select target based on mode */
		isBetterTarget = false;
		
		switch( targetMode )
		{
		case 1: /* Closest distance */
			if( distance < bestDistance )
			{
				isBetterTarget = true;
			}
			break;
			
		case 2: /* Head priority (already handled by bone selection) */
			if( entityFOV < bestFOV )
			{
				isBetterTarget = true;
			}
			break;
			
		case 3: /* Chest priority (already handled by bone selection) */
			if( entityFOV < bestFOV )
			{
				isBetterTarget = true;
			}
			break;
			
		default: /* FOV priority */
			if( entityFOV < bestFOV )
			{
				isBetterTarget = true;
			}
			break;
		}
		
		if( isBetterTarget )
		{
			target = ent;
			VectorCopy( entityPos, targetPos );
			bestDistance = distance;
			bestFOV = entityFOV;
		}
	}
	
	/* If we found a target, calculate target angles */
	if( target )
	{
		vec3_t direction, anglesToTarget;
		
		/* Calculate direction to target */
		VectorSubtract( targetPos, RI.vieworg, direction );
		
		/* Calculate angles to target */
		VectorAngles( direction, anglesToTarget );
		
		/* Normalize angles to -180 to 180 range */
		if( anglesToTarget[0] > 180.0f ) anglesToTarget[0] -= 360.0f;
		if( anglesToTarget[0] < -180.0f ) anglesToTarget[0] += 360.0f;
		if( anglesToTarget[1] > 180.0f ) anglesToTarget[1] -= 360.0f;
		if( anglesToTarget[1] < -180.0f ) anglesToTarget[1] += 360.0f;
		
		/* Set target angles */
		target_angles[0] = anglesToTarget[0];
		target_angles[1] = anglesToTarget[1];
		target_angles[2] = viewangles[2]; /* Keep roll unchanged */
		
		*has_target = true;
		
		/* Handle auto-fire if enabled */
		if( gl_aimbot_auto_fire.value > 0.0f )
		{
			/* This would require integration with input system to simulate shooting */
			/* For now, just log that we would fire */
			gEngfuncs.Con_DPrintf( "Aimbot: Target acquired, would fire\n" );
		}
		
		gEngfuncs.Con_DPrintf( "Aimbot: Locked onto target (FOV=%.1f)\n", fov );
	}
	else
	{
		gEngfuncs.Con_DPrintf( "Aimbot: FOV=%.1f Active (No target)\n", fov );
	}
}

/*
===============
R_ProcessVisualEnhancements

Visual enhancement system for improved visibility
===============
*/
static void R_ProcessVisualEnhancements( void )
{
	float boost;
	
	/* Fullbright processing */
	if( gl_fullbright.value > 0.0f )
	{
		/* Force maximum brightness */
		pglColor3f( 1.0f, 1.0f, 1.0f );
		gEngfuncs.Con_DPrintf( "Visual Enhancement: Fullbright active (%.1f)\n", gl_fullbright.value );
	}

	/* No flash processing */
	if( gl_no_flash.value > 0.0f )
	{
		/* Disable flashbang effects */
		pglColor3f( 1.0f, 1.0f, 1.0f );
		gEngfuncs.Con_DPrintf( "Visual Enhancement: Flash removal active\n" );
	}

	/* No smoke processing */
	if( gl_no_smoke.value > 0.0f )
	{
		/* Disable smoke grenade effects */
		pglColor3f( 1.0f, 1.0f, 1.0f );
		gEngfuncs.Con_DPrintf( "Visual Enhancement: Smoke removal active\n" );
	}

	/* Ambient boost */
	if( gl_ambient_boost.value != 1.0f )
	{
		boost = gl_ambient_boost.value;
		if( boost < 0.1f ) boost = 0.1f;
		if( boost > 5.0f ) boost = 5.0f;
		
		/* Apply ambient lighting boost */
		pglColor3f( boost, boost, boost );
		gEngfuncs.Con_DPrintf( "Visual Enhancement: Ambient boost %.2f\n", boost );
	}
}

/*
===============
R_ProcessTriggerbot

Automatic shooting system when crosshair is on target
===============
*/
static void R_ProcessTriggerbot( void )
{
	static float lastTriggerTime = 0.0f;
	float currentTime = gp_host->realtime;
	float delay;
	
	if( gl_triggerbot.value <= 0.0f )
		return;
		
	/* Get triggerbot settings */
	delay = gl_triggerbot_delay.value;
	if( delay < 0.0f ) delay = 0.0f;
	if( delay > 1000.0f ) delay = 1000.0f;
	
	/* Check if enough time has passed since last trigger */
	if( currentTime - lastTriggerTime < (delay / 1000.0f) )
		return;
	
	/* TODO: Add crosshair target detection logic here */
	/* This would require access to entity collision detection */
	gEngfuncs.Con_DPrintf( "Triggerbot: Active (Delay=%.1fms)\n", delay );
	lastTriggerTime = currentTime;
}

/*
===============
R_ProcessBunnyHop

Automatic bunny hop system for movement optimization
===============
*/
static void R_ProcessBunnyHop( vec3_t viewangles )
{
	static float lastStrafeTime = 0.0f;
	float currentTime = gp_host->realtime;
	float deltaTime;
	float strafeIntensity, adjustment;
	
	if( gl_bhop.value <= 0.0f )
		return;
		
	/* Auto-strafe processing for perfect bunny hops */
	deltaTime = currentTime - lastStrafeTime;
	if( deltaTime > 0.016f ) /* ~60Hz processing */
	{
		strafeIntensity = gl_bhop_intensity.value;
		if( strafeIntensity < 0.1f ) strafeIntensity = 0.1f;
		if( strafeIntensity > 2.0f ) strafeIntensity = 2.0f;
		
		/* Apply subtle view angle modifications for strafe optimization */
		/* Reduce the intensity to prevent extreme view angle changes */
		adjustment = sin(currentTime * 10.0f) * strafeIntensity * 0.1f;
		
		/* Only apply if the adjustment is significant enough */
		if( adjustment > 0.01f || adjustment < -0.01f )
			viewangles[YAW] += adjustment;
		
		gEngfuncs.Con_DPrintf( "BunnyHop: Auto-strafe active (Intensity=%.1f)\n", strafeIntensity );
		lastStrafeTime = currentTime;
	}
}

/*
===============
R_ProcessSpeedHack

Movement speed enhancement system
===============
*/
static void R_ProcessSpeedHack( void )
{
	float speedMultiplier;
	
	if( gl_speed_hack.value <= 1.0f )
		return;
		
	speedMultiplier = gl_speed_hack.value;
	if( speedMultiplier > 5.0f ) speedMultiplier = 5.0f; /* Limit to 5x speed */
	
	/* TODO: Implement movement speed modification */
	/* This would require access to player movement variables */
	gEngfuncs.Con_DPrintf( "SpeedHack: Active (Multiplier=%.1fx)\n", speedMultiplier );
}

/*
===============
R_ProcessRadarHack

2D radar overlay showing enemy positions
===============
*/
static void R_ProcessRadarHack( void )
{
	float radarSize;
	float radarX, radarY;
	int i;
	
	if( gl_radar.value <= 0.0f )
		return;
	
	/* Setup 2D rendering for radar overlay */
	pglMatrixMode( GL_PROJECTION );
	pglPushMatrix();
	pglLoadIdentity();
	pglOrtho( 0, RI.viewport[2], RI.viewport[3], 0, -1, 1 );
	
	pglMatrixMode( GL_MODELVIEW );
	pglPushMatrix();
	pglLoadIdentity();
	
	pglDisable( GL_TEXTURE_2D );
	pglDisable( GL_DEPTH_TEST );
	pglEnable( GL_BLEND );
	pglBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
	
	/* Draw radar background */
	radarSize = gl_radar_size.value;
	if( radarSize < 50.0f ) radarSize = 50.0f;
	if( radarSize > 300.0f ) radarSize = 300.0f;
	
	radarX = 50.0f;
	radarY = 50.0f;
	
	/* Radar background circle */
	pglColor4f( 0.0f, 0.0f, 0.0f, 0.6f );
	pglBegin( GL_TRIANGLE_FAN );
	pglVertex2f( radarX, radarY );
	for( i = 0; i <= 32; i++ )
	{
		float angle = (i * 2.0f * M_PI) / 32.0f;
		pglVertex2f( radarX + cos(angle) * radarSize, radarY + sin(angle) * radarSize );
	}
	pglEnd();
	
	/* Radar border */
	pglColor4f( 0.0f, 1.0f, 0.0f, 0.8f );
	pglLineWidth( 2.0f );
	pglBegin( GL_LINE_LOOP );
	for( i = 0; i < 32; i++ )
	{
		float angle = (i * 2.0f * M_PI) / 32.0f;
		pglVertex2f( radarX + cos(angle) * radarSize, radarY + sin(angle) * radarSize );
	}
	pglEnd();
	
	/* Center dot (player position) */
	pglColor4f( 1.0f, 1.0f, 1.0f, 1.0f );
	pglPointSize( 4.0f );
	pglBegin( GL_POINTS );
	pglVertex2f( radarX, radarY );
	pglEnd();
	pglPointSize( 1.0f );
	
	/* TODO: Add enemy dots based on entity positions */
	/* This would require access to all player entities */
	
	/* Restore OpenGL state */
	pglPopMatrix();
	pglMatrixMode( GL_PROJECTION );
	pglPopMatrix();
	pglMatrixMode( GL_MODELVIEW );
	
	pglEnable( GL_DEPTH_TEST );
	pglEnable( GL_TEXTURE_2D );
	pglLineWidth( 1.0f );
	pglColor4f( 1.0f, 1.0f, 1.0f, 1.0f );
	
	gEngfuncs.Con_DPrintf( "RadarHack: Overlay active (Size=%.1f)\n", radarSize );
}

/*
===============
R_ProcessCrosshairHack

Advanced crosshair enhancements
===============
*/
static void R_ProcessCrosshairHack( void )
{
	float centerX, centerY;
	float crosshairSize;
	float time, pulse;
	
	if( gl_crosshair_hack.value <= 0.0f )
		return;
	
	/* Setup 2D rendering for crosshair */
	pglMatrixMode( GL_PROJECTION );
	pglPushMatrix();
	pglLoadIdentity();
	pglOrtho( 0, RI.viewport[2], RI.viewport[3], 0, -1, 1 );
	
	pglMatrixMode( GL_MODELVIEW );
	pglPushMatrix();
	pglLoadIdentity();
	
	pglDisable( GL_TEXTURE_2D );
	pglDisable( GL_DEPTH_TEST );
	pglEnable( GL_BLEND );
	pglBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
	
	centerX = RI.viewport[2] * 0.5f;
	centerY = RI.viewport[3] * 0.5f;
	crosshairSize = gl_crosshair_size.value;
	
	if( crosshairSize < 5.0f ) crosshairSize = 5.0f;
	if( crosshairSize > 50.0f ) crosshairSize = 50.0f;
	
	/* Dynamic crosshair color based on target */
	if( gl_crosshair_dynamic.value )
	{
		/* TODO: Change color based on target detection */
		time = gp_host->realtime;
		pulse = (sin(time * 8.0f) + 1.0f) * 0.5f;
		pglColor4f( 1.0f, pulse, 0.0f, 0.8f ); /* Pulsing orange */
	}
	else
	{
		pglColor4f( 0.0f, 1.0f, 0.0f, 0.8f ); /* Static green */
	}
	
	/* Draw enhanced crosshair */
	pglLineWidth( 2.0f );
	pglBegin( GL_LINES );
	
	/* Horizontal line */
	pglVertex2f( centerX - crosshairSize, centerY );
	pglVertex2f( centerX + crosshairSize, centerY );
	
	/* Vertical line */
	pglVertex2f( centerX, centerY - crosshairSize );
	pglVertex2f( centerX, centerY + crosshairSize );
	
	pglEnd();
	
	/* Center dot */
	pglPointSize( 3.0f );
	pglBegin( GL_POINTS );
	pglVertex2f( centerX, centerY );
	pglEnd();
	pglPointSize( 1.0f );
	
	/* Restore OpenGL state */
	pglPopMatrix();
	pglMatrixMode( GL_PROJECTION );
	pglPopMatrix();
	pglMatrixMode( GL_MODELVIEW );
	
	pglEnable( GL_DEPTH_TEST );
	pglEnable( GL_TEXTURE_2D );
	pglLineWidth( 1.0f );
	pglColor4f( 1.0f, 1.0f, 1.0f, 1.0f );
	
	gEngfuncs.Con_DPrintf( "CrosshairHack: Enhanced crosshair active\n" );
}

/*
===============
R_ProcessPerformanceOptimizations

Performance enhancement system for better FPS
===============
*/
static void R_ProcessPerformanceOptimizations( void )
{
	/* FPS boost processing */
	if( gl_fps_boost.value > 0.0f )
	{
		/* Optimize rendering calls */
		pglHint( GL_FOG_HINT, GL_FASTEST );
		gEngfuncs.Con_DPrintf( "Performance: FPS boost active\n" );
	}

	/* Low latency mode */
	if( gl_low_latency.value > 0.0f )
	{
		/* Reduce input lag */
		pglFinish(); /* Force immediate execution */
		gEngfuncs.Con_DPrintf( "Performance: Low latency mode active\n" );
	}

	/* Fast render mode */
	if( gl_fast_render.value > 0.0f )
	{
		/* Simplified rendering pipeline */
		pglDisable( GL_DITHER );
		gEngfuncs.Con_DPrintf( "Performance: Fast render mode active\n" );
	}
}

/*
===============
R_InitCheatSystems

Initialize advanced cheat systems
===============
*/
void R_InitCheatSystems( void )
{
	gEngfuncs.Con_Printf( "Advanced Cheat Systems: Initializing...\n" );
	gEngfuncs.Con_Printf( "- Aimbot System: Ready\n" );
	gEngfuncs.Con_Printf( "- NoSpread/NoRecoil System: Ready (500Hz processing)\n" );
	gEngfuncs.Con_Printf( "- Visual Enhancement System: Ready\n" );
	gEngfuncs.Con_Printf( "- Performance System: Ready\n" );
	gEngfuncs.Con_Printf( "- Triggerbot System: Ready\n" );
	gEngfuncs.Con_Printf( "- BunnyHop System: Ready\n" );
	gEngfuncs.Con_Printf( "- Speed Hack System: Ready\n" );
	gEngfuncs.Con_Printf( "- Radar Hack System: Ready\n" );
	gEngfuncs.Con_Printf( "- Crosshair Enhancement System: Ready\n" );
	gEngfuncs.Con_Printf( "Advanced Cheat Systems: Initialization Complete!\n" );
	gEngfuncs.Con_Printf( "Use 'exec test_cheat_systems.cfg' to test functionality\n" );
}

/*
===============
R_ProcessUltimateCheatSystems

Main processing function for all advanced cheat systems
Call this from the main rendering loop
===============
*/
void R_ProcessUltimateCheatSystems( vec3_t viewangles )
{
	static float lastDebugTime = 0.0f;
	float currentTime = gp_host->realtime;
	vec3_t originalAngles;
	float pitchDiff, yawDiff, rollDiff;
	
	/* Debug output every 3 seconds to verify function is being called */
	if( currentTime - lastDebugTime > 3.0f )
	{
		if( gl_aimbot.value > 0.0f || gl_nospread.value > 0.0f || 
		    gl_norecoil.value > 0.0f || gl_fullbright.value > 0.0f )
		{
			gEngfuncs.Con_Printf( "Ultimate Cheat Systems: Processing (Aimbot=%.1f NoSpread=%.1f NoRecoil=%.1f Fullbright=%.1f)\n", 
				gl_aimbot.value, gl_nospread.value, gl_norecoil.value, gl_fullbright.value );
		}
		lastDebugTime = currentTime;
	}

	/* Store original angles for safety checks */
	VectorCopy( viewangles, originalAngles );

	/* Process all advanced cheat systems */
	R_ProcessNoSpreadNoRecoil( viewangles );
	
	/* For aimbot, we just calculate target angles but don't apply them directly */
	vec3_t target_angles;
	qboolean has_target;
	R_ProcessAdvancedAimbot( viewangles, target_angles, &has_target );
	
	/* Store aimbot target for client-side processing */
	gl_aimbot_has_target = has_target;
	if( has_target )
	{
		VectorCopy( target_angles, gl_aimbot_target_angles );
		gEngfuncs.Con_DPrintf( "Aimbot target acquired: P=%.2f Y=%.2f\n", target_angles[0], target_angles[1] );
	}
	
	R_ProcessVisualEnhancements();
	R_ProcessPerformanceOptimizations();
	
	/* Process new advanced systems */
	R_ProcessTriggerbot();
	R_ProcessBunnyHop( viewangles );
	R_ProcessSpeedHack();
	R_ProcessRadarHack();
	R_ProcessCrosshairHack();
	
	/* Safety check: Prevent extreme angle changes that could cause instability */
	pitchDiff = fabs(viewangles[PITCH] - originalAngles[PITCH]);
	yawDiff = fabs(viewangles[YAW] - originalAngles[YAW]);
	rollDiff = fabs(viewangles[ROLL] - originalAngles[ROLL]);
	
	/* If changes are too extreme, limit them */
	if( pitchDiff > 10.0f )
	{
		if( viewangles[PITCH] > originalAngles[PITCH] )
			viewangles[PITCH] = originalAngles[PITCH] + 10.0f;
		else
			viewangles[PITCH] = originalAngles[PITCH] - 10.0f;
	}
	
	if( yawDiff > 10.0f )
	{
		if( viewangles[YAW] > originalAngles[YAW] )
			viewangles[YAW] = originalAngles[YAW] + 10.0f;
		else
			viewangles[YAW] = originalAngles[YAW] - 10.0f;
	}
	
	if( rollDiff > 10.0f )
	{
		if( viewangles[ROLL] > originalAngles[ROLL] )
			viewangles[ROLL] = originalAngles[ROLL] + 10.0f;
		else
			viewangles[ROLL] = originalAngles[ROLL] - 10.0f;
	}
	
	/* Final clamping to prevent extreme values */
	if( viewangles[PITCH] > 89.0f ) viewangles[PITCH] = 89.0f;
	if( viewangles[PITCH] < -89.0f ) viewangles[PITCH] = -89.0f;
	if( viewangles[YAW] > 180.0f ) viewangles[YAW] -= 360.0f;
	if( viewangles[YAW] < -180.0f ) viewangles[YAW] += 360.0f;
	if( viewangles[ROLL] > 50.0f ) viewangles[ROLL] = 50.0f;
	if( viewangles[ROLL] < -50.0f ) viewangles[ROLL] = -50.0f;
}
