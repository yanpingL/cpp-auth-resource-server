"use client";

import { useQuery } from "@tanstack/react-query";
import Link from "next/link";
import { useRouter } from "next/navigation";
import { useEffect } from "react";
import { getAuthToken } from "@/features/auth/authStore";
import { FileDownloadButton } from "@/features/files/components/FileDownloadButton";
import { getResource } from "../api";

type ResourceDetailProps = {
  id: number;
};

export function ResourceDetail({ id }: ResourceDetailProps) {
  const router = useRouter();

  // Detail pages still use the browser JWT, so unauthorized users go to login.
  useEffect(() => {
    if (!getAuthToken()) {
      router.replace("/login");
    }
  }, [router]);

  const resourceQuery = useQuery({
    queryKey: ["resources", id],
    queryFn: () => getResource(id),
    enabled: Number.isInteger(id) && id > 0,
  });

  return (
    <main className="min-h-screen bg-slate-50 px-6 py-10 text-slate-950">
      <section className="mx-auto w-full max-w-3xl space-y-5">
        <div className="flex flex-wrap items-center gap-3">
          <Link className="text-sm font-medium text-emerald-700" href="/resources">
            Back to resources
          </Link>
        </div>

        {resourceQuery.isLoading ? (
          <p className="rounded-lg border border-slate-200 bg-white p-5 text-sm text-slate-600">
            Loading resource...
          </p>
        ) : null}

        {resourceQuery.isError ? (
          <p className="rounded-md bg-red-50 px-3 py-2 text-sm text-red-700">
            {resourceQuery.error.message}
          </p>
        ) : null}

        {resourceQuery.data ? (
          <article className="space-y-5 rounded-lg border border-slate-200 bg-white p-6 shadow-sm">
            <div className="space-y-2">
              <div className="flex flex-wrap items-center gap-2">
                <h1 className="break-words text-3xl font-semibold">
                  {resourceQuery.data.title || "Untitled resource"}
                </h1>
                {resourceQuery.data.is_file ? (
                  <span className="rounded-full bg-amber-100 px-2 py-1 text-xs font-medium text-amber-800">
                    File
                  </span>
                ) : (
                  <span className="rounded-full bg-emerald-100 px-2 py-1 text-xs font-medium text-emerald-800">
                    Text
                  </span>
                )}
              </div>
              <p className="text-sm text-slate-500">
                Resource ID: {resourceQuery.data.id}
              </p>
              {resourceQuery.data.is_file ? (
                <FileDownloadButton resourceId={resourceQuery.data.id} />
              ) : null}
            </div>

            <div className="space-y-2">
              <h2 className="text-sm font-semibold uppercase tracking-[0.16em] text-slate-500">
                Content
              </h2>
              <p className="whitespace-pre-wrap break-words rounded-md bg-slate-50 p-4 text-sm leading-6 text-slate-700">
                {resourceQuery.data.content || "No content"}
              </p>
            </div>
          </article>
        ) : null}
      </section>
    </main>
  );
}
