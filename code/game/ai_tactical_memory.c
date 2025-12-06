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

#include "g_local.h"
#include "../botlib/botlib.h"
#include "../botlib/be_aas.h"
#include "../botlib/be_ai_goal.h"
#include "../botlib/be_ai_move.h"
#include "ai_tactical_memory.h"
#include "ai_cast.h"
#include "ai_llm.h"  // For ai_llm_debug

// Global tactical memory - one per team
static tactical_memory_t g_tacticalMemory[MAX_TEAMS];
static qboolean g_tacticalMemoryInitialized = qfalse;

/*
================
TacticalMemory_Init

Initialize tactical memory system
================
*/
void TacticalMemory_Init(void) {
	int i, j;
	
	memset(g_tacticalMemory, 0, sizeof(g_tacticalMemory));
	
	// Initialize each team's memory
	for (i = 0; i < MAX_TEAMS; i++) {
		g_tacticalMemory[i].teamNum = i;
		g_tacticalMemory[i].teamMorale = 1.0f;
		g_tacticalMemory[i].lastUpdateTime = 0;
		
		// Initialize enemy slots
		for (j = 0; j < MAX_TACTICAL_ENEMIES; j++) {
			g_tacticalMemory[i].enemies[j].entityNum = -1;
			g_tacticalMemory[i].enemies[j].confirmed = qfalse;
		}
		
		// Initialize cover points
		for (j = 0; j < MAX_COVER_POINTS; j++) {
			g_tacticalMemory[i].coverPoints[j].occupiedBy = -1;
		}
	}
	
	g_tacticalMemoryInitialized = qtrue;
	
	G_Printf("Tactical Memory initialized for %d teams\n", MAX_TEAMS);
}

/*
================
TacticalMemory_Shutdown

Cleanup tactical memory
================
*/
void TacticalMemory_Shutdown(void) {
	g_tacticalMemoryInitialized = qfalse;
	G_Printf("Tactical Memory shutdown\n");
}

/*
================
TacticalMemory_GetForTeam

Get tactical memory for a specific team
================
*/
tactical_memory_t *TacticalMemory_GetForTeam(int teamNum) {
	if (!g_tacticalMemoryInitialized) {
		return NULL;
	}
	
	if (teamNum < 0 || teamNum >= MAX_TEAMS) {
		return NULL;
	}
	
	return &g_tacticalMemory[teamNum];
}

/*
================
TacticalMemory_UpdateEnemy

Update or add enemy information
================
*/
void TacticalMemory_UpdateEnemy(int teamNum, int enemyNum, vec3_t position, 
                                 vec3_t velocity, int weaponType, qboolean confirmed) {
	tactical_memory_t *tm;
	tactical_enemy_info_t *enemy;
	int i, freeSlot = -1;
	gentity_t *ent;
	
	if (!g_tacticalMemoryInitialized) {
		return;
	}
	
	tm = TacticalMemory_GetForTeam(teamNum);
	if (!tm) {
		return;
	}
	
	if (enemyNum < 0 || enemyNum >= MAX_GENTITIES) {
		return;
	}
	
	ent = &g_entities[enemyNum];
	if (!ent->inuse) {
		return;
	}
	
	// Find existing entry or free slot
	for (i = 0; i < MAX_TACTICAL_ENEMIES; i++) {
		if (tm->enemies[i].entityNum == enemyNum) {
			enemy = &tm->enemies[i];
			break;
		}
		if (tm->enemies[i].entityNum == -1 && freeSlot == -1) {
			freeSlot = i;
		}
	}
	
	// Create new entry if not found
	if (i == MAX_TACTICAL_ENEMIES) {
		if (freeSlot == -1) {
			return; // No free slots
		}
		enemy = &tm->enemies[freeSlot];
		memset(enemy, 0, sizeof(*enemy));
		enemy->entityNum = enemyNum;
		tm->enemyCount++;
		tm->totalEnemiesSpotted++;
	}
	
	// Update information
	VectorCopy(position, enemy->position);
	if (velocity) {
		VectorCopy(velocity, enemy->lastKnownVelocity);
	}
	enemy->lastSeenTime = level.time;
	enemy->weaponType = weaponType;
	enemy->threatLevel = 0.5f; // Default, can be refined
	enemy->confirmed = confirmed;
	enemy->isPlayer = (ent->s.number < MAX_CLIENTS && ent->client != NULL);
	enemy->health = ent->health;
	
	tm->lastUpdateTime = level.time;
	
	// Log new enemy detections
	if (ai_llm_debug.integer && i == MAX_TACTICAL_ENEMIES) {
		G_Printf("^3[INTEL] Team %d: New enemy detected at position (%.0f, %.0f, %.0f)\n",
		        teamNum, position[0], position[1], position[2]);
	}
}

/*
================
TacticalMemory_GetEnemy

Get enemy info by entity number
================
*/
tactical_enemy_info_t *TacticalMemory_GetEnemy(int teamNum, int enemyNum) {
	tactical_memory_t *tm;
	int i;
	
	tm = TacticalMemory_GetForTeam(teamNum);
	if (!tm) {
		return NULL;
	}
	
	for (i = 0; i < MAX_TACTICAL_ENEMIES; i++) {
		if (tm->enemies[i].entityNum == enemyNum) {
			return &tm->enemies[i];
		}
	}
	
	return NULL;
}

/*
================
TacticalMemory_GetEnemies

Get all active enemies for a team
================
*/
tactical_enemy_info_t *TacticalMemory_GetEnemies(int teamNum, int *outCount) {
	tactical_memory_t *tm;
	
	tm = TacticalMemory_GetForTeam(teamNum);
	if (!tm) {
		*outCount = 0;
		return NULL;
	}
	
	*outCount = tm->enemyCount;
	return tm->enemies;
}

/*
================
TacticalMemory_RemoveEnemy

Mark enemy as dead/removed
================
*/
void TacticalMemory_RemoveEnemy(int teamNum, int enemyNum) {
	tactical_memory_t *tm;
	int i;
	
	tm = TacticalMemory_GetForTeam(teamNum);
	if (!tm) {
		return;
	}
	
	for (i = 0; i < MAX_TACTICAL_ENEMIES; i++) {
		if (tm->enemies[i].entityNum == enemyNum) {
			tm->enemies[i].entityNum = -1;
			tm->enemies[i].confirmed = qfalse;
			if (tm->enemyCount > 0) {
				tm->enemyCount--;
			}
			break;
		}
	}
}

/*
================
TacticalMemory_UpdateFriendly

Update friendly unit information
================
*/
void TacticalMemory_UpdateFriendly(int teamNum, int entityNum, vec3_t position, int health) {
	tactical_memory_t *tm;
	
	tm = TacticalMemory_GetForTeam(teamNum);
	if (!tm) {
		return;
	}
	
	if (entityNum < 0 || entityNum >= MAX_CLIENTS) {
		return;
	}
	
	VectorCopy(position, tm->friendlyPositions[entityNum]);
	tm->friendlyHealth[entityNum] = health;
	tm->lastUpdateTime = level.time;
	
	// Count active friendlies
	int activeCount = 0;
	for (int i = 0; i < MAX_CLIENTS; i++) {
		if (tm->friendlyHealth[i] > 0) {
			activeCount++;
		}
	}
	tm->friendlyCount = activeCount;
}

/*
================
TacticalMemory_AddCoverPoint

Add or update a cover point
================
*/
void TacticalMemory_AddCoverPoint(int teamNum, vec3_t position, vec3_t normal, 
                                   float quality, qboolean isHard) {
	tactical_memory_t *tm;
	tactical_cover_point_t *cover;
	int i;
	float dist, closestDist = 64.0f; // Merge nearby cover points
	int closestIndex = -1;
	
	tm = TacticalMemory_GetForTeam(teamNum);
	if (!tm) {
		return;
	}
	
	// Check if cover point already exists nearby
	for (i = 0; i < tm->coverPointCount && i < MAX_COVER_POINTS; i++) {
		dist = Distance(position, tm->coverPoints[i].position);
		if (dist < closestDist) {
			closestDist = dist;
			closestIndex = i;
		}
	}
	
	if (closestIndex >= 0) {
		// Update existing nearby cover
		cover = &tm->coverPoints[closestIndex];
	} else {
		// Add new cover point
		if (tm->coverPointCount >= MAX_COVER_POINTS) {
			return; // Full
		}
		cover = &tm->coverPoints[tm->coverPointCount];
		tm->coverPointCount++;
	}
	
	VectorCopy(position, cover->position);
	VectorCopy(normal, cover->normal);
	cover->quality = quality;
	cover->isHard = isHard;
	cover->lastUpdateTime = level.time;
}

/*
================
TacticalMemory_FindNearestCover

Find nearest available cover point
================
*/
tactical_cover_point_t *TacticalMemory_FindNearestCover(int teamNum, vec3_t position, 
                                                          float maxDist, int *outIndex) {
	tactical_memory_t *tm;
	int i, bestIndex = -1;
	float dist, bestDist = maxDist;
	
	tm = TacticalMemory_GetForTeam(teamNum);
	if (!tm) {
		return NULL;
	}
	
	for (i = 0; i < tm->coverPointCount && i < MAX_COVER_POINTS; i++) {
		if (tm->coverPoints[i].occupiedBy >= 0) {
			continue; // Occupied
		}
		
		dist = Distance(position, tm->coverPoints[i].position);
		if (dist < bestDist) {
			bestDist = dist;
			bestIndex = i;
		}
	}
	
	if (bestIndex >= 0 && outIndex) {
		*outIndex = bestIndex;
		return &tm->coverPoints[bestIndex];
	}
	
	return NULL;
}

/*
================
TacticalMemory_SetCoverOccupied

Mark cover point as occupied or free
================
*/
void TacticalMemory_SetCoverOccupied(int teamNum, int coverIndex, int entityNum) {
	tactical_memory_t *tm;
	
	tm = TacticalMemory_GetForTeam(teamNum);
	if (!tm) {
		return;
	}
	
	if (coverIndex < 0 || coverIndex >= tm->coverPointCount) {
		return;
	}
	
	tm->coverPoints[coverIndex].occupiedBy = entityNum;
}

/*
================
TacticalMemory_AddObjective

Add an objective/control point
================
*/
void TacticalMemory_AddObjective(int teamNum, vec3_t position, const char *name, int importance) {
	tactical_memory_t *tm;
	tactical_objective_t *obj;
	
	tm = TacticalMemory_GetForTeam(teamNum);
	if (!tm) {
		return;
	}
	
	if (tm->objectiveCount >= MAX_TACTICAL_OBJECTIVES) {
		return;
	}
	
	obj = &tm->objectives[tm->objectiveCount];
	VectorCopy(position, obj->position);
	Q_strncpyz(obj->name, name, sizeof(obj->name));
	obj->importance = importance;
	obj->controlledBy = -1;
	obj->active = qtrue;
	
	tm->objectiveCount++;
}

/*
================
TacticalMemory_UpdateObjective

Update objective control status
================
*/
void TacticalMemory_UpdateObjective(int teamNum, int objIndex, int controlledBy) {
	tactical_memory_t *tm;
	
	tm = TacticalMemory_GetForTeam(teamNum);
	if (!tm) {
		return;
	}
	
	if (objIndex < 0 || objIndex >= tm->objectiveCount) {
		return;
	}
	
	tm->objectives[objIndex].controlledBy = controlledBy;
}

/*
================
TacticalMemory_RecordEvent

Record a battle event for learning
================
*/
void TacticalMemory_RecordEvent(int teamNum, tactical_event_type_t type, 
                                 int entityNum, vec3_t position, const char *description) {
	tactical_memory_t *tm;
	tactical_event_t *event;
	
	tm = TacticalMemory_GetForTeam(teamNum);
	if (!tm) {
		return;
	}
	
	// Ring buffer
	event = &tm->recentEvents[tm->eventIndex];
	tm->eventIndex = (tm->eventIndex + 1) % MAX_RECENT_EVENTS;
	
	event->type = type;
	event->timestamp = level.time;
	event->entityNum = entityNum;
	if (position) {
		VectorCopy(position, event->position);
	} else {
		VectorClear(event->position);
	}
	if (description) {
		Q_strncpyz(event->description, description, sizeof(event->description));
	} else {
		event->description[0] = '\0';
	}
}

/*
================
TacticalMemory_GetRecentEvents

Get N most recent events
================
*/
tactical_event_t *TacticalMemory_GetRecentEvents(int teamNum, int count, int *outCount) {
	tactical_memory_t *tm;
	
	tm = TacticalMemory_GetForTeam(teamNum);
	if (!tm) {
		*outCount = 0;
		return NULL;
	}
	
	*outCount = (count < MAX_RECENT_EVENTS) ? count : MAX_RECENT_EVENTS;
	return tm->recentEvents;
}

/*
================
TacticalMemory_CalculateMorale

Calculate team morale based on performance
================
*/
float TacticalMemory_CalculateMorale(int teamNum) {
	tactical_memory_t *tm;
	float morale = 1.0f;
	float killRatio;
	
	tm = TacticalMemory_GetForTeam(teamNum);
	if (!tm) {
		return 1.0f;
	}
	
	// Base morale on kill/death ratio
	if (tm->friendlyCasualties > 0) {
		killRatio = (float)tm->enemyCasualties / (float)tm->friendlyCasualties;
		morale = killRatio / (killRatio + 1.0f); // Normalize to 0.0-1.0
	} else if (tm->enemyCasualties > 0) {
		morale = 1.0f; // No casualties, winning
	} else {
		morale = 0.7f; // Neutral
	}
	
	// Clamp
	if (morale < 0.0f) morale = 0.0f;
	if (morale > 1.0f) morale = 1.0f;
	
	tm->teamMorale = morale;
	return morale;
}

/*
================
TacticalMemory_RecordCasualty

Record a team casualty
================
*/
void TacticalMemory_RecordCasualty(int teamNum, qboolean friendly) {
	tactical_memory_t *tm;
	
	tm = TacticalMemory_GetForTeam(teamNum);
	if (!tm) {
		return;
	}
	
	if (friendly) {
		tm->friendlyCasualties++;
		if (tm->friendlyCount > 0) {
			tm->friendlyCount--;
		}
	} else {
		tm->enemyCasualties++;
	}
	
	// Recalculate morale
	TacticalMemory_CalculateMorale(teamNum);
}

/*
================
TacticalMemory_GetThreatCount

Get count of active confirmed threats
================
*/
int TacticalMemory_GetThreatCount(int teamNum) {
	tactical_memory_t *tm;
	int count = 0, i;
	
	tm = TacticalMemory_GetForTeam(teamNum);
	if (!tm) {
		return 0;
	}
	
	for (i = 0; i < MAX_TACTICAL_ENEMIES; i++) {
		if (tm->enemies[i].entityNum >= 0 && tm->enemies[i].confirmed) {
			count++;
		}
	}
	
	return count;
}

/*
================
TacticalMemory_PredictEnemyPosition

Predict enemy position based on last known velocity
================
*/
void TacticalMemory_PredictEnemyPosition(tactical_enemy_info_t *enemy, 
                                          float deltaTime, vec3_t outPos) {
	vec3_t predicted;
	
	if (!enemy || enemy->entityNum < 0) {
		VectorClear(outPos);
		return;
	}
	
	// Simple linear prediction
	VectorMA(enemy->position, deltaTime / 1000.0f, enemy->lastKnownVelocity, predicted);
	VectorCopy(predicted, outPos);
}

/*
================
TacticalMemory_CleanupStaleData

Remove old enemy information
================
*/
void TacticalMemory_CleanupStaleData(int teamNum, int currentTime, int maxAge) {
	tactical_memory_t *tm;
	int i;
	
	tm = TacticalMemory_GetForTeam(teamNum);
	if (!tm) {
		return;
	}
	
	for (i = 0; i < MAX_TACTICAL_ENEMIES; i++) {
		if (tm->enemies[i].entityNum < 0) {
			continue;
		}
		
		if ((currentTime - tm->enemies[i].lastSeenTime) > maxAge) {
			// Too old, remove
			tm->enemies[i].entityNum = -1;
			if (tm->enemyCount > 0) {
				tm->enemyCount--;
			}
		}
	}
}

/*
================
TacticalMemory_Debug

Print debug information
================
*/
void TacticalMemory_Debug(int teamNum) {
	tactical_memory_t *tm;
	int i;
	
	tm = TacticalMemory_GetForTeam(teamNum);
	if (!tm) {
		G_Printf("TacticalMemory_Debug: Invalid team %d\n", teamNum);
		return;
	}
	
	G_Printf("=== Tactical Memory Team %d ===\n", teamNum);
	G_Printf("Enemies: %d/%d spotted\n", tm->enemyCount, tm->totalEnemiesSpotted);
	G_Printf("Friendlies: %d alive, %d total\n", tm->friendlyCount, tm->totalFriendlies);
	G_Printf("Morale: %.2f\n", tm->teamMorale);
	G_Printf("Casualties: %d friendly, %d enemy\n", tm->friendlyCasualties, tm->enemyCasualties);
	G_Printf("Cover points: %d\n", tm->coverPointCount);
	G_Printf("Objectives: %d\n", tm->objectiveCount);
	
	G_Printf("Active enemies:\n");
	for (i = 0; i < MAX_TACTICAL_ENEMIES; i++) {
		if (tm->enemies[i].entityNum >= 0) {
			G_Printf("  Enemy %d: pos(%.1f,%.1f,%.1f) threat:%.2f %s\n",
					tm->enemies[i].entityNum,
					tm->enemies[i].position[0],
					tm->enemies[i].position[1],
					tm->enemies[i].position[2],
					tm->enemies[i].threatLevel,
					tm->enemies[i].confirmed ? "CONFIRMED" : "predicted");
		}
	}
}

