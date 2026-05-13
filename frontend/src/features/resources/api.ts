import { getAuthToken } from "@/features/auth/authStore";
import { apiFetch } from "@/shared/api/client";
import type { Resource, ResourceFormValues } from "./types";

// Resource feature API: mirrors the C++ /api/resources endpoints.
type ResourceListResponse = {
  data: Resource[];
};

type ResourceStatusResponse = {
  status: "created" | "updated" | "deleted";
};

function authHeaders(): HeadersInit {
  const token = getAuthToken();

  return token ? { Authorization: `Bearer ${token}` } : {};
}

export async function listResources() {
  const response = await apiFetch<ResourceListResponse>("/api/resources", {
    headers: authHeaders(),
  });

  return response.data;
}

export function getResource(id: number) {
  return apiFetch<Resource>(`/api/resources?id=${id}`, {
    headers: authHeaders(),
  });
}

export function createTextResource(values: ResourceFormValues) {
  return apiFetch<ResourceStatusResponse>("/api/resources", {
    method: "POST",
    headers: authHeaders(),
    body: JSON.stringify({
      title: values.title,
      content: values.content,
      is_file: false,
    }),
  });
}

export function createFileResource(values: ResourceFormValues) {
  return apiFetch<ResourceStatusResponse>("/api/resources", {
    method: "POST",
    headers: authHeaders(),
    body: JSON.stringify({
      title: values.title,
      content: values.content,
      is_file: true,
    }),
  });
}

export function updateTextResource(
  id: number,
  values: ResourceFormValues,
) {
  return apiFetch<ResourceStatusResponse>("/api/resources", {
    method: "PUT",
    headers: authHeaders(),
    body: JSON.stringify({
      id,
      title: values.title,
      content: values.content,
    }),
  });
}

export function deleteResource(id: number) {
  return apiFetch<ResourceStatusResponse>(`/api/resources?id=${id}`, {
    method: "DELETE",
    headers: authHeaders(),
  });
}
