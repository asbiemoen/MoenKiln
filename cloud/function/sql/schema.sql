-- Run once against the miln database as a superuser, then grant to miln_app.

CREATE TABLE IF NOT EXISTS firing_log (
  id          BIGSERIAL    PRIMARY KEY,
  firing_id   INTEGER      NOT NULL,
  ts          TIMESTAMPTZ  NOT NULL DEFAULT NOW(),
  sec_elapsed INTEGER,
  temp_c      NUMERIC(6,1),
  setpoint_c  NUMERIC(6,1),
  relay       BOOLEAN,
  duty_pct    SMALLINT,
  segment     TEXT,
  is_err      BOOLEAN      NOT NULL DEFAULT FALSE
);

CREATE INDEX IF NOT EXISTS idx_firing_log_firing_id_ts
  ON firing_log(firing_id, ts);

-- TC continuous temperature log (from MILN tc_log.h, always active when enabled)
CREATE TABLE IF NOT EXISTS tc_log (
  id                BIGSERIAL    PRIMARY KEY,
  received_at       TIMESTAMPTZ  NOT NULL DEFAULT NOW(),
  device_id         TEXT         NOT NULL,
  batch_start       TIMESTAMPTZ,
  state             TEXT,
  sample_interval_ms INTEGER     NOT NULL DEFAULT 1000,
  readings          SMALLINT[]   NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_tc_log_device_received
  ON tc_log(device_id, received_at);

CREATE TABLE IF NOT EXISTS auth_tokens (
  token      TEXT        PRIMARY KEY,
  expires_at TIMESTAMPTZ NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_auth_tokens_expires
  ON auth_tokens(expires_at);

-- Dedicated app user (create password in Azure Portal / psql)
-- CREATE USER miln_app WITH PASSWORD 'change-me';
-- GRANT CONNECT ON DATABASE miln TO miln_app;
-- GRANT USAGE ON SCHEMA public TO miln_app;
-- GRANT INSERT, SELECT ON firing_log TO miln_app;
-- GRANT INSERT, SELECT ON tc_log TO miln_app;
-- GRANT INSERT, SELECT, DELETE ON auth_tokens TO miln_app;
-- GRANT USAGE, SELECT ON ALL SEQUENCES IN SCHEMA public TO miln_app;
