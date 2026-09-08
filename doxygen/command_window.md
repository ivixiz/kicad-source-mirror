# Command Window

The PCB and Schematic Editors expose **View → Panels → Command Window**. Commands
operate on the editor's current, live document, including unsaved changes. They do
not directly rewrite or save `.kicad_pcb` or `.kicad_sch` files. Save the document using
the usual editor commands when desired.

Each editor window has its own executor, output and session history. Enter runs a
command; Up and Down browse history and restore the unfinished input line. `help`
lists commands available in that editor; `help <command>` describes their syntax.

## Syntax and coordinates

Command names are lowercase. Arguments are separated by spaces or tabs. Use single
or double quotes for arguments containing spaces; a backslash escapes the following
character. Empty quoted arguments are preserved. There is no shell, expression
expansion, scripting, or command chaining. Each submission contains one command.

Examples in the PCB Editor:

```
context
help pcb.move
pcb.list_footprints
pcb.get R1
pcb.move R1 25.4 50.8
pcb.move R1 1000 2000 mil
pcb.rotate R1 90
pcb.list_nets
```

`pcb.move <reference> <x> <y> [mm|mil|in]` sets an absolute position, using the board's
internal origin and axes (positive Y down), independent of the displayed user origin
or units. The default unit is millimetres. `pcb.rotate <reference> <degrees>` rotates
about the footprint's own anchor, with positive angles counterclockwise. Numbers use
a dot as their decimal separator. Invalid numbers, coordinates outside the supported
range, ambiguous references, locked footprints and busy editors return errors before
editing. A no-op creates no undo entry. Each successful change is one ordinary KiCad
undo step, and participates in the existing connectivity and view update pipeline.

Examples in the Schematic Editor:

```
context
schematic.list_symbols
schematic.get U1
```

Schematic queries include hierarchical sheet instances and symbol units. They return
stable symbol UUIDs together with sheet paths so callers can distinguish occurrences
of a shared screen. A reference lookup returns every matching unit/instance; it does
not silently select one. Querying does not annotate symbols or repair instance data.

## Rule checks

`drc.run` and `erc.run` execute the native checks against the live editor model and
open the existing progress/results dialog. They reuse its current check options,
exclusions, marker handling and cancellation behavior. DRC retains zone-refill and
schematic-parity options; ERC retains power-symbol annotation and connection cleanup.
These are diagnostic workflows with the same side effects as the existing Run buttons.

Results include `completed`, error/warning/exclusion counts, and a distinct status
for cancellation or failure. Completed means execution finished, even when violations
were found. DRC also returns violation records with affected-item IDs; ERC reports
`annotation_required` so a caller can detect incomplete annotation. Cancelled runs
report partial counts. `changed` is true when a check replaces the live diagnostic
state; it does not mean the document was saved. Invalid DRC rules retain prior markers.

The command providers call inspection-tool methods; they do not implement check
algorithms or manipulate dialog widgets. Check workflows reject nested runs and
prevent their editor or dialog from being destroyed while checks are on the stack.

## Architecture and extension points

`include/command/command.h` defines the reusable command service in `KICAD_COMMAND`:

- `PARSER` converts text into a `REQUEST` without executing anything.
- `REGISTRY` owns named `DESCRIPTOR`s, including usage, description, editor scope,
  argument bounds, effects and handler. Registration is explicit per editor, avoiding
  initialization-order and cross-KIFACE dependencies.
- `EXECUTOR` owns the registry and an editor `CONTEXT`. It performs name, scope, arity
  and readiness checks before dispatch, and prevents reentrant execution. Its
  `Execute(REQUEST)` entry point serves structured callers through the same path.
- `CONTEXT` is the adapter boundary. Providers retain the owning editor frame and
  resolve its document, tool manager and selection for each invocation. They never
  retain document-item pointers across commands.
- `RESULT` separates status, readable message, JSON data and a changed flag.
  `ToJson()` serializes the envelope. Payloads contain values and stable identifiers,
  never addresses or widget handles. An error must not be inferred from message text.
- `HISTORY` implements bounded, session-local input navigation independently of widgets.

The PCB provider is `pcbnew/commands/pcb_commands.cpp`; the schematic provider is
`eeschema/commands/schematic_commands.cpp`. Editor-specific dependencies remain in
those modules. The shared `COMMAND_WINDOW` widget owns an executor and renders its
results. Menu actions and pane persistence use the existing `COMMON_CONTROL`,
`ACTIONS`, `EDA_PANE` and application-settings infrastructure.

To add a command, register a descriptor in the appropriate provider and implement a
handler against the existing live model or a tool action. Use `BOARD_COMMIT` or
`SCH_COMMIT` for edits, validate the entire request before mutation, and revert an
unfinished commit if an exception escapes. Do not cache raw item pointers, bypass
undo, or add independent redraws after a commit that already updates the view.

Editor-backed execution is synchronous on the editor's UI thread. A future transport
must marshal requests to that thread and bind them to a specific live editor. The
executor's reentrancy guard is not a worker-thread synchronization primitive. There
is no external server, authentication layer, AI integration or arbitrary-code command
in this implementation. Additional providers can expose simulation, libraries,
placement or routing without introducing those concerns into the text widget.

Queries traverse only the relevant existing containers on demand. Nets use the
board's existing net index; hierarchy queries preserve cached sheet paths. There is
no polling or periodic document scan. History and visible output are bounded.

## Validation

`qa/tests/common/test_command.cpp` tests parser boundaries and quoting, registry
invariants, dispatch validation, busy/no-document handling, context freshness, JSON
serialization, reentrancy, exception recovery and history navigation.

For an editor smoke test, open an existing board and an annotated hierarchical
schematic, then:

1. Show/hide the panel through View → Panels and the pane close button. Restart and
   verify visibility/height persistence.
2. Query objects, edit them with the normal GUI, and repeat queries to verify the
   new in-memory state. Switch documents in the same frame and repeat `context`.
3. Move and rotate a footprint, then Undo and Redo each command. Check the footprint,
   pads and ratsnest. Repeat a no-op and confirm no extra undo step appears.
4. Try a locked footprint, duplicate/missing reference, NaN/infinity, an out-of-range
   coordinate, bad arity, an unfinished quote and a command during an active edit.
5. Query a multiunit symbol and a repeated sheet, checking unit and sheet identifiers.
6. Type a draft, browse history in both directions, and verify the draft is restored.
7. Run `drc.run` and `erc.run`, comparing counts with the native results dialogs,
   including hidden severity filters and exclusions. Test cancellation, repeated
   runs, malformed DRC rules, and a close request during a check.
8. Select a schematic variant with value/footprint overrides and check query results.
