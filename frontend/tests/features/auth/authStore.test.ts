import { beforeEach, describe, expect, it } from "vitest";
import {
  clearAuthSession,
  getAuthToken,
  saveAuthSession,
} from "@/features/auth/authStore";

describe("authStore", () => {
  beforeEach(() => {
    window.localStorage.clear();
  });

  it("saves and clears the auth token in localStorage", () => {
    saveAuthSession("token-123", 42);

    expect(getAuthToken()).toBe("token-123");

    clearAuthSession();

    expect(getAuthToken()).toBeNull();
  });
});
