"use client";

import { useMutation, useQueryClient } from "@tanstack/react-query";
import Link from "next/link";
import { useRouter } from "next/navigation";
import { useEffect } from "react";
import { getAuthToken } from "@/features/auth/authStore";
import { createTextResource } from "../api";
import type { ResourceFormValues } from "../types";
import { ResourceForm } from "./ResourceForm";

export function CreateResourcePage() {
  const router = useRouter();
  const queryClient = useQueryClient();

  useEffect(() => {
    if (!getAuthToken()) {
      router.replace("/login");
    }
  }, [router]);

  const createMutation = useMutation({
    mutationFn: createTextResource,
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: ["resources"] });
      router.push("/resources");
    },
  });

  function handleCreate(values: ResourceFormValues) {
    createMutation.mutate(values);
  }

  return (
    <main className="relative h-screen overflow-hidden bg-[#202124] px-6 py-10 text-slate-950">
      <div
        aria-hidden="true"
        className="absolute inset-0 bg-[url('/user_space_background.jpg')] bg-cover bg-center"
      />
      <div aria-hidden="true" className="absolute inset-0 bg-black/55" />

      <section className="relative z-10 mx-auto flex h-full w-full max-w-5xl flex-col gap-5 overflow-hidden">
        <div>
          <Link
            className="inline-flex items-center gap-2 rounded-md bg-slate-950 px-4 py-2 text-sm font-semibold text-white shadow-lg shadow-black/20 transition hover:-translate-y-0.5 hover:bg-slate-700"
            href="/resources"
          >
            Back
          </Link>
        </div>

        <div className="flex min-h-0 flex-1 flex-col overflow-hidden rounded-lg border border-white/15 bg-white/90 p-6 shadow-2xl shadow-black/20 backdrop-blur-sm md:p-8">
          <h1 className="text-3xl font-semibold">Create Resource</h1>
          <p className="mt-1 text-sm text-slate-600">
            Write plain text or Markdown. Headings, lists, links, code, and bold
            text will render on the detail page.
          </p>

          <div className="mt-6 min-h-0 flex-1">
            <ResourceForm
              editorSize="large"
              fillAvailableHeight
              isSubmitting={createMutation.isPending}
              onCancel={() => router.push("/resources")}
              onSubmit={handleCreate}
              submitLabel="Create Resource"
            />
          </div>

          {createMutation.isError ? (
            <p className="mt-4 rounded-md bg-red-50 px-3 py-2 text-sm text-red-700">
              {createMutation.error.message}
            </p>
          ) : null}
        </div>
      </section>
    </main>
  );
}
