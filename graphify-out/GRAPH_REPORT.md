# Graph Report - .  (2026-05-13)

## Corpus Check
- Corpus is ~6,601 words - fits in a single context window. You may not need a graph.

## Summary
- 94 nodes · 99 edges · 21 communities
- Extraction: 100% EXTRACTED · 0% INFERRED · 0% AMBIGUOUS
- Token cost: 0 input · 0 output

## Community Hubs (Navigation)
- [[_COMMUNITY_Widget Interaction Layer|Widget Interaction Layer]]
- [[_COMMUNITY_Emulator Render Engine|Emulator Render Engine]]
- [[_COMMUNITY_Session Process Control|Session Process Control]]
- [[_COMMUNITY_Scrollback Buffer Logic|Scrollback Buffer Logic]]
- [[_COMMUNITY_Theme JSON IO|Theme JSON IO]]

## God Nodes (most connected - your core abstractions)
1. `feedOutput()` - 6 edges
2. `putCharacter()` - 4 edges
3. `newline()` - 4 edges
4. `recalculateGrid()` - 4 edges
5. `size()` - 4 edges
6. `loadFromJson()` - 3 edges
7. `cellAt()` - 3 edges
8. `lineText()` - 3 edges
9. `index()` - 3 edges
10. `start()` - 3 edges

## Surprising Connections (you probably didn't know these)
- None detected - all connections are within the same source files.

## Communities (21 total, 0 thin omitted)

### Community 0 - "Widget Interaction Layer"
Cohesion: 0.15
Nodes (10): cellSelected(), loadThemeFromFile(), mouseMoveEvent(), mousePressEvent(), paintEvent(), recalculateGrid(), resizeEvent(), setTheme() (+2 more)

### Community 1 - "Emulator Render Engine"
Cohesion: 0.19
Nodes (9): applySgr(), cellAt(), clearScreen(), feedOutput(), index(), lineText(), moveCursor(), newline() (+1 more)

### Community 2 - "Session Process Control"
Cohesion: 0.39
Nodes (5): isRunning(), start(), TerminalSession(), terminate(), writeInput()

### Community 3 - "Scrollback Buffer Logic"
Cohesion: 0.43
Nodes (4): pushLine(), setMaxLines(), size(), slice()

### Community 5 - "Theme JSON IO"
Cohesion: 0.6
Nodes (3): loadFromFile(), loadFromJson(), readColor()

## Suggested Questions
_Not enough signal to generate questions. This usually means the corpus has no AMBIGUOUS edges, no bridge nodes, no INFERRED relationships, and all communities are tightly cohesive. Add more files or run with --mode deep to extract richer edges._