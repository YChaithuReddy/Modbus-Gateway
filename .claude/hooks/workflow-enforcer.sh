#!/bin/bash
# Workflow Enforcer Hook (UserPromptSubmit)
# Fires on every user prompt to remind Claude of mandatory procedures

# Read JSON input from stdin
INPUT=$(cat)

# Extract the user's prompt text
PROMPT=$(echo "$INPUT" | jq -r '.prompt // empty')

# Convert to lowercase for matching
PROMPT_LOWER=$(echo "$PROMPT" | tr '[:upper:]' '[:lower:]')

# Detect task type from prompt keywords
IS_BUG=false
IS_SENSOR=false
IS_FIX=false
IS_WEB=false
IS_MODBUS=false
IS_MEMORY=false

[[ "$PROMPT_LOWER" == *"bug"* ]] || [[ "$PROMPT_LOWER" == *"broken"* ]] || [[ "$PROMPT_LOWER" == *"not working"* ]] || [[ "$PROMPT_LOWER" == *"error"* ]] || [[ "$PROMPT_LOWER" == *"crash"* ]] && IS_BUG=true
[[ "$PROMPT_LOWER" == *"sensor"* ]] || [[ "$PROMPT_LOWER" == *"modbus"* ]] || [[ "$PROMPT_LOWER" == *"register"* ]] || [[ "$PROMPT_LOWER" == *"byte order"* ]] || [[ "$PROMPT_LOWER" == *"rs485"* ]] && IS_SENSOR=true
[[ "$PROMPT_LOWER" == *"fix"* ]] || [[ "$PROMPT_LOWER" == *"debug"* ]] || [[ "$PROMPT_LOWER" == *"why"* ]] || [[ "$PROMPT_LOWER" == *"issue"* ]] && IS_FIX=true
[[ "$PROMPT_LOWER" == *"web"* ]] || [[ "$PROMPT_LOWER" == *"page"* ]] || [[ "$PROMPT_LOWER" == *"html"* ]] || [[ "$PROMPT_LOWER" == *"http"* ]] || [[ "$PROMPT_LOWER" == *"ui"* ]] && IS_WEB=true
[[ "$PROMPT_LOWER" == *"modbus"* ]] || [[ "$PROMPT_LOWER" == *"rs485"* ]] || [[ "$PROMPT_LOWER" == *"slave"* ]] && IS_MODBUS=true
[[ "$PROMPT_LOWER" == *"heap"* ]] || [[ "$PROMPT_LOWER" == *"memory"* ]] || [[ "$PROMPT_LOWER" == *"stack"* ]] || [[ "$PROMPT_LOWER" == *"overflow"* ]] && IS_MEMORY=true

# Build context-aware reminder
REMINDER="MANDATORY WORKFLOW REMINDER:\n"
REMINDER+="1. Follow the 7-step process (Understanding > Classification > Root Cause > Strategy > Implementation > Validation)\n"
REMINDER+="2. Create a TaskList BEFORE starting work (TaskCreate for each step)\n"
REMINDER+="3. Check MEMORY.md for past bugs and known patterns before proposing fixes\n"

if $IS_BUG || $IS_FIX; then
  REMINDER+="4. BUG/FIX DETECTED: Do NOT jump to code. Find root cause FIRST. Check anti-patterns.md and debugging-log.md for known traps.\n"
fi

if $IS_SENSOR || $IS_MODBUS; then
  REMINDER+="4. SENSOR/MODBUS: Verify byte order with raw register hex math. Check MEMORY.md sensor quick reference. Update ALL locations (sensor_manager.c + web_config.c test handler).\n"
fi

if $IS_WEB; then
  REMINDER+="4. WEB UI: Minimize TCP sends. web_config.c is 609KB - small edits ONLY. Check error 104/128 debugging checklist.\n"
fi

if $IS_MEMORY; then
  REMINDER+="4. MEMORY: Check heap targets (80-90KB free without web, 50-70KB with web). Never reduce TCP buffers below 5760.\n"
fi

REMINDER+="5. After ALL changes: invoke /learn-and-remember to update memory files.\n"

# Output as system message
cat << EOF
{
  "systemMessage": "$REMINDER"
}
EOF

exit 0
