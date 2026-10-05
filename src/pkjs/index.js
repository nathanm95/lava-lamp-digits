var Clay = require('@rebble/clay');
var clayConfig = require('./config');

// Clay renders the settings page, remembers the answers on the phone and posts
// them to the watch keyed by the messageKeys in package.json.
var clay = new Clay(clayConfig, null, { autoHandleEvents: true });
