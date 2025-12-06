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
// Name:         ai_strategy.h
// Function:     Adaptive Battle Strategy System
// Programmer:   AI-Enhanced
//===========================================================================

#ifndef __AI_STRATEGY_H__
#define __AI_STRATEGY_H__

#include "../qcommon/q_shared.h"

// Strategy types
typedef enum {
	STRATEGY_NONE,
	STRATEGY_AGGRESSIVE,       // All-out assault
	STRATEGY_DEFENSIVE,        // Hold positions, defensive
	STRATEGY_FLANKING,         // Flank and pincer movements
	STRATEGY_HIT_AND_RUN,      // Quick strikes then retreat
	STRATEGY_SUPPRESSION,      // Pin enemy down
	STRATEGY_CAUTIOUS,         // Careful, methodical advance
	STRATEGY_RETREAT           // Organized withdrawal
} battle_strategy_t;

// Battle performance tracking
typedef struct {
	int friendlyCasualties;
	int enemyCasualties;
	int objectivesLost;
	int objectivesCaptured;
	float strategyEffectiveness;  // 0.0-1.0
	char currentStrategy[64];
	battle_strategy_t strategyType;
	int strategyStartTime;
	int strategyChangeTime;
	int lastAdaptationTime;
	int successfulEngagements;
	int failedEngagements;
	float avgTimeToKill;           // Average time to eliminate enemy
	float avgSurvivalTime;         // Average friendly survival time
} battle_performance_t;

//
// Public API
//

// Initialize strategy system
void AICast_StrategyInit(void);

// Shutdown strategy system
void AICast_StrategyShutdown(void);

// Get battle performance for a team
battle_performance_t *AICast_GetPerformance(int teamNum);

// Update performance metrics (call when events occur)
void AICast_RecordEngagement(int teamNum, qboolean success);
void AICast_RecordKill(int teamNum, qboolean friendly, int timeTaken);
void AICast_RecordObjective(int teamNum, qboolean captured);

// Evaluate current strategy effectiveness
float AICast_EvaluateStrategy(int teamNum);

// Suggest strategy adaptation based on performance
battle_strategy_t AICast_SuggestStrategy(int teamNum);

// Apply new strategy to team
void AICast_ApplyStrategy(int teamNum, battle_strategy_t strategy);

// Get strategy name as string
const char *AICast_StrategyName(battle_strategy_t strategy);

// Periodic strategy evaluation (call every 30 seconds or so)
void AICast_AdaptStrategy(int teamNum);

// Reset performance metrics (for new battle/map)
void AICast_ResetPerformance(int teamNum);

#endif // __AI_STRATEGY_H__

