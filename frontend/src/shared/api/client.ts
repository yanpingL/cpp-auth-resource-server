export class ApiError extends Error {
  constructor(
    message: string,
    readonly status: number,
  ) {
    super(message);
  }
}

// Shared HTTP wrapper: feature APIs call this instead of using fetch directly.
export async function apiFetch<T>(
  path: string,
  options: RequestInit = {},
): Promise<T> {
  const isFormData = options.body instanceof FormData;
  const response = await fetch(path, {
    ...options,
    headers: {
      ...(isFormData ? {} : { "Content-Type": "application/json" }),
      ...options.headers,
    },
  });

  const body = await response.json().catch(() => null);

  if (!response.ok) {
    // The C++ backend returns JSON errors as { "error": "..." }.
    const message =
      body && typeof body.error === "string"
        ? body.error
        : `Request failed with status ${response.status}`;

    throw new ApiError(message, response.status);
  }

  return body as T;
}
