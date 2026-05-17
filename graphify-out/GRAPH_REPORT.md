# Graph Report - nord_terminal  (2026-05-16)

## Corpus Check
- 21 files · ~25,107 words
- Verdict: corpus is large enough that graph structure adds value.

## Summary
- 190 nodes · 378 edges · 25 communities (24 shown, 1 thin omitted)
- Extraction: 99% EXTRACTED · 1% INFERRED · 0% AMBIGUOUS · INFERRED: 3 edges (avg confidence: 0.8)
- Token cost: 0 input · 0 output

## Community Hubs (Navigation)
- [[_COMMUNITY_Community 0|Community 0]]
- [[_COMMUNITY_Community 1|Community 1]]
- [[_COMMUNITY_Community 2|Community 2]]
- [[_COMMUNITY_Community 3|Community 3]]
- [[_COMMUNITY_Community 4|Community 4]]
- [[_COMMUNITY_Community 5|Community 5]]
- [[_COMMUNITY_Community 6|Community 6]]
- [[_COMMUNITY_Community 7|Community 7]]
- [[_COMMUNITY_Community 9|Community 9]]
- [[_COMMUNITY_Community 10|Community 10]]
- [[_COMMUNITY_Community 11|Community 11]]
- [[_COMMUNITY_Community 12|Community 12]]

## God Nodes (most connected - your core abstractions)
1. `handleCsi()` - 20 edges
2. `feedByte()` - 15 edges
3. `makeEraseCell()` - 12 edges
4. `start()` - 12 edges
5. `putCodepoint()` - 11 edges
6. `index()` - 10 edges
7. `traceLog()` - 10 edges
8. `scrollUp()` - 9 edges
9. `flushPendingSessionOutput()` - 9 edges
10. `markDirtyAll()` - 8 edges

## Surprising Connections (you probably didn't know these)
- `terminal()` --calls--> `defaultTerminalFont()`  [INFERRED]
  include/nordterminal/TerminalTheme.h → src/terminal/TerminalTheme.cpp
- `startShell()` --calls--> `TerminalScrollback()`  [INFERRED]
  src/terminal/TerminalWidget.cpp → src/terminal/TerminalScrollback.cpp
- `flushPendingSessionOutput()` --calls--> `TerminalScrollback()`  [INFERRED]
  src/terminal/TerminalWidget.cpp → src/terminal/TerminalScrollback.cpp

## Communities (25 total, 1 thin omitted)

### Community 0 - "Community 0"
Cohesion: 0.1
Nodes (44): TerminalScrollback(), cellSelected(), consumeSessionOutput(), cursorViewportRect(), escapedBytePreview(), flushPendingSessionOutput(), focusInEvent(), focusOutEvent() (+36 more)

### Community 1 - "Community 1"
Cohesion: 0.22
Nodes (20): buildWindowsEnvironmentBlock(), closeHandleSafely(), closeMasterPty(), ConPtyState(), finalizeChildExit(), flushPendingWriteBuffer(), formatHResultError(), formatWin32Error() (+12 more)

### Community 2 - "Community 2"
Cohesion: 0.11
Nodes (3): cellAt(), codepointDisplayWidth(), lineText()

### Community 3 - "Community 3"
Cohesion: 0.19
Nodes (14): deleteLines(), effectiveParam(), eraseInLine(), handleCsi(), insertLines(), lineHasVisibleGlyph(), markDirtyRange(), parseCsiParameters() (+6 more)

### Community 4 - "Community 4"
Cohesion: 0.31
Nodes (10): defaultLineFeedNewLineMode(), eraseInDisplay(), markDirtyAll(), moveCursor(), reset(), resetScrollRegion(), resize(), setPrivateMode() (+2 more)

### Community 5 - "Community 5"
Cohesion: 0.6
Nodes (10): clearLine(), clearWideCellAt(), deleteChars(), eraseChars(), index(), insertBlankChars(), makeEraseCell(), markDirtyRow() (+2 more)

### Community 6 - "Community 6"
Cohesion: 0.25
Nodes (8): activeCharset(), feedByte(), feedUtf8Byte(), flushIncompleteUtf8(), handleEscapeIntermediateFinal(), handleOsc(), isFinalCsiByte(), mapDecSpecialGraphicsChar()

### Community 7 - "Community 7"
Cohesion: 0.48
Nodes (5): lineText(), pushLine(), setMaxLines(), size(), slice()

### Community 10 - "Community 10"
Cohesion: 0.4
Nodes (6): ansi256ToColor(), applySgr(), clearScreen(), feedOutput(), newline(), putCharacter()

### Community 11 - "Community 11"
Cohesion: 0.6
Nodes (3): loadFromFile(), loadFromJson(), readColor()

### Community 12 - "Community 12"
Cohesion: 0.7
Nodes (4): discoverThemes(), main(), preferredThemeIndex(), themeDirectoryCandidates()

## Knowledge Gaps
- **1 thin communities (<3 nodes) omitted from report** — run `graphify query` to explore isolated nodes.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `TerminalScrollback()` connect `Community 0` to `Community 7`?**
  _High betweenness centrality (0.018) - this node is a cross-community bridge._
- **Should `Community 0` be split into smaller, more focused modules?**
  _Cohesion score 0.1 - nodes in this community are weakly interconnected._
- **Should `Community 2` be split into smaller, more focused modules?**
  _Cohesion score 0.11 - nodes in this community are weakly interconnected._