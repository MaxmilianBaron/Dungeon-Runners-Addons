import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";
import { fileURLToPath } from "node:url";
import { createTestHarness } from "wrangler";

test("D1 synchronizes listings and protects updates", async (t) => {
    const service = createTestHarness({
        workers: [{ configPath: fileURLToPath(new URL("./wrangler.jsonc", import.meta.url)) }],
    });
    t.after(() => service.close());
    await service.listen();
    const { DB: db } = await service.getWorker().getEnv();
    for (const file of ["0001_offers.sql", "0002_wanted_options.sql"]) {
        const migration = await readFile(new URL("./migrations/" + file, import.meta.url), "utf8");
        await db.batch(migration.split(";").filter(part => part.trim()).map(sql => db.prepare(sql.trim())));
    }

    const testAddresses = new Map();
    async function call(path, method = "GET", token, data) {
        const result = await service.fetch("/v1/" + path, {
            method,
            headers: {
                ...(token ? { authorization: "Bearer " + token } : {}),
                ...(testAddresses.has(token) ? { "cf-connecting-ip": testAddresses.get(token) } : {}),
                ...(data ? { "content-type": "application/json" } : {}),
            },
            body: data ? JSON.stringify(data) : undefined,
        });
        return [result.status, await result.json()];
    }

    assert.deepEqual(await call("health"), [200, { version: 1 }]);
    const [issued, alice] = await call("identity", "POST");
    const [, bob] = await call("identity", "POST");
    testAddresses.set(alice.token, "192.0.2.1");
    testAddresses.set(bob.token, "192.0.2.2");
    assert.equal(issued, 201);
    assert.match(alice.token, /^[0-9a-f]{64}$/);
    assert.notEqual(alice.token, bob.token);

    const id = "c".repeat(32);
    const catalogMatch = { definition: "demo.items.armor", signature: "" };
    const listing = {
        revision: 0, character: "DemoRanger", wanted: "Demo Armor", match: catalogMatch,
        item: { definition: "demo.items.bow", name: "Demo Bow", signature: "a".repeat(64), stats: ["Damage 10–25", "+4 Dexterity"], rollTier: 3 },
    };
    assert.equal((await call("offers/" + id, "PUT", undefined, listing))[0], 401);
    assert.equal((await call("offers/" + id, "PUT", alice.token, { ...listing, wanted: "25 gold", match: undefined }))[0], 400);
    assert.equal((await call("offers/" + id, "PUT", alice.token, listing))[0], 201);
    assert.equal((await call("offers/" + id, "PUT", alice.token, listing))[0], 200);
    const [, first] = await call("offers");
    assert.equal(first.offers.length, 1);
    assert.equal(first.pageCount, 1);
    assert.deepEqual(first.offers[0].item, listing.item);
    assert.equal(first.offers[0].character, listing.character);
    assert.equal(JSON.stringify(first).includes(alice.token), false);
    assert.equal("owner" in first.offers[0], false);
    assert.deepEqual((await call("wanted"))[1], { wanted: [], next: "" });
    assert.deepEqual((await call("tracked?ids=" + id))[1], { tracked: [] });

    assert.equal((await call("offers?mine=1", "GET", bob.token))[1].offers.length, 0);
    assert.equal((await call("offers/" + id, "PUT", bob.token, { revision: 1, wanted: listing.wanted, match: catalogMatch }))[0], 404);
    const match = { definition: "demo.items.bow", signature: "b".repeat(64) };
    const update = { revision: 1, wanted: "Another demo bow", match };
    assert.equal((await call("offers/" + id, "PUT", alice.token, update))[0], 200);
    assert.equal((await call("offers/" + id, "PUT", alice.token, update))[0], 200);
    assert.equal((await call("offers/" + id, "PUT", alice.token, { revision: 1, wanted: "Stale edit", match: catalogMatch }))[0], 409);
    assert.equal((await call("offers", "GET", bob.token))[1].offers[0].wanted, update.wanted);
    assert.deepEqual((await call("wanted"))[1], { wanted: [match], next: "" });
    assert.deepEqual((await call("tracked?ids=" + id))[1], { tracked: [{
        id, character: listing.character, offered: listing.item.name, definition: match.definition, signature: match.signature,
    }] });
    assert.equal((await call("offers/" + id, "PUT", alice.token, { revision: 2, closed: true }))[0], 200);
    assert.equal((await call("offers"))[1].offers.length, 0);
    assert.deepEqual((await call("wanted"))[1], { wanted: [], next: "" });
    assert.equal((await call("offers?mine=1", "GET", alice.token))[1].offers.length, 0);
    assert.deepEqual((await call("tracked?ids=" + id))[1], { tracked: [] });
    assert.equal((await call("tracked?ids=" + id + "." + id))[0], 400);
    assert.equal((await db.prepare("SELECT closed FROM offers WHERE id=?").bind(id).first()).closed, 1);
    assert.equal((await call("offers/" + "d".repeat(32), "PUT", alice.token, { ...listing, item: { ...listing.item, stats: [] } }))[0], 400);

    for (let index = 0; index < 8; index++) {
        const next = index.toString(16).padStart(32, "0");
        const wanted = index < 2 ? { ...listing, match } : { ...listing, match: { definition: match.definition, signature: "" } };
        assert.equal((await call("offers/" + next, "PUT", alice.token, wanted))[0], 201);
    }
    assert.deepEqual((await call("wanted"))[1], { wanted: [match], next: "" });
    assert.deepEqual((await call("wanted?after=" + match.signature))[1], { wanted: [], next: "" });
    const [, page] = await call("offers");
    const [, last] = await call("offers?before=" + page.next);
    assert.equal(page.offers.length, 6);
    assert.equal(last.offers.length, 2);
    assert.equal(page.pageCount, 2);
    assert.equal(last.pageCount, 2);
    assert.equal((await call("offers?mine=1", "GET", bob.token))[1].pageCount, 0);
    assert.equal(new Set([...page.offers, ...last.offers].map(offer => offer.id)).size, 8);
    await db.prepare("UPDATE offers SET expires_at=1 WHERE wanted_signature=?").bind(match.signature).run();
    assert.deepEqual((await call("wanted"))[1], { wanted: [], next: "" });

    const legacy = "e".repeat(32);
    const now = Math.floor(Date.now() / 1000);
    await db.prepare(
        "INSERT INTO offers (id, owner, character, item, wanted, updated_at, expires_at)" +
        " SELECT ?, owner, character, item, ?, ?, ? FROM offers WHERE id=?"
    ).bind(legacy, "25 gold", now + 60, now + 3600, id).run();
    assert.equal((await call("offers"))[1].offers.some(offer => offer.id === legacy), false);
    const legacyOffer = (await call("offers?mine=1", "GET", alice.token))[1].offers.find(offer => offer.id === legacy);
    assert.equal(legacyOffer.wanted, "");
    assert.equal(legacyOffer.wantedDefinition, "");
    assert.equal((await call("offers/" + legacy, "PUT", alice.token, { revision: 1, wanted: listing.wanted, match: catalogMatch }))[0], 200);
    assert.equal((await call("offers"))[1].offers.some(offer => offer.id === legacy), true);

    const requestId = "f".repeat(32);
    const options = ["Mythic", "Primary Stats · Endurance + Agility", "Resistances · Fire + Ice"];
    const request = { ...listing, item: null, match: { ...catalogMatch, options } };
    assert.equal((await call("offers/" + requestId, "PUT", bob.token, request))[0], 201);
    const openTrade = { ...listing, wanted: "", match: null };
    assert.equal((await call("offers/" + requestId, "PUT", bob.token, { revision: 1, wanted: "", match: null }))[0], 400);
    const wantedRequest = (await call("offers"))[1].offers.find(offer => offer.id === requestId);
    assert.equal(wantedRequest.item, null);
    assert.deepEqual(wantedRequest.wantedOptions, options);
    assert.deepEqual((await call("wanted"))[1], { wanted: [], next: "" });
    assert.equal((await call("offers/" + requestId, "PUT", bob.token, { revision: 1, closed: true }))[0], 200);
    assert.equal((await call("offers?mine=1", "GET", bob.token))[1].offers.length, 0);
    const openId = "9".repeat(32);
    const { rollTier, ...legacyItem } = listing.item;
    assert.equal((await call("offers/" + openId, "PUT", bob.token, { ...openTrade, item: null }))[0], 400);
    assert.equal((await call("offers/" + openId, "PUT", bob.token, { ...openTrade, item: { ...legacyItem, rollTier: rollTier+1 } }))[0], 400);
    assert.equal((await call("offers/" + openId, "PUT", bob.token, { ...openTrade, item: legacyItem }))[0], 201);
    const openOffer = (await call("offers"))[1].offers.find(offer => offer.id === openId);
    assert.equal(openOffer.wanted, "");
    assert.deepEqual(openOffer.item, legacyItem);
    assert.deepEqual((await call("tracked?ids=" + openId))[1], { tracked: [] });
});
