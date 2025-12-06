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
// Name:         ai_llm.cpp
// Function:     LLM Integration Implementation
// Programmer:   AI-Enhanced
//===========================================================================

// Declare C linkage for game engine functions
extern "C" {
#include "g_local.h"

// Include bot headers explicitly to ensure types are defined
#include "../botlib/botlib.h"
#include "../botlib/be_aas.h"
#include "../botlib/be_ai_goal.h"
#include "../botlib/be_ai_move.h"

#include "ai_cast.h"
#include "ai_llm.h"
#include "ai_tactical_memory.h"
#include "ai_squad.h"
}  // extern "C"

// Undefine conflicting macros before including C++ headers and llama.h
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

// Include C++ headers and llama.h
#include <string>
#include <vector>
#include <queue>

// Use nlohmann/json for proper JSON parsing (header-only library)
#include "../llama.cpp/vendor/nlohmann/json.hpp"
using json = nlohmann::ordered_json;
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <algorithm>
#include <cctype>

#include <llama.h>

// LLM Context
struct llm_context_t {
	llama_model *model;
	llama_context *ctx;
	llama_sampler *sampler;
	bool initialized;
	std::mutex mutex;
	int generation_count;      // Track number of generations
	int max_generations;       // Recreate context after this many generations
	
	llm_context_t() : model(nullptr), ctx(nullptr), sampler(nullptr), 
	                  initialized(false), generation_count(0), max_generations(300) {}  // Recreate less frequently (was 30)
};

// Request types
enum request_type_t {
	REQUEST_STRATEGIC,
	REQUEST_DIALOGUE
};

// LLM Request
struct llm_request_t {
	request_type_t type;
	int entityNum;
	std::string prompt;
	std::string eventType; // For dialogue requests
	int timestamp;
};

// LLM Response
struct llm_response_t {
	request_type_t type;
	int entityNum;
	std::string output;
	int timestamp;
	bool ready;
};

// Global LLM state
static llm_context_t g_llm;
static std::queue<llm_request_t> g_requestQueue;
static std::vector<llm_response_t> g_responses;
static std::mutex g_queueMutex;
static std::mutex g_responseMutex;
static std::thread g_workerThread;
static std::atomic<bool> g_shutdownRequested(false);
static std::condition_variable g_queueCondition;
static std::atomic<bool> g_processingRequest(false);
static std::atomic<bool> g_gamePaused(false);  // Set when game is pausing/restarting

// Configuration
#define MAX_QUEUE_SIZE 16  // Allow more in-flight requests; still bounded

// Forward declarations
static void LLM_WorkerThread();
static std::string LLM_BuildStrategicPrompt(cast_state_t *cs);
static std::string LLM_BuildDialoguePrompt(cast_state_t *cs, const char *eventType);
static void LLM_ParseStrategicResponse(const std::string &response, llm_decision_t *out);
static void LLM_ParseDialogueResponse(const std::string &response, llm_dialogue_t *out);
static std::string LLM_Generate(const std::string &prompt, int maxTokens, qboolean requireCompleteJSON = qfalse);
static void LLM_RecreateContext();

//
// Utility Functions
//

const char *LLM_ActionToString(llm_action_t action) {
	switch (action) {
		case LLM_ACTION_ATTACK:         return "ATTACK";
		case LLM_ACTION_DEFEND:         return "DEFEND";
		case LLM_ACTION_RETREAT:        return "RETREAT";
		case LLM_ACTION_SUPPORT:        return "SUPPORT";
		case LLM_ACTION_INVESTIGATE:    return "INVESTIGATE";
		case LLM_ACTION_PATROL:         return "PATROL";
		case LLM_ACTION_AMBUSH:         return "AMBUSH";
		case LLM_ACTION_FLANK_LEFT:     return "FLANK_LEFT";
		case LLM_ACTION_FLANK_RIGHT:    return "FLANK_RIGHT";
		case LLM_ACTION_SUPPRESS_FIRE:  return "SUPPRESS_FIRE";
		case LLM_ACTION_ADVANCE_COVER:  return "ADVANCE_COVER";
		case LLM_ACTION_HOLD_POSITION:  return "HOLD_POSITION";
		case LLM_ACTION_FORM_UP:        return "FORM_UP";
		case LLM_ACTION_SPLIT_SQUAD:    return "SPLIT_SQUAD";
		case LLM_ACTION_COVERING_FIRE:  return "COVERING_FIRE";
		case LLM_ACTION_BREACH:         return "BREACH";
		case LLM_ACTION_OVERWATCH:      return "OVERWATCH";
		default:                        return "NONE";
	}
}

static llm_action_t LLM_StringToAction(const std::string &str) {
	// Check specific tactical actions first (order matters - check more specific before generic)
	if (str.find("FLANK_LEFT") != std::string::npos || str.find("FLANK LEFT") != std::string::npos) 
		return LLM_ACTION_FLANK_LEFT;
	if (str.find("FLANK_RIGHT") != std::string::npos || str.find("FLANK RIGHT") != std::string::npos) 
		return LLM_ACTION_FLANK_RIGHT;
	if (str.find("SUPPRESS") != std::string::npos) return LLM_ACTION_SUPPRESS_FIRE;
	if (str.find("ADVANCE") != std::string::npos) return LLM_ACTION_ADVANCE_COVER;
	if (str.find("HOLD") != std::string::npos) return LLM_ACTION_HOLD_POSITION;
	if (str.find("FORM_UP") != std::string::npos || str.find("FORM UP") != std::string::npos) 
		return LLM_ACTION_FORM_UP;
	if (str.find("SPLIT") != std::string::npos) return LLM_ACTION_SPLIT_SQUAD;
	if (str.find("COVERING") != std::string::npos) return LLM_ACTION_COVERING_FIRE;
	if (str.find("BREACH") != std::string::npos) return LLM_ACTION_BREACH;
	if (str.find("OVERWATCH") != std::string::npos) return LLM_ACTION_OVERWATCH;
	// Base actions
	if (str.find("ATTACK") != std::string::npos) return LLM_ACTION_ATTACK;
	if (str.find("DEFEND") != std::string::npos) return LLM_ACTION_DEFEND;
	if (str.find("RETREAT") != std::string::npos) return LLM_ACTION_RETREAT;
	if (str.find("SUPPORT") != std::string::npos) return LLM_ACTION_SUPPORT;
	if (str.find("INVESTIGATE") != std::string::npos) return LLM_ACTION_INVESTIGATE;
	if (str.find("PATROL") != std::string::npos) return LLM_ACTION_PATROL;
	if (str.find("AMBUSH") != std::string::npos) return LLM_ACTION_AMBUSH;
	return LLM_ACTION_NONE;
}

//
// Main API Implementation
//

qboolean LLM_Init(const char *modelPath) {
	G_Printf("LLM_Init: Initializing llama.cpp...\n");
	
	std::lock_guard<std::mutex> lock(g_llm.mutex);
	
	if (g_llm.initialized) {
		G_Printf("LLM_Init: Already initialized\n");
		return qtrue;
	}
	
	// Initialize llama.cpp backend
	llama_backend_init();
	
	// Load model
	llama_model_params model_params = llama_model_default_params();
	model_params.n_gpu_layers = ai_llm_gpu_layers.integer; // Use cvar to control GPU offload
	model_params.main_gpu = 0;  // Use first GPU
	model_params.split_mode = LLAMA_SPLIT_MODE_NONE; // Don't split across GPUs
	
	g_llm.model = llama_model_load_from_file(modelPath, model_params);
	if (!g_llm.model) {
		G_Printf("LLM_Init: Failed to load model from %s\n", modelPath);
		return qfalse;
	}
	
	// Create context
	llama_context_params ctx_params = llama_context_default_params();
	ctx_params.n_ctx = 8192;  
	ctx_params.n_threads = ai_llm_max_threads.integer;
	ctx_params.n_batch = 1024;
	ctx_params.n_ubatch = 1024;
	
	g_llm.ctx = llama_init_from_model(g_llm.model, ctx_params);
	if (!g_llm.ctx) {
		G_Printf("LLM_Init: Failed to create context\n");
		llama_model_free(g_llm.model);
		g_llm.model = nullptr;
		return qfalse;
	}
	
	// Create sampler
	auto sparams = llama_sampler_chain_default_params();
	g_llm.sampler = llama_sampler_chain_init(sparams);
	
	llama_sampler_chain_add(g_llm.sampler, llama_sampler_init_temp(0.7f));
	llama_sampler_chain_add(g_llm.sampler, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));
	
	g_llm.initialized = true;
	
	// Start worker thread
	g_shutdownRequested = false;
	g_gamePaused = false;  // Allow processing
	g_workerThread = std::thread(LLM_WorkerThread);
	
	G_Printf("LLM_Init: Successfully initialized\n");
	return qtrue;
}

void LLM_PauseProcessing(qboolean pause) {
	g_gamePaused = pause ? true : false;
	
	if (pause) {
		// Clear all pending requests when pausing
		{
			std::lock_guard<std::mutex> lock(g_queueMutex);
			while (!g_requestQueue.empty()) {
				g_requestQueue.pop();
			}
		}
		
		{
			std::lock_guard<std::mutex> lock(g_responseMutex);
			g_responses.clear();
		}
	}
}

void LLM_Shutdown(void) {
	if (!g_llm.initialized) {
		return;
	}
	
	G_Printf("LLM_Shutdown: Shutting down llama.cpp...\n");
	
	// Pause processing first
	g_gamePaused = true;
	
	// Signal worker thread to stop
	g_shutdownRequested = true;
	g_queueCondition.notify_all();
	
	// Wait for any in-progress request to finish (with timeout)
	int timeout = 0;
	while (g_processingRequest && timeout < 100) {
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
		timeout++;
	}
	
	// Wait for worker thread
	if (g_workerThread.joinable()) {
		g_workerThread.join();
	}
	
	// Clear queues
	{
		std::lock_guard<std::mutex> lock(g_queueMutex);
		while (!g_requestQueue.empty()) {
			g_requestQueue.pop();
		}
	}
	
	{
		std::lock_guard<std::mutex> lock(g_responseMutex);
		g_responses.clear();
	}
	
	{
		std::lock_guard<std::mutex> lock(g_llm.mutex);
		
		if (g_llm.sampler) {
			try {
				llama_sampler_free(g_llm.sampler);
			} catch(...) {}
			g_llm.sampler = nullptr;
		}
		
		if (g_llm.ctx) {
			try {
				llama_free(g_llm.ctx);
			} catch(...) {}
			g_llm.ctx = nullptr;
		}
		
		if (g_llm.model) {
			try {
				llama_model_free(g_llm.model);
			} catch(...) {}
			g_llm.model = nullptr;
		}
		
		g_llm.initialized = false;
	}
	
	try {
		llama_backend_free();
	} catch(...) {}
	
	G_Printf("LLM_Shutdown: Shutdown complete\n");
}

qboolean LLM_IsReady(void) {
	return g_llm.initialized ? qtrue : qfalse;
}

void LLM_RequestStrategicDecision(cast_state_t *cs) {
	if (!g_llm.initialized || !cs || g_shutdownRequested || g_gamePaused) {
		return;
	}
	
	// Validate entity is still valid
	if (cs->entityNum < 0 || cs->entityNum >= MAX_GENTITIES) {
		return;
	}
	gentity_t *ent = &g_entities[cs->entityNum];
	if (!ent->inuse) {
		return;
	}
	
	llm_request_t request;
	request.type = REQUEST_STRATEGIC;
	request.entityNum = cs->entityNum;
	request.prompt = LLM_BuildStrategicPrompt(cs);
	request.timestamp = level.time;
	
	std::lock_guard<std::mutex> lock(g_queueMutex);
	
	// Limit queue size to prevent overwhelming the system.
	// If full, drop the oldest to keep new, freshest requests.
	if (g_requestQueue.size() >= MAX_QUEUE_SIZE) {
		g_requestQueue.pop();
		G_Printf("^3[LLM] Request queue full (%d). Dropping oldest.\n", MAX_QUEUE_SIZE);
	}
	
	g_requestQueue.push(request);
	g_queueCondition.notify_one();
}

void LLM_RequestDialogue(cast_state_t *cs, const char *eventType) {
	if (!g_llm.initialized || !cs || !eventType || g_shutdownRequested || g_gamePaused) {
		return;
	}
	
	// Validate entity is still valid
	if (cs->entityNum < 0 || cs->entityNum >= MAX_GENTITIES) {
		return;
	}
	gentity_t *ent = &g_entities[cs->entityNum];
	if (!ent->inuse) {
		return;
	}
	
	llm_request_t request;
	request.type = REQUEST_DIALOGUE;
	request.entityNum = cs->entityNum;
	request.eventType = eventType;
	request.prompt = LLM_BuildDialoguePrompt(cs, eventType);
	request.timestamp = level.time;
	
	std::lock_guard<std::mutex> lock(g_queueMutex);
	
	// Limit queue size to prevent overwhelming the system.
	// If full, drop the oldest to keep new, freshest requests.
	if (g_requestQueue.size() >= MAX_QUEUE_SIZE) {
		g_requestQueue.pop();
		G_Printf("^3[LLM] Request queue full (%d). Dropping oldest.\n", MAX_QUEUE_SIZE);
	}
	
	g_requestQueue.push(request);
	g_queueCondition.notify_one();
}

qboolean LLM_GetStrategicDecision(int entityNum, llm_decision_t *out) {
	if (!out) {
		return qfalse;
	}
	
	std::lock_guard<std::mutex> lock(g_responseMutex);
	
	for (auto it = g_responses.begin(); it != g_responses.end(); ++it) {
		if (it->type == REQUEST_STRATEGIC && it->entityNum == entityNum && it->ready) {
			LLM_ParseStrategicResponse(it->output, out);
			g_responses.erase(it);
			return qtrue;
		}
	}
	
	return qfalse;
}

qboolean LLM_GetDialogue(int entityNum, llm_dialogue_t *out) {
	if (!out) {
		return qfalse;
	}
	
	std::lock_guard<std::mutex> lock(g_responseMutex);
	
	for (auto it = g_responses.begin(); it != g_responses.end(); ++it) {
		if (it->type == REQUEST_DIALOGUE && it->entityNum == entityNum && it->ready) {
			LLM_ParseDialogueResponse(it->output, out);
			g_responses.erase(it);
			return qtrue;
		}
	}
	
	return qfalse;
}

qboolean LLM_HasPendingDecision(int entityNum) {
	std::lock_guard<std::mutex> lock(g_responseMutex);
	
	for (const auto &response : g_responses) {
		if (response.type == REQUEST_STRATEGIC && response.entityNum == entityNum) {
			return qtrue;
		}
	}
	
	return qfalse;
}

qboolean LLM_HasPendingDialogue(int entityNum) {
	std::lock_guard<std::mutex> lock(g_responseMutex);
	
	for (const auto &response : g_responses) {
		if (response.type == REQUEST_DIALOGUE && response.entityNum == entityNum) {
			return qtrue;
		}
	}
	
	return qfalse;
}

void LLM_Update(void) {
	// This is called from the game loop
	// The actual processing happens in the worker thread
	// No action needed here currently
}

void LLM_ClearPendingRequests(int entityNum) {
	{
		std::lock_guard<std::mutex> lock(g_responseMutex);
		g_responses.erase(
			std::remove_if(g_responses.begin(), g_responses.end(),
				[entityNum](const llm_response_t &r) { return r.entityNum == entityNum; }),
			g_responses.end()
		);
	}
}

// Cleanup old responses (called periodically)
void LLM_CleanupOldResponses(int currentTime) {
	std::lock_guard<std::mutex> lock(g_responseMutex);
	
	// Remove responses older than 10 seconds
	g_responses.erase(
		std::remove_if(g_responses.begin(), g_responses.end(),
			[currentTime](const llm_response_t &r) { return r.timestamp + 10000 < currentTime; }),
		g_responses.end()
	);
}

//
// Prompt Building
//

static std::string LLM_BuildStrategicPrompt(cast_state_t *cs) {
	// Validate entity is still valid before accessing
	if (cs->entityNum < 0 || cs->entityNum >= MAX_GENTITIES) {
		return "Invalid entity";
	}
	
	gentity_t *ent = &g_entities[cs->entityNum];
	if (!ent->inuse) {
		return "Entity destroyed";
	}
	
	std::string prompt;
	
	// Get character type
	const char *charType = "soldier";
	if (cs->aiCharacter < NUM_CHARACTERS && cs->aiCharacter >= 0) {
		charType = aiDefaults[cs->aiCharacter].name;
	}
	
	// === TACTICAL SITUATION REPORT ===
	prompt = "=== TACTICAL SITUATION REPORT ===\n\n";
	
	// Your Role
	const char *roleName = "None";
	switch (cs->squadRole) {
		case SQUAD_ROLE_LEADER: roleName = "LEADER"; break;
		case SQUAD_ROLE_SCOUT: roleName = "SCOUT"; break;
		case SQUAD_ROLE_ASSAULT: roleName = "ASSAULT"; break;
		case SQUAD_ROLE_SUPPORT: roleName = "SUPPORT"; break;
	}
	prompt += "Your Role: " + std::string(roleName) + " (" + std::string(charType) + ")\n";
	prompt += "Your Health: " + std::to_string(ent->health) + "%\n\n";
	
	// Squad Status
	squad_t *squad = AICast_GetSquad(cs->entityNum);
	if (squad && squad->active) {
		prompt += "Squad Status:\n";
		prompt += "  Size: " + std::to_string(squad->memberCount) + " members\n";
		if (squad->currentOrder[0]) {
			prompt += "  Current Order: " + std::string(squad->currentOrder) + "\n";
		}
		prompt += "\n";
	}
	
	// Enemy Intel from Tactical Memory
	tactical_memory_t *tm = TacticalMemory_GetForTeam(ent->aiTeam);
	if (tm) {
		int threatCount = TacticalMemory_GetThreatCount(ent->aiTeam);
		prompt += "Enemy Intel:\n";
		prompt += "  Active Threats: " + std::to_string(threatCount) + "\n";
		prompt += "  Casualties: Enemy " + std::to_string(tm->enemyCasualties) + 
		          ", Friendly " + std::to_string(tm->friendlyCasualties) + "\n";
		
		// Team Morale
		const char *moraleStr = "Unknown";
		if (tm->teamMorale > 0.7f) moraleStr = "High";
		else if (tm->teamMorale > 0.4f) moraleStr = "Medium";
		else moraleStr = "Low";
		prompt += "  Team Morale: " + std::string(moraleStr) + "\n\n";
	}
	
	// Current State
	const char *stateName = "Unknown";
	if (cs->aiState == AISTATE_RELAXED) stateName = "Patrol";
	else if (cs->aiState == AISTATE_QUERY) stateName = "Investigating";
	else if (cs->aiState == AISTATE_ALERT) stateName = "Alert";
	else if (cs->aiState == AISTATE_COMBAT) stateName = "Combat";
	prompt += "Current State: " + std::string(stateName) + "\n\n";
	
	// Tactical Options with context
	prompt += "TACTICAL OPTIONS:\n";
	prompt += "ATTACK - Engage enemy aggressively\n";
	prompt += "DEFEND - Hold position and suppress\n";
	prompt += "RETREAT - Fall back to safer position\n";
	prompt += "SUPPORT - Assist nearby allies\n";
	prompt += "INVESTIGATE - Check suspicious area\n";
	prompt += "PATROL - Continue patrol route\n";
	prompt += "HOLD_POSITION - Maintain current position\n";
	prompt += "COVERING_FIRE - Provide suppressive fire\n\n";
	
	// Add enemy context if available
	if (cs->enemyNum >= 0 && cs->enemyNum < MAX_GENTITIES) {
		gentity_t *enemy = &g_entities[cs->enemyNum];
		if (enemy->inuse) {
			vec3_t diff;
			VectorSubtract(enemy->r.currentOrigin, ent->r.currentOrigin, diff);
			float dist = VectorLength(diff);
			prompt += "Enemy Distance: " + std::to_string((int)dist) + " units\n";
			if (cs->vislist[cs->enemyNum].visible_timestamp > level.time - 1000) {
				prompt += "Enemy Status: VISIBLE\n";
			} else {
				prompt += "Enemy Status: Last seen " + std::to_string((level.time - cs->vislist[cs->enemyNum].visible_timestamp) / 1000) + " seconds ago\n";
			}
			prompt += "\n";
		}
	}
	
	prompt += "Respond with JSON format:\n";
	prompt += "{\n";
	prompt += "  \"action\": \"ATTACK\" | \"DEFEND\" | \"RETREAT\" | \"SUPPORT\" | \"INVESTIGATE\" | \"PATROL\",\n";
	prompt += "  \"confidence\": 0.0-1.0,\n";
	prompt += "  \"reasoning\": \"brief explanation\"\n";
	prompt += "}\n";
	prompt += "Example: {\"action\": \"ATTACK\", \"confidence\": 0.85, \"reasoning\": \"Enemy visible and vulnerable\"}\n";
	prompt += "Response:";
	
	return prompt;
}

static std::string LLM_BuildDialoguePrompt(cast_state_t *cs, const char *eventType) {
	// Validate entity is still valid before accessing
	if (cs->entityNum < 0 || cs->entityNum >= MAX_GENTITIES) {
		return "Invalid entity";
	}
	
	gentity_t *ent = &g_entities[cs->entityNum];
	if (!ent->inuse) {
		return "Entity destroyed";
	}
	
	std::string prompt;
	
	const char *charType = "soldier";
	if (cs->aiCharacter < NUM_CHARACTERS && cs->aiCharacter >= 0) {
		charType = aiDefaults[cs->aiCharacter].name;
	}
	
	// Get squad role for context
	const char *roleName = "soldier";
	switch (cs->squadRole) {
		case SQUAD_ROLE_LEADER: roleName = "squad leader"; break;
		case SQUAD_ROLE_SCOUT: roleName = "scout"; break;
		case SQUAD_ROLE_ASSAULT: roleName = "assault"; break;
		case SQUAD_ROLE_SUPPORT: roleName = "support"; break;
	}
	
	prompt = "You are a " + std::string(charType) + " " + std::string(roleName) + " in WW2 combat.\n";
	prompt += "Event: " + std::string(eventType) + "\n";
	
	// Add tactical context based on event
	if (strcmp(eventType, "enemy_spotted") == 0) {
		prompt += "You spotted an enemy. Alert your squad with military precision.\n";
	} else if (strcmp(eventType, "need_backup") == 0) {
		prompt += "You're under heavy fire. Request immediate support.\n";
	} else if (strcmp(eventType, "flanking") == 0) {
		prompt += "You're executing a flanking maneuver. Announce your movement.\n";
	} else if (strcmp(eventType, "taking_fire") == 0) {
		prompt += "You're taking enemy fire. Alert your team.\n";
	} else if (strcmp(eventType, "grenade") == 0) {
		prompt += "Enemy grenade nearby! Warn your squad immediately.\n";
	} else if (strcmp(eventType, "victory") == 0) {
		prompt += "Enemies down. Report all clear.\n";
	} else if (strcmp(eventType, "retreat_ordered") == 0) {
		prompt += "Squad leader ordered retreat. Acknowledge.\n";
	} else if (strcmp(eventType, "assault_ordered") == 0) {
		prompt += "Assault order received. Confirm readiness.\n";
	}
	
	prompt += "\nRespond ONLY with valid JSON format (no other text):\n";
	prompt += "{\n";
	prompt += "  \"dialogue\": \"short tactical callout in GERMAN (3-8 words)\",\n";
	prompt += "  \"priority\": 1-10 (optional, default 5),\n";
	prompt += "  \"target\": \"squad\" | \"team\" | \"all\" (optional, default \"squad\")\n";
	prompt += "}\n";
	prompt += "Example: {\"dialogue\": \"Feind gesichtet, Position 3-5\", \"priority\": 7, \"target\": \"squad\"}\n";
	prompt += "IMPORTANT: Respond ONLY with the JSON object, nothing else. No explanations, no markdown, just the JSON.\n";
	prompt += "Response:";
	
	return prompt;
}

//
// Response Parsing
//

// Extract and parse JSON from response using nlohmann/json
static json LLM_ParseJSONFromResponse(const std::string &response) {
	// Find the first '{'
	size_t jsonStart = response.find('{');
	if (jsonStart == std::string::npos) {
		return json(); // Return null JSON
	}
	
	// Find the last '}'
	size_t jsonEnd = response.rfind('}');
	if (jsonEnd == std::string::npos || jsonEnd < jsonStart) {
		// Fallback: recover dialogue field manually if possible
		size_t dialogueStart = response.find("\"dialogue\"", jsonStart);
		if (dialogueStart != std::string::npos) {
			size_t colonPos = response.find(':', dialogueStart);
			if (colonPos != std::string::npos) {
				size_t valueStart = colonPos + 1;
				while (valueStart < response.length() && (response[valueStart] == ' ' || response[valueStart] == '\t')) {
					valueStart++;
				}
				if (valueStart < response.length() && response[valueStart] == '"') {
					valueStart++; // Skip opening quote
					size_t valueEnd = response.find('"', valueStart);
					if (valueEnd != std::string::npos) {
						std::string dialogueValue = response.substr(valueStart, valueEnd - valueStart);
						json minimal;
						minimal["dialogue"] = dialogueValue;
						minimal["priority"] = 5; // Default priority
						return minimal;
					}
				}
			}
		}
		return json(); // Return null JSON if we can't recover
	}
	
	// Extract and parse
	std::string jsonStr = response.substr(jsonStart, jsonEnd - jsonStart + 1);
	try {
		return json::parse(jsonStr);
	} catch (...) {
		return json(); // Return null JSON on parse error
	}
}

static void LLM_ParseStrategicResponse(const std::string &response, llm_decision_t *out) {
	memset(out, 0, sizeof(llm_decision_t));
	
	out->confidence = 0.8f; // Default confidence
	out->targetEntityNum = -1;
	
	// Try to parse JSON first
	json j = LLM_ParseJSONFromResponse(response);
	if (!j.is_null() && j.is_object()) {
		// Parse action
		if (j.contains("action") && j["action"].is_string()) {
			std::string actionStr = j["action"].get<std::string>();
			out->action = LLM_StringToAction(actionStr);
		} else {
			out->action = LLM_ACTION_NONE;
		}
		
		// Parse confidence
		if (j.contains("confidence")) {
			if (j["confidence"].is_number()) {
				float conf = j["confidence"].get<float>();
				if (conf >= 0.0f && conf <= 1.0f) {
					out->confidence = conf;
				} else if (conf > 1.0f && conf <= 100.0f) {
					// Handle percentage format
					out->confidence = conf / 100.0f;
				}
			}
		}
		
		// Parse reasoning
		if (j.contains("reasoning") && j["reasoning"].is_string()) {
			std::string reasoningStr = j["reasoning"].get<std::string>();
			size_t maxLen = sizeof(out->reasoning) - 1;
			if (reasoningStr.length() < maxLen) {
				Q_strncpyz(out->reasoning, reasoningStr.c_str(), sizeof(out->reasoning));
			} else {
				std::string truncated = reasoningStr.substr(0, maxLen - 4);
				truncated += "...";
				Q_strncpyz(out->reasoning, truncated.c_str(), sizeof(out->reasoning));
			}
		} else {
			Q_strncpyz(out->reasoning, "LLM decision", sizeof(out->reasoning));
		}
		
		out->isValid = (out->action != LLM_ACTION_NONE) ? qtrue : qfalse;
		return; // Successfully parsed JSON, done
	}
	
	// Fallback to old parsing method if JSON not found
	out->action = LLM_StringToAction(response);
	
	// Try to parse confidence from response
	// Look for patterns like "confidence: 0.85" or "(confidence: 0.85)" or "0.85" after confidence
	std::string lowerResponse = response;
	std::transform(lowerResponse.begin(), lowerResponse.end(), lowerResponse.begin(), ::tolower);
	
	// First, try to find explicit "confidence" keyword
	size_t confPos = lowerResponse.find("confidence");
	if (confPos != std::string::npos) {
		// Find the number after "confidence" - look for colon, equals, or space
		size_t searchStart = confPos + 9; // After "confidence"
		size_t numStart = lowerResponse.find_first_of("0123456789.", searchStart);
		if (numStart != std::string::npos && numStart < searchStart + 10) { // Within 10 chars
			size_t numEnd = lowerResponse.find_first_not_of("0123456789.", numStart);
			if (numEnd == std::string::npos) {
				numEnd = lowerResponse.length();
			}
			std::string confStr = lowerResponse.substr(numStart, numEnd - numStart);
			try {
				float parsedConf = std::stof(confStr);
				// Clamp to valid range
				if (parsedConf >= 0.0f && parsedConf <= 1.0f) {
					out->confidence = parsedConf;
				} else if (parsedConf > 1.0f && parsedConf <= 100.0f) {
					// Handle percentage format (0-100)
					out->confidence = parsedConf / 100.0f;
				}
			} catch (...) {
				// Parsing failed, use default
			}
		}
	}
	
	// If confidence not found yet, try to find a number in parentheses like "(0.85)" or "(confidence: 0.85)"
	if (out->confidence == 0.8f) { // Still using default
		size_t parenStart = response.find('(');
		if (parenStart != std::string::npos) {
			size_t parenEnd = response.find(')', parenStart);
			if (parenEnd != std::string::npos && parenEnd - parenStart < 50) { // Reasonable length
				std::string parenContent = response.substr(parenStart + 1, parenEnd - parenStart - 1);
				std::string lowerParen = parenContent;
				std::transform(lowerParen.begin(), lowerParen.end(), lowerParen.begin(), ::tolower);
				
				// Check if it contains "confidence"
				if (lowerParen.find("confidence") != std::string::npos) {
					// Extract number after confidence
					size_t confInParen = lowerParen.find("confidence");
					size_t numStart = lowerParen.find_first_of("0123456789.", confInParen);
					if (numStart != std::string::npos) {
						size_t numEnd = lowerParen.find_first_not_of("0123456789.", numStart);
						if (numEnd == std::string::npos) {
							numEnd = lowerParen.length();
						}
						std::string confStr = lowerParen.substr(numStart, numEnd - numStart);
						try {
							float parsedConf = std::stof(confStr);
							if (parsedConf >= 0.0f && parsedConf <= 1.0f) {
								out->confidence = parsedConf;
							} else if (parsedConf > 1.0f && parsedConf <= 100.0f) {
								out->confidence = parsedConf / 100.0f;
							}
						} catch (...) {
							// Parsing failed
						}
					}
				} else if (parenContent.find_first_not_of("0123456789.") == std::string::npos) {
					// Just a number in parentheses
					try {
						float parsedConf = std::stof(parenContent);
						if (parsedConf >= 0.0f && parsedConf <= 1.0f) {
							out->confidence = parsedConf;
						} else if (parsedConf > 1.0f && parsedConf <= 100.0f) {
							out->confidence = parsedConf / 100.0f;
						}
					} catch (...) {
						// Parsing failed
					}
				}
			}
		}
	}
	
	// Validate action - if action is NONE, try to infer from response text
	if (out->action == LLM_ACTION_NONE) {
		// Try to infer action from response text (check most specific first)
		if (lowerResponse.find("retreat") != std::string::npos || 
		    lowerResponse.find("fall back") != std::string::npos ||
		    lowerResponse.find("withdraw") != std::string::npos) {
			out->action = LLM_ACTION_RETREAT;
			// Lower confidence for inferred actions
			if (out->confidence > 0.6f) out->confidence = 0.6f;
		} else if (lowerResponse.find("attack") != std::string::npos || 
		           lowerResponse.find("engage") != std::string::npos ||
		           lowerResponse.find("assault") != std::string::npos) {
			out->action = LLM_ACTION_ATTACK;
			if (out->confidence > 0.6f) out->confidence = 0.6f;
		} else if (lowerResponse.find("defend") != std::string::npos || 
		           lowerResponse.find("hold") != std::string::npos ||
		           lowerResponse.find("defensive") != std::string::npos) {
			out->action = LLM_ACTION_DEFEND;
			if (out->confidence > 0.6f) out->confidence = 0.6f;
		} else if (lowerResponse.find("support") != std::string::npos || 
		           lowerResponse.find("help") != std::string::npos ||
		           lowerResponse.find("backup") != std::string::npos) {
			out->action = LLM_ACTION_SUPPORT;
			if (out->confidence > 0.6f) out->confidence = 0.6f;
		} else if (lowerResponse.find("investigate") != std::string::npos || 
		           lowerResponse.find("check") != std::string::npos ||
		           lowerResponse.find("scout") != std::string::npos) {
			out->action = LLM_ACTION_INVESTIGATE;
			if (out->confidence > 0.6f) out->confidence = 0.6f;
		} else if (lowerResponse.find("patrol") != std::string::npos) {
			out->action = LLM_ACTION_PATROL;
			if (out->confidence > 0.6f) out->confidence = 0.6f;
		}
	}
	
	// Validate action/confidence pair - if confidence is very low, action might be unreliable
	if (out->action != LLM_ACTION_NONE && out->confidence < 0.3f) {
		// Very low confidence - mark as potentially invalid but still use it
		// (better than no action, but with caution)
		if (ai_llm_debug.integer >= 2) {
			G_Printf("LLM_ParseStrategicResponse: Low confidence (%.2f) for action, using with caution\n", out->confidence);
		}
	}
	
	out->isValid = (out->action != LLM_ACTION_NONE) ? qtrue : qfalse;
	
	// Extract reasoning from response if available (truncate if too long)
	if (response.length() > 0) {
		size_t maxReasonLen = sizeof(out->reasoning) - 1;
		if (response.length() < maxReasonLen) {
			Q_strncpyz(out->reasoning, response.c_str(), sizeof(out->reasoning));
		} else {
			// Truncate and add ellipsis
			std::string truncated = response.substr(0, maxReasonLen - 4);
			truncated += "...";
			Q_strncpyz(out->reasoning, truncated.c_str(), sizeof(out->reasoning));
		}
	} else {
		Q_strncpyz(out->reasoning, "LLM decision", sizeof(out->reasoning));
	}
}

static void LLM_ParseDialogueResponse(const std::string &response, llm_dialogue_t *out) {
	memset(out, 0, sizeof(llm_dialogue_t));
	
	// Try to parse JSON first
	json j = LLM_ParseJSONFromResponse(response);
	if (ai_llm_debug.integer >= 2) {
		if (j.is_null()) {
			G_Printf("LLM_ParseDialogueResponse: JSON parsing returned null. Raw response: %s\n", response.c_str());
		} else if (!j.is_object()) {
			G_Printf("LLM_ParseDialogueResponse: JSON is not an object. Type: %d\n", (int)j.type());
		} else if (!j.contains("dialogue")) {
			G_Printf("LLM_ParseDialogueResponse: JSON object missing 'dialogue' field. Keys: %s\n", j.dump().c_str());
		}
	}
	
	if (!j.is_null() && j.is_object() && j.contains("dialogue") && j["dialogue"].is_string()) {
		std::string dialogueStr = j["dialogue"].get<std::string>();
		
		// Clean up the dialogue text
		std::string cleaned = dialogueStr;
		
		// Trim whitespace
		size_t start = cleaned.find_first_not_of(" \n\r\t");
		size_t end = cleaned.find_last_not_of(" \n\r\t");
		
		if (start != std::string::npos && end != std::string::npos) {
			cleaned = cleaned.substr(start, end - start + 1);
		} else {
			cleaned = "";
		}
		
		// Truncate to first newline if any remain
		size_t newline = cleaned.find('\n');
		if (newline != std::string::npos) {
			cleaned = cleaned.substr(0, newline);
		}
		
		// Final validation
		if (cleaned.length() >= 2 && cleaned.find_first_not_of(" \n\r\t") != std::string::npos) {
			Q_strncpyz(out->text, cleaned.c_str(), sizeof(out->text));
			out->shouldSpeak = qtrue;
			
			// Parse priority if provided
			if (j.contains("priority") && j["priority"].is_number()) {
				int priority = j["priority"].get<int>();
				if (priority >= 1 && priority <= 10) {
					out->priority = priority;
				} else {
					out->priority = 5; // Default if out of range
				}
			} else {
				out->priority = 5; // Default priority
			}
			
			// Parse target if provided (for future use - currently always broadcast)
			// Could be used to target specific squad members or teams
			if (j.contains("target") && j["target"].is_string()) {
				std::string target = j["target"].get<std::string>();
				// For now, we always broadcast, but this could be extended
				// to target specific entities based on the target string
				out->targetEntityNum = -1; // Broadcast
			} else {
				out->targetEntityNum = -1; // Default: broadcast
			}
			
			if (ai_llm_debug.integer >= 2) {
				// G_Printf("LLM_ParseDialogueResponse: Successfully parsed JSON dialogue: '%s' (priority: %d)\n", cleaned.c_str(), out->priority);
			}
			return; // Successfully parsed JSON, done
		} else {
			if (ai_llm_debug.integer >= 1) {
				// G_Printf("LLM_ParseDialogueResponse: Parsed dialogue text failed validation (too short or empty). Cleaned: '%s'\n", cleaned.c_str());
			}
		}
	} else {
		if (ai_llm_debug.integer >= 1) {
			// G_Printf("LLM_ParseDialogueResponse: JSON parsing failed, using fallback. Response (first 300 chars): %.300s\n", response.c_str());
		}
	}
	
	// Fallback to old parsing method if JSON not found
	// Extract the actual dialogue (trim whitespace and newlines)
	std::string cleaned = response;
	
	// Remove common prompt artifacts and action codes
	// Remove "Action:" prefix and any numbers that follow (like "Action: 113")
	size_t actionPos = cleaned.find("Action:");
	if (actionPos != std::string::npos) {
		// Find the end of the action line (newline or end of string)
		size_t actionEnd = cleaned.find('\n', actionPos);
		if (actionEnd != std::string::npos) {
			cleaned.erase(actionPos, actionEnd - actionPos + 1);
		} else {
			cleaned.erase(actionPos);
		}
	}
	
	// Remove "=== TACTICAL SITUATION REPORT ===" and similar strategic report markers
	size_t tacticalPos = cleaned.find("=== TACTICAL");
	if (tacticalPos != std::string::npos) {
		size_t tacticalEnd = cleaned.find('\n', tacticalPos);
		if (tacticalEnd != std::string::npos) {
			cleaned.erase(tacticalPos, tacticalEnd - tacticalPos + 1);
		} else {
			cleaned.erase(tacticalPos);
		}
	}
	
	// Remove any remaining "===" markers
	while ((tacticalPos = cleaned.find("===")) != std::string::npos) {
		size_t tacticalEnd = cleaned.find('\n', tacticalPos);
		if (tacticalEnd != std::string::npos) {
			cleaned.erase(tacticalPos, tacticalEnd - tacticalPos + 1);
		} else {
			cleaned.erase(tacticalPos);
		}
	}
	
	// Remove role/character type descriptions that leak from prompts
	// Patterns like "You are a [type] [role] in [context]"
	std::string lowerCleaned = cleaned;
	std::transform(lowerCleaned.begin(), lowerCleaned.end(), lowerCleaned.begin(), ::tolower);
	
	// Remove "You are a" patterns
	size_t youArePos = lowerCleaned.find("you are a");
	if (youArePos != std::string::npos) {
		// Find the end of this sentence (period, newline, or end)
		size_t sentenceEnd = cleaned.find('.', youArePos);
		if (sentenceEnd == std::string::npos) {
			sentenceEnd = cleaned.find('\n', youArePos);
		}
		if (sentenceEnd == std::string::npos) {
			sentenceEnd = cleaned.length();
		} else {
			sentenceEnd++; // Include the period/newline
		}
		// Remove the entire sentence
		cleaned.erase(youArePos, sentenceEnd - youArePos);
		// Update lowercase version for further checks
		lowerCleaned = cleaned;
		std::transform(lowerCleaned.begin(), lowerCleaned.end(), lowerCleaned.begin(), ::tolower);
	}
	
	// Remove "You are" patterns (shorter version)
	youArePos = lowerCleaned.find("you are ");
	if (youArePos != std::string::npos && youArePos < 20) { // Only at start
		size_t sentenceEnd = cleaned.find('.', youArePos);
		if (sentenceEnd == std::string::npos) {
			sentenceEnd = cleaned.find('\n', youArePos);
		}
		if (sentenceEnd == std::string::npos) {
			sentenceEnd = cleaned.length();
		} else {
			sentenceEnd++;
		}
		cleaned.erase(youArePos, sentenceEnd - youArePos);
		lowerCleaned = cleaned;
		std::transform(lowerCleaned.begin(), lowerCleaned.end(), lowerCleaned.begin(), ::tolower);
	}
	
	// Remove common character type mentions that might appear
	const char* characterTypes[] = {
		"black guard", "venom", "soldier", "nazi", "officer",
		"scout", "assault", "support", "leader", "squad"
	};
	for (const char* type : characterTypes) {
		size_t typePos = lowerCleaned.find(type);
		if (typePos != std::string::npos && typePos < 50) { // Only near start
			// Check if it's part of a role description
			if (typePos > 0 && (lowerCleaned[typePos-1] == ' ' || lowerCleaned[typePos-1] == 'a')) {
				// Might be part of "a [type]" or "[type] [role]"
				// Find the end of this phrase
				size_t phraseEnd = cleaned.find_first_of(".\n", typePos);
				if (phraseEnd == std::string::npos) {
					phraseEnd = cleaned.find(" in ", typePos);
					if (phraseEnd != std::string::npos) {
						phraseEnd += 4; // Include " in "
						phraseEnd = cleaned.find_first_of(".\n", phraseEnd);
					}
				}
				if (phraseEnd != std::string::npos && phraseEnd - typePos < 100) {
					// Remove if it's a reasonable length phrase
					cleaned.erase(typePos, phraseEnd - typePos + 1);
					lowerCleaned = cleaned;
					std::transform(lowerCleaned.begin(), lowerCleaned.end(), lowerCleaned.begin(), ::tolower);
				}
			}
		}
	}
	
	// Remove "in WW2 combat" or similar context phrases
	size_t contextPos = lowerCleaned.find(" in ");
	if (contextPos != std::string::npos && contextPos < 100) {
		// Check if followed by context words
		if (lowerCleaned.find("ww2", contextPos) != std::string::npos ||
		    lowerCleaned.find("combat", contextPos) != std::string::npos ||
		    lowerCleaned.find("battle", contextPos) != std::string::npos) {
			size_t contextEnd = cleaned.find_first_of(".\n", contextPos);
			if (contextEnd == std::string::npos) {
				contextEnd = cleaned.length();
			} else {
				contextEnd++;
			}
			cleaned.erase(contextPos, contextEnd - contextPos);
			lowerCleaned = cleaned;
			std::transform(lowerCleaned.begin(), lowerCleaned.end(), lowerCleaned.begin(), ::tolower);
		}
	}
	
	// Remove standalone numbers that might be action codes (like "113" at start)
	size_t numStart = cleaned.find_first_not_of(" \n\r\t");
	if (numStart != std::string::npos) {
		size_t numEnd = cleaned.find_first_of(" \n\r\t", numStart);
		if (numEnd != std::string::npos && numEnd > numStart) {
			std::string firstToken = cleaned.substr(numStart, numEnd - numStart);
			// Check if it's just a number (potential action code)
			bool isNumber = true;
			for (char c : firstToken) {
				if (!std::isdigit(c)) {
					isNumber = false;
					break;
				}
			}
			if (isNumber && firstToken.length() <= 4) {
				// Remove this number token
				cleaned.erase(numStart, numEnd - numStart);
			}
		}
	}
	
	// Trim whitespace and newlines
	size_t start = cleaned.find_first_not_of(" \n\r\t");
	size_t end = cleaned.find_last_not_of(" \n\r\t");
	
	if (start != std::string::npos && end != std::string::npos) {
		cleaned = cleaned.substr(start, end - start + 1);
	} else {
		cleaned = "";
	}
	
	// Truncate to first newline (if any remain)
	size_t newline = cleaned.find('\n');
	if (newline != std::string::npos) {
		cleaned = cleaned.substr(0, newline);
	}
	
	// Final validation: ensure we have actual dialogue text
	// Also check that it's not raw JSON
	if (cleaned.length() < 2 || cleaned.find_first_not_of(" \n\r\t") == std::string::npos) {
		cleaned = ""; // Too short or only whitespace
	} else if (cleaned[0] == '{' || cleaned.find("\"dialogue\"") != std::string::npos) {
		// This is raw JSON, not parsed dialogue - don't use it
		if (ai_llm_debug.integer >= 1) {
			G_Printf("LLM_ParseDialogueResponse: Fallback text appears to be raw JSON, rejecting: %.100s\n", cleaned.c_str());
		}
		cleaned = "";
	}
	
	if (cleaned.length() > 0) {
		Q_strncpyz(out->text, cleaned.c_str(), sizeof(out->text));
		out->targetEntityNum = -1; // Broadcast
		out->shouldSpeak = qtrue;
		out->priority = 5; // Default priority
	} else {
		out->text[0] = '\0';
		out->targetEntityNum = -1;
		out->shouldSpeak = qfalse;
		out->priority = 5;
	}
	out->priority = 5; // Medium priority
}

//
// Context Recreation
//

static void LLM_RecreateContext() {
	// Free old context
	if (g_llm.ctx) {
		llama_free(g_llm.ctx);
		g_llm.ctx = nullptr;
	}
	
	if (g_llm.sampler) {
		llama_sampler_free(g_llm.sampler);
		g_llm.sampler = nullptr;
	}
	
	// Create new context
	llama_context_params ctx_params = llama_context_default_params();
	ctx_params.n_ctx = 4096;
	ctx_params.n_threads = ai_llm_max_threads.integer;
	ctx_params.n_batch = 512; // Larger batch for better GPU utilization
	ctx_params.n_ubatch = 512; // Unified batch size
	// Flash attention is auto-enabled by default
	
	g_llm.ctx = llama_init_from_model(g_llm.model, ctx_params);
	if (!g_llm.ctx) {
		// G_Printf("LLM_RecreateContext: Failed to recreate context\n");
		return;
	}
	
	// Create new sampler
	auto sparams = llama_sampler_chain_default_params();
	g_llm.sampler = llama_sampler_chain_init(sparams);
	
	llama_sampler_chain_add(g_llm.sampler, llama_sampler_init_temp(0.7f));
	llama_sampler_chain_add(g_llm.sampler, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));
	
	g_llm.generation_count = 0;
	
	// G_Printf("LLM_RecreateContext: Context recreated successfully\n");
}

//
// LLM Generation
//

static std::string LLM_Generate(const std::string &prompt, int maxTokens, qboolean requireCompleteJSON) {
	if (!g_llm.initialized || g_shutdownRequested) {
		return "";
	}
	
	// std::lock_guard<std::mutex> lock(g_llm.mutex);
	// We are already in worker thread, no need to lock if we assume single worker?
	// Actually g_llm.mutex protects model access.
	std::lock_guard<std::mutex> lock(g_llm.mutex);
	
	// Check shutdown again after acquiring lock
	if (g_shutdownRequested || !g_llm.ctx) {
		return "";
	}
	
	// Remove G_Printf calls from here as they are not thread safe
	// ... (logic continues)
	
	// Proactively clear KV cache if we're approaching limits
	// Clear more aggressively to prevent memory slot exhaustion
	if (g_llm.generation_count >= (g_llm.max_generations * 3 / 4)) {
		// Clear cache when at 75% of max generations to prevent exhaustion
		try {
			llama_memory_t mem = llama_get_memory(g_llm.ctx);
			if (mem) {
				llama_memory_clear(mem, true);  // true = clear data
				// Reset counter after clearing
				g_llm.generation_count = 0;
				// if (ai_llm_debug.integer >= 2) {
				// 	G_Printf("LLM_Generate: Proactively cleared KV cache at 75%% of max generations\n");
				// }
			}
		} catch (...) {
			// If clearing fails, try to recreate context
			if (g_llm.generation_count >= g_llm.max_generations) {
				LLM_RecreateContext();
			}
		}
	}
	
	// Check if we need to recreate context (prevent KV cache overflow)
	// Only recreate if we've done many generations AND KV cache is actually full
	if (g_llm.generation_count >= g_llm.max_generations) {
		// Check if KV cache is actually full before recreating
		try {
			llama_memory_t mem = llama_get_memory(g_llm.ctx);
			if (mem) {
				// Try to clear first instead of recreating
				llama_memory_clear(mem, true);  // true = clear data
				// Reset counter after clearing
				g_llm.generation_count = 0;
			} else {
				// If we can't get memory, recreate context
				LLM_RecreateContext();
			}
		} catch (...) {
			// If clearing fails, recreate context
			LLM_RecreateContext();
		}
		if (!g_llm.ctx || g_shutdownRequested) {
			return "";
		}
	}
	
	// Get vocab from model
	const struct llama_vocab * vocab = llama_model_get_vocab(g_llm.model);
	
	// Tokenize prompt
	std::vector<llama_token> tokens;
	tokens.resize(prompt.size() + 1024);
	int n_tokens = llama_tokenize(vocab, prompt.c_str(), prompt.size(), 
	                               tokens.data(), tokens.size(), true, false);
	
	if (n_tokens < 0) {
		// G_Printf("LLM_Generate: Tokenization failed\n");
		return "";
	}
	tokens.resize(n_tokens);
	
	// Validate batch size before creating batch
	// Large batches can cause memory slot exhaustion
	if (n_tokens > 512) {
		// Clear cache before processing large batch
		try {
			llama_memory_t mem = llama_get_memory(g_llm.ctx);
			if (mem) {
				llama_memory_clear(mem, true);
				g_llm.generation_count = 0;
				// if (ai_llm_debug.integer >= 2) {
				// 	G_Printf("LLM_Generate: Cleared cache before large batch (%d tokens)\n", n_tokens);
				// }
			}
		} catch (...) {
			// If clearing fails, truncate batch instead
			if (n_tokens > 1024) {
				// G_Printf("LLM_Generate: Warning: Truncating large batch from %d to 512 tokens\n", n_tokens);
				tokens.resize(512);
				n_tokens = 512;
			}
		}
	}
	
	// Reset sampler for new generation
	llama_sampler_reset(g_llm.sampler);
	
	// Create batch for prompt with proper sequence ID
	llama_batch batch = llama_batch_get_one(tokens.data(), n_tokens);
	
	// Evaluate prompt with retry logic
	int decode_retries = 0;
	int decode_result = llama_decode(g_llm.ctx, batch);
	if (decode_result != 0) {
		// Try clearing KV cache first before recreating context
		// Retry up to 2 times with cache clearing
		while (decode_result != 0 && decode_retries < 2) {
			decode_retries++;
			try {
				llama_memory_t mem = llama_get_memory(g_llm.ctx);
				if (mem) {
					llama_memory_clear(mem, true);
					// Reset generation counter
					g_llm.generation_count = 0;
					// if (ai_llm_debug.integer >= 1) {
					// 	G_Printf("LLM_Generate: Cleared KV cache and retrying decode (attempt %d)\n", decode_retries);
					// }
					// Retry decode after clearing
					batch = llama_batch_get_one(tokens.data(), n_tokens);
					decode_result = llama_decode(g_llm.ctx, batch);
					if (decode_result == 0) {
						// Success after clearing, continue
						break;
					}
				} else {
					// Can't get memory, break to recreate context
					break;
				}
			} catch (...) {
				// Exception during memory operations, break to recreate
				break;
			}
		}
		
		// If still failed after retries, recreate context
		if (decode_result != 0) {
			// if (ai_llm_debug.integer >= 1) {
			// 	G_Printf("LLM_Generate: Failed to decode after %d retries, recreating context...\n", decode_retries);
			// }
			LLM_RecreateContext();
			if (!g_llm.ctx) {
				return "";
			}
			// Try one more time with fresh context
			batch = llama_batch_get_one(tokens.data(), n_tokens);
			decode_result = llama_decode(g_llm.ctx, batch);
			if (decode_result != 0) {
				// G_Printf("LLM_Generate: Failed to decode even after context recreation\n");
				return "";
			}
		}
	}
	
	// Generate tokens
	std::string result;
	int braceCount = 0; // Track JSON braces for complete JSON detection
	bool inJSON = false;
	
	for (int i = 0; i < maxTokens; i++) {
		llama_token new_token = llama_sampler_sample(g_llm.sampler, g_llm.ctx, -1);
		
		// Check for EOS
		if (llama_vocab_is_eog(vocab, new_token)) {
			// If we require complete JSON and we're in the middle of JSON, continue
			if (requireCompleteJSON && inJSON && braceCount > 0) {
				// Don't stop on EOS if JSON is incomplete
				continue;
			}
			break;
		}
		
		// Decode token to text
		char buf[128];
		int len = llama_token_to_piece(vocab, new_token, buf, sizeof(buf), 0, false);
		if (len > 0) {
			result.append(buf, len);
			
			// Track JSON braces for complete JSON detection
			for (int j = 0; j < len; j++) {
				if (buf[j] == '{') {
					inJSON = true;
					braceCount++;
				} else if (buf[j] == '}') {
					braceCount--;
					if (braceCount == 0 && inJSON) {
						// Complete JSON object found
						if (requireCompleteJSON) {
							// Stop here - we have complete JSON
							g_llm.generation_count++;
							// if (ai_llm_debug.integer >= 2) {
							// 	G_Printf("LLM_Generate: Complete JSON detected, stopping generation: %s\n", result.c_str());
							// }
							return result;
						}
					}
				}
			}
		}
		
		// For dialogue (requireCompleteJSON), don't stop on newline - we need complete JSON
		// For strategic, stop on newline for single-line responses
		if (!requireCompleteJSON && result.find('\n') != std::string::npos) {
			break;
		}
		
		// Prepare next batch with single token
		batch = llama_batch_get_one(&new_token, 1);
		int token_decode_result = llama_decode(g_llm.ctx, batch);
		if (token_decode_result != 0) {
			// Decode failed - try clearing cache and retry once
			try {
				llama_memory_t mem = llama_get_memory(g_llm.ctx);
				if (mem) {
					llama_memory_clear(mem, true);
					g_llm.generation_count = 0;
					// Retry with cleared cache
					batch = llama_batch_get_one(&new_token, 1);
					token_decode_result = llama_decode(g_llm.ctx, batch);
				}
			} catch (...) {
				// Ignore exceptions during retry
			}
			
			if (token_decode_result != 0) {
				// Still failed, but return what we have so far
				if (result.length() > 0) {
					if (ai_llm_debug.integer >= 2) {
						G_Printf("LLM_Generate: Decode failed during generation, returning partial result\n");
					}
					break;
				}
				if (ai_llm_debug.integer >= 1) {
					G_Printf("LLM_Generate: Failed to decode token, stopping generation\n");
				}
				return "";
			}
		}
	}
	
	// Increment generation counter
	g_llm.generation_count++;
	
	return result;
}

//
// Worker Thread
//

static void LLM_WorkerThread() {
	while (!g_shutdownRequested) {
		llm_request_t request;
		
		// Wait for requests
		{
			std::unique_lock<std::mutex> lock(g_queueMutex);
			g_queueCondition.wait(lock, [] { 
				return !g_requestQueue.empty() || g_shutdownRequested; 
			});
			
			if (g_shutdownRequested) {
				break;
			}
			
			if (g_requestQueue.empty()) {
				continue;
			}
			
			request = g_requestQueue.front();
			g_requestQueue.pop();
		}
		
		// Check again if we should shutdown or are paused
		if (g_shutdownRequested || g_gamePaused) {
			continue;  // Skip this request
		}
		
		// Mark as processing
		g_processingRequest = true;
		
		// Process request with error handling
		std::string output;
		try {
			// Check shutdown before each operation
			if (g_shutdownRequested) {
				g_processingRequest = false;
				break;
			}
			
			if (request.type == REQUEST_STRATEGIC) {
				output = LLM_Generate(request.prompt, 100, qtrue); // Increased from 10 to 100, require JSON
			} else if (request.type == REQUEST_DIALOGUE) {
				// Dialogue needs enough tokens for complete JSON: {"dialogue": "...", "priority": N}
				// Minimum ~30-40 tokens, use 100 to ensure completion even with longer German text
				// Pass qtrue to requireCompleteJSON so generation continues until we get a closing brace
				output = LLM_Generate(request.prompt, 200, qtrue); // Increased from 100 to 200
			}
		} catch (...) {
			// Catch any C++ exceptions to prevent crashes
			output = "";
		}
		
		g_processingRequest = false;
		
		// Only store response if not shutting down or paused
		if (!g_shutdownRequested && !g_gamePaused && output.length() > 0) {
			std::lock_guard<std::mutex> lock(g_responseMutex);
			llm_response_t response;
			response.type = request.type;
			response.entityNum = request.entityNum;
			response.output = output;
			response.timestamp = request.timestamp;
			response.ready = true;
			g_responses.push_back(response);
			
			// Limit response list size
			if (g_responses.size() > 20) {
				g_responses.erase(g_responses.begin());
			}
		}
	}
}

