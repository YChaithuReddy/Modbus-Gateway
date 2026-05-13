#!/bin/bash
# Pre-Commit Quality Gate Hook (PreToolUse on Bash for git commit)
# Catches common embedded firmware issues before they get committed

# Read JSON input from stdin
INPUT=$(cat)

# Extract the bash command
COMMAND=$(echo "$INPUT" | jq -r '.tool_input.command // empty')

# Only run for git commit commands
if [[ "$COMMAND" != *"git commit"* ]]; then
  exit 0
fi

ISSUES=""
ISSUE_COUNT=0

# Check 1: sprintf usage in staged C files (should be snprintf)
SPRINTF_FILES=$(git diff --cached --name-only -- '*.c' '*.h' | xargs grep -l 'sprintf(' 2>/dev/null | grep -v 'snprintf' | head -5)
if [[ -n "$SPRINTF_FILES" ]]; then
  ISSUES+="WARNING: sprintf() found in staged files (use snprintf instead): $SPRINTF_FILES\n"
  ISSUE_COUNT=$((ISSUE_COUNT + 1))
fi

# Check 2: Partition table modified
PARTITION_CHANGED=$(git diff --cached --name-only | grep -i 'partition.*\.csv' | head -1)
if [[ -n "$PARTITION_CHANGED" ]]; then
  ISSUES+="CRITICAL: Partition table modified! This is FROZEN since v1.3.7. OTA devices will need manual reflash.\n"
  ISSUE_COUNT=$((ISSUE_COUNT + 1))
fi

# Check 3: Check for mutex issues - vTaskDelay inside mutex-protected sections
DELAY_IN_MUTEX=$(git diff --cached -- '*.c' | grep -A5 'xSemaphoreTake' | grep 'vTaskDelay' 2>/dev/null | head -3)
if [[ -n "$DELAY_IN_MUTEX" ]]; then
  ISSUES+="WARNING: vTaskDelay found near mutex acquisition. Verify mutex is released before delay.\n"
  ISSUE_COUNT=$((ISSUE_COUNT + 1))
fi

# Check 4: Version string check
VERSION_CHANGE=$(git diff --cached -- 'main/iot_configs.h' | grep '+.*FW_VERSION' | head -1)
if [[ -n "$VERSION_CHANGE" ]]; then
  ISSUES+="REMINDER: FW_VERSION_STRING changed. Verify it matches the intended release version.\n"
  ISSUE_COUNT=$((ISSUE_COUNT + 1))
fi

# Output results
if [[ $ISSUE_COUNT -gt 0 ]]; then
  cat << EOF
{
  "systemMessage": "PRE-COMMIT QUALITY GATE ($ISSUE_COUNT issues):\n$ISSUES\nReview these before proceeding with the commit. Fix any real issues, or confirm they are intentional."
}
EOF
else
  cat << EOF
{
  "systemMessage": "PRE-COMMIT QUALITY GATE: All checks passed."
}
EOF
fi

exit 0
