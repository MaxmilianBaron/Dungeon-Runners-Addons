const day = 86400;
const duration = 30 * day;
const limit = 6;
const maxOffers = 24;
const encoder = new TextEncoder();

function response(value, status = 200) {
    return Response.json(value, {
        status,
        headers: { "Cache-Control": "no-store", "X-Content-Type-Options": "nosniff" },
    });
}

class RequestError extends Error {
    constructor(status, message) {
        super(message);
        this.status = status;
    }
}

async function parse(request) {
    if (request.headers.get("content-type")?.split(";")[0] !== "application/json")
        throw new RequestError(415, "Send a JSON request.");
    if (!request.body) throw new RequestError(400, "Missing request.");
    const reader = request.body.getReader();
    const data = new Uint8Array(12288);
    let length = 0;
    try {
        for (;;) {
            const { value, done } = await reader.read();
            if (done) break;
            if (length + value.length > data.length) {
                await reader.cancel();
                throw new RequestError(413, "The offer is too large.");
            }
            data.set(value, length);
            length += value.length;
        }
        try {
            return JSON.parse(new TextDecoder("utf-8", { fatal: true }).decode(data.subarray(0, length)));
        } catch {
            throw new RequestError(400, "Invalid JSON.");
        }
    } finally {
        reader.releaseLock();
    }
}

function text(value, maxLength, field) {
    if (typeof value !== "string" || !value.trim() || encoder.encode(value).length > maxLength ||
        /[\u0000-\u001f\u007f\u202a-\u202e\u2066-\u2069]/u.test(value))
        throw new RequestError(400, "Invalid " + field + ".");
    return value.trim();
}

function itemOf(value) {
    const definition = value?.definition;
    if (typeof definition !== "string" || !/^[a-z0-9_.-]{1,192}$/.test(definition) ||
        typeof value.signature !== "string" || !/^[0-9a-f]{64}$/.test(value.signature) ||
        !Array.isArray(value.stats) || !value.stats.length || value.stats.length > 28)
        throw new RequestError(400, "Invalid item.");
    const rollTier = value.rollTier;
    if (rollTier !== undefined && (!Number.isInteger(rollTier) || rollTier < 1 || rollTier > 3))
        throw new RequestError(400, "Invalid item roll tier.");
    return {
        definition,
        signature: value.signature,
        name: text(value.name, 160, "item name"),
        stats: value.stats.map(line => text(line, 192, "item stat")),
        ...(rollTier === undefined ? {} : { rollTier }),
    };
}

function wantedOf(value) {
    if (typeof value?.definition !== "string" || !/^[a-z0-9_.-]{1,192}$/.test(value.definition) ||
        typeof value.signature !== "string" || !(value.signature === "" || /^[0-9a-f]{64}$/.test(value.signature)))
        throw new RequestError(400, "Invalid wanted item.");
    const options = value.options ?? [];
    if (!Array.isArray(options) || options.length > 8 || (value.signature && options.length))
        throw new RequestError(400, "Invalid wanted item variant.");
    return { definition: value.definition, signature: value.signature, options: JSON.stringify(options.map(option => text(option, 192, "wanted variant"))) };
}

function hex(bytes) {
    return Array.from(new Uint8Array(bytes), value => value.toString(16).padStart(2, "0")).join("");
}

async function ownerOf(request) {
    const token = request.headers.get("authorization")?.match(/^Bearer ([0-9a-f]{64})$/)?.[1];
    if (!token) throw new RequestError(401, "A Trade Auction token is required.");
    return hex(await crypto.subtle.digest("SHA-256", encoder.encode(token)));
}

async function rateLimit(binding, key) {
    const { success } = await binding.limit({ key });
    if (!success) throw new RequestError(429, "Wait a minute and try again.");
}

const selection = "id, character, item, wanted, wanted_definition, wanted_signature, wanted_options, revision, closed, updated_at";

async function list(env, url, request, now) {
    const mine = url.searchParams.get("mine") === "1";
    const owner = mine ? await ownerOf(request) : "";
    const cursor = url.searchParams.get("before");
    const point = cursor?.match(/^(\d{1,10})\.([0-9a-f]{32})$/);
    if (cursor && !point) throw new RequestError(400, "Invalid page cursor.");
    const visibility = mine ? "closed = 0 AND owner = ?" : "closed = 0 AND (wanted_definition <> '' OR (wanted = '' AND item <> 'null'))";
    const filter = point ? " AND (updated_at, id) < (?, ?)" : "";
    const args = [now, ...(mine ? [owner] : [])];
    const where = " FROM offers WHERE expires_at > ? AND " + visibility;
    const [rows, totals] = await env.DB.batch([
        env.DB.prepare("SELECT " + selection + where + filter + " ORDER BY updated_at DESC, id DESC LIMIT ?")
            .bind(...args, ...(point ? [Number(point[1]), point[2]] : []), limit + 1),
        env.DB.prepare("SELECT count(*) AS total" + where).bind(...args),
    ]);
    const page = rows.results.slice(0, limit);
    const last = page.at(-1);
    return response({
        offers: page.map(({ id, character, item, wanted, wanted_definition, wanted_signature, wanted_options, revision, closed, updated_at }) =>
            ({ id, character, item: JSON.parse(item), wanted: wanted_definition ? wanted : "", wantedDefinition: wanted_definition, wantedSignature: wanted_signature,
                wantedOptions: JSON.parse(wanted_options), revision, closed: closed === 1, updatedAt: updated_at })),
        next: rows.results.length > limit ? last.updated_at + "." + last.id : "",
        pageCount: Math.ceil(totals.results[0].total / limit),
    });
}

async function wantedIndex(env, url, now) {
    const after = url.searchParams.get("after") || "";
    if (after && !/^[0-9a-f]{64}$/.test(after)) throw new RequestError(400, "Invalid wanted cursor.");
    const rows = await env.DB.prepare(
        "SELECT wanted_signature AS signature, MIN(wanted_definition) AS definition" +
        " FROM offers WHERE wanted_signature > ? AND closed=0 AND expires_at>?" +
        " GROUP BY wanted_signature ORDER BY wanted_signature LIMIT 129"
    ).bind(after, now).all();
    const wanted = rows.results.slice(0, 128);
    return response({ wanted, next: rows.results.length > 128 ? wanted.at(-1).signature : "" });
}

async function trackedOffers(env, url, now) {
    const ids = (url.searchParams.get("ids") || "").split(".");
    if (ids.length > 12 || ids.some(id => !/^[0-9a-f]{32}$/.test(id)) || new Set(ids).size !== ids.length)
        throw new RequestError(400, "Invalid tracked offers.");
    const placeholders = ids.map(() => "?").join(",");
    const rows = await env.DB.prepare(
        "SELECT id, character, item, wanted_definition, wanted_signature FROM offers WHERE id IN (" + placeholders +
        ") AND closed=0 AND expires_at>? AND wanted_signature<>'' AND item<>'null' ORDER BY id"
    ).bind(...ids, now).all();
    return response({ tracked: rows.results.map(row => ({
        id: row.id, character: row.character, offered: JSON.parse(row.item).name,
        definition: row.wanted_definition, signature: row.wanted_signature,
    })) });
}

async function write(env, request, id, now) {
    const owner = await ownerOf(request);
    await rateLimit(env.WRITE_LIMIT, request.headers.get("cf-connecting-ip") || owner);
    const body = await parse(request);
    const rev = body?.revision;
    if (!Number.isSafeInteger(rev) || rev < 0 || rev >= 2147483646)
        throw new RequestError(400, "Invalid offer revision.");
    if (body.closed !== undefined && typeof body.closed !== "boolean")
        throw new RequestError(400, "Invalid offer status.");
    const closing = body.closed === true;
    const openTrade = body.match === null && body.wanted === "";
    const match = closing ? null : openTrade ? { definition: "", signature: "", options: "[]" } : wantedOf(body.match);
    const wanted = closing ? null : openTrade ? "" : text(body.wanted, 160, "wanted item");
    if (rev === 0) {
        if (body.closed) throw new RequestError(400, "New offers must be open.");
        if (openTrade && body.item === null) throw new RequestError(400, "Select an item.");
        const character = text(body.character, 64, "character");
        const item = JSON.stringify(body.item === null ? null : itemOf(body.item));
        const results = await env.DB.batch([
            env.DB.prepare("DELETE FROM offers WHERE owner=? AND expires_at<=?").bind(owner, now),
            env.DB.prepare(
                "INSERT INTO offers (id, owner, character, item, wanted, wanted_definition, wanted_signature, wanted_options, updated_at, expires_at)" +
                " SELECT ?, ?, ?, ?, ?, ?, ?, ?, ?, ? WHERE (SELECT COUNT(*) FROM offers WHERE owner=? AND closed=0) < ?" +
                " ON CONFLICT(id) DO NOTHING"
            ).bind(id, owner, character, item, wanted, match.definition, match.signature, match.options, now, now + duration, owner, maxOffers),
            env.DB.prepare("SELECT owner, character, item, wanted, wanted_definition, wanted_signature, wanted_options, revision, closed FROM offers WHERE id=?").bind(id),
        ]);
        const record = results[2].results[0];
        if (!record) throw new RequestError(409, "Each installation can keep up to 24 offers for 30 days.");
        if (record.owner !== owner || record.character !== character || record.item !== item ||
            record.wanted !== wanted || record.wanted_definition !== match.definition || record.wanted_signature !== match.signature || record.wanted_options !== match.options ||
            record.revision !== 1 || record.closed)
            throw new RequestError(409, "The offer changed. Refresh your offers.");
        return response({ id, revision: 1 }, results[1].meta.changes ? 201 : 200);
    }

    const result = await env.DB.prepare(
        "UPDATE offers SET wanted=COALESCE(?,wanted), wanted_definition=COALESCE(?,wanted_definition)," +
        " wanted_signature=COALESCE(?,wanted_signature), wanted_options=COALESCE(?,wanted_options), closed=?, revision=revision+1, updated_at=?, expires_at=?" +
        " WHERE id=? AND owner=? AND revision=? AND closed=0 AND expires_at>? AND (?=0 OR item<>'null')"
    ).bind(wanted, match?.definition ?? null, match?.signature ?? null, match?.options ?? null,
        closing ? 1 : 0, now, now + duration, id, owner, rev, now, Number(!closing && openTrade)).run();
    if (!result.meta.changes) {
        const current = await env.DB.prepare("SELECT revision, item, wanted, wanted_definition, wanted_signature, wanted_options, closed FROM offers WHERE id=? AND owner=? AND expires_at>?")
            .bind(id, owner, now).first();
        if (!current) throw new RequestError(404, "The offer is unavailable.");
        if (openTrade && !closing && current.item === "null") throw new RequestError(400, "Select an item.");
        if (current.revision !== rev + 1 || current.closed !== Number(closing) ||
            (!closing && (current.wanted !== wanted || current.wanted_definition !== match.definition || current.wanted_signature !== match.signature ||
                current.wanted_options !== match.options)))
            throw new RequestError(409, "The offer changed. Refresh your offers.");
    }
    return response({ id, revision: rev + 1 });
}

export default {
    async fetch(request, env) {
        try {
            const url = new URL(request.url);
            const now = Math.floor(Date.now() / 1000);
            if (request.method === "GET" && url.pathname === "/v1/health") {
                await env.DB.prepare("SELECT 1 FROM offers LIMIT 1").all();
                return response({ version: 1 });
            }
            if (request.method === "POST" && url.pathname === "/v1/identity") {
                await rateLimit(env.IDENTITY_LIMIT, request.headers.get("cf-connecting-ip") || "local");
                return response({ token: hex(crypto.getRandomValues(new Uint8Array(32)).buffer) }, 201);
            }
            if (request.method === "GET" && url.pathname === "/v1/offers")
                return await list(env, url, request, now);
            if (request.method === "GET" && url.pathname === "/v1/wanted")
                return await wantedIndex(env, url, now);
            if (request.method === "GET" && url.pathname === "/v1/tracked")
                return await trackedOffers(env, url, now);
            const id = url.pathname.match(/^\/v1\/offers\/([0-9a-f]{32})$/)?.[1];
            if (request.method === "PUT" && id) return await write(env, request, id, now);
            return response({ error: "Unknown endpoint." }, 404);
        } catch (error) {
            if (error instanceof RequestError) return response({ error: error.message }, error.status);
            console.error("Trade Auction database or runtime error");
            return response({ error: "Trade Auction is unavailable. Try again later." }, 503);
        }
    },
    async scheduled(event, env) {
        await env.DB.prepare("DELETE FROM offers WHERE expires_at<=?").bind(Math.floor(event.scheduledTime / 1000)).run();
    },
};
