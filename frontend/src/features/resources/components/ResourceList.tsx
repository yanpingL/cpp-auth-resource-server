"use client";

import { useMutation, useQuery, useQueryClient } from "@tanstack/react-query";
import Link from "next/link";
import { useRouter } from "next/navigation";
import { useEffect, useState } from "react";
import { clearAuthSession, getAuthToken } from "@/features/auth/authStore";
import { FileDownloadButton } from "@/features/files/components/FileDownloadButton";
import { FileUploadPanel } from "@/features/files/components/FileUploadPanel";
import {
  createTextResource,
  deleteResource,
  listResources,
  updateTextResource,
} from "../api";
import type { Resource, ResourceFormValues } from "../types";
import { ResourceForm } from "./ResourceForm";

export function ResourceList() {
  const router = useRouter();
  const queryClient = useQueryClient();
  const [editingResource, setEditingResource] = useState<Resource | null>(null);

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

  // After changing resources, invalidate the list so it refetches fresh data.
  const createMutation = useMutation({
    mutationFn: createTextResource,
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: ["resources"] });
    },
  });

  const updateMutation = useMutation({
    mutationFn: ({
      id,
      values,
    }: {
      id: number;
      values: ResourceFormValues;
    }) => updateTextResource(id, values),
    onSuccess: () => {
      setEditingResource(null);
      queryClient.invalidateQueries({ queryKey: ["resources"] });
    },
  });

  const deleteMutation = useMutation({
    mutationFn: deleteResource,
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: ["resources"] });
    },
  });

  function handleCreate(values: ResourceFormValues) {
    createMutation.mutate(values);
  }

  function handleUpdate(values: ResourceFormValues) {
    if (!editingResource) {
      return;
    }

    updateMutation.mutate({ id: editingResource.id, values });
  }

  function handleDelete(resource: Resource) {
    const confirmed = window.confirm(`Delete "${resource.title}"?`);
    if (confirmed) {
      deleteMutation.mutate(resource.id);
    }
  }

  function handleLogout() {
    clearAuthSession();
    queryClient.clear();
    router.push("/login");
  }

  const error =
    resourcesQuery.error ||
    createMutation.error ||
    updateMutation.error ||
    deleteMutation.error;

  return (
    <main className="min-h-screen bg-slate-50 px-6 py-10 text-slate-950">
      <section className="mx-auto w-full max-w-6xl space-y-6">
        <div className="flex items-center justify-between gap-4">
          <div>
            <h1 className="text-3xl font-semibold">Resources</h1>
            <p className="mt-1 text-sm text-slate-600">
              Your authenticated text and file resources from the C++ backend.
            </p>
          </div>
          <button
            className="rounded-full bg-slate-950 px-4 py-2 text-sm font-medium text-white"
            onClick={handleLogout}
            type="button"
          >
            Logout
          </button>
        </div>

        <div className="grid gap-6 lg:grid-cols-[360px_1fr]">
        <aside className="space-y-5 rounded-lg border border-slate-200 bg-white p-5 shadow-sm">
          <div>
            <Link className="text-sm font-medium text-emerald-700" href="/">
              Home
            </Link>
            <h1 className="mt-3 text-2xl font-semibold">
              {editingResource ? "Edit resource" : "Create resource"}
            </h1>
            <p className="mt-1 text-sm text-slate-600">
              Create text resources or upload files through presigned URLs.
            </p>
          </div>

          {editingResource ? (
            <ResourceForm
              initialValues={{
                title: editingResource.title,
                content: editingResource.content,
              }}
              isSubmitting={updateMutation.isPending}
              key={editingResource.id}
              onCancel={() => setEditingResource(null)}
              onSubmit={handleUpdate}
              submitLabel="Update resource"
            />
          ) : (
            <ResourceForm
              isSubmitting={createMutation.isPending}
              key="create-resource"
              onSubmit={handleCreate}
              submitLabel="Create resource"
            />
          )}

          {!editingResource ? (
            <div className="border-t border-slate-200 pt-5">
              <h2 className="mb-3 text-lg font-semibold">Upload file</h2>
              {/* File upload is a separate feature plugged into this page. */}
              <FileUploadPanel />
            </div>
          ) : null}
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
              No resources yet. Create your first text resource.
            </p>
          ) : null}

          <div className="grid gap-3">
            {resourcesQuery.data?.map((resource) => (
              <article
                className="rounded-lg border border-slate-200 bg-white p-5 shadow-sm"
                key={resource.id}
              >
                <div className="flex flex-col gap-3 sm:flex-row sm:items-start sm:justify-between">
                  <div className="min-w-0 space-y-2">
                    <div className="flex flex-wrap items-center gap-2">
                      <h3 className="break-words text-lg font-semibold">
                        <Link
                          className="hover:text-emerald-700"
                          href={`/resources/${resource.id}`}
                        >
                          {resource.title || "Untitled resource"}
                        </Link>
                      </h3>
                      {resource.is_file ? (
                        <span className="rounded-full bg-amber-100 px-2 py-1 text-xs font-medium text-amber-800">
                          File
                        </span>
                      ) : null}
                    </div>
                    <p className="whitespace-pre-wrap break-words text-sm leading-6 text-slate-600">
                      {resource.content || "No content"}
                    </p>
                  </div>

                  <div className="flex shrink-0 gap-2">
                    {!resource.is_file ? (
                      <button
                        className="rounded-md border border-slate-300 px-3 py-2 text-sm font-medium"
                        onClick={() => setEditingResource(resource)}
                        type="button"
                      >
                        Edit
                      </button>
                    ) : (
                      <FileDownloadButton resourceId={resource.id} />
                    )}
                    <button
                      className="rounded-md border border-red-200 px-3 py-2 text-sm font-medium text-red-700 disabled:opacity-50"
                      disabled={deleteMutation.isPending}
                      onClick={() => handleDelete(resource)}
                      type="button"
                    >
                      Delete
                    </button>
                  </div>
                </div>
              </article>
            ))}
          </div>
        </section>
        </div>
      </section>
    </main>
  );
}
