# AI Strategy Effectiveness Analysis

## Executive Summary
The AI strategy system shows **critical tracking failures** that prevent it from learning and adapting. While the infrastructure is well-designed, several bugs prevent the system from recording combat data, resulting in a strategy that cannot improve.

---

## Key Findings

### 1. **Critical Bug: Player Kills Not Tracked**
**Severity: HIGH**

**Problem:**
- Console shows: `K/D: 0/0` despite 9+ AI casualties
- All casualties from player kills are **not being recorded** in strategy system

**Root Cause** (`ai_cast_events.c:315-327`):
```c
if ( attacker && attacker->aiTeam >= 0 && attacker->aiTeam != self->aiTeam ) {
    // Only records kills when BOTH entities are AI with valid teams
    AICast_RecordKill( attacker->aiTeam, qfalse, 1000 );
}
```

**Issue:** Player kills are ignored because the player entity doesn't have a valid `aiTeam`. The condition requires both attacker and victim to be AI.

**Impact:**
- Strategy effectiveness stuck at default 0.50 (neutral)
- No adaptation occurs because threshold is < 0.40
- AI cannot learn from being repeatedly killed by player

---

### 2. **Strategy Adaptation Not Triggering**
**Severity: MEDIUM**

**Observations from logs:**
- Effectiveness: 0.50 (unchanged across all evaluations)
- Strategy: "Defensive" (never changes)
- Evaluation interval: 30 seconds (working correctly)

**Why it fails:**
```c
// ai_strategy.c:365 - Only adapts if effectiveness < 0.4
if (effectiveness < 0.4f) {
    suggested = AICast_SuggestStrategy(teamNum);
    // ... adapt strategy
}
```

Since effectiveness is stuck at 0.50, adaptation never triggers.

**Actual Performance:**
- Team 0: 0 kills, 9+ deaths (should be ~0.0 effectiveness)
- Should trigger RETREAT strategy immediately

---

### 3. **LLM Decisions Show Overconfidence**
**Severity: LOW-MEDIUM**

**Pattern Observed:**
- All LLM decisions: 0.80 confidence
- Actions chosen: DEFEND (most common), ATTACK, RETREAT, INVESTIGATE
- Results: Units die shortly after decisions

**Examples:**
```
^7[LLM] nazi7 (ASSAULT, Squad 1): ATTACK (0.80 conf)
^2[AI EXECUTE] nazi7 attacking aggressively
^1[CASUALTY] nazi7 killed by player
```

**Analysis:**
- LLM makes reasonable tactical choices
- But without proper feedback (kill tracking), it cannot learn
- High confidence (0.80) suggests model is not uncertain, but results are poor
- This indicates the prompt/context may need adjustment

---

### 4. **Squad Coordination Partially Working**
**Severity: LOW**

**Working:**
✓ Squad formation (4 members per squad)
✓ Role assignment (Leader, Assault, Support)
✓ Order propagation (assault, fallback, hold_position)
✓ Intel sharing between squad members

**Not Working:**
✗ Orders don't prevent deaths
✗ No tactical advantage observed from squad behavior
✗ Units execute orders but still die quickly

**Example:**
```
^5[TACTICS] Squad Leader nazi16 issued order: fallback
LLM Dialogue [nazi16]: "Assault Order Received. Ready."
^1[CASUALTY] nazi16 killed by player
```

Squad leaders die before orders can be executed effectively.

---

## Performance Metrics

### Team 0 Performance (from logs)
| Metric | Expected | Actual | Status |
|--------|----------|--------|--------|
| Friendly Casualties | 9+ | 0 | ❌ Not tracked |
| Enemy Casualties | 0 | 0 | ✓ Correct |
| K/D Ratio | 0.0 | "0/0" | ❌ Bug |
| Effectiveness | ~0.0 | 0.50 | ❌ Wrong |
| Strategy | RETREAT | DEFENSIVE | ❌ No adaptation |

### Casualty List (Player kills)
1. xdog1 (Team 0)
2. nazi15 (Team 0)
3. nazi5 (Team 0)
4. nazi7 (Team 0)
5. nazi8 (Team 0)
6. nazi16 (Team 0)
7. nazi17 (Team 0)
8. nazi9 (Team 0)

**0% survival rate** - all AI units killed, no player casualties.

---

## System Behavior Analysis

### What's Working
1. **Intel System**: Enemy detection and sharing working well
   - Units spot enemies at various ranges (227-1646 units)
   - Intel shared across team with position tracking
   
2. **LLM Integration**: Making decisions at appropriate intervals
   - Strategic decisions every few seconds
   - Dialogue generation contextual and appropriate
   - Decision logging comprehensive

3. **Squad System**: Structure and assignments functional
   - Squads formed with correct size (4 members)
   - Roles assigned appropriately
   - Formation offsets calculated

4. **Tactical Memory**: Context building appears functional
   - Enemy positions tracked
   - Team composition known
   - Threat assessment occurring

### What's Broken
1. **Performance Tracking**: Critical failure in kill recording
2. **Strategy Adaptation**: Locked to defensive with no learning
3. **Combat Effectiveness**: 0% survival despite "intelligent" decisions
4. **Feedback Loop**: No connection between actions and outcomes

---

## Recommended Fixes

### Priority 1: Fix Kill Tracking
**File:** `code/game/ai_cast_events.c:313-336`

**Current code:**
```c
if ( attacker && attacker->aiTeam >= 0 && attacker->aiTeam != self->aiTeam ) {
    // Only handles AI-vs-AI kills
    AICast_RecordKill( attacker->aiTeam, qfalse, 1000 );
}
```

**Should be:**
```c
if ( attacker && self->aiTeam >= 0 ) {
    // Record friendly casualty for victim's team
    if ( ai_adaptive_strategy.integer ) {
        AICast_RecordKill( self->aiTeam, qtrue, level.time - self->spawnTime );
    }
    
    // If attacker is AI from different team, record enemy kill
    if ( attacker->aiTeam >= 0 && attacker->aiTeam != self->aiTeam ) {
        TacticalMemory_RecordCasualty( attacker->aiTeam, qfalse );
        if ( ai_adaptive_strategy.integer ) {
            AICast_RecordKill( attacker->aiTeam, qfalse, 1000 );
            AICast_RecordEngagement( attacker->aiTeam, qtrue );
        }
    }
}
```

### Priority 2: Adjust Effectiveness Calculation
**File:** `code/game/ai_strategy.c:169-208`

**Issue:** With all deaths and no kills, effectiveness should be near 0.0, not 0.5

**Current:** Starts at 0.5 and only adjusts if there's data
**Should:** Calculate based on available data, default to lower effectiveness when taking casualties

### Priority 3: Lower Adaptation Threshold
**File:** `code/game/ai_strategy.c:365`

**Current:** `if (effectiveness < 0.4f)`
**Recommended:** `if (effectiveness < 0.5f)` 

This allows adaptation from neutral state.

### Priority 4: Tune LLM Context
The LLM is making decisions but needs better feedback:
- Include recent casualty information in context
- Add team effectiveness score to decision prompt
- Reduce confidence when team is losing badly

---

## Tactical Assessment

### Why AI is Losing

1. **Predictable Defensive Behavior**: Always defending, never adapting
2. **No Learning**: Can't learn from mistakes (kill tracking broken)
3. **Poor Position Selection**: Units defend in place, not tactical positions
4. **Suicide Attacks**: ATTACK orders result in aggressive charges into death
5. **No Retreat**: Even with 100% casualty rate, strategy doesn't change to retreat

### What Good Strategy Would Look Like

With proper tracking, the system should:
1. **Detect poor performance** (0% survival → effectiveness 0.0)
2. **Suggest RETREAT** (line 250-255 in ai_strategy.c)
3. **Fall back to better positions**
4. **Regroup and try different approach**
5. **Learn from player tactics** (via LLM context)

---

## Conclusion

**Current State:** The AI strategy system has excellent architecture but is **non-functional due to critical tracking bugs**. The system cannot learn or adapt because it doesn't know it's losing.

**Potential:** Once kill tracking is fixed, the system should be able to:
- Recognize losing situations
- Adapt strategy appropriately  
- Use LLM intelligence to make informed decisions
- Coordinate squad tactics effectively

**Next Steps:**
1. Fix player kill tracking (Priority 1)
2. Record friendly casualties for victim's team
3. Test that effectiveness calculation reflects reality
4. Verify strategy adaptation triggers correctly
5. Tune LLM prompts based on real performance data

---

## Technical Debt

- **Missing engagement tracking**: Successful/failed engagements not recorded consistently
- **No objective tracking**: Objective capture/loss not implemented in game code
- **Spawn time tracking**: Need `self->spawnTime` to calculate accurate survival time
- **Formation effectiveness**: Formations calculated but not enforced during combat
- **Cover point system**: Cover points tracked but not used for tactical positioning

---

*Analysis Date: Dec 6, 2025*
*Game Version: RealRTCW with LLM Integration*
*AI System: Tactical Memory + Adaptive Strategy + LLM Decision Making*


