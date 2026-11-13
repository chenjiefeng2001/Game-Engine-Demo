# P7-A G4 — AssetBrowser Dependency View Closure

**Status**: PASS — SCOPE-BOUND

- Asset Browser has **Show Dependencies** action and `DependencyList`.
- Rows display direction (`->` / `<-`), type, committed path, GUID, slot/role and unresolved state.
- Selection is based on the native JSON query; no filename guessing in UI.
- UIA test selects Mesh, invokes Show Dependencies, and asserts a Material dependency row exists.
- Reverse dependencies are returned by native query; the view renders both directions.
- Known limitation: double-click navigation to a dependency is not implemented in P7-A; the
  asset list/selection remains the navigation surface.
