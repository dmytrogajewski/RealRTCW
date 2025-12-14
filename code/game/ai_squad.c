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
#include "ai_cast.h"
#include "ai_squad.h"
#include "ai_tactical_memory.h"
#include "ai_llm.h"

#define MAX_SQUADS MAX_TACTICAL_SQUADS  // Use the definition from ai_tactical_memory.h
#define IDEAL_SQUAD_SIZE 4

// Global squad data
static squad_t g_squads[MAX_SQUADS];
static int g_squadCount = 0;
static qboolean g_squadSystemInitialized = qfalse;

// External references
extern cast_state_t *caststates;
extern int numcast;

/*
================
AICast_SquadInit

Initialize squad system
================
*/
void AICast_SquadInit(void) {
	memset(g_squads, 0, sizeof(g_squads));
	g_squadCount = 0;
	g_squadSystemInitialized = qtrue;
	
	G_Printf("Squad system initialized\n");
}

/*
================
AICast_SquadShutdown

Cleanup squad system
================
*/
void AICast_SquadShutdown(void) {
	g_squadSystemInitialized = qfalse;
	G_Printf("Squad system shutdown\n");
}

/*
================
AICast_AssignSquads

Auto-assign NPCs to squads based on team and proximity
================
*/
void AICast_AssignSquads(void) {
	int i;
	cast_state_t *cs;
	gentity_t *ent;
	int teamSquadCounts[4] = {0};      // 4 teams max
	int currentSquadId[4] = {-1, -1, -1, -1};
	
	if (!g_squadSystemInitialized) {
		return;
	}
	
	// Reset squads
	g_squadCount = 0;
	memset(g_squads, 0, sizeof(g_squads));
	
	// Assign NPCs to squads
	int processedCount = 0;
	for (i = 0; i < numcast; i++) {
		cs = &caststates[i];
		ent = &g_entities[cs->entityNum];
		
		// Skip invalid or dead entities
		if (!ent->inuse || ent->health <= 0) {
			// Clear squad assignment for dead/invalid entities
			if (cs->squadId >= 0) {
				cs->squadId = -1;
				cs->squadRole = SQUAD_ROLE_NONE;
			}
			continue;
		}
		
		// Skip player (entity 0) and non-cast AI
		if (!(ent->r.svFlags & SVF_CASTAI)) {
			cs->squadId = -1;
			continue;
		}
		
		// Skip NPCs that are currently running a script - they should follow their scripts
		// But allow NPCs that have scripts defined but aren't actively executing them
		if (cs->castScriptStatus.castScriptEventIndex >= 0) {
			cs->squadId = -1;
			cs->squadRole = SQUAD_ROLE_NONE;
			continue;
		}
		
		// Skip entities without valid team or with team >= 4
		if (ent->aiTeam < 0 || ent->aiTeam >= 4) {
			cs->squadId = -1;
			if (ai_llm_debug.integer >= 2) {
				G_Printf("Skipping entity %d with invalid aiTeam: %d\n", cs->entityNum, ent->aiTeam);
			}
			continue;
		}
		
		processedCount++;
		int team = ent->aiTeam;
		
		if (ai_llm_debug.integer >= 3) {
			G_Printf("Processing NPC %d for team %d squad assignment\n", cs->entityNum, team);
		}
		
		// Create new squad if needed
		if (teamSquadCounts[team] % IDEAL_SQUAD_SIZE == 0 || currentSquadId[team] == -1) {
			if (g_squadCount >= MAX_SQUADS) {
				if (ai_llm_debug.integer) {
					G_Printf("Squad limit reached (%d)\n", MAX_SQUADS);
				}
				break; // Too many squads
			}
			
			if (ai_llm_debug.integer >= 2) {
				G_Printf("Creating squad %d for team %d\n", g_squadCount, team);
			}
			
			currentSquadId[team] = g_squadCount;
			g_squads[g_squadCount].squadId = g_squadCount;
			g_squads[g_squadCount].teamNum = team;
			g_squads[g_squadCount].active = qtrue;
			g_squads[g_squadCount].leaderNum = -1;
			g_squads[g_squadCount].memberCount = 0;
			g_squadCount++;
		}
		
		// Add to current squad for this team
		int squadId = currentSquadId[team];
		if (g_squads[squadId].memberCount < 8) {
			g_squads[squadId].members[g_squads[squadId].memberCount] = cs->entityNum;
			g_squads[squadId].memberCount++;
			cs->squadId = squadId;
			teamSquadCounts[team]++;
		}
	}
	
	// Track previous squad state to suppress duplicate messages
	static int prevSquadCount = 0;
	static int prevSquadLeaders[MAX_SQUADS];
	static int prevSquadMemberCounts[MAX_SQUADS];
	static qboolean firstAssignment = qtrue;
	
	// Assign roles to each squad
	for (i = 0; i < g_squadCount; i++) {
		AICast_AssignSquadRoles(i);
	}
	
	// Only print squad formation messages if squads actually changed
	qboolean squadsChanged = qfalse;
	if (firstAssignment || g_squadCount != prevSquadCount) {
		squadsChanged = qtrue;
	} else {
		// Check if any squad leaders or member counts changed
		for (i = 0; i < g_squadCount && i < MAX_SQUADS; i++) {
			if (g_squads[i].leaderNum != prevSquadLeaders[i] ||
			    g_squads[i].memberCount != prevSquadMemberCounts[i]) {
				squadsChanged = qtrue;
				break;
			}
		}
	}
	
	if (squadsChanged || firstAssignment) {
		// Always print squad summary (this is important feedback)
		G_Printf("^5[SQUAD] Assignment: %d NPCs processed, %d squads formed (total cast=%d)\n", 
		        processedCount, g_squadCount, numcast);
		if (ai_llm_debug.integer) {
			for (i = 0; i < g_squadCount && i < 10; i++) {
				G_Printf("  Squad %d: %d members, team %d\n", 
				        i, g_squads[i].memberCount, g_squads[i].teamNum);
			}
		}
		
		// Update previous state
		prevSquadCount = g_squadCount;
		for (i = 0; i < g_squadCount && i < MAX_SQUADS; i++) {
			prevSquadLeaders[i] = g_squads[i].leaderNum;
			prevSquadMemberCounts[i] = g_squads[i].memberCount;
		}
		// Clear remaining slots
		for (i = g_squadCount; i < MAX_SQUADS; i++) {
			prevSquadLeaders[i] = -1;
			prevSquadMemberCounts[i] = 0;
		}
		firstAssignment = qfalse;
	}
}

/*
================
AICast_AssignSquadRoles

Assign roles based on character type and position
================
*/
void AICast_AssignSquadRoles(int squadId) {
	squad_t *squad;
	cast_state_t *cs;
	gentity_t *ent;
	int i;
	int leaderIndex = -1;
	int bestLeaderScore = -1;
	
	if (squadId < 0 || squadId >= g_squadCount) {
		return;
	}
	
	squad = &g_squads[squadId];
	
	if (squad->memberCount == 0) {
		return;
	}
	
	// Find best leader (highest rank/health)
	// First pass: remove dead/invalid members and find leader
	for (i = 0; i < squad->memberCount; i++) {
		int entNum = squad->members[i];
		
		// Validate entity number
		if (entNum < 0 || entNum >= MAX_GENTITIES) {
			continue;
		}
		
		ent = &g_entities[entNum];
		if (!ent->inuse || ent->health <= 0) {
			// Dead or invalid entity - skip it
			continue;
		}
		
		cs = AICast_GetCastState(entNum);
		if (!cs) continue;
		
		int score = ent->health; // Simple: healthiest becomes leader
		if (score > bestLeaderScore) {
			bestLeaderScore = score;
			leaderIndex = i;
		}
	}
	
	// If no valid leader found, mark squad as invalid
	if (leaderIndex < 0 || bestLeaderScore <= 0) {
		squad->leaderNum = -1;
		if (ai_llm_debug.integer) {
			G_Printf("^3[SQUAD %d] Warning: No valid leader found, squad may be disbanded\n", squadId);
		}
		return;
	}
	
	// Assign roles - only to valid, living members
	int validMemberIndex = 0;
	for (i = 0; i < squad->memberCount; i++) {
		int entNum = squad->members[i];
		
		// Validate entity
		if (entNum < 0 || entNum >= MAX_GENTITIES) {
			continue;
		}
		
		ent = &g_entities[entNum];
		if (!ent->inuse || ent->health <= 0) {
			// Skip dead/invalid members
			continue;
		}
		
		cs = AICast_GetCastState(entNum);
		if (!cs) continue;
		
		// Check if this is the leader
		if (i == leaderIndex) {
			cs->squadRole = SQUAD_ROLE_LEADER;
			cs->squadLeaderNum = entNum; // Leader is own leader
			squad->leaderNum = entNum;
		} else if (validMemberIndex == 0 && leaderIndex != i) {
			// First non-leader becomes scout
			cs->squadRole = SQUAD_ROLE_SCOUT;
			cs->squadLeaderNum = squad->members[leaderIndex];
		} else if (validMemberIndex % 2 == 0) {
			cs->squadRole = SQUAD_ROLE_ASSAULT;
			cs->squadLeaderNum = squad->members[leaderIndex];
		} else {
			cs->squadRole = SQUAD_ROLE_SUPPORT;
			cs->squadLeaderNum = squad->members[leaderIndex];
		}
		
		cs->squadMemberCount = squad->memberCount;
		// Copy squad members list for easy access
		memcpy(cs->squadMembers, squad->members, sizeof(squad->members));
		
		validMemberIndex++;
	}
	
	// Log squad composition - only if this is a new formation or significant change
	// Suppress duplicate messages by checking if leader/member count changed
	static int lastLoggedSquadId = -1;
	static int lastLoggedLeader = -1;
	static int lastLoggedMemberCount = 0;
	
	// Only log if this is a different squad, or leader/member count changed
	if (ai_llm_debug.integer && 
	    (squadId != lastLoggedSquadId || 
	     squad->leaderNum != lastLoggedLeader || 
	     squad->memberCount != lastLoggedMemberCount)) {
		const char *leaderName = "(null)";
		if (squad->leaderNum >= 0 && squad->leaderNum < MAX_GENTITIES) {
			gentity_t *leaderEnt = &g_entities[squad->leaderNum];
			if (leaderEnt->inuse && leaderEnt->aiName) {
				leaderName = leaderEnt->aiName;
			}
		}
		G_Printf("^5[SQUAD %d] Formed: Leader=%s, %d members, Team %d\n", 
		        squadId, leaderName, squad->memberCount, squad->teamNum);
		
		// Update tracking
		lastLoggedSquadId = squadId;
		lastLoggedLeader = squad->leaderNum;
		lastLoggedMemberCount = squad->memberCount;
	}
}

/*
================
AICast_GetSquad

Get squad for an entity
================
*/
squad_t *AICast_GetSquad(int entityNum) {
	cast_state_t *cs;
	
	cs = AICast_GetCastState(entityNum);
	if (!cs || cs->squadId < 0 || cs->squadId >= g_squadCount) {
		return NULL;
	}
	
	return &g_squads[cs->squadId];
}

/*
================
AICast_SquadLeaderThink

Squad leader makes tactical decisions using LLM
Called from main think loop for squad leaders
================
*/
void AICast_SquadLeaderThink(cast_state_t *cs) {
	squad_t *squad;
	llm_decision_t decision;
	
	if (!cs || cs->squadRole != SQUAD_ROLE_LEADER) {
		return;
	}
	
	// Skip NPCs currently running a script - let the script control behavior
	if (cs->castScriptStatus.castScriptEventIndex >= 0) {
		return;
	}
	
	squad = AICast_GetSquad(cs->entityNum);
	if (!squad) {
		return;
	}
	
	// Check if we have a pending LLM decision
	if (LLM_GetStrategicDecision(cs->entityNum, &decision)) {
		if (decision.isValid) {
			// Translate LLM decision into squad order
			const char *order = NULL;
			
			switch (decision.action) {
				case LLM_ACTION_ATTACK:
					order = "assault";
					break;
				case LLM_ACTION_DEFEND:
					order = "defensive_position";
					break;
				case LLM_ACTION_RETREAT:
					order = "fallback";
					break;
				case LLM_ACTION_SUPPORT:
					order = "support_fire";
					break;
				default:
					order = "hold_position";
					break;
			}
			
			if (order) {
				AICast_SquadOrder(squad->squadId, order);
				
				// Log tactical decision
				G_Printf("^5[TACTICS] Squad Leader %s (Squad %d) issued order: %s (reason: %s)\n", 
				        g_entities[cs->entityNum].aiName, squad->squadId, order, decision.reasoning);
				
				// Request dialogue to announce order
				LLM_RequestDialogue(cs, "assault_ordered");
			}
		}
	}
	
	// Update formation
	AICast_UpdateFormation(squad->squadId);
}

/*
================
AICast_SquadMemberExecute

Squad members execute leader's orders
================
*/
void AICast_SquadMemberExecute(cast_state_t *cs) {
	squad_t *squad;
	gentity_t *ent, *leaderEnt;
	vec3_t formationPos;
	float distToFormation;
	
	if (!cs || cs->squadRole == SQUAD_ROLE_LEADER || cs->squadLeaderNum < 0) {
		return;
	}
	
	// Skip NPCs currently running a script - let the script control behavior
	if (cs->castScriptStatus.castScriptEventIndex >= 0) {
		return;
	}
	
	squad = AICast_GetSquad(cs->entityNum);
	if (!squad) {
		return;
	}
	
	ent = &g_entities[cs->entityNum];
	leaderEnt = &g_entities[cs->squadLeaderNum];
	
	if (!leaderEnt->inuse) {
		return;
	}
	
	// Calculate desired formation position
	VectorAdd(leaderEnt->r.currentOrigin, cs->squadFormationOffset, formationPos);
	distToFormation = Distance(ent->r.currentOrigin, formationPos);
	
	// Maintain formation if far from position and not in active combat
	// Formation is more important for support roles, less for scouts
	float formationTolerance = 128.0f;
	if (cs->squadRole == SQUAD_ROLE_SUPPORT) {
		formationTolerance = 96.0f; // Tighter formation for support
	} else if (cs->squadRole == SQUAD_ROLE_SCOUT) {
		formationTolerance = 192.0f; // Looser for scouts
	}
	
	if (ai_formation_strict.value > 0.5f && distToFormation > formationTolerance && cs->aiState < AISTATE_COMBAT) {
		// Move to formation position
		vec3_t moveDir;
		VectorSubtract(formationPos, ent->r.currentOrigin, moveDir);
		VectorNormalize(moveDir);
		
		// Use formation position as movement goal (if bot state is valid)
		if (cs->bs && cs->bs->inuse) {
			VectorCopy(formationPos, cs->bs->teamgoal.origin);
			cs->bs->teamgoal.entitynum = -1;
			cs->bs->teamgoal.areanum = BotPointAreaNum(formationPos);
		}
		
		static int lastFormationLog[MAX_CLIENTS] = {0};
		if (ai_llm_debug.integer >= 2 && level.time - lastFormationLog[cs->entityNum] > 10000) {
			const char *roleStr = "Member";
			switch (cs->squadRole) {
				case SQUAD_ROLE_SCOUT: roleStr = "Scout"; break;
				case SQUAD_ROLE_ASSAULT: roleStr = "Assault"; break;
				case SQUAD_ROLE_SUPPORT: roleStr = "Support"; break;
			}
			G_Printf("^6[FORMATION] %s (%s) moving to formation position (%.0f units away)\n", 
			        ent->aiName, roleStr, distToFormation);
			lastFormationLog[cs->entityNum] = level.time;
		}
	}
	
	// Check if leader or squad member has spotted an enemy - react immediately
	if (leaderEnt->inuse && leaderEnt->health > 0) {
		cast_state_t *leaderCS = AICast_GetCastState(cs->squadLeaderNum);
		if (leaderCS && leaderCS->enemyNum >= 0) {
			gentity_t *enemyEnt = &g_entities[leaderCS->enemyNum];
			if (enemyEnt->inuse) {
				// Leader has an enemy - check if we can see it too
				if (AICast_EntityVisible(cs, leaderCS->enemyNum, qtrue)) {
					// We can see the enemy - engage immediately
					if (cs->enemyNum != leaderCS->enemyNum) {
						cs->enemyNum = leaderCS->enemyNum;
						if (cs->aiState < AISTATE_COMBAT) {
							AICast_StateChange(cs, AISTATE_COMBAT);
						}
						// Take cover and engage
						vec3_t enemyPos;
						VectorCopy(enemyEnt->r.currentOrigin, enemyPos);
						if (AICast_GetTakeCoverPos(cs, leaderCS->enemyNum, enemyPos, cs->takeCoverPos)) {
							cs->takeCoverTime = level.time + 2000 + rand() % 2000;
							if (ai_llm_debug.integer >= 2) {
								G_Printf("^3[SQUAD] %s reacting to leader's enemy contact, taking cover\n", ent->aiName);
							}
						}
					}
				} else {
					// Can't see enemy but leader can - move to support position
					vec3_t diff;
					VectorSubtract(enemyEnt->r.currentOrigin, ent->r.currentOrigin, diff);
					float dist = VectorLength(diff);
					if (dist < 1000.0f && cs->enemyNum < 0) {
						// Set enemy from leader's intel
						cs->enemyNum = leaderCS->enemyNum;
						cs->lastEnemy = leaderCS->enemyNum;
						
						// Try to find cover position
						vec3_t enemyPos;
						VectorCopy(enemyEnt->r.currentOrigin, enemyPos);
						if (AICast_GetTakeCoverPos(cs, leaderCS->enemyNum, enemyPos, cs->takeCoverPos)) {
							cs->takeCoverTime = level.time + 3000 + rand() % 2000;
							if (cs->aiState < AISTATE_COMBAT) {
								AICast_StateChange(cs, AISTATE_COMBAT);
							}
							if (ai_llm_debug.integer >= 2) {
								G_Printf("^3[SQUAD] %s moving to support leader against enemy (%.0f units)\n", 
								        ent->aiName, dist);
							}
						} else if (cs->aiState < AISTATE_COMBAT) {
							// No cover but still engage
							AICast_StateChange(cs, AISTATE_COMBAT);
						}
					}
				}
			}
		}
		
		// Also check other squad members for enemy contacts
		int j;
		for (j = 0; j < squad->memberCount; j++) {
			int memberNum = squad->members[j];
			if (memberNum == cs->entityNum || memberNum == cs->squadLeaderNum) continue;
			
			cast_state_t *memberCS = AICast_GetCastState(memberNum);
			if (memberCS && memberCS->enemyNum >= 0) {
				gentity_t *memberEnt = &g_entities[memberNum];
				gentity_t *enemyEnt = &g_entities[memberCS->enemyNum];
				if (memberEnt->inuse && enemyEnt->inuse && memberEnt->health > 0) {
					// Squad member has enemy - check if we can see it
					if (AICast_EntityVisible(cs, memberCS->enemyNum, qtrue)) {
						// We can see the enemy - engage
						if (cs->enemyNum != memberCS->enemyNum) {
							cs->enemyNum = memberCS->enemyNum;
							if (cs->aiState < AISTATE_COMBAT) {
								AICast_StateChange(cs, AISTATE_COMBAT);
							}
							// Take cover
							vec3_t enemyPos;
							VectorCopy(enemyEnt->r.currentOrigin, enemyPos);
							if (AICast_GetTakeCoverPos(cs, memberCS->enemyNum, enemyPos, cs->takeCoverPos)) {
								cs->takeCoverTime = level.time + 2000 + rand() % 2000;
							}
						}
						break; // Found enemy from squad member
					}
				}
			}
		}
	}
	
	// Execute current squad order with role-based behavior
	if (squad->currentOrder[0]) {
		// Translate squad orders into actions based on role
		if (strcmp(squad->currentOrder, "assault") == 0) {
			// Role-based assault behavior
			if (cs->squadRole == SQUAD_ROLE_SCOUT) {
				// Scout: Move ahead to spot enemies
				cs->attributes[AGGRESSION] = 0.7f;
				cs->llm_currentAction = LLM_ACTION_INVESTIGATE;
			} else if (cs->squadRole == SQUAD_ROLE_ASSAULT) {
				// Assault: Aggressive engagement
				cs->attributes[AGGRESSION] = 0.9f;
				cs->llm_currentAction = LLM_ACTION_ATTACK;
			} else if (cs->squadRole == SQUAD_ROLE_SUPPORT) {
				// Support: Provide covering fire
				cs->attributes[AGGRESSION] = 0.6f;
				cs->llm_currentAction = LLM_ACTION_COVERING_FIRE;
			}
			cs->llm_actionStartTime = level.time;
		} else if (strcmp(squad->currentOrder, "defensive_position") == 0) {
			// Role-based defensive behavior
			if (cs->squadRole == SQUAD_ROLE_SCOUT) {
				// Scout: Watch flanks
				cs->attributes[AGGRESSION] = 0.3f;
				cs->attributes[TACTICAL] = 0.8f;
				cs->llm_currentAction = LLM_ACTION_OVERWATCH;
			} else if (cs->squadRole == SQUAD_ROLE_ASSAULT) {
				// Assault: Hold position
				cs->attributes[AGGRESSION] = 0.4f;
				cs->attributes[TACTICAL] = 0.9f;
				cs->llm_currentAction = LLM_ACTION_HOLD_POSITION;
			} else if (cs->squadRole == SQUAD_ROLE_SUPPORT) {
				// Support: Defensive fire
				cs->attributes[AGGRESSION] = 0.5f;
				cs->attributes[TACTICAL] = 0.9f;
				cs->llm_currentAction = LLM_ACTION_DEFEND;
			}
			cs->llm_actionStartTime = level.time;
		} else if (strcmp(squad->currentOrder, "fallback") == 0) {
			// Role-based retreat behavior
			if (cs->squadRole == SQUAD_ROLE_SCOUT) {
				// Scout: Cover retreat
				cs->llm_currentAction = LLM_ACTION_COVERING_FIRE;
			} else {
				// Others: Retreat
				cs->llm_currentAction = LLM_ACTION_RETREAT;
			}
			cs->llm_actionStartTime = level.time;
		} else if (strcmp(squad->currentOrder, "support_fire") == 0) {
			// Covering fire order
			cs->llm_currentAction = LLM_ACTION_COVERING_FIRE;
			cs->llm_actionStartTime = level.time;
		}
		
		// Log order execution (periodically)
		static int lastLogTime[MAX_CLIENTS] = {0};
		if (ai_llm_debug.integer && level.time - lastLogTime[cs->entityNum] > 8000) {
			const char *roleStr = "Member";
			switch (cs->squadRole) {
				case SQUAD_ROLE_SCOUT: roleStr = "Scout"; break;
				case SQUAD_ROLE_ASSAULT: roleStr = "Assault"; break;
				case SQUAD_ROLE_SUPPORT: roleStr = "Support"; break;
			}
			G_Printf("^6[SQUAD] %s (%s) executing: %s\n", 
			        ent->aiName, roleStr, squad->currentOrder);
			lastLogTime[cs->entityNum] = level.time;
		}
	}
}

/*
================
AICast_UpdateFormation

Maintain squad formation during movement
================
*/
void AICast_UpdateFormation(int squadId) {
	squad_t *squad;
	cast_state_t *leader, *member;
	gentity_t *leaderEnt;
	vec3_t forward, right;
	int i;
	
	if (squadId < 0 || squadId >= g_squadCount) {
		return;
	}
	
	squad = &g_squads[squadId];
	if (squad->leaderNum < 0) {
		return;
	}
	
	leader = AICast_GetCastState(squad->leaderNum);
	leaderEnt = &g_entities[squad->leaderNum];
	
	if (!leader || !leaderEnt->inuse) {
		return;
	}
	
	// Get leader's facing direction
	AngleVectors(leaderEnt->s.angles, forward, right, NULL);
	
	// Calculate formation offsets for members
	for (i = 0; i < squad->memberCount; i++) {
		int entNum = squad->members[i];
		gentity_t *memberEnt;
		
		if (entNum == squad->leaderNum) {
			continue; // Skip leader
		}
		
		// Safety: validate entity before accessing
		if (entNum < 0 || entNum >= MAX_GENTITIES) {
			continue;
		}
		
		memberEnt = &g_entities[entNum];
		if (!memberEnt->inuse || memberEnt->health <= 0) {
			continue;
		}
		
		member = AICast_GetCastState(entNum);
		if (!member) {
			continue;
		}
		
		// Simple wedge formation
		// Scout ahead, assault to sides, support behind
		vec3_t offset;
		VectorClear(offset);
		
		switch (member->squadRole) {
			case SQUAD_ROLE_SCOUT:
				VectorMA(offset, 128, forward, offset);  // 128 units ahead
				break;
			case SQUAD_ROLE_ASSAULT:
				if (i % 2 == 0) {
					VectorMA(offset, 64, right, offset);   // Right flank
				} else {
					VectorMA(offset, -64, right, offset);  // Left flank
				}
				VectorMA(offset, -32, forward, offset);   // Slightly behind
				break;
			case SQUAD_ROLE_SUPPORT:
				VectorMA(offset, -96, forward, offset);   // Behind leader
				break;
		}
		
		VectorCopy(offset, member->squadFormationOffset);
	}
	
	// Log formation updates (periodically)
	static int lastFormationLog[MAX_TACTICAL_SQUADS] = {0};
	if (ai_llm_debug.integer >= 2 && level.time - lastFormationLog[squadId] > 20000) {
		G_Printf("^6[FORMATION] Squad %d maintaining formation: %d members in wedge\n", 
		        squadId, squad->memberCount);
		lastFormationLog[squadId] = level.time;
	}
}

/*
================
AICast_CoordinatedAttack

Execute coordinated multi-NPC attack
================
*/
void AICast_CoordinatedAttack(int squadId, int targetNum) {
	squad_t *squad;
	
	if (squadId < 0 || squadId >= g_squadCount) {
		return;
	}
	
	squad = &g_squads[squadId];
	
	// Set coordinated attack order
	Com_sprintf(squad->currentOrder, sizeof(squad->currentOrder), 
	            "coordinated_attack_%d", targetNum);
	squad->lastOrderTime = level.time;
	
	// Broadcast to squad
	AICast_SquadBroadcast(squadId, "Execute coordinated assault!");
}

/*
================
AICast_SquadOrder

Issue order to entire squad
================
*/
void AICast_SquadOrder(int squadId, const char *order) {
	squad_t *squad;
	
	if (squadId < 0 || squadId >= g_squadCount) {
		return;
	}
	
	squad = &g_squads[squadId];
	Q_strncpyz(squad->currentOrder, order, sizeof(squad->currentOrder));
	squad->lastOrderTime = level.time;
}

/*
================
AICast_TeamSquadOrder

Issue order to all squads on a team
================
*/
void AICast_TeamSquadOrder(int teamNum, const char *order) {
	int i;
	
	if (teamNum < 0 || teamNum >= MAX_TEAMS) {
		return;
	}
	
	for (i = 0; i < g_squadCount; i++) {
		if (g_squads[i].active && g_squads[i].teamNum == teamNum) {
			AICast_SquadOrder(i, order);
		}
	}
}

/*
================
AICast_SquadBroadcast

Broadcast message to squad members
================
*/
void AICast_SquadBroadcast(int squadId, const char *message) {
	squad_t *squad;
	cast_state_t *cs;
	gentity_t *ent;
	int i;
	
	if (squadId < 0 || squadId >= g_squadCount) {
		return;
	}
	
	squad = &g_squads[squadId];
	
	// Send to all squad members
	for (i = 0; i < squad->memberCount; i++) {
		// Safety: validate entity before accessing
		if (squad->members[i] < 0 || squad->members[i] >= MAX_GENTITIES) {
			continue;
		}
		
		ent = &g_entities[squad->members[i]];
		if (!ent->inuse || ent->health <= 0) {
			continue;
		}
		
		cs = AICast_GetCastState(squad->members[i]);
		if (cs) {
			// This would trigger voice lines/dialogue
			if (ai_llm_debug.integer) {
				G_Printf("Squad broadcast to %d: %s\n", cs->entityNum, message);
			}
		}
	}
}

/*
================
AICast_IsSquadLeader

Check if entity is squad leader
================
*/
qboolean AICast_IsSquadLeader(int entityNum) {
	cast_state_t *cs;
	
	cs = AICast_GetCastState(entityNum);
	if (!cs) {
		return qfalse;
	}
	
	return (cs->squadRole == SQUAD_ROLE_LEADER);
}

/*
================
AICast_GetSquadMembers

Get list of squad member entity numbers
================
*/
int AICast_GetSquadMembers(int squadId, int *outMembers, int maxMembers) {
	squad_t *squad;
	int count, i;
	
	if (squadId < 0 || squadId >= g_squadCount || !outMembers) {
		return 0;
	}
	
	squad = &g_squads[squadId];
	count = (squad->memberCount < maxMembers) ? squad->memberCount : maxMembers;
	
	for (i = 0; i < count; i++) {
		outMembers[i] = squad->members[i];
	}
	
	return count;
}

/*
================
AICast_AddSquadMember

Add member to squad
================
*/
qboolean AICast_AddSquadMember(int squadId, int entityNum) {
	squad_t *squad;
	cast_state_t *cs;
	
	if (squadId < 0 || squadId >= g_squadCount) {
		return qfalse;
	}
	
	squad = &g_squads[squadId];
	
	if (squad->memberCount >= 8) {
		return qfalse; // Squad full
	}
	
	cs = AICast_GetCastState(entityNum);
	if (!cs) {
		return qfalse;
	}
	
	squad->members[squad->memberCount] = entityNum;
	squad->memberCount++;
	cs->squadId = squadId;
	
	// Reassign roles
	AICast_AssignSquadRoles(squadId);
	
	return qtrue;
}

/*
================
AICast_RemoveSquadMember

Remove member from squad
================
*/
void AICast_RemoveSquadMember(int squadId, int entityNum) {
	squad_t *squad;
	int i, j;
	qboolean wasLeader = qfalse;
	
	if (squadId < 0 || squadId >= g_squadCount) {
		return;
	}
	
	squad = &g_squads[squadId];
	
	// Check if this was the leader
	if (squad->leaderNum == entityNum) {
		wasLeader = qtrue;
	}
	
	// Find and remove member
	for (i = 0; i < squad->memberCount; i++) {
		if (squad->members[i] == entityNum) {
			// Shift remaining members down
			for (j = i; j < squad->memberCount - 1; j++) {
				squad->members[j] = squad->members[j + 1];
			}
			squad->memberCount--;
			
			// Clear squad reference from removed entity
			cast_state_t *cs = AICast_GetCastState(entityNum);
			if (cs) {
				cs->squadId = -1;
				cs->squadRole = SQUAD_ROLE_NONE;
				cs->squadLeaderNum = -1;
			}
			
			// Reassign roles if needed (especially if leader died)
			if (squad->memberCount > 0) {
				if (wasLeader) {
					// Leader died - immediately promote new leader
					if (ai_llm_debug.integer) {
						G_Printf("^3[SQUAD %d] Leader died, promoting new leader\n", squadId);
					}
				}
				AICast_AssignSquadRoles(squadId);
			} else {
				// Squad is empty, mark as inactive
				squad->active = qfalse;
				squad->leaderNum = -1;
			}
			break;
		}
	}
}

/*
================
AICast_FindNearestSquadMember

Find nearest squad member
================
*/
int AICast_FindNearestSquadMember(cast_state_t *cs, float maxDist) {
	squad_t *squad;
	gentity_t *ent, *memberEnt;
	int i, nearest = -1;
	float dist, nearestDist = maxDist;
	
	if (!cs || cs->squadId < 0) {
		return -1;
	}
	
	squad = AICast_GetSquad(cs->entityNum);
	if (!squad) {
		return -1;
	}
	
	ent = &g_entities[cs->entityNum];
	
	for (i = 0; i < squad->memberCount; i++) {
		if (squad->members[i] == cs->entityNum) {
			continue; // Skip self
		}
		
		memberEnt = &g_entities[squad->members[i]];
		if (!memberEnt->inuse) {
			continue;
		}
		
		dist = Distance(ent->r.currentOrigin, memberEnt->r.currentOrigin);
		if (dist < nearestDist) {
			nearestDist = dist;
			nearest = squad->members[i];
		}
	}
	
	return nearest;
}

/*
================
AICast_SquadCohesive

Check if squad is cohesive (not too spread out)
================
*/
qboolean AICast_SquadCohesive(int squadId, float maxSpread) {
	squad_t *squad;
	gentity_t *ent1, *ent2;
	int i, j;
	float dist;
	
	if (squadId < 0 || squadId >= g_squadCount) {
		return qfalse;
	}
	
	squad = &g_squads[squadId];
	
	if (squad->memberCount < 2) {
		return qtrue; // Single member is always cohesive
	}
	
	// Check distances between all members
	for (i = 0; i < squad->memberCount; i++) {
		ent1 = &g_entities[squad->members[i]];
		if (!ent1->inuse) continue;
		
		for (j = i + 1; j < squad->memberCount; j++) {
			ent2 = &g_entities[squad->members[j]];
			if (!ent2->inuse) continue;
			
			dist = Distance(ent1->r.currentOrigin, ent2->r.currentOrigin);
			if (dist > maxSpread) {
				return qfalse; // Too spread out
			}
		}
	}
	
	return qtrue;
}

/*
================
AICast_SquadRegroup

Order squad to regroup at rally point
================
*/
void AICast_SquadRegroup(int squadId, vec3_t rallyPoint) {
	squad_t *squad;
	
	if (squadId < 0 || squadId >= g_squadCount) {
		return;
	}
	
	squad = &g_squads[squadId];
	VectorCopy(rallyPoint, squad->rallyPoint);
	
	AICast_SquadOrder(squadId, "regroup");
	AICast_SquadBroadcast(squadId, "Regroup on my position!");
}

