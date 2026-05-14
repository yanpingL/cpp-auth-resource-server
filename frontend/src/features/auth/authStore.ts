const TOKEN_STORAGE_KEY = "resource_manager_token";
const USER_ID_STORAGE_KEY = "resource_manager_user_id";
const USER_NAME_STORAGE_KEY = "resource_manager_user_name";

function normalizeUserName(userName: string | null | undefined) {
  const trimmed = userName?.trim();

  if (!trimmed) {
    return null;
  }

  const normalizedPlaceholder = trimmed.toLowerCase();
  if (normalizedPlaceholder === "undefined" || normalizedPlaceholder === "null") {
    return null;
  }

  return trimmed;
}

// Browser-only auth storage used by API calls that need a Bearer token.
export function saveAuthSession(
  token: string,
  userId: number,
  userName?: string,
) {
  window.localStorage.setItem(TOKEN_STORAGE_KEY, token);
  window.localStorage.setItem(USER_ID_STORAGE_KEY, String(userId));

  const normalizedUserName = normalizeUserName(userName);
  if (normalizedUserName) {
    window.localStorage.setItem(USER_NAME_STORAGE_KEY, normalizedUserName);
  } else {
    window.localStorage.removeItem(USER_NAME_STORAGE_KEY);
  }
}

export function getAuthToken() {
  if (typeof window === "undefined") {
    return null;
  }

  return window.localStorage.getItem(TOKEN_STORAGE_KEY);
}

export function getAuthUserName() {
  if (typeof window === "undefined") {
    return null;
  }

  const userName = normalizeUserName(
    window.localStorage.getItem(USER_NAME_STORAGE_KEY),
  );

  if (!userName) {
    window.localStorage.removeItem(USER_NAME_STORAGE_KEY);
  }

  return userName;
}

export function clearAuthSession() {
  window.localStorage.removeItem(TOKEN_STORAGE_KEY);
  window.localStorage.removeItem(USER_ID_STORAGE_KEY);
  window.localStorage.removeItem(USER_NAME_STORAGE_KEY);
}
