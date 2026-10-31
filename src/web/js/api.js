// Thin client for the C++ engine's local HTTP API.
// Every call resolves to parsed JSON or rejects with an ApiError whose message
// is the engine's own user-facing explanation.

export class ApiError extends Error {
  constructor(message, status, line) {
    super(message);
    this.status = status;
    this.line = line;
  }
}

async function request(method, path, body, { raw = false } = {}) {
  let response;
  try {
    response = await fetch(path, {
      method,
      headers: body === undefined ? {} : { "Content-Type": raw ? "text/plain" : "application/json" },
      body: body === undefined ? undefined : raw ? body : JSON.stringify(body),
    });
  } catch {
    throw new ApiError("The C++ engine is not responding. If you closed it, start Launch.exe again.", 0);
  }
  const type = response.headers.get("Content-Type") || "";
  if (!response.ok) {
    if (response.status === 413) throw new ApiError("The file is larger than the 64 MB limit.", 413);
    const payload = type.includes("json") ? await response.json().catch(() => ({})) : {};
    throw new ApiError(payload.error || `Request failed (HTTP ${response.status}).`, response.status, payload.line);
  }
  if (type.includes("json")) return response.json();
  return response.text();
}

export const api = {
  get: (path) => request("GET", path),
  post: (path, body) => request("POST", path, body ?? {}),
  postText: (path, text) => request("POST", path, text, { raw: true }),
};

// Triggers a browser download of text produced by the engine.
export function saveText(text, filename, type = "text/csv") {
  const url = URL.createObjectURL(new Blob([text], { type }));
  const a = Object.assign(document.createElement("a"), { href: url, download: filename });
  document.body.appendChild(a);
  a.click();
  a.remove();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}

// Decodes a base64 string produced by the engine into a typed array.
export function decodeBase64(b64, ArrayType) {
  const binary = atob(b64);
  const bytes = new Uint8Array(binary.length);
  for (let i = 0; i < binary.length; i++) bytes[i] = binary.charCodeAt(i);
  return new ArrayType(bytes.buffer);
}
