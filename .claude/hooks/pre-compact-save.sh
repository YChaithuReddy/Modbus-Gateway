#!/bin/bash
# PreCompact Hook - Save key findings before context window compression
# Fires when the context is about to be compressed to free up space
# CRITICAL: Anything not saved to memory files will be LOST after compaction

cat << 'EOF'
{
  "systemMessage": "CONTEXT COMPRESSION IMMINENT.\n\nBefore compression, you MUST save any unsaved discoveries:\n\n1. Were any NEW sensor byte orders confirmed? → Add to MEMORY.md sensor quick reference\n2. Were any NEW bugs found? → Add to memory/debugging-log.md\n3. Were any NEW anti-patterns found? → Add to memory/anti-patterns.md\n4. Were any file dependencies discovered? → Add to memory/file-map.md\n5. Were any register maps or GPIO changes made? → Update HARDWARE_CONNECTIONS.md\n6. Were any tasks left incomplete? → Note them in the task list with full context\n\nAnything NOT written to a file will be LOST after this compression.\nWrite all unsaved learnings NOW using Edit/Write tools."
}
EOF

exit 0
