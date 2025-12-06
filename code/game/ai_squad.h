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
// Name:         ai_squad.h
// Function:     Squad Coordination and Role Assignment
// Programmer:   AI-Enhanced
//===========================================================================

#ifndef __AI_SQUAD_H__
#define __AI_SQUAD_H__

#include "../qcommon/q_shared.h"

// Forward declaration (ai_cast.h will be included in .c files)
struct cast_state_s;

// Squad structure
typedef struct {
	int squadId;
	int leaderNum;
	int memberCount;
	int members[8];            // Entity numbers
	int teamNum;               // Which team owns this squad
	qboolean active;
	vec3_t rallyPoint;         // Current rally/regroup point
	char currentOrder[64];     // Current tactical order
	int lastOrderTime;
} squad_t;

//
// Public API
//

// Initialize squad system
void AICast_SquadInit(void);

// Shutdown squad system
void AICast_SquadShutdown(void);

// Assign NPCs to squads on level start
void AICast_AssignSquads(void);

// Assign roles to squad members
void AICast_AssignSquadRoles(int squadId);

// Get squad for an entity
squad_t *AICast_GetSquad(int entityNum);

// Squad leader tactical decision making (uses LLM)
void AICast_SquadLeaderThink(struct cast_state_s *cs);

// Squad members execute leader's orders
void AICast_SquadMemberExecute(struct cast_state_s *cs);

// Update squad formation
void AICast_UpdateFormation(int squadId);

// Execute coordinated attack
void AICast_CoordinatedAttack(int squadId, int targetNum);

// Issue order to squad
void AICast_SquadOrder(int squadId, const char *order);

// Issue order to all squads on a team
void AICast_TeamSquadOrder(int teamNum, const char *order);

// Broadcast message to squad
void AICast_SquadBroadcast(int squadId, const char *message);

// Check if entity is squad leader
qboolean AICast_IsSquadLeader(int entityNum);

// Get squad members
int AICast_GetSquadMembers(int squadId, int *outMembers, int maxMembers);

// Add member to squad
qboolean AICast_AddSquadMember(int squadId, int entityNum);

// Remove member from squad
void AICast_RemoveSquadMember(int squadId, int entityNum);

// Find nearest squad member
int AICast_FindNearestSquadMember(struct cast_state_s *cs, float maxDist);

// Check squad cohesion (are members too spread out?)
qboolean AICast_SquadCohesive(int squadId, float maxSpread);

// Regroup squad at rally point
void AICast_SquadRegroup(int squadId, vec3_t rallyPoint);

#endif // __AI_SQUAD_H__

