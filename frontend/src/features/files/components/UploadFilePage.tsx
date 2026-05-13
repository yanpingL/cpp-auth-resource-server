"use client";

import Link from "next/link";
import { useRouter } from "next/navigation";
import { useEffect } from "react";
import { getAuthToken } from "@/features/auth/authStore";
import { FileUploadPanel } from "./FileUploadPanel";

export function UploadFilePage() {
  const router = useRouter();

  useEffect(() => {
    if (!getAuthToken()) {
      router.replace("/login");
    }
  }, [router]);

  return (
    <main className="relative min-h-screen overflow-hidden bg-[#202124] px-6 py-10 text-slate-950">
      <div
        aria-hidden="true"
        className="absolute inset-0 bg-[url('/user_space_background.jpg')] bg-cover bg-center"
      />
      <div aria-hidden="true" className="absolute inset-0 bg-black/55" />

      <section className="relative z-10 mx-auto w-full max-w-xl space-y-5">
        <div>
          <Link
            className="inline-flex items-center gap-2 rounded-md bg-slate-950 px-4 py-2 text-sm font-semibold text-white shadow-lg shadow-black/20 transition hover:-translate-y-0.5 hover:bg-slate-700"
            href="/resources"
          >
            Back
          </Link>
        </div>

        <div className="rounded-lg border border-white/15 bg-white/90 p-6 shadow-2xl shadow-black/20 backdrop-blur-sm">
          <h1 className="text-3xl font-semibold">Upload File</h1>
          <p className="mt-1 text-sm text-slate-600">
            Upload a file and save it as a resource.
          </p>

          <div className="mt-6">
            <FileUploadPanel onUploaded={() => router.push("/resources")} />
          </div>
        </div>
      </section>
    </main>
  );
}
