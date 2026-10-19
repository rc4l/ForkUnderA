// [rc4l] The demo site's Worker entry, which the runtime lets export only handlers, so its logic and tests live with respond.mjs.
import { handle } from './respond.mjs';

export default { fetch: (request, env) => handle(request, env.ASSETS) };
