"use client";

import { useMutation, useQuery, useQueryClient } from "@tanstack/react-query";
import Link from "next/link";
import { useRouter } from "next/navigation";
import { useEffect, useState } from "react";
import { clearAuthSession, getAuthToken } from "@/features/auth/authStore";
import { deleteResource, listResources } from "../api";
import type { Resource } from "../types";

export function ResourceList() {
  const router = useRouter();
  const queryClient = useQueryClient();
  const [resourceToDelete, setResourceToDelete] = useState<Resource | null>(
    null,
  );

  // This page needs browser auth state, so the guard runs client-side.
  useEffect(() => {
    if (!getAuthToken()) {
      router.replace("/login");
    }
  }, [router]);

  const resourcesQuery = useQuery({
    queryKey: ["resources"],
    queryFn: listResources,
  });

  const deleteMutation = useMutation({
    mutationFn: deleteResource,
    onSuccess: () => {
      setResourceToDelete(null);
      queryClient.invalidateQueries({ queryKey: ["resources"] });
    },
  });

  function handleConfirmDelete() {
    if (resourceToDelete) {
      deleteMutation.mutate(resourceToDelete.id);
    }
  }

  function handleLogout() {
    clearAuthSession();
    queryClient.clear();
    router.push("/");
  }

  const error = resourcesQuery.error || deleteMutation.error;
  const resourceCount = resourcesQuery.data?.length ?? 0;
  const fileCount =
    resourcesQuery.data?.filter((resource) => resource.is_file).length ?? 0;
  const textCount = resourceCount - fileCount;

  return (
    <main className="relative min-h-screen overflow-hidden bg-[#202124] px-6 py-10 text-white">
      <div
        aria-hidden="true"
        className="absolute inset-0 bg-[url('/user_space_background.jpg')] bg-cover bg-center"
      />
      <div aria-hidden="true" className="absolute inset-0 bg-black/55" />
      <section className="relative z-10 mx-auto w-full max-w-6xl space-y-6">
        <div className="flex items-center justify-between gap-4">
          <Link
            className="inline-flex items-center gap-3 font-semibold transition hover:-translate-y-0.5 hover:text-white/70"
            href="/"
          >
            <span className="flex size-10 items-center justify-center rounded-md bg-white text-xl font-bold text-slate-950">
              R
            </span>
            <span className="text-xl text-white">esource Manager</span>
          </Link>
          <button
            className="rounded-full bg-slate-950 px-4 py-2 text-sm font-semibold text-white shadow-lg shadow-black/20 transition hover:-translate-y-0.5 hover:bg-slate-700"
            onClick={handleLogout}
            type="button"
          >
            Logout
          </button>
        </div>

        <div className="flex flex-col gap-4 sm:flex-row sm:items-end sm:justify-between">
          <div>
            <h1 className="text-3xl font-semibold text-white">
              Your Resources Warehouse
            </h1>
            <p className="mt-1 text-sm text-white/65">
              Review your authenticated resources.
            </p>
          </div>

          <div className="flex flex-wrap gap-3">
            <Link
              className="rounded-md border border-white/15 bg-white/90 px-4 py-2 text-sm font-semibold text-slate-950 shadow-lg shadow-black/10 backdrop-blur-sm transition hover:-translate-y-0.5 hover:bg-white hover:shadow-xl"
              href="/resources/new"
            >
              Create Resource
            </Link>
            <Link
              className="rounded-md border border-white/35 px-4 py-2 text-sm font-semibold text-white shadow-lg shadow-black/20 transition hover:-translate-y-0.5 hover:bg-white/10"
              href="/resources/upload"
            >
              Upload File
            </Link>
          </div>
        </div>

        <div className="grid gap-6 lg:grid-cols-[280px_1fr]">
          <aside className="min-h-[68vh] space-y-4 rounded-lg border border-white/15 bg-white/90 p-5 text-slate-950 shadow-2xl shadow-black/20 backdrop-blur-sm">
            <div>
              <p className="text-sm font-medium text-slate-500">User profile</p>
              <img
                alt="User profile"
                className="mx-auto mt-3 size-20 rounded-full border border-slate-200 bg-slate-950 object-cover shadow-sm"
                src="/user_avatar.svg"
              />
              <p className="mt-1 text-sm text-slate-600">
                Personal details can live here later.
              </p>
            </div>

            <div className="grid gap-3 border-t border-slate-200 pt-4">
              <div className="flex items-center justify-between text-sm">
                <span className="text-slate-600">Total resources</span>
                <span className="font-semibold">{resourceCount}</span>
              </div>
              <div className="flex items-center justify-between text-sm">
                <span className="text-slate-600">Text resources</span>
                <span className="font-semibold">{textCount}</span>
              </div>
              <div className="flex items-center justify-between text-sm">
                <span className="text-slate-600">File resources</span>
                <span className="font-semibold">{fileCount}</span>
              </div>
            </div>
          </aside>

          <section className="space-y-4">
            {error ? (
              <p className="rounded-md bg-red-50 px-3 py-2 text-sm text-red-700">
                {error.message}
              </p>
            ) : null}

            {resourcesQuery.isLoading ? (
              <p className="rounded-lg border border-slate-200 bg-white p-5 text-sm text-slate-600">
                Loading resources...
              </p>
            ) : null}

            {resourcesQuery.data?.length === 0 ? (
              <p className="rounded-lg border border-slate-200 bg-white p-5 text-sm text-slate-600">
                No resources available yet.
              </p>
            ) : null}

            <div className="max-h-[68vh] overflow-y-auto pr-2">
              <div className="grid gap-3">
                {resourcesQuery.data?.map((resource) => (
                  <article
                    className="cursor-pointer rounded-lg border border-white/15 bg-white/90 p-5 text-slate-950 shadow-lg shadow-black/10 backdrop-blur-sm transition hover:-translate-y-0.5 hover:shadow-xl"
                    key={resource.id}
                    onClick={() => router.push(`/resources/${resource.id}`)}
                    onKeyDown={(event) => {
                      if (event.key === "Enter" || event.key === " ") {
                        event.preventDefault();
                        router.push(`/resources/${resource.id}`);
                      }
                    }}
                    role="link"
                    tabIndex={0}
                  >
                    <div className="flex flex-col gap-3 sm:flex-row sm:items-start sm:justify-between">
                      <div className="min-w-0 space-y-2">
                        <div className="flex flex-wrap items-center gap-2">
                          <h3 className="break-words text-lg font-semibold">
                            {resource.title || "Untitled resource"}
                          </h3>
                          <span className="rounded-full bg-slate-100 px-2 py-1 text-xs font-medium text-slate-700">
                            {resource.is_file ? "File" : "Text"}
                          </span>
                        </div>
                      </div>

                      <div className="flex shrink-0 gap-2">
                        <button
                          className="rounded-md border border-red-200 px-3 py-2 text-sm font-semibold text-red-700 transition hover:-translate-y-0.5 hover:bg-red-50 disabled:translate-y-0 disabled:opacity-50"
                          disabled={deleteMutation.isPending}
                          onClick={(event) => {
                            event.stopPropagation();
                            setResourceToDelete(resource);
                          }}
                          type="button"
                        >
                          Delete
                        </button>
                      </div>
                    </div>
                  </article>
                ))}
              </div>
            </div>
          </section>
        </div>
      </section>

      {resourceToDelete ? (
        <div
          aria-modal="true"
          className="fixed inset-0 z-20 flex items-center justify-center bg-black/60 px-6"
          role="dialog"
        >
          <div className="w-full max-w-md rounded-lg border border-white/15 bg-white p-6 text-slate-950 shadow-2xl shadow-black/30">
            <h2 className="text-2xl font-semibold">Delete resource?</h2>
            <p className="mt-3 text-sm leading-6 text-slate-600">
              This will permanently delete{" "}
              <span className="font-semibold text-slate-950">
                {resourceToDelete.title || "Untitled resource"}
              </span>
              .
            </p>

            {deleteMutation.isError ? (
              <p className="mt-4 rounded-md bg-red-50 px-3 py-2 text-sm text-red-700">
                {deleteMutation.error.message}
              </p>
            ) : null}

            <div className="mt-6 flex justify-end gap-3">
              <button
                className="rounded-md border border-slate-300 px-4 py-2 text-sm font-semibold text-slate-950 transition hover:-translate-y-0.5 hover:bg-slate-100 disabled:translate-y-0 disabled:opacity-50"
                disabled={deleteMutation.isPending}
                onClick={() => setResourceToDelete(null)}
                type="button"
              >
                Cancel
              </button>
              <button
                className="rounded-md bg-red-700 px-4 py-2 text-sm font-semibold text-white transition hover:-translate-y-0.5 hover:bg-red-800 disabled:translate-y-0 disabled:opacity-50"
                disabled={deleteMutation.isPending}
                onClick={handleConfirmDelete}
                type="button"
              >
                {deleteMutation.isPending ? "Deleting..." : "Delete"}
              </button>
            </div>
          </div>
        </div>
      ) : null}
    </main>
  );
}
