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
      // Open the presigned URL separately so the app page stays in place.
      window.open(data.download_url, "_blank", "noopener,noreferrer");
    },
  });

  return (
    <div className="space-y-2">
      <button
        className="rounded-md border border-slate-300 px-3 py-2 text-sm font-semibold transition hover:-translate-y-0.5 hover:bg-slate-100 disabled:translate-y-0 disabled:opacity-50"
        disabled={downloadMutation.isPending}
        onClick={(event) => {
          event.stopPropagation();
          downloadMutation.mutate(resourceId);
        }}
        type="button"
      >
        {downloadMutation.isPending ? "Preparing..." : "Preview"}
      </button>
      {downloadMutation.isError ? (
        <p className="text-sm text-red-700">{downloadMutation.error.message}</p>
      ) : null}
    </div>
  );
}
