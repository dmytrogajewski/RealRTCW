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
#include "ai_strategy.h"
#include "ai_tactical_memory.h"
#include "ai_squad.h"
#include "ai_llm.h"  // For ai_llm_debug cvar

#define STRATEGY_EVALUATION_INTERVAL 30000  // Evaluate every 30 seconds

// Global performance tracking - one per team
static battle_performance_t g_performance[MAX_TEAMS];
static qboolean g_strategyInitialized = qfalse;

/*
================
AICast_StrategyInit

Initialize strategy system
================
*/
void AICast_StrategyInit(void) {
	int i;
	
	memset(g_performance, 0, sizeof(g_performance));
	
	for (i = 0; i < MAX_TEAMS; i++) {
		g_performance[i].strategyType = STRATEGY_DEFENSIVE;
		g_performance[i].strategyEffectiveness = 0.5f;
		Q_strncpyz(g_performance[i].currentStrategy, "Defensive", 
		          sizeof(g_performance[i].currentStrategy));
	}
	
	g_strategyInitialized = qtrue;
	
	G_Printf("Strategy system initialized\n");
}

/*
================
AICast_StrategyShutdown

Shutdown strategy system
================
*/
void AICast_StrategyShutdown(void) {
	g_strategyInitialized = qfalse;
	G_Printf("Strategy system shutdown\n");
}

/*
================
AICast_GetPerformance

Get battle performance for a team
================
*/
battle_performance_t *AICast_GetPerformance(int teamNum) {
	if (!g_strategyInitialized || teamNum < 0 || teamNum >= MAX_TEAMS) {
		return NULL;
	}
	
	return &g_performance[teamNum];
}

/*
================
AICast_RecordEngagement

Record result of combat engagement
================
*/
void AICast_RecordEngagement(int teamNum, qboolean success) {
	battle_performance_t *perf;
	
	perf = AICast_GetPerformance(teamNum);
	if (!perf) {
		return;
	}
	
	if (success) {
		perf->successfulEngagements++;
	} else {
		perf->failedEngagements++;
	}
}

/*
================
AICast_RecordKill

Record a kill (friendly or enemy)
================
*/
void AICast_RecordKill(int teamNum, qboolean friendly, int timeTaken) {
	battle_performance_t *perf;
	
	perf = AICast_GetPerformance(teamNum);
	if (!perf) {
		return;
	}
	
	if (friendly) {
		perf->friendlyCasualties++;
		// Update average survival time
		if (timeTaken > 0) {
			perf->avgSurvivalTime = (perf->avgSurvivalTime + timeTaken) / 2.0f;
		}
	} else {
		perf->enemyCasualties++;
		// Update average time to kill
		if (timeTaken > 0) {
			perf->avgTimeToKill = (perf->avgTimeToKill + timeTaken) / 2.0f;
		}
	}
}

/*
================
AICast_RecordObjective

Record objective capture/loss
================
*/
void AICast_RecordObjective(int teamNum, qboolean captured) {
	battle_performance_t *perf;
	
	perf = AICast_GetPerformance(teamNum);
	if (!perf) {
		return;
	}
	
	if (captured) {
		perf->objectivesCaptured++;
	} else {
		perf->objectivesLost++;
	}
}

/*
================
AICast_EvaluateStrategy

Evaluate current strategy effectiveness
Returns 0.0-1.0, where 1.0 = highly effective
================
*/
float AICast_EvaluateStrategy(int teamNum) {
	battle_performance_t *perf;
	float effectiveness = 0.5f;  // Start neutral
	float killRatio, engagementRatio, objectiveRatio;
	int totalCasualties, totalEngagements, totalObjectives;
	
	perf = AICast_GetPerformance(teamNum);
	if (!perf) {
		return 0.5f;
	}
	
	// Calculate kill/death ratio contribution
	totalCasualties = perf->friendlyCasualties + perf->enemyCasualties;
	if (totalCasualties > 0) {
		killRatio = (float)perf->enemyCasualties / (float)totalCasualties;
		effectiveness += (killRatio - 0.5f);  // +/- 0.5 based on ratio
	} else if (perf->friendlyCasualties > 0) {
		// Taking casualties with no enemy kills = very bad
		effectiveness -= 0.4f;
	}
	
	// Penalty for high friendly casualties regardless of kill ratio
	if (perf->friendlyCasualties > 5) {
		effectiveness -= 0.1f * ((perf->friendlyCasualties - 5) / 5.0f);
	}
	
	// Calculate engagement success ratio
	totalEngagements = perf->successfulEngagements + perf->failedEngagements;
	if (totalEngagements > 0) {
		engagementRatio = (float)perf->successfulEngagements / (float)totalEngagements;
		effectiveness += (engagementRatio - 0.5f) * 0.5f;  // +/- 0.25
	}
	
	// Calculate objective success ratio
	totalObjectives = perf->objectivesCaptured + perf->objectivesLost;
	if (totalObjectives > 0) {
		objectiveRatio = (float)perf->objectivesCaptured / (float)totalObjectives;
		effectiveness += (objectiveRatio - 0.5f) * 0.5f;  // +/- 0.25
	}
	
	// Clamp to valid range
	if (effectiveness < 0.0f) effectiveness = 0.0f;
	if (effectiveness > 1.0f) effectiveness = 1.0f;
	
	perf->strategyEffectiveness = effectiveness;
	
	return effectiveness;
}

/*
================
AICast_SuggestStrategy

Suggest new strategy based on performance
================
*/
battle_strategy_t AICast_SuggestStrategy(int teamNum) {
	battle_performance_t *perf;
	tactical_memory_t *tm;
	float effectiveness;
	float killRatio;
	int threatCount;
	
	perf = AICast_GetPerformance(teamNum);
	tm = TacticalMemory_GetForTeam(teamNum);
	
	if (!perf || !tm) {
		return STRATEGY_DEFENSIVE;
	}
	
	effectiveness = AICast_EvaluateStrategy(teamNum);
	
	// Calculate kill ratio
	if (perf->friendlyCasualties > 0) {
		killRatio = (float)perf->enemyCasualties / (float)perf->friendlyCasualties;
	} else {
		killRatio = (perf->enemyCasualties > 0) ? 10.0f : 1.0f;
	}
	
	threatCount = TacticalMemory_GetThreatCount(teamNum);
	
	// Decision logic for strategy
	
	// If current strategy is very effective, keep it
	if (effectiveness > 0.75f) {
		return perf->strategyType;
	}
	
	// If taking heavy casualties, go defensive or retreat
	if (killRatio < 0.5f) {
		if (perf->friendlyCasualties > 5) {
			return STRATEGY_RETREAT;
		}
		return STRATEGY_DEFENSIVE;
	}
	
	// If dominating, be aggressive
	if (killRatio > 2.0f && effectiveness > 0.6f) {
		return STRATEGY_AGGRESSIVE;
	}
	
	// If outnumbered significantly, use hit-and-run or flanking
	if (threatCount > tm->friendlyCount * 1.5f) {
		return STRATEGY_HIT_AND_RUN;
	}
	
	// If even match, use flanking
	if (threatCount > 0 && threatCount <= tm->friendlyCount * 1.2f) {
		return STRATEGY_FLANKING;
	}
	
	// If few enemies, be cautiously aggressive
	if (threatCount > 0 && threatCount < tm->friendlyCount) {
		return STRATEGY_CAUTIOUS;
	}
	
	// Default to defensive
	return STRATEGY_DEFENSIVE;
}

/*
================
AICast_ApplyStrategy

Apply new strategy to team
================
*/
void AICast_ApplyStrategy(int teamNum, battle_strategy_t strategy) {
	battle_performance_t *perf;
	const char *stratName;
	const char *squadOrder = NULL;
	
	perf = AICast_GetPerformance(teamNum);
	if (!perf) {
		return;
	}
	
	if (perf->strategyType == strategy) {
		return; // Already using this strategy
	}
	
	perf->strategyType = strategy;
	perf->strategyChangeTime = level.time;
	stratName = AICast_StrategyName(strategy);
	Q_strncpyz(perf->currentStrategy, stratName, sizeof(perf->currentStrategy));
	
	// Translate strategy to squad orders
	switch (strategy) {
		case STRATEGY_RETREAT:
			squadOrder = "fallback";
			break;
		case STRATEGY_AGGRESSIVE:
			squadOrder = "assault";
			break;
		case STRATEGY_DEFENSIVE:
		case STRATEGY_CAUTIOUS:
			squadOrder = "defensive_position";
			break;
		case STRATEGY_FLANKING:
			squadOrder = "flank_enemy";
			break;
		case STRATEGY_HIT_AND_RUN:
		case STRATEGY_SUPPRESSION:
			squadOrder = "support_fire";
			break;
		default:
			squadOrder = "hold_position";
			break;
	}
	
	// Propagate strategy to all squads on this team
	if (squadOrder && ai_squad_coordination.integer) {
		AICast_TeamSquadOrder(teamNum, squadOrder);
	}
	
	if (ai_llm_debug.integer) {
		G_Printf("^4[STRATEGY] Team %d adapted strategy to: %s (issued '%s' to all squads)\n", 
		        teamNum, stratName, squadOrder ? squadOrder : "none");
	}
}

/*
================
AICast_StrategyName

Get strategy name as string
================
*/
const char *AICast_StrategyName(battle_strategy_t strategy) {
	switch (strategy) {
		case STRATEGY_AGGRESSIVE:   return "Aggressive";
		case STRATEGY_DEFENSIVE:    return "Defensive";
		case STRATEGY_FLANKING:     return "Flanking";
		case STRATEGY_HIT_AND_RUN:  return "Hit and Run";
		case STRATEGY_SUPPRESSION:  return "Suppression";
		case STRATEGY_CAUTIOUS:     return "Cautious";
		case STRATEGY_RETREAT:      return "Retreat";
		default:                    return "None";
	}
}

/*
================
AICast_AdaptStrategy

Periodic strategy evaluation and adaptation
Call this every 30 seconds or so
================
*/
void AICast_AdaptStrategy(int teamNum) {
	battle_performance_t *perf;
	battle_strategy_t suggested;
	float effectiveness;
	
	perf = AICast_GetPerformance(teamNum);
	if (!perf) {
		return;
	}
	
	// Don't adapt too frequently
	if (level.time - perf->lastAdaptationTime < STRATEGY_EVALUATION_INTERVAL) {
		return;
	}
	
	perf->lastAdaptationTime = level.time;
	
	// Evaluate current strategy
	effectiveness = AICast_EvaluateStrategy(teamNum);
	
	// Log periodic strategy evaluation
	G_Printf("^4[STRATEGY] Team %d evaluation: Effectiveness %.2f, Strategy: %s, K/D: %d/%d\n",
	        teamNum, effectiveness, AICast_StrategyName(perf->strategyType),
	        perf->enemyCasualties, perf->friendlyCasualties);
	
	// Emergency adaptation for catastrophic performance
	qboolean emergency = qfalse;
	if (effectiveness < 0.2f || (perf->friendlyCasualties > 0 && perf->enemyCasualties == 0 && perf->friendlyCasualties >= 3)) {
		emergency = qtrue;
		G_Printf("^1[STRATEGY EMERGENCY] Team %d: Catastrophic performance detected!\n", teamNum);
	}
	
	// If strategy is failing or neutral, consider adaptation (raised from 0.4 to 0.5)
	if (effectiveness < 0.5f || emergency) {
		suggested = AICast_SuggestStrategy(teamNum);
		
		if (suggested != perf->strategyType || emergency) {
			if (emergency) {
				G_Printf("^1[STRATEGY ADAPT] Team %d: EMERGENCY! Changing from %s to %s (%.2f effectiveness)\n",
				        teamNum,
				        AICast_StrategyName(perf->strategyType),
				        AICast_StrategyName(suggested),
				        effectiveness);
			} else {
				G_Printf("^1[STRATEGY ADAPT] Team %d: Strategy ineffective (%.2f)! Changing from %s to %s\n",
				        teamNum, effectiveness,
				        AICast_StrategyName(perf->strategyType),
				        AICast_StrategyName(suggested));
			}
			
			AICast_ApplyStrategy(teamNum, suggested);
			
			// Reset some metrics for fresh evaluation of new strategy
			perf->successfulEngagements = 0;
			perf->failedEngagements = 0;
		}
	} else if (effectiveness > 0.7f) {
		G_Printf("^2[STRATEGY] Team %d: Current strategy %s working well (%.2f effectiveness)\n",
		        teamNum, AICast_StrategyName(perf->strategyType), effectiveness);
	}
}

/*
================
AICast_ResetPerformance

Reset performance metrics for new battle
================
*/
void AICast_ResetPerformance(int teamNum) {
	battle_performance_t *perf;
	
	perf = AICast_GetPerformance(teamNum);
	if (!perf) {
		return;
	}
	
	perf->friendlyCasualties = 0;
	perf->enemyCasualties = 0;
	perf->objectivesLost = 0;
	perf->objectivesCaptured = 0;
	perf->successfulEngagements = 0;
	perf->failedEngagements = 0;
	perf->avgTimeToKill = 0.0f;
	perf->avgSurvivalTime = 0.0f;
	perf->strategyEffectiveness = 0.5f;
	perf->strategyStartTime = level.time;
}

