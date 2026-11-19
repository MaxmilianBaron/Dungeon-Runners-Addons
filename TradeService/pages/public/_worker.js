export default {
    fetch(request, env) {
        if (new URL(request.url).pathname.startsWith("/v1/")) {
            return env.TRADE_AUCTION.fetch(request);
        }
        return env.ASSETS.fetch(request);
    },
};
