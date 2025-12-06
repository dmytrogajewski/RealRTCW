# LLM Integration for RealRTCW

This document describes the LLM (Large Language Model) integration for RealRTCW, which enables NPCs to use AI for strategic decision-making and dynamic dialogue generation.

## Overview

The integration uses llama.cpp with a small qwen3:4b model to provide:
- **Strategic Decision Making**: NPCs request tactical guidance from the LLM during combat
- **Dynamic Dialogue**: Context-aware battle chatter and callouts based on game events

## Requirements

1. **Model File**: Download a qwen3:4b GGUF model file
   - Recommended: `qwen3-4b-q4_0.gguf` or similar quantized version
   - Place in: `main/models/qwen3-4b.gguf`
   - Download from: https://huggingface.co/Qwen/Qwen3-4B-GGUF

2. **Build Requirements**:
   - C++11 compatible compiler (g++ 4.9+ or clang 3.3+)
   - pthread library
   - Sufficient RAM (4-6GB recommended for the model)

## Building

The build system has been configured to automatically compile and link with llama.cpp:

```bash
make
```

The Makefile will:
- Compile `code/game/ai_llm.cpp` with C++11
- Link with pre-built llama.cpp libraries in `code/llama.cpp/build/bin/`
- Include necessary headers from llama.cpp

## Configuration (Console Variables)

All LLM features are controlled via cvars:

### ai_llm_enabled
- **Default**: `0` (disabled)
- **Description**: Master switch for LLM features
- **Usage**: Set to `1` to enable LLM decision-making and dialogue
- **Example**: `/set ai_llm_enabled 1`

### ai_llm_model_path
- **Default**: `"main/models/qwen3-4b.gguf"`
- **Description**: Path to the GGUF model file
- **Usage**: Change if you use a different model or location
- **Example**: `/set ai_llm_model_path "main/models/custom-model.gguf"`

### ai_llm_strategic_interval
- **Default**: `3` (seconds)
- **Description**: How often NPCs request strategic decisions from the LLM
- **Range**: `1-10` seconds recommended
- **Example**: `/set ai_llm_strategic_interval 5`

### ai_llm_dialogue_interval
- **Default**: `5` (seconds)
- **Description**: Minimum time between dialogue generation attempts
- **Range**: `3-15` seconds recommended
- **Example**: `/set ai_llm_dialogue_interval 7`

### ai_llm_max_threads
- **Default**: `4`
- **Description**: Number of CPU threads for LLM inference
- **Range**: `1` to number of CPU cores
- **Example**: `/set ai_llm_max_threads 8`

### ai_llm_debug
- **Default**: `0`
- **Description**: Enable debug output for LLM decisions and dialogue
- **Usage**: Set to `1` to see LLM reasoning in console
- **Example**: `/set ai_llm_debug 1`

## Usage

1. **Download and place the model file** in `main/models/qwen3-4b.gguf`

2. **Start the game** and load a singleplayer map

3. **Enable LLM features** in the console:
   ```
   /set ai_llm_enabled 1
   ```

4. **Observe NPC behavior**:
   - NPCs in combat will request strategic decisions every 3 seconds (default)
   - NPCs will generate contextual dialogue during battles
   - Dialogue appears as center-print messages: `[NPC Name]: <dialogue>`

5. **Adjust settings** as needed:
   ```
   /set ai_llm_strategic_interval 5    # Less frequent strategic updates
   /set ai_llm_dialogue_interval 10    # Less frequent dialogue
   /set ai_llm_debug 1                 # See LLM decisions in console
   ```

## How It Works

### Strategic Decision Making

1. **Request**: Every `ai_llm_strategic_interval` seconds, NPCs in ALERT or COMBAT state send their context to the LLM
2. **Context**: Includes health, ammo, enemy info, ally count, current state
3. **Response**: LLM suggests an action: ATTACK, DEFEND, RETREAT, SUPPORT, INVESTIGATE, PATROL, or AMBUSH
4. **Application**: The suggestion is logged (debug mode) but traditional AI still executes (hybrid approach)

### Dynamic Dialogue

1. **Triggers**: Combat events like enemy_spotted, taking_fire, etc.
2. **Context**: NPC character type, event type, current situation
3. **Response**: Short (5-10 word) realistic battle callout
4. **Display**: Shown as center-print message to all players
5. **Cooldown**: Minimum 3 seconds between dialogue to prevent spam

## Performance Considerations

- **First inference**: May take 1-2 seconds as model loads into cache
- **Subsequent inferences**: 100-500ms depending on CPU and model size
- **Memory usage**: ~2.5-3GB additional RAM for qwen3:4b model
- **CPU usage**: Inference runs in separate thread to avoid blocking game

## Troubleshooting

### "LLM initialization failed" message
- Check that model file exists at the specified path
- Verify model file is a valid GGUF format
- Ensure sufficient RAM is available
- Check console for specific error messages

### No dialogue appearing
- Verify `ai_llm_enabled 1` is set
- Check `ai_llm_debug 1` to see if dialogue is being generated
- Ensure NPCs are in combat situations
- Dialogue has cooldown periods to prevent spam

### Poor performance / lag
- Reduce `ai_llm_max_threads` if CPU is overloaded
- Increase `ai_llm_strategic_interval` and `ai_llm_dialogue_interval`
- Use a smaller/more quantized model (q4_0, q5_0 instead of q8_0)
- Consider disabling with `ai_llm_enabled 0` on slower systems

### Strategic decisions not affecting gameplay
- Current implementation logs decisions but relies on traditional AI for execution
- Future updates can modify `code/game/ai_cast_funcs.c` to apply LLM decisions
- The infrastructure is in place for full LLM-driven behavior

## Architecture

### Files Modified/Created

**New Files**:
- `code/game/ai_llm.h` - LLM interface header
- `code/game/ai_llm.cpp` - LLM implementation
- `code/llama.cpp/` - llama.cpp library (submodule)

**Modified Files**:
- `code/game/ai_cast.h` - Added LLM state fields to cast_state_t
- `code/game/ai_cast_think.c` - Integrated LLM updates into think loop
- `code/game/g_main.c` - Added cvars, init/shutdown calls
- `Makefile` - Added C++ compilation and llama.cpp linking

### Threading Model

- **Main thread**: Game logic, AI traditional decision making
- **Worker thread**: LLM inference processing (async, non-blocking)
- **Synchronization**: Mutex-protected request queue and response list

### Data Flow

```
NPC Think Loop (main thread)
    ↓
Request Strategic Decision → Queue (thread-safe)
    ↓
Worker Thread → llama.cpp inference
    ↓
Response → Response List (thread-safe)
    ↓
NPC Think Loop retrieves result
    ↓
Log/Apply Decision
```

## Future Enhancements

Potential improvements for future versions:

1. **Full Strategic Control**: Modify AI behavior functions to act on LLM decisions
2. **Voice Synthesis**: Integrate TTS for spoken dialogue
3. **Conversation System**: NPCs respond to each other's dialogue
4. **Player Interaction**: LLM-driven NPC responses to player actions/commands
5. **Dynamic Objectives**: LLM suggests tactical objectives during missions
6. **Personality Traits**: Different models/prompts for character types

## Credits

- **llama.cpp**: https://github.com/ggerganov/llama.cpp
- **Qwen Models**: https://github.com/QwenLM/Qwen3
- **RealRTCW**: Original game engine

## License

This integration follows the same GPL license as RealRTCW. The llama.cpp library has its own MIT license.

