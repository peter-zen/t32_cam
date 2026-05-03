# Bootstrap Review - 2026-05-03

## What existed before

- Root `AGENTS.md`: comprehensive coding guidelines (no knowledge rules)
- Root `README.md`: minimal build instructions
- `doc/knowledge/spec/`: one file (`tcp_event_heartbeat_spec.md`)
- `doc/`: ~18 scattered analysis/design/report markdown files, plus subdirectories (`analysis/`, `design/`, `job/`, `ref/`, `reference/`, `review/`, `roadmap/`, `rtsp/`, `solution/`, `spec/`)

## What was created or normalized

- `doc/knowledge/README.md`: knowledge guide with reading order and directory guide
- `doc/knowledge/overview.md`: project overview (architecture, modules, constraints)
- `doc/knowledge/working-set.md`: current focus and read-first docs
- `doc/knowledge/todo.md`: Now / Next / Later / Blocked
- `doc/knowledge/specs/`: moved from `spec/` (contains `tcp_event_heartbeat_spec.md`)
- `doc/knowledge/decisions/`: created (empty)
- `doc/knowledge/bugs/`: created (empty)
- `doc/knowledge/playbooks/`: created
- `doc/knowledge/refs/`: created (empty)
- `doc/knowledge/inbox/`: created (empty)
- `doc/knowledge/reviews/`: created
- Root `AGENTS.md`: added "Project knowledge rules" section pointing to `doc/knowledge/`

## What sample doc was seeded

- `doc/knowledge/playbooks/local-build-and-run.md`: simulation and hardware build, run, and test procedures

## What should be migrated next

- Review `doc/` root analysis reports for relevance; promote valid ones to `specs/`, `decisions/`, or `bugs/`
- Add mDNS and HTTP API specs under `doc/knowledge/specs/`
- Add RTSP debugging playbook
- Document camera service API contract under `specs/`
