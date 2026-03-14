#!/bin/sh
# Generate runtime config from environment variables
# This allows docker-compose to control frontend feature flags without rebuilding

cat > /usr/share/nginx/html/config.js << EOF
window.__APP_CONFIG__ = {
  enableEacMenu: ${ENABLE_EAC_MENU:-false}
};
EOF

exec nginx -g "daemon off;"
