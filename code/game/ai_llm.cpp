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
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <algorithm>

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
	                  initialized(false), generation_count(0), max_generations(30) {}  // Recreate more frequently
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
#define MAX_QUEUE_SIZE 5  // Prevent queue from growing too large

// Forward declarations
static void LLM_WorkerThread();
static std::string LLM_BuildStrategicPrompt(cast_state_t *cs);
static std::string LLM_BuildDialoguePrompt(cast_state_t *cs, const char *eventType);
static void LLM_ParseStrategicResponse(const std::string &response, llm_decision_t *out);
static void LLM_ParseDialogueResponse(const std::string &response, llm_dialogue_t *out);
static std::string LLM_Generate(const std::string &prompt, int maxTokens);
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
	ctx_params.n_ctx = 4096;  // Larger context - you have the VRAM
	ctx_params.n_threads = ai_llm_max_threads.integer;
	ctx_params.n_batch = 512; // Larger batch for better GPU utilization
	ctx_params.n_ubatch = 512; // Unified batch size
	// Flash attention is auto-enabled by default
	
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
	
	// Limit queue size to prevent overwhelming the system
	if (g_requestQueue.size() >= MAX_QUEUE_SIZE) {
		return; // Queue full, skip this request
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
	
	// Limit queue size to prevent overwhelming the system
	if (g_requestQueue.size() >= MAX_QUEUE_SIZE) {
		return; // Queue full, skip this request
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
	
	// Tactical Options
	prompt += "TACTICAL OPTIONS:\n";
	prompt += "ATTACK - Engage enemy\n";
	prompt += "DEFEND - Hold position\n";
	prompt += "RETREAT - Fall back\n";
	prompt += "SUPPORT - Help allies\n";
	prompt += "INVESTIGATE - Check area\n";
	prompt += "PATROL - Continue patrol\n\n";
	prompt += "Action:";
	
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
	
	prompt += "\nShort tactical callout (3-8 words):\n";
	
	return prompt;
}

//
// Response Parsing
//

static void LLM_ParseStrategicResponse(const std::string &response, llm_decision_t *out) {
	memset(out, 0, sizeof(llm_decision_t));
	
	out->action = LLM_StringToAction(response);
	out->confidence = 0.8f; // Default confidence
	out->targetEntityNum = -1;
	out->isValid = (out->action != LLM_ACTION_NONE) ? qtrue : qfalse;
	
	Q_strncpyz(out->reasoning, "LLM decision", sizeof(out->reasoning));
}

static void LLM_ParseDialogueResponse(const std::string &response, llm_dialogue_t *out) {
	memset(out, 0, sizeof(llm_dialogue_t));
	
	// Extract the actual dialogue (trim whitespace and newlines)
	std::string cleaned = response;
	size_t start = cleaned.find_first_not_of(" \n\r\t");
	size_t end = cleaned.find_last_not_of(" \n\r\t");
	
	if (start != std::string::npos && end != std::string::npos) {
		cleaned = cleaned.substr(start, end - start + 1);
	}
	
	// Truncate to first newline
	size_t newline = cleaned.find('\n');
	if (newline != std::string::npos) {
		cleaned = cleaned.substr(0, newline);
	}
	
	Q_strncpyz(out->text, cleaned.c_str(), sizeof(out->text));
	out->targetEntityNum = -1; // Broadcast
	out->shouldSpeak = (cleaned.length() > 0) ? qtrue : qfalse;
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
		G_Printf("LLM_RecreateContext: Failed to recreate context\n");
		return;
	}
	
	// Create new sampler
	auto sparams = llama_sampler_chain_default_params();
	g_llm.sampler = llama_sampler_chain_init(sparams);
	
	llama_sampler_chain_add(g_llm.sampler, llama_sampler_init_temp(0.7f));
	llama_sampler_chain_add(g_llm.sampler, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));
	
	g_llm.generation_count = 0;
	
	G_Printf("LLM_RecreateContext: Context recreated successfully\n");
}

//
// LLM Generation
//

static std::string LLM_Generate(const std::string &prompt, int maxTokens) {
	if (!g_llm.initialized || g_shutdownRequested) {
		return "";
	}
	
	std::lock_guard<std::mutex> lock(g_llm.mutex);
	
	// Check shutdown again after acquiring lock
	if (g_shutdownRequested || !g_llm.ctx) {
		return "";
	}
	
	// Check if we need to recreate context (prevent KV cache overflow)
	if (g_llm.generation_count >= g_llm.max_generations) {
		LLM_RecreateContext();
		if (!g_llm.ctx || g_shutdownRequested) {
			return "";
		}
	}
	
	// Clear KV cache before each generation to prevent accumulation
	// This ensures we don't run out of memory slots
	try {
		llama_memory_t mem = llama_get_memory(g_llm.ctx);
		if (mem) {
			llama_memory_clear(mem, true);  // true = clear data
		}
	} catch (...) {
		// If clearing fails, recreate context
		LLM_RecreateContext();
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
		G_Printf("LLM_Generate: Tokenization failed\n");
		return "";
	}
	tokens.resize(n_tokens);
	
	// Reset sampler for new generation
	llama_sampler_reset(g_llm.sampler);
	
	// Create batch for prompt
	llama_batch batch = llama_batch_get_one(tokens.data(), tokens.size());
	
	// Evaluate prompt
	if (llama_decode(g_llm.ctx, batch) != 0) {
		G_Printf("LLM_Generate: Failed to decode prompt, recreating context...\n");
		// Context is full, recreate it
		LLM_RecreateContext();
		
		// Try one more time with fresh context
		if (!g_llm.ctx) {
			return "";
		}
		
		batch = llama_batch_get_one(tokens.data(), tokens.size());
		if (llama_decode(g_llm.ctx, batch) != 0) {
			G_Printf("LLM_Generate: Failed again after recreation\n");
			return "";
		}
	}
	
	// Generate tokens
	std::string result;
	
	for (int i = 0; i < maxTokens; i++) {
		llama_token new_token = llama_sampler_sample(g_llm.sampler, g_llm.ctx, -1);
		
		// Check for EOS
		if (llama_vocab_is_eog(vocab, new_token)) {
			break;
		}
		
		// Decode token to text
		char buf[128];
		int len = llama_token_to_piece(vocab, new_token, buf, sizeof(buf), 0, false);
		if (len > 0) {
			result.append(buf, len);
		}
		
		// Check for newline (stop generation for single-line responses)
		if (result.find('\n') != std::string::npos) {
			break;
		}
		
		// Prepare next batch with single token
		batch = llama_batch_get_one(&new_token, 1);
		if (llama_decode(g_llm.ctx, batch) != 0) {
			// Decode failed, but return what we have so far
			if (result.length() > 0) {
				break;
			}
			G_Printf("LLM_Generate: Failed to decode token, stopping generation\n");
			return "";
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
				output = LLM_Generate(request.prompt, 10); // Short response
			} else if (request.type == REQUEST_DIALOGUE) {
				output = LLM_Generate(request.prompt, 20); // Slightly longer
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

