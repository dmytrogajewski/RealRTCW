/*
===========================================================================

Return to Castle Wolfenstein single player GPL Source Code
Copyright (C) 1999-2010 id Software LLC, a ZeniMax Media company. 

This file is part of the Return to Castle Wolfenstein single player GPL Source Code (RTCW SP Source Code).  

RTCW SP Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

RTCW SP Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with RTCW SP Source Code.  If not, see <http://www.gnu.org/licenses/>.

===========================================================================
*/

//===========================================================================
//
// Name:         ai_tactical_memory.h
// Function:     Shared Tactical Memory for AI Squad Coordination
// Programmer:   AI-Enhanced
//===========================================================================

#ifndef __AI_TACTICAL_MEMORY_H__
#define __AI_TACTICAL_MEMORY_H__

#include "../qcommon/q_shared.h"

// Forward declarations (avoid circular dependencies)
#ifndef MAX_CLIENTS
#define MAX_CLIENTS 64
#endif

#ifndef MAX_TEAMS
#define MAX_TEAMS 4  // TEAM_FREE, TEAM_RED, TEAM_BLUE, TEAM_SPECTATOR
#endif

// Constants
#define MAX_TACTICAL_ENEMIES    64
#define MAX_COVER_POINTS        128
#define MAX_TACTICAL_OBJECTIVES 16    // Renamed to avoid conflict with bg_public.h
#define MAX_RECENT_EVENTS       32
#define MAX_TACTICAL_SQUADS     24    // Renamed to avoid conflict, increased to handle more NPCs

// Enemy information shared across squad
typedef struct {
	vec3_t position;              // Last known position
	vec3_t lastKnownVelocity;     // Movement direction/speed
	int lastSeenTime;             // When last spotted (level.time)
	int lastSeenBy;               // Entity that spotted them
	int entityNum;                // Entity number (-1 if not active)
	float threatLevel;            // 0.0-1.0 threat assessment
	int weaponType;               // What weapon they're using
	int health;                   // Estimated health (if known)
	qboolean confirmed;           // true = seen, false = predicted
	qboolean isPlayer;            // Is this the player?
} tactical_enemy_info_t;

// Cover point information
typedef struct {
	vec3_t position;              // Cover position
	vec3_t normal;                // Direction of cover (facing)
	float quality;                // Cover quality 0.0-1.0
	int occupiedBy;               // Entity num using it (-1 if free)
	int lastUpdateTime;           // When last evaluated
	qboolean isHard;              // Hard cover vs concealment
} tactical_cover_point_t;

// Objective/control point
typedef struct {
	vec3_t position;              // Objective location
	char name[64];                // Objective name
	int controlledBy;             // Team controlling it (-1 = neutral)
	int importance;               // Strategic value (0-10)
	qboolean active;              // Is this objective active?
} tactical_objective_t;

// Recent battle events for adaptation
typedef enum {
	EVENT_NONE,
	EVENT_ENEMY_SPOTTED,
	EVENT_FRIENDLY_KILLED,
	EVENT_ENEMY_KILLED,
	EVENT_TAKING_FIRE,
	EVENT_FLANK_DETECTED,
	EVENT_OBJECTIVE_LOST,
	EVENT_OBJECTIVE_CAPTURED,
	EVENT_RETREAT_ORDERED,
	EVENT_ASSAULT_ORDERED
} tactical_event_type_t;

typedef struct {
	tactical_event_type_t type;
	int timestamp;                // When it happened
	int entityNum;                // Who it happened to/by
	vec3_t position;              // Where it happened
	char description[128];        // Human-readable description
} tactical_event_t;

// Main tactical memory structure (one per team)
typedef struct {
	int teamNum;                  // Team this belongs to
	
	// Enemy tracking
	tactical_enemy_info_t enemies[MAX_TACTICAL_ENEMIES];
	int enemyCount;               // Active enemies
	int totalEnemiesSpotted;      // Total seen (including dead)
	
	// Friendly tracking
	int friendlyCount;            // Alive friendlies
	int totalFriendlies;          // Total friendlies (including dead)
	vec3_t friendlyPositions[MAX_CLIENTS];  // Current positions
	int friendlyHealth[MAX_CLIENTS];        // Current health
	
	// Tactical positions
	tactical_cover_point_t coverPoints[MAX_COVER_POINTS];
	int coverPointCount;
	
	// Objectives
	tactical_objective_t objectives[MAX_TACTICAL_OBJECTIVES];
	int objectiveCount;
	
	// Battle flow
	float teamMorale;             // 0.0-1.0 based on performance
	int friendlyCasualties;       // Casualties this engagement
	int enemyCasualties;          // Enemy casualties
	
	// Recent events for learning/adaptation
	tactical_event_t recentEvents[MAX_RECENT_EVENTS];
	int eventIndex;               // Ring buffer index
	
	// Combat zones
	vec3_t hotZones[16];          // Areas of active combat
	int hotZoneCount;
	
	// Last update
	int lastUpdateTime;
	
} tactical_memory_t;

//
// Public API
//

// Initialize tactical memory system
void TacticalMemory_Init(void);

// Shutdown tactical memory
void TacticalMemory_Shutdown(void);

// Get tactical memory for a team
tactical_memory_t *TacticalMemory_GetForTeam(int teamNum);

// Update enemy information (called when NPC sees enemy)
void TacticalMemory_UpdateEnemy(int teamNum, int enemyNum, vec3_t position, 
                                 vec3_t velocity, int weaponType, qboolean confirmed);

// Get enemy info by entity number
tactical_enemy_info_t *TacticalMemory_GetEnemy(int teamNum, int enemyNum);

// Get all enemies for a team
tactical_enemy_info_t *TacticalMemory_GetEnemies(int teamNum, int *outCount);

// Mark enemy as dead/removed
void TacticalMemory_RemoveEnemy(int teamNum, int enemyNum);

// Update friendly positions (call from AICast_Think)
void TacticalMemory_UpdateFriendly(int teamNum, int entityNum, vec3_t position, int health);

// Add/update cover point
void TacticalMemory_AddCoverPoint(int teamNum, vec3_t position, vec3_t normal, 
                                   float quality, qboolean isHard);

// Find nearest available cover
tactical_cover_point_t *TacticalMemory_FindNearestCover(int teamNum, vec3_t position, 
                                                          float maxDist, int *outIndex);

// Mark cover as occupied/free
void TacticalMemory_SetCoverOccupied(int teamNum, int coverIndex, int entityNum);

// Add objective
void TacticalMemory_AddObjective(int teamNum, vec3_t position, const char *name, int importance);

// Update objective control
void TacticalMemory_UpdateObjective(int teamNum, int objIndex, int controlledBy);

// Record an event
void TacticalMemory_RecordEvent(int teamNum, tactical_event_type_t type, 
                                 int entityNum, vec3_t position, const char *description);

// Get recent events (for LLM context)
tactical_event_t *TacticalMemory_GetRecentEvents(int teamNum, int count, int *outCount);

// Calculate team morale based on performance
float TacticalMemory_CalculateMorale(int teamNum);

// Update team casualties
void TacticalMemory_RecordCasualty(int teamNum, qboolean friendly);

// Get count of active threats
int TacticalMemory_GetThreatCount(int teamNum);

// Predict enemy position based on velocity
void TacticalMemory_PredictEnemyPosition(tactical_enemy_info_t *enemy, 
                                          float deltaTime, vec3_t outPos);

// Clear old/stale enemy info
void TacticalMemory_CleanupStaleData(int teamNum, int currentTime, int maxAge);

// Debug: Print tactical memory state
void TacticalMemory_Debug(int teamNum);

#endif // __AI_TACTICAL_MEMORY_H__

