"use client";

import { useMutation, useQuery, useQueryClient } from "@tanstack/react-query";
import Link from "next/link";
import { useRouter } from "next/navigation";
import type { FormEvent } from "react";
import { useEffect, useState } from "react";
import ReactMarkdown from "react-markdown";
import remarkGfm from "remark-gfm";
import { getAuthToken } from "@/features/auth/authStore";
import { requestDownloadUrl } from "@/features/files/api";
import { deleteResource, getResource, updateTextResource } from "../api";
import type { ResourceFormValues } from "../types";
import { ResourceForm } from "./ResourceForm";

type ResourceDetailProps = {
  id: number;
};

function appendPdfFitFragment(url: string) {
  if (!url.toLowerCase().includes(".pdf")) {
    return url;
  }

  return `${url}#view=FitH&zoom=page-width`;
}

function isImageResource(title: string, content: string) {
  return /\.(png|jpe?g|gif|webp|bmp|svg)(\?|#|$)/i.test(`${title} ${content}`);
}

export function ResourceDetail({ id }: ResourceDetailProps) {
  const router = useRouter();
  const queryClient = useQueryClient();
  const [isEditing, setIsEditing] = useState(false);
  const [isDeleteModalOpen, setIsDeleteModalOpen] = useState(false);

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

  const filePreviewQuery = useQuery({
    queryKey: ["files", "preview", id],
    queryFn: () => requestDownloadUrl(id),
    enabled: Boolean(resourceQuery.data?.is_file && !isEditing),
  });

  const updateMutation = useMutation({
    mutationFn: (values: ResourceFormValues) => updateTextResource(id, values),
    onSuccess: () => {
      setIsEditing(false);
      queryClient.invalidateQueries({ queryKey: ["resources"] });
      queryClient.invalidateQueries({ queryKey: ["resources", id] });
    },
  });

  const deleteMutation = useMutation({
    mutationFn: deleteResource,
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: ["resources"] });
      router.push("/resources");
    },
  });

  function handleSave(values: ResourceFormValues) {
    updateMutation.mutate(values);
  }

  function handleFileTitleSave(event: FormEvent<HTMLFormElement>) {
    event.preventDefault();
    if (!resourceQuery.data) {
      return;
    }

    const formData = new FormData(event.currentTarget);
    const title = String(formData.get("title") ?? "");

    updateMutation.mutate({
      title,
      content: resourceQuery.data.content,
    });
  }

  return (
    <main className="relative h-screen overflow-hidden bg-[#202124] px-6 py-10 text-slate-950">
      <div
        aria-hidden="true"
        className="absolute inset-0 bg-[url('/user_space_background.jpg')] bg-cover bg-center"
      />
      <div aria-hidden="true" className="absolute inset-0 bg-black/55" />
      <section className="relative z-10 mx-auto flex h-full w-full max-w-3xl flex-col gap-5">
        <div className="flex flex-wrap items-center justify-between gap-3">
          <Link
            className="inline-flex items-center gap-2 rounded-md bg-slate-950 px-4 py-2 text-sm font-semibold text-white shadow-lg shadow-black/20 transition hover:-translate-y-0.5 hover:bg-slate-700"
            href="/resources"
          >
            Back
          </Link>
          {resourceQuery.data && !isEditing ? (
            <div className="flex flex-wrap gap-3">
              <button
                className="rounded-md bg-white/90 px-4 py-2 text-sm font-semibold text-slate-950 shadow-lg shadow-black/20 transition hover:-translate-y-0.5 hover:bg-slate-200"
                onClick={() => setIsEditing(true)}
                type="button"
              >
                Edit
              </button>
              <button
                className="rounded-md border border-red-200 bg-transparent px-4 py-2 text-sm font-semibold text-red-500 shadow-lg shadow-black/20 transition hover:-translate-y-0.5 hover:bg-red-50"
                onClick={() => setIsDeleteModalOpen(true)}
                type="button"
              >
                Delete
              </button>
            </div>
          ) : null}
        </div>

        {resourceQuery.isLoading ? (
          <p className="rounded-lg border border-white/15 bg-white/90 p-5 text-sm text-slate-600 shadow-lg shadow-black/10 backdrop-blur-sm">
            Loading resource...
          </p>
        ) : null}

        {resourceQuery.isError ? (
          <p className="rounded-md bg-red-50 px-3 py-2 text-sm text-red-700">
            {resourceQuery.error.message}
          </p>
        ) : null}

        {updateMutation.isError ? (
          <p className="rounded-md bg-red-50 px-3 py-2 text-sm text-red-700">
            {updateMutation.error.message}
          </p>
        ) : null}

        {filePreviewQuery.isError ? (
          <p className="rounded-md bg-red-50 px-3 py-2 text-sm text-red-700">
            {filePreviewQuery.error.message}
          </p>
        ) : null}

        {deleteMutation.isError && !isDeleteModalOpen ? (
          <p className="rounded-md bg-red-50 px-3 py-2 text-sm text-red-700">
            {deleteMutation.error.message}
          </p>
        ) : null}

        {resourceQuery.data ? (
          <article
            className="min-h-0 flex-1 space-y-5 overflow-y-auto rounded-lg border border-white/15 bg-white/90 p-6 shadow-2xl shadow-black/20 backdrop-blur-sm"
          >
            {isEditing ? (
              <div className="space-y-4">
                <div>
                  <h1 className="text-3xl font-semibold">
                    {resourceQuery.data.is_file ? "Edit File Title" : "Edit Resource"}
                  </h1>
                  <p className="mt-1 text-sm text-slate-500">
                    Save updates to send a PUT request to the backend.
                  </p>
                </div>
                {resourceQuery.data.is_file ? (
                  <form className="space-y-4" onSubmit={handleFileTitleSave}>
                    <label className="block space-y-2 text-sm font-medium">
                      <span>Title</span>
                      <input
                        className="w-full rounded-md border border-slate-300 bg-slate-50 px-3 py-2 outline-none focus:border-slate-950"
                        defaultValue={resourceQuery.data.title}
                        name="title"
                        required
                      />
                    </label>

                    <div className="flex flex-wrap gap-3">
                      <button
                        className="rounded-md bg-slate-950 px-4 py-2 text-sm font-semibold text-white disabled:opacity-50"
                        disabled={updateMutation.isPending}
                        type="submit"
                      >
                        {updateMutation.isPending ? "Saving..." : "Save"}
                      </button>
                      <button
                        className="rounded-md border border-slate-300 px-4 py-2 text-sm font-semibold"
                        onClick={() => setIsEditing(false)}
                        type="button"
                      >
                        Cancel
                      </button>
                    </div>
                  </form>
                ) : (
                  <ResourceForm
                    editorSize="large"
                    initialValues={{
                      title: resourceQuery.data.title,
                      content: resourceQuery.data.content,
                    }}
                    isSubmitting={updateMutation.isPending}
                    onCancel={() => setIsEditing(false)}
                    onSubmit={handleSave}
                    submitLabel="Save"
                  />
                )}
              </div>
            ) : (
              <>
                <div className="flex flex-col gap-3 sm:flex-row sm:items-start sm:justify-between">
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
                </div>

                {resourceQuery.data.is_file ? (
                  <div className="flex h-[58vh] items-center justify-center rounded-md bg-slate-50 p-3">
                    {filePreviewQuery.isLoading ? (
                      <p className="p-4 text-sm text-slate-600">
                        Loading file preview...
                      </p>
                    ) : null}
                    {filePreviewQuery.data &&
                    isImageResource(
                      resourceQuery.data.title,
                      resourceQuery.data.content,
                    ) ? (
                      <img
                        alt={resourceQuery.data.title || "File preview"}
                        className="max-h-full max-w-full object-contain"
                        src={filePreviewQuery.data.download_url}
                      />
                    ) : null}
                    {filePreviewQuery.data &&
                    !isImageResource(
                      resourceQuery.data.title,
                      resourceQuery.data.content,
                    ) ? (
                      <iframe
                        className="h-full w-full rounded-md border border-slate-200 bg-white"
                        src={appendPdfFitFragment(filePreviewQuery.data.download_url)}
                        title={resourceQuery.data.title || "File preview"}
                      />
                    ) : null}
                  </div>
                ) : (
                  <div className="space-y-2">
                    <div className="resource-markdown rounded-md bg-slate-50 p-4 text-sm leading-6 text-slate-700">
                      <ReactMarkdown remarkPlugins={[remarkGfm]}>
                        {resourceQuery.data.content || "No content"}
                      </ReactMarkdown>
                    </div>
                  </div>
                )}
              </>
            )}
          </article>
        ) : null}
      </section>

      {isDeleteModalOpen && resourceQuery.data ? (
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
                {resourceQuery.data.title || "Untitled resource"}
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
                onClick={() => setIsDeleteModalOpen(false)}
                type="button"
              >
                Cancel
              </button>
              <button
                className="rounded-md bg-red-700 px-4 py-2 text-sm font-semibold text-white transition hover:-translate-y-0.5 hover:bg-red-800 disabled:translate-y-0 disabled:opacity-50"
                disabled={deleteMutation.isPending}
                onClick={() => deleteMutation.mutate(resourceQuery.data.id)}
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
