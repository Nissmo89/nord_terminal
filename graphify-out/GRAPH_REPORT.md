# Graph Report - nord_terminal  (2026-05-15)

## Corpus Check
- 21 files · ~26,305 words
- Verdict: corpus is large enough that graph structure adds value.

## Summary
- 176 nodes · 326 edges · 25 communities (24 shown, 1 thin omitted)
- Extraction: 99% EXTRACTED · 1% INFERRED · 0% AMBIGUOUS · INFERRED: 3 edges (avg confidence: 0.8)
- Token cost: 0 input · 0 output

## Community Hubs (Navigation)
- [[_COMMUNITY_Community 0|Community 0]]
- [[_COMMUNITY_Community 1|Community 1]]
- [[_COMMUNITY_Community 3|Community 3]]
- [[_COMMUNITY_Community 4|Community 4]]
- [[_COMMUNITY_Community 5|Community 5]]
- [[_COMMUNITY_Community 6|Community 6]]
- [[_COMMUNITY_Community 7|Community 7]]
- [[_COMMUNITY_Community 8|Community 8]]
- [[_COMMUNITY_Community 10|Community 10]]
- [[_COMMUNITY_Community 11|Community 11]]
- [[_COMMUNITY_Community 12|Community 12]]

## God Nodes (most connected - your core abstractions)
1. `handleCsi()` - 20 edges
2. `feedByte()` - 15 edges
3. `start()` - 12 edges
4. `makeEraseCell()` - 10 edges
5. `putCodepoint()` - 9 edges
6. `scrollUp()` - 8 edges
7. `markDirtyAll()` - 8 edges
8. `markDirtyRow()` - 8 edges
9. `index()` - 8 edges
10. `feedOutput()` - 7 edges

## Surprising Connections (you probably didn't know these)
- `terminal()` --calls--> `defaultTerminalFont()`  [INFERRED]
  include/nordterminal/TerminalTheme.h → src/terminal/TerminalTheme.cpp
- `startShell()` --calls--> `TerminalScrollback()`  [INFERRED]
  src/terminal/TerminalWidget.cpp → src/terminal/TerminalScrollback.cpp
- `flushPendingSessionOutput()` --calls--> `TerminalScrollback()`  [INFERRED]
  src/terminal/TerminalWidget.cpp → src/terminal/TerminalScrollback.cpp

## Communities (25 total, 1 thin omitted)

### Community 0 - "Community 0"
Cohesion: 0.12
Nodes (32): TerminalScrollback(), cellSelected(), consumeSessionOutput(), cursorViewportRect(), flushPendingSessionOutput(), focusInEvent(), focusOutEvent(), keyPressEvent() (+24 more)

### Community 1 - "Community 1"
Cohesion: 0.22
Nodes (20): buildWindowsEnvironmentBlock(), closeHandleSafely(), closeMasterPty(), ConPtyState(), finalizeChildExit(), flushPendingWriteBuffer(), formatHResultError(), formatWin32Error() (+12 more)

### Community 3 - "Community 3"
Cohesion: 0.24
Nodes (11): deleteLines(), effectiveParam(), handleCsi(), insertLines(), parseCsiParameters(), recordViewportScroll(), rowData(), scrollDown() (+3 more)

### Community 4 - "Community 4"
Cohesion: 0.24
Nodes (10): activeCharset(), codepointDisplayWidth(), feedByte(), feedUtf8Byte(), flushIncompleteUtf8(), handleEscapeIntermediateFinal(), handleOsc(), isFinalCsiByte() (+2 more)

### Community 5 - "Community 5"
Cohesion: 0.29
Nodes (10): clearScreen(), defaultLineFeedNewLineMode(), markDirtyAll(), moveCursor(), reset(), resetScrollRegion(), resize(), setPrivateMode() (+2 more)

### Community 6 - "Community 6"
Cohesion: 0.36
Nodes (10): clearLine(), deleteChars(), eraseChars(), eraseInDisplay(), eraseInLine(), index(), insertBlankChars(), makeEraseCell() (+2 more)

### Community 7 - "Community 7"
Cohesion: 0.33
Nodes (7): ansi256ToColor(), applySgr(), cellAt(), feedOutput(), lineText(), newline(), putCharacter()

### Community 8 - "Community 8"
Cohesion: 0.48
Nodes (5): lineText(), pushLine(), setMaxLines(), size(), slice()

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

- **Why does `TerminalScrollback()` connect `Community 0` to `Community 8`?**
  _High betweenness centrality (0.016) - this node is a cross-community bridge._
- **Should `Community 0` be split into smaller, more focused modules?**
  _Cohesion score 0.12 - nodes in this community are weakly interconnected._
- **Should `Community 2` be split into smaller, more focused modules?**
  _Cohesion score 0.12 - nodes in this community are weakly interconnected._