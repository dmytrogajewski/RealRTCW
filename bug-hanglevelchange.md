# Bug: Game Hangs on Level Change

## Symptoms
- Game hangs during level transitions (map changes)
- Loading screen displays but never progresses
- Initial map load works fine - hang only occurs on transitions
- 100% reproducible

## Environment
- Branch: iortcw-migration
- Binary: build/release-linux-x86_64/iowolfsp.x86_64

## Root Cause (FOUND)

The hang is caused by **one AI entity failing to complete spawning**, which blocks the entire game initialization sequence.

### The Chain of Events

1. **AI spawn counter mismatch**: 
   - `numSpawningCast = 84` (entities that started spawning)
   - `numcast = 83` (entities that finished spawning)
   - One entity started spawn but never completed

2. **This blocks `AICast_CheckLoadGame()`** in `ai_cast.c:804`:
   ```c
   if ( numSpawningCast != numcast ) {
       ready = qfalse;
   }
   ```

3. **Because `ready = qfalse`**, the "rockandroll" command is never sent:
   ```c
   if ( ready ) {
       // ... this never executes
       trap_SendServerCommand( -1, "rockandroll\n" );
   }
   ```

4. **The "rockandroll" command** is what opens the pregame UI and eventually clears `cg_norender`

5. **Meanwhile, `cg_norender = 1`** was set during map load, blocking all world rendering in `cg_view.c:1559-1561`:
   ```c
   if ( cg_norender.integer ) {
       return;  // Early return - no rendering happens
   }
   ```

6. **The UI is also not rendered** because:
   - `Key_GetCatcher() = 0` (no KEYCATCH_UI flag set)
   - The pregame popup was never opened
   - `cl_paused = 0` (would be 1 if pregame menu opened)

### Key GDB Findings

| Variable | Value | Meaning |
|----------|-------|---------|
| `numSpawningCast` | 84 | AI entities that started spawning |
| `numcast` | 83 | AI entities that finished spawning |
| `saveGamePending` | qtrue | Game waiting for init to complete |
| `cg_norender.integer` | 1 | World rendering disabled |
| `Key_GetCatcher()` | 0 | No UI catching input |
| `cl_paused` | 0 | Not paused (would be 1 with pregame menu) |
| `cg.snap` | valid pointer | Snapshots ARE being received |
| `cg.snap->snapFlags` | 0 | No SNAPFLAG_NOT_ACTIVE |
| `G_RunFrame` | being called | Server IS running |
| `CG_DrawActiveFrame` | being called | Cgame IS running |

## Debugging Workflow Used

### 1. Attach GDB to running game
```bash
cd /home/dmitriy/sources/RealRTCW/build/release-linux-x86_64
sudo gdb -p <PID> -batch -ex "command"
```

### 2. Load cgame symbols (required for cgame variables)
```bash
# Find cgame base address
sudo gdb -p <PID> -batch -ex "info proc mappings" | grep cgame
# Output: 0x7f6844e00000 ... cgame.sp.x86_64.so

# Load symbols with correct .text offset
sudo gdb -p <PID> -batch \
  -ex "add-symbol-file ./main/cgame.sp.x86_64.so 0x7f6844e50000" \
  -ex "p cg.infoScreenText"
```

### 3. Load qagame symbols
```bash
sudo gdb -p <PID> -batch \
  -ex "add-symbol-file ./main/qagame.sp.x86_64.so" \
  -ex "p numSpawningCast" \
  -ex "p numcast"
```

### 4. Check if functions are being called
```bash
# Set breakpoint and continue
sudo gdb -p <PID> -batch \
  -ex "b G_RunFrame" \
  -ex "c" &
sleep 3 && sudo killall -9 gdb
```

### 5. Key variables to check
```bash
# Client/server state
-ex "p cls.state"        # Should be CA_ACTIVE
-ex "p sv.state"         # Should be SS_GAME

# Rendering state
-ex "p cg_norender"      # Check .integer field
-ex "p cg.snap"          # Should not be NULL
-ex "p cg.snap->snapFlags"  # Should be 0

# AI spawning
-ex "p numSpawningCast"  # Must equal numcast
-ex "p numcast"
-ex "p saveGamePending"  # Should become qfalse

# UI state
-ex "p Key_GetCatcher()" # Should have KEYCATCH_UI if menu open
-ex "p cl_paused->integer"
```

## Code Flow Analysis

### Normal Level Transition Flow
1. `CheckReloadStatus()` triggers `spmap <nextmap>`
2. Map loads, AI entities spawn via `AIChar_spawn()`
3. Each spawn increments `numSpawningCast++`
4. Entity think function calls `AICast_CreateCharacter()`
5. Successful spawn increments `numcast++`
6. When `numSpawningCast == numcast`, `AICast_CheckLoadGame()` proceeds
7. "rockandroll" command sent to client
8. Client opens pregame menu, sets `KEYCATCH_UI`
9. Player clicks Start, `g_playerStart` set to 1
10. `cg_norender` set to 0, game renders

### Broken Flow (Current Bug)
1-4. Same as above
5. **One entity fails to complete** - `numcast` stays at 83
6. `numSpawningCast (84) != numcast (83)` - `ready = qfalse`
7. "rockandroll" never sent
8. Pregame menu never opens
9. `cg_norender` stays at 1
10. Game shows loading screen forever

## Hypothesis Testing

### Hypothesis 1: One AI entity failed AICast_CreateCharacter() returning NULL
**Theory**: An entity called `AICast_DelayedSpawnCast()` (numSpawningCast++) but `AICast_CreateCharacter()` returned NULL, so numcast wasn't incremented. The freed entity would have `aiCharacter != 0` but `inuse = qfalse`.

**Test**: Check g_entities[128..131] for freed AI entities
```
g_entities[128].aiCharacter = 0, inuse = qtrue
g_entities[129].aiCharacter = 0, inuse = qtrue
g_entities[130].aiCharacter = 0, inuse = qtrue
g_entities[131].aiCharacter = 0, inuse = qtrue
```

**Result**: DISPROVEN - No freed AI entities found. These are world entities (aiCharacter=0).

---

### Hypothesis 2: Player entity is being counted in numSpawningCast
**Theory**: The player (entity 0) is somehow being counted in numSpawningCast even though it shouldn't be.

**Test**: Check player entity flags
```
g_entities[0].aiCharacter = 0
g_entities[0].r.svFlags & SVF_CASTAI = 0
caststates[0].bs = NULL
caststates[0].aiCharacter = 0
```

**Result**: DISPROVEN - Player has no AI flags, not counted as AI.

---

### Hypothesis 3: numSpawningCast double-incremented for one entity
**Theory**: `AICast_DelayedSpawnCast()` was called twice for the same entity.

**Test**: Count actual spawned AI (caststates with bs != NULL) vs numSpawningCast
```
numSpawningCast = 84
numcast = 83
level.numPlayingClients = 84 (83 AI + 1 player)

caststates[1-83].bs = valid pointers (83 AI)
caststates[84].bs = NULL
caststates[85].bs = NULL
```

**Result**: CONFIRMED - 83 AI entities exist but numSpawningCast=84. One extra increment happened.

---

### Hypothesis 4: Entity with aiCharacter set but spawn never completed
**Theory**: An entity in g_entities[0..127] has `aiCharacter != 0` but `caststates[i].bs = NULL`.

**Test**: Check entities 84-87
```
g_entities[84].aiCharacter = 0
g_entities[84].inuse = qfalse
g_entities[84].classname = "clientslot"
g_entities[84].client = valid pointer (unused slot)

g_entities[85-87].aiCharacter = 0
```

**Result**: DISPROVEN - No entity has aiCharacter set without completing spawn.

---

### Hypothesis 5: Player entity counted in numSpawningCast during level transition
**Theory**: During level change, player somehow triggers numSpawningCast++.

**Test**: Verify player entity state
```
g_entities[0].classname = "player"
g_entities[0].aiName = "player"
g_entities[0].aiCharacter = 0 (already verified)
```

**Result**: DISPROVEN - Player has aiCharacter=0, not counted.

---

### Hypothesis 6: numSpawningCast not reset during level transition
**Theory**: Counter carries over from previous map (tram had 62 AI).

**Test**: Check if numSpawningCast would be 62+84=146 if not reset
```
Previous map (tram): total cast=62
Current map (village1): numSpawningCast=84, numcast=83
```

**Result**: DISPROVEN - If not reset, we'd have 146+. Counter is correctly reset.

---

### Hypothesis 7: Extra AICast_DelayedSpawnCast call from non-SP_ai source
**Theory**: Maybe survival mode or script system calls it directly.

**Test**: Grep all callers of AICast_DelayedSpawnCast
```
All calls are from SP_ai_* functions in ai_cast_characters.c
No external calls found.
```

**Result**: DISPROVEN - Only SP_ai_* functions call it.

---

### Hypothesis 8: SP_ai_* function called twice for same entity (BSP parsing issue)
**Theory**: An entity in the BSP is spawned twice due to parsing error or duplicate entity.

**Test**: Check log for G_CallSpawn of ai_* entities
```
G_CallSpawn only logs info_player_start, not ai_* entities
Cannot verify from logs alone
```

**Result**: INCONCLUSIVE - Need different test method

---

### Hypothesis 9: Save/load corrupts numSpawningCast or numcast
**Theory**: During level transition from save game, counters get corrupted in G_LoadGame or save system.

**Test**: Grep for numSpawningCast/numcast in g_save.c
```
No matches found in g_save.c
```

**Result**: DISPROVEN - These counters are not saved/loaded, they're computed fresh each map load.

---

### Hypothesis 10: AICast_ShutdownClient called during spawn, decrementing numcast
**Theory**: An AI entity spawns (numcast++) but then immediately dies/shuts down (numcast--), while numSpawningCast stays at 84.

**Test**: Check AI health values
```
g_entities[1].health = 25
g_entities[40].health = 32
g_entities[83].health = 27
All AI alive with health > 0
```

**Result**: DISPROVEN - No AI died during spawn.

---

### Hypothesis 11: Bug only happens during LEVEL TRANSITION, not fresh map load
**Theory**: The issue is specific to level transition code path, not initial map load. Maybe `savegame_loading` cvar affects spawn counting.

**Test**: Check g_reloading value
```
g_reloading.integer = 1 (confirms level transition)
level.reloadDelayTime = 0
```

**Result**: CONFIRMED we're in level transition, but need fresh load comparison to verify.

---

### Hypothesis 12: Player entity increments numSpawningCast during transition
**Theory**: During level transition, player respawn somehow calls code that increments numSpawningCast. Check G_LoadPersistant or player spawn code.

**Test**: Read G_LoadPersistant code
```
G_LoadPersistant reads:
- PersReadEntity(f, &g_entities[0])
- PersReadClient(f, &level.clients[0])
- PersReadCastState(f, AICast_GetCastState(0))
None of these modify numSpawningCast
```

**Result**: DISPROVEN - G_LoadPersistant doesn't touch spawn counters.

---

### Hypothesis 13: ai_marker or ai_effect entity incorrectly increments counter
**Theory**: There are ai_marker and ai_effect entities in the spawn table that might call AICast_DelayedSpawnCast by mistake.

**Test**: Read SP_ai_marker, SP_ai_effect, SP_ai_trigger in ai_cast_script_ents.c
```
SP_ai_marker: Only does trace/positioning, no AI spawn
SP_ai_effect: Only sets up effect link to AI, no spawn
SP_ai_trigger: Script trigger entity, no spawn
None call AICast_DelayedSpawnCast
```

**Result**: DISPROVEN - These entities don't increment spawn counters.

---

### Hypothesis 14: Survival mode creates extra AI entity
**Theory**: Survival mode code (g_survival*.c, ai_cast_survival.c) might spawn an extra AI during level transition.

**Test**: Check g_gametype value
```
g_gametype = 0 (GT_SINGLE_PLAYER, not survival)
```

**Result**: DISPROVEN - Not in survival mode.

---

### Hypothesis 15: AICast_CreateCharacter_Survival called instead of normal spawn
**Theory**: Even in single player, survival spawn path might be called by accident.

**Test**: Check when AICast_CreateCharacter_Survival is called
```
ai_cast.c:445: Only called if g_gametype.integer == GT_SURVIVAL
g_gametype = 0 (single player)
```

**Result**: DISPROVEN - Survival code path not taken.

---

### Hypothesis 16: The bug is a RACE CONDITION - numcast decremented after save
**Theory**: When game saves between levels, an AI might be shutdown (numcast--) but the count was already snapshotted/checked.

**Test**: Check savegame_loading cvar
```
savegame_loading = 0
We're on "else" path (line 801+) - new level transition, NOT loading savegame
```

**Result**: INCONCLUSIVE - Not a savegame load issue, but race condition still possible.

---

## Summary After 16 Hypotheses

**Confirmed facts:**
- 83 AI entities spawned correctly (caststates[1-83].bs valid)
- numSpawningCast = 84 (one extra increment)
- numcast = 83 (correct count)
- No dead AI, no failed spawn errors in log
- Not survival mode, not savegame load
- All SP_ai_* functions call AICast_DelayedSpawnCast correctly

**Root cause still unknown.** The extra numSpawningCast++ is unexplained.

**Proposed fix:** Rather than continue searching, implement a pragmatic workaround:
1. Add timeout to spawn wait (after X seconds, proceed anyway)
2. OR change check to `numSpawningCast < numcast` (catch only missing spawns)
3. OR add logging to identify the 84th entity that incremented counter

---

## Next Steps

1. **Find which AI entity is failing to spawn**
   - Add logging to `AIChar_spawn()` and `AICast_CreateCharacter()`
   - Check return value of `AICast_CreateCharacter()` - if NULL, spawn failed
   - Look at entities with `think == AIChar_spawn` that never completed

2. **Check recent changes to AI spawning code**
   - The Dog entity config strings were the last output
   - Check changes to `ai_cast_script.c` script parsing
   - The `castScriptStatus.castScriptEventIndex >= 0` change in ai_squad.c

3. **Potential quick fix**
   - Add timeout to `AICast_CheckLoadGame()` 
   - If spawn takes too long, proceed anyway with warning
