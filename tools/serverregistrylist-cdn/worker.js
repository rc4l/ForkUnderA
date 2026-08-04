// [rc4l] The caching layer in front of serverregistries.txt.
//
// GitHub holds the source of truth (edited by pull request); this Worker is what CLIENTS actually
// fetch. Cloudflare serves it from cache, so a hundred thousand players cost GitHub nothing and the
// file stays reviewable in git.
//
// Two properties matter more than anything else here:
//
//   1. It must never answer 200 with something that is not a list. A client treats "parsed zero
//      entries" as a failed fetch and keeps its cache, but only if we don't hand it a 200 that looks
//      authoritative. Upstream trouble therefore becomes a 5xx, never an empty success.
//
//   2. It must never challenge. A game client cannot solve a CAPTCHA or run a JS interstitial, so a
//      bot challenge on this hostname is indistinguishable from an outage -- and it lands hardest on
//      shared and CGNAT address ranges, which is most of the world outside the US. Challenges are
//      disabled for this hostname in the dashboard; see README.md. This file cannot enforce that, so
//      it is written down in both places.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

const SOURCE = "https://raw.githubusercontent.com/rc4l/ZandroX/main/serverregistries.txt";

// 6 hours, matching what the client uses. The client refreshes at most every 6h and this cache holds
// for 6h, so a newly listed server registry reaches everyone within ~12h worst case.
const TTL_SECONDS = 6 * 60 * 60;

function plain(body, status) {
	return new Response(body, {
		status,
		headers: {
			"content-type": "text/plain; charset=utf-8",
			"cache-control": `public, max-age=${TTL_SECONDS}`,
			"x-content-type-options": "nosniff",
		},
	});
}

export default {
	async fetch(request) {
		if (request.method !== "GET" && request.method !== "HEAD") {
			return plain("method not allowed\n", 405);
		}

		let upstream;
		try {
			upstream = await fetch(SOURCE, {
				cf: { cacheTtl: TTL_SECONDS, cacheEverything: true },
				headers: { "user-agent": "ZandroX-serverregistrylist" },
			});
		} catch (err) {
			// Network trouble reaching GitHub. A 5xx tells the client "keep your cache", which is
			// exactly right -- far better than serving an empty list that would look definitive.
			return plain("upstream unreachable\n", 502);
		}

		if (!upstream.ok) {
			return plain(`upstream returned ${upstream.status}\n`, 502);
		}

		const body = await upstream.text();

		// Paranoia that has already paid for itself once: if GitHub ever serves us an error page or a
		// redirect body, it would be a 200 full of HTML. Refuse to pass that on as a list.
		if (body.length === 0 || body.trimStart().startsWith("<")) {
			return plain("upstream did not return a list\n", 502);
		}

		return plain(body, 200);
	},
};
