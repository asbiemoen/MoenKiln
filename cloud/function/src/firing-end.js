const { app } = require('@azure/functions');
const { getPool } = require('./db');

const VALID_REASONS = ['completed', 'stopped', 'estop', 'disconnected'];

// Called once by MILN when a firing ends (completed / stopped / e-stopped).
// Idempotent UPSERT so WiFi retries can't create duplicates or races.
app.http('firing-end', {
  methods: ['POST'],
  authLevel: 'anonymous',
  route: 'firing-end',
  handler: async (req, ctx) => {
    if (req.headers.get('x-kiln-key') !== process.env.KILN_API_KEY) {
      return { status: 401, body: 'Unauthorized' };
    }

    if (!req.headers.get('content-type')?.includes('application/json')) {
      return { status: 400, body: 'Content-Type must be application/json' };
    }

    let body;
    try { body = await req.json(); } catch { return { status: 400, body: 'Invalid JSON' }; }

    const { firing_id, reason } = body;

    // Strict: only a real positive integer, never a coercible value (true → 1).
    if (typeof firing_id !== 'number' || !Number.isInteger(firing_id) || firing_id < 1) {
      return { status: 400, body: 'Missing or invalid firing_id' };
    }

    // Reject unknown reasons rather than silently masking e.g. an estop as completed.
    if (reason !== undefined && !VALID_REASONS.includes(reason)) {
      return { status: 400, body: 'Invalid reason' };
    }
    const endReason = reason || 'completed';

    try {
      await getPool().query(
        `INSERT INTO firing_meta (firing_id, ended_at, end_reason, updated_at)
         VALUES ($1, now(), $2, now())
         ON CONFLICT (firing_id) DO UPDATE
           SET ended_at   = EXCLUDED.ended_at,
               end_reason = EXCLUDED.end_reason,
               updated_at = now()`,
        [firing_id, endReason]
      );
      return { status: 200, body: 'ok' };
    } catch (err) {
      ctx.error('firing-end upsert error:', err.message);
      return { status: 500, body: 'Database error' };
    }
  },
});
