const TOKEN_STORAGE_KEY = "resource_manager_token";
const USER_ID_STORAGE_KEY = "resource_manager_user_id";

// Browser-only auth storage used by API calls that need a Bearer token.
export function saveAuthSession(token: string, userId: number) {
  window.localStorage.setItem(TOKEN_STORAGE_KEY, token);
  window.localStorage.setItem(USER_ID_STORAGE_KEY, String(userId));
}

export function getAuthToken() {
  if (typeof window === "undefined") {
    return null;
  }

  return window.localStorage.getItem(TOKEN_STORAGE_KEY);
}

export function clearAuthSession() {
  window.localStorage.removeItem(TOKEN_STORAGE_KEY);
  window.localStorage.removeItem(USER_ID_STORAGE_KEY);
}
