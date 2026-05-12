"use client";

import { useMutation } from "@tanstack/react-query";
import Link from "next/link";
import { useRouter } from "next/navigation";
import { FormEvent, useState } from "react";
import { loginUser, registerUser } from "../api";
import { saveAuthSession } from "../authStore";

export function RegisterForm() {
  const router = useRouter();
  const [name, setName] = useState("");
  const [email, setEmail] = useState("");
  const [password, setPassword] = useState("");

  // Register first, then login so the app receives a JWT immediately.
  const registerMutation = useMutation({
    mutationFn: async () => {
      await registerUser({ name, email, password });
      return loginUser({ email, password });
    },
    onSuccess: (data) => {
      saveAuthSession(data.token, data.user_id);
      router.push("/resources");
    },
  });

  function handleSubmit(event: FormEvent<HTMLFormElement>) {
    event.preventDefault();
    registerMutation.mutate();
  }

  return (
    <main className="flex min-h-screen items-center justify-center bg-slate-50 px-6 text-slate-950">
      <form
        className="w-full max-w-md space-y-5 rounded-lg border border-slate-200 bg-white p-6 shadow-sm"
        onSubmit={handleSubmit}
      >
        <div>
          <h1 className="text-2xl font-semibold">Register</h1>
          <p className="mt-1 text-sm text-slate-600">
            Create an account, then continue to resources.
          </p>
        </div>

        <label className="block space-y-2 text-sm font-medium">
          <span>Name</span>
          <input
            className="w-full rounded-md border border-slate-300 px-3 py-2 outline-none focus:border-slate-950"
            onChange={(event) => setName(event.target.value)}
            required
            type="text"
            value={name}
          />
        </label>

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

        {registerMutation.isError ? (
          <p className="rounded-md bg-red-50 px-3 py-2 text-sm text-red-700">
            {registerMutation.error.message}
          </p>
        ) : null}

        <button
          className="w-full rounded-md bg-slate-950 px-4 py-2 font-medium text-white disabled:opacity-50"
          disabled={registerMutation.isPending}
          type="submit"
        >
          {registerMutation.isPending ? "Creating account..." : "Register"}
        </button>

        <p className="text-center text-sm text-slate-600">
          Already registered?{" "}
          <Link className="font-medium text-emerald-700" href="/login">
            Login
          </Link>
        </p>
      </form>
    </main>
  );
}
