const { app } = require('@azure/functions');
const { getPool } = require('./db');

const HOST_KEY = process.env.INGEST_HOST_KEY;

app.http('ingest', {
  methods: ['POST'],
  authLevel: 'anonymous',
  route: 'ingest',
  handler: async (req, ctx) => {
    // Authenticate via x-functions-key header
    const provided = req.headers.get('x-functions-key');
    if (!provided || provided !== HOST_KEY) {
      ctx.warn('ingest: rejected request — missing or invalid x-functions-key');
      return { status: 200, jsonBody: { ok: false } };
    }

    let body;
    try {
      body = await req.json();
    } catch {
      ctx.warn('ingest: invalid JSON body');
      return { status: 200, jsonBody: { ok: false } };
    }

    const { state, readings } = body;

    if (!Array.isArray(readings) || readings.length < 1) {
      ctx.warn('ingest: readings missing or empty');
      return { status: 200, jsonBody: { ok: false } };
    }

    try {
      await getPool().query(
        `INSERT INTO tc_log
           (received_at, device_id, state, sample_interval_ms, batch_start, readings)
         VALUES
           (NOW(), $1, $2, $3, NULL, $4::smallint[])`,
        [
          'kiln-01',
          typeof state === 'string' ? state : null,
          1000,
          readings,
        ]
      );
      return { status: 200, jsonBody: { ok: true } };
    } catch (err) {
      ctx.error('ingest: DB insert failed:', err.message);
      return { status: 200, jsonBody: { ok: false } };
    }
  },
});
