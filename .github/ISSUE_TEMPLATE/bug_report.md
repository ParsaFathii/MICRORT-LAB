---
name: Bug report
about: Something behaves incorrectly or crashes
title: "[bug] "
labels: bug
assignees: ''
---

**What happened?**

A clear, concise description of the incorrect behavior.

**How to reproduce**

1. Command / UI steps that triggered it
2. The experiment or simulation id, if applicable
3. What you expected instead

**Environment**

- Component: engine / kernel / services/api / web workstation
- OS and compiler: (e.g. Ubuntu 24.04, gcc-13)
- Python: (e.g. 3.12)
- Version / commit: (e.g. v1.0.0)

**Evidence**

```
Paste the command, output, error text, or a screenshot here.
For engine issues include the config JSON (or the experiment id).
```

**Determinism note**

MicroRT-Lab results are byte-deterministic for a given config + seed
(modulo `wallMicros`). If you report a determinism mismatch, please
attach both result documents.
