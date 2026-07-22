const nodemailer = require('nodemailer');

let transport;

// Domeneshop's relay: STARTTLS on 587 (port 25 is blocked outbound on Azure).
function getTransport() {
  if (!transport) {
    transport = nodemailer.createTransport({
      host: process.env.SMTP_HOST,
      port: Number(process.env.SMTP_PORT || 587),
      secure: false,
      requireTLS: true,
      auth: { user: process.env.SMTP_USER, pass: process.env.SMTP_PASS },
      connectionTimeout: 10000,
      greetingTimeout: 10000,
      socketTimeout: 20000,
    });
  }
  return transport;
}

function sendMail({ to, subject, html }) {
  return getTransport().sendMail({
    from: process.env.MAIL_FROM || 'Moen Kiln <app@moenadvisory.no>',
    to,
    subject,
    html,
  });
}

module.exports = { sendMail };
