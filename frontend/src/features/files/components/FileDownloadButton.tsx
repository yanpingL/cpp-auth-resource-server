"use client";

import { useMutation } from "@tanstack/react-query";
import { requestDownloadUrl } from "../api";

type FileDownloadButtonProps = {
  resourceId: number;
};

export function FileDownloadButton({ resourceId }: FileDownloadButtonProps) {
  // First ask the backend for a short-lived download URL for this resource.
  const downloadMutation = useMutation({
    mutationFn: requestDownloadUrl,
    onSuccess: (data) => {
      // Navigating to the presigned URL makes the browser GET the file directly.
      window.location.href = data.download_url;
    },
  });

  return (
    <div className="space-y-2">
      <button
        className="rounded-md border border-slate-300 px-3 py-2 text-sm font-medium disabled:opacity-50"
        disabled={downloadMutation.isPending}
        onClick={() => downloadMutation.mutate(resourceId)}
        type="button"
      >
        {downloadMutation.isPending ? "Preparing..." : "Download"}
      </button>
      {downloadMutation.isError ? (
        <p className="text-sm text-red-700">{downloadMutation.error.message}</p>
      ) : null}
    </div>
  );
}
