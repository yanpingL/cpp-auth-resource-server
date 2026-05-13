import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import {
  createFileResource,
  createTextResource,
  deleteResource,
  getResource,
  listResources,
  updateTextResource,
} from "@/features/resources/api";

describe("resources api", () => {
  beforeEach(() => {
    window.localStorage.setItem("resource_manager_token", "jwt-token");
  });

  afterEach(() => {
    window.localStorage.clear();
    vi.unstubAllGlobals();
  });

  it("lists resources with the bearer token", async () => {
    const fetchMock = vi.fn().mockResolvedValue(
      new Response(JSON.stringify({ data: [] }), {
        status: 200,
        headers: { "Content-Type": "application/json" },
      }),
    );
    vi.stubGlobal("fetch", fetchMock);

    await expect(listResources()).resolves.toEqual([]);

    expect(fetchMock).toHaveBeenCalledWith("/api/resources", {
      headers: {
        Authorization: "Bearer jwt-token",
        "Content-Type": "application/json",
      },
    });
  });

  it("gets a single resource by id", async () => {
    const fetchMock = vi.fn().mockResolvedValue(
      new Response(
        JSON.stringify({
          id: 7,
          title: "One",
          content: "Body",
          is_file: false,
        }),
        {
          status: 200,
          headers: { "Content-Type": "application/json" },
        },
      ),
    );
    vi.stubGlobal("fetch", fetchMock);

    await getResource(7);

    expect(fetchMock).toHaveBeenCalledWith("/api/resources?id=7", {
      headers: {
        Authorization: "Bearer jwt-token",
        "Content-Type": "application/json",
      },
    });
  });

  it("creates, updates, and deletes resources with expected payloads", async () => {
    const fetchMock = vi.fn().mockResolvedValue(
      new Response(JSON.stringify({ status: "created" }), {
        status: 200,
        headers: { "Content-Type": "application/json" },
      }),
    );
    vi.stubGlobal("fetch", fetchMock);

    await createTextResource({ title: "Text", content: "Hello" });
    await createFileResource({ title: "File", content: "http://file" });
    await updateTextResource(3, { title: "Updated", content: "Body" });
    await deleteResource(3);

    expect(fetchMock).toHaveBeenNthCalledWith(
      1,
      "/api/resources",
      expect.objectContaining({
        method: "POST",
        body: JSON.stringify({
          title: "Text",
          content: "Hello",
          is_file: false,
        }),
      }),
    );
    expect(fetchMock).toHaveBeenNthCalledWith(
      2,
      "/api/resources",
      expect.objectContaining({
        method: "POST",
        body: JSON.stringify({
          title: "File",
          content: "http://file",
          is_file: true,
        }),
      }),
    );
    expect(fetchMock).toHaveBeenNthCalledWith(
      3,
      "/api/resources",
      expect.objectContaining({
        method: "PUT",
        body: JSON.stringify({
          id: 3,
          title: "Updated",
          content: "Body",
        }),
      }),
    );
    expect(fetchMock).toHaveBeenNthCalledWith(
      4,
      "/api/resources?id=3",
      expect.objectContaining({
        method: "DELETE",
      }),
    );
  });
});
