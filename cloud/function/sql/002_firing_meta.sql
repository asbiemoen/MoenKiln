-- Migration 002: explicit firing-end metadata.
-- Run once against the miln database as the table owner, then GRANT to miln_app.
-- Additive and online — safe to run on a live database (no downtime).

-- One row per firing, written by POST /api/firing-end when MILN ends a firing.
-- Kept separate from the append-only firing_log measurement series so that
-- end metadata never pollutes aggregates (MAX(temp_c), COUNT(*), ...).
CREATE TABLE IF NOT EXISTS firing_meta (
  firing_id   INTEGER     PRIMARY KEY,
  ended_at    TIMESTAMPTZ NOT NULL DEFAULT now(),
  end_reason  TEXT        NOT NULL DEFAULT 'completed'
    CHECK (end_reason IN ('completed', 'stopped', 'estop', 'disconnected')),
  updated_at  TIMESTAMPTZ NOT NULL DEFAULT now()
);

-- firing_id is client-supplied (not SERIAL/IDENTITY), so no sequence grant needed.
GRANT SELECT, INSERT, UPDATE, DELETE ON firing_meta TO miln_app;
