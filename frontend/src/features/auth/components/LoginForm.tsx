"use client";

import { useMutation } from "@tanstack/react-query";
import Link from "next/link";
import { useRouter } from "next/navigation";
import { FormEvent, useState } from "react";
import { loginUser } from "../api";
import { saveAuthSession } from "../authStore";

export function LoginForm() {
  const router = useRouter();
  const [email, setEmail] = useState("");
  const [password, setPassword] = useState("");

  // Mutations represent user-triggered backend actions such as login.
  const loginMutation = useMutation({
    mutationFn: loginUser,
    onSuccess: (data) => {
      saveAuthSession(data.token, data.user_id, data.name);
      router.push("/resources");
    },
  });

  function handleSubmit(event: FormEvent<HTMLFormElement>) {
    event.preventDefault();
    // Current form state becomes the request body for POST /api/login.
    loginMutation.mutate({ email, password });
  }

  return (
    <main className="relative h-screen overflow-hidden bg-[#202124] text-slate-950">
      <div
        aria-hidden="true"
        className="home-page-background absolute inset-0 bg-[url('/home_page.jpg')] bg-cover bg-center"
      />
      <div aria-hidden="true" className="absolute inset-0 bg-black/65" />
      <div className="relative z-10 flex h-full items-center justify-center overflow-y-auto px-6 py-10">
        <form
          className="w-full max-w-md space-y-5 rounded-lg border border-white/20 bg-white p-6 shadow-2xl shadow-black/30"
          onSubmit={handleSubmit}
        >
        <div>
          <Link className="inline-flex items-center gap-3 font-semibold" href="/">
            <span className="flex size-10 items-center justify-center rounded-md bg-slate-950 text-xl font-bold text-white">
              R
            </span>
            <span className="text-2xl text-slate-950">esource Manager</span>
          </Link>
          <p className="mt-1 text-sm text-slate-600">
            Use your registered email and password.
          </p>
        </div>

        <label className="block space-y-2 text-sm font-medium">
          <span>Email</span>
          <input
            className="w-full rounded-md border border-slate-300 px-3 py-2 outline-none focus:border-slate-950"
            onChange={(event) => setEmail(event.target.value)}
            required
            type="email"
            value={email}
          />
        </label>

        <label className="block space-y-2 text-sm font-medium">
          <span>Password</span>
          <input
            className="w-full rounded-md border border-slate-300 px-3 py-2 outline-none focus:border-slate-950"
            onChange={(event) => setPassword(event.target.value)}
            required
            type="password"
            value={password}
          />
        </label>

        {loginMutation.isError ? (
          <p className="rounded-md bg-red-50 px-3 py-2 text-sm text-red-700">
            {loginMutation.error.message}
          </p>
        ) : null}

        <button
          className="w-full rounded-md bg-slate-950 px-4 py-2 font-medium text-white disabled:opacity-50"
          disabled={loginMutation.isPending}
          type="submit"
        >
          {loginMutation.isPending ? "Logging in..." : "Login"}
        </button>

        <p className="text-center text-sm text-slate-600">
          New here?{" "}
          <Link className="font-medium text-emerald-700" href="/register">
            Create an account
          </Link>
        </p>
        </form>
      </div>
    </main>
  );
}
