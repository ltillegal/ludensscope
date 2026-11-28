// The canonical host is ludensscope.com. The pages.dev address stays alive only
// as a permanent redirect so search engines see one address, not two.
const CANONICAL_HOST = "ludensscope.com";
const LEGACY_HOST = "ludensscope.pages.dev";

export async function onRequest(context) {
  const url = new URL(context.request.url);

  if (url.hostname === LEGACY_HOST) {
    const target = `https://${CANONICAL_HOST}${url.pathname}${url.search}`;
    return Response.redirect(target, 301);
  }

  return context.next();
}
