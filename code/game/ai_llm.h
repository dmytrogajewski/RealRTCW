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
// Name:         ai_llm.h
// Function:     LLM Integration for AI Decision Making and Dialogue
// Programmer:   AI-Enhanced
//===========================================================================

#ifndef __AI_LLM_H__
#define __AI_LLM_H__

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations
typedef struct cast_state_s cast_state_t;

// LLM Strategic Action Types
typedef enum {
	LLM_ACTION_NONE = 0,
	LLM_ACTION_ATTACK,           // Engage enemies aggressively
	LLM_ACTION_DEFEND,           // Hold position and suppress
	LLM_ACTION_RETREAT,          // Fall back to cover
	LLM_ACTION_SUPPORT,          // Assist nearby allies
	LLM_ACTION_INVESTIGATE,      // Check suspicious area
	LLM_ACTION_PATROL,           // Continue patrol route
	LLM_ACTION_AMBUSH,           // Set up ambush position
	// New tactical actions
	LLM_ACTION_FLANK_LEFT,       // Flank enemy from left
	LLM_ACTION_FLANK_RIGHT,      // Flank enemy from right
	LLM_ACTION_SUPPRESS_FIRE,    // Pin enemy down with suppression
	LLM_ACTION_ADVANCE_COVER,    // Advance using cover (bounding overwatch)
	LLM_ACTION_HOLD_POSITION,    // Hold current position
	LLM_ACTION_FORM_UP,          // Rally to squad leader
	LLM_ACTION_SPLIT_SQUAD,      // Divide squad for pincer movement
	LLM_ACTION_COVERING_FIRE,    // Provide covering fire for allies
	LLM_ACTION_BREACH,           // Stack up and breach (coordinated entry)
	LLM_ACTION_OVERWATCH,        // Provide overwatch from elevated/covered position
} llm_action_t;

// LLM Strategic Decision Output
typedef struct {
	llm_action_t action;           // Recommended action
	float confidence;              // Confidence level (0.0-1.0)
	int targetEntityNum;           // Target entity (-1 if none)
	vec3_t targetPosition;         // Target position (if applicable)
	char reasoning[256];           // Brief explanation of decision
	qboolean isValid;              // Whether this decision is valid
} llm_decision_t;

// LLM Dialogue Output
typedef struct {
	char text[512];                // Generated dialogue text
	int targetEntityNum;           // Who to speak to (-1 for broadcast)
	qboolean shouldSpeak;          // Whether to actually speak
	int priority;                  // Priority (0-10, higher = more important)
} llm_dialogue_t;

//
// Main LLM Interface Functions
//

// Initialize LLM system
// Returns qtrue on success, qfalse on failure
qboolean LLM_Init(const char *modelPath);

// Shutdown LLM system
void LLM_Shutdown(void);

// Check if LLM is initialized and ready
qboolean LLM_IsReady(void);

//
// Strategic Decision Making
//

// Request strategic decision from LLM (async)
// This queues the request; use LLM_GetStrategicDecision to retrieve result
void LLM_RequestStrategicDecision(cast_state_t *cs);

// Get strategic decision (non-blocking)
// Returns qtrue if decision is ready, qfalse if still processing
qboolean LLM_GetStrategicDecision(int entityNum, llm_decision_t *out);

// Check if a decision is pending for an entity
qboolean LLM_HasPendingDecision(int entityNum);

//
// Dialogue Generation
//

// Request dialogue generation from LLM (async)
// eventType: "enemy_spotted", "need_backup", "flanking", "taking_fire", etc.
void LLM_RequestDialogue(cast_state_t *cs, const char *eventType);

// Get generated dialogue (non-blocking)
// Returns qtrue if dialogue is ready, qfalse if still processing
qboolean LLM_GetDialogue(int entityNum, llm_dialogue_t *out);

// Check if dialogue is pending for an entity
qboolean LLM_HasPendingDialogue(int entityNum);

//
// Update Functions (call from game loop)
//

// Process pending LLM requests
// Call this once per server frame
void LLM_Update(void);

//
// Utility Functions
//

// Get action name as string
const char *LLM_ActionToString(llm_action_t action);

// Clear all pending requests for an entity
void LLM_ClearPendingRequests(int entityNum);

// Pause/resume LLM processing (for game events like player death, map restart)
void LLM_PauseProcessing(qboolean pause);

//
// Configuration (cvars)
//

// These are implemented in the game module
extern vmCvar_t ai_llm_enabled;           // Master enable/disable
extern vmCvar_t ai_llm_strategic_interval; // Seconds between strategic updates
extern vmCvar_t ai_llm_dialogue_interval;  // Seconds between dialogue updates
extern vmCvar_t ai_llm_max_threads;        // Max inference threads
extern vmCvar_t ai_llm_debug;             // Debug output
extern vmCvar_t ai_llm_gpu_layers;        // Number of layers to offload to GPU (0=CPU only)

#ifdef __cplusplus
}
#endif

#endif // __AI_LLM_H__

