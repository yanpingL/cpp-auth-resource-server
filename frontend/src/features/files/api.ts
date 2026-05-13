import { getAuthToken } from "@/features/auth/authStore";
import { apiFetch } from "@/shared/api/client";
import type {
  DownloadUrlResponse,
  UploadUrlRequest,
  UploadUrlResponse,
} from "./types";

// File feature API: presigned URLs keep file bytes out of the C++ server.
function authHeaders(): HeadersInit {
  const token = getAuthToken();

  return token ? { Authorization: `Bearer ${token}` } : {};
}

export function requestUploadUrl(payload: UploadUrlRequest) {
  return apiFetch<UploadUrlResponse>("/api/files/upload-url", {
    method: "POST",
    headers: authHeaders(),
    body: JSON.stringify(payload),
  });
}

export async function uploadFileToStorage(file: File, uploadUrl: string) {
  // This PUT goes directly to MinIO/S3, not to the Next.js or C++ app.
  const response = await fetch(uploadUrl, {
    method: "PUT",
    headers: {
      "Content-Type": file.type,
    },
    body: file,
  });

  if (!response.ok) {
    throw new Error(`File upload failed with status ${response.status}`);
  }
}

export function requestDownloadUrl(resourceId: number) {
  return apiFetch<DownloadUrlResponse>(
    `/api/files/download-url?resource_id=${resourceId}`,
    {
      headers: authHeaders(),
    },
  );
}
