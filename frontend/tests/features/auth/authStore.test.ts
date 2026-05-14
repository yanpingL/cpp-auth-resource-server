import { beforeEach, describe, expect, it } from "vitest";
import {
  clearAuthSession,
  getAuthToken,
  getAuthUserName,
  saveAuthSession,
} from "@/features/auth/authStore";

describe("authStore", () => {
  beforeEach(() => {
    window.localStorage.clear();
  });

  it("saves and clears auth session details in localStorage", () => {
    saveAuthSession("token-123", 42, "Andrew");

    expect(getAuthToken()).toBe("token-123");
    expect(getAuthUserName()).toBe("Andrew");

    clearAuthSession();

    expect(getAuthToken()).toBeNull();
    expect(getAuthUserName()).toBeNull();
  });

  it("does not keep placeholder user names", () => {
    saveAuthSession("token-123", 42, "undefined");

    expect(getAuthToken()).toBe("token-123");
    expect(getAuthUserName()).toBeNull();
    expect(window.localStorage.getItem("resource_manager_user_name")).toBeNull();
  });
});
