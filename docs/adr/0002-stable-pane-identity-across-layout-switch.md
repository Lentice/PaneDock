# Panes are stable identities; layout only picks which panes are visible

Switching the layout template once merged shrunk-out panes' tabs into a
surviving pane, so a pane's tabs could migrate to another pane and a later grow
rebuilt the pane empty. PaneDock treats panes as up to four stable identities
per Group that permanently own their tabs; the template merely decides which
are visible and how they are arranged. Shrinking hides a pane (and de-realizes
its live view, per NFR-002) without moving its tabs; growing back reveals the
same identity with its tabs intact. Chosen over merging because migrating tabs
broke the "tabs stay where they were" mental model and made a shrink/grow round
trip lose each pane's remembered content.
