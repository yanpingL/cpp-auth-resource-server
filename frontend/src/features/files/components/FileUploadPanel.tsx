"use client";

import { useMutation, useQueryClient } from "@tanstack/react-query";
import type { FormEvent } from "react";
import { useState } from "react";
import { createFileResource } from "@/features/resources/api";
import { requestUploadUrl, uploadFileToStorage } from "../api";

const allowedTypes = [
  "text/plain",
  "application/pdf",
  "image/png",
  "image/jpeg",
  "application/msword",
  "application/vnd.openxmlformats-officedocument.wordprocessingml.document",
];

export function FileUploadPanel() {
  const queryClient = useQueryClient();
  // UI state: title is typed text; file is the selected local File object.
  const [title, setTitle] = useState("");
  const [file, setFile] = useState<File | null>(null);

  // Flow: ask backend for upload URL, upload bytes, then save file metadata.
  const uploadMutation = useMutation({
    mutationFn: async () => {
      if (!file) {
        throw new Error("Choose a file first");
      }

      const upload = await requestUploadUrl({
        filename: file.name,
        content_type: file.type,
      });

      await uploadFileToStorage(file, upload.upload_url);

      // File resources store the public object URL in resources.content.
      return createFileResource({
        title: title || file.name,
        content: upload.public_url,
      });
    },
    onSuccess: () => {
      setTitle("");
      setFile(null);
      queryClient.invalidateQueries({ queryKey: ["resources"] });
    },
  });

  function handleSubmit(event: FormEvent<HTMLFormElement>) {
    event.preventDefault();
    uploadMutation.mutate();
  }

  return (
    <form className="space-y-4" onSubmit={handleSubmit}>
      <label className="block space-y-2 text-sm font-medium">
        <span>Title</span>
        <input
          className="w-full rounded-md border border-slate-300 px-3 py-2 outline-none focus:border-slate-950"
          onChange={(event) => setTitle(event.target.value)}
          placeholder={file?.name ?? "File title"}
          value={title}
        />
      </label>

      <label className="block space-y-2 text-sm font-medium">
        <span>File</span>
        <input
          accept={allowedTypes.join(",")}
          className="w-full rounded-md border border-slate-300 bg-white px-3 py-2 text-sm"
          // <input type="file"> gives the browser-native local file picker.
          onChange={(event) => setFile(event.target.files?.[0] ?? null)}
          required
          type="file"
        />
      </label>

      {file && !allowedTypes.includes(file.type) ? (
        <p className="rounded-md bg-amber-50 px-3 py-2 text-sm text-amber-800">
          This file type is not supported by the backend.
        </p>
      ) : null}

      {uploadMutation.isError ? (
        <p className="rounded-md bg-red-50 px-3 py-2 text-sm text-red-700">
          {uploadMutation.error.message}
        </p>
      ) : null}

      <button
        className="rounded-md bg-slate-950 px-4 py-2 text-sm font-medium text-white disabled:opacity-50"
        disabled={
          uploadMutation.isPending || !file || !allowedTypes.includes(file.type)
        }
        type="submit"
      >
        {uploadMutation.isPending ? "Uploading..." : "Upload file"}
      </button>
    </form>
  );
}
