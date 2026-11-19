# Trade Auction API

The addon publishes the character name and selected wanted item when the player confirms a request, with the offered item included for item-for-item trades. The catalog shows modifier combinations from the installed game data. Inventory and bank contents stay in the game. An installation token controls edits to its offers. Exact drop alerts run locally against a periodically refreshed index of open requests with an exact item fingerprint.

## Local verification

```sh
npm ci
npm test
npm run migrate
npm run dev
```

## Deployment

Use Wrangler with the Cloudflare account that will host the service. Create a D1 database with `npx wrangler d1 create dungeon-runners-trade-auction`. Copy `wrangler.jsonc` to `wrangler.production.jsonc` and set `database_id` to the new database ID.

```sh
npx wrangler d1 migrations apply DB --remote --config wrangler.production.jsonc
npx wrangler deploy --config wrangler.production.jsonc
```

Deploy the Pages project with its service binding to the Trade Auction Worker.

```sh
cd pages
npx wrangler pages deploy ./public --force --project-name dungeon-runners-trade-auction --branch main
```

Check `GET /v1/health` on the Pages hostname in `Source/https_client.h`. The release manifest includes the rebuilt runtime and `Addons/TradeAuction/addon.ini`.
