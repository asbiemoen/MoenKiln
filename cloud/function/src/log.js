const { app } = require('@azure/functions');
const { getPool } = require('./db');

app.http('log', {
  methods: ['POST'],
  authLevel: 'anonymous',
  route: 'log',
  handler: async (req, ctx) => {
    if (req.headers.get('x-kiln-key') !== process.env.KILN_API_KEY) {
      return { status: 401, body: 'Unauthorized' };
    }

    if (!req.headers.get('content-type')?.includes('application/json')) {
      return { status: 400, body: 'Content-Type must be application/json' };
    }

    let body;
    try { body = await req.json(); } catch { return { status: 400, body: 'Invalid JSON' }; }

    const { firing_id, sec, temp, sp, relay, duty, segment, is_err } = body;

    if (firing_id === undefined || temp === undefined) {
      return { status: 400, body: 'Missing required fields: firing_id, temp' };
    }

    if (typeof temp !== 'number' || temp < -50 || temp > 1400) {
      return { status: 400, body: 'temp out of range' };
    }

    try {
      await getPool().query(
        `INSERT INTO firing_log
           (firing_id, sec_elapsed, temp_c, setpoint_c, relay, duty_pct, segment, is_err)
         VALUES ($1, $2, $3, $4, $5, $6, $7, $8)`,
        [
          Number(firing_id),
          sec != null ? Number(sec) : null,
          Number(temp),
          sp  != null ? Number(sp)  : null,
          relay  ?? false,
          duty != null ? Number(duty) : null,
          segment ?? null,
          is_err ?? false,
        ]
      );
      return { status: 200, body: 'ok' };
    } catch (err) {
      ctx.log.error('DB insert error:', err.message);
      return { status: 500, body: 'Database error' };
    }
  },
});
